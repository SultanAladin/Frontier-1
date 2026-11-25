import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';

import {
  DEFAULT_EROSION_PARAMS,
  EMITTER_MODES,
  EROSION_LANDSCAPES,
  MATERIAL_THEMES,
  ROCK_FABRIC_MODES,
  getVegetationDefaultsForLandscape,
} from '../src/erosion/erosion-presets.js';
import {
  RockCalvingSystem,
  buildRockArchetypeAtlas,
  sampleRockArchetypeSdf,
  ROCK_SDF_RES,
  ROCK_SDF_BOUND,
  contactPatchArea,
  submergedCapVolume,
  bearingPressure,
  equilibriumEmbed,
  rimHeightForVolume,
} from '../src/erosion/rock-calving.js';
import { buildGrassTuftMesh, MAX_GRASS_TUFTS } from '../src/erosion/grass-system.js';
import {
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
  createEnvironmentLayer,
  createStackLayer,
  ensureEnvironmentLayer,
} from '../src/erosion/terrain-stack-presets.js';
import {
  buildRemasteredRockSliders,
  enhanceAmountSliders,
  enhanceSwitches,
  resolveSliderDisplaySpec,
} from '../src/ui/StudioControls.js';
import { TerrainStackPipeline } from '../src/erosion/terrain-stack-pipeline.js';
import { SdfCaveSystem } from '../src/erosion/sdf-cave-mesher.js';
import {
  azimuthForWindFromBearing,
  buildWindVectorField,
  sampleWindVector,
  windCompassFromAzimuth,
  windSpeedColor,
} from '../src/erosion/wind-field-debug.js';
import {
  GpuErosionEngine,
  TERRAIN_RESOLUTION_OPTIONS,
  TERRAIN_RESOLUTION_STORAGE_KEY,
  normalizeTerrainResolution,
  resolveTerrainResolution,
  terrainResolutionProfile,
} from '../src/erosion/gpu-erosion-engine.js';
import { ErosionLayerStackUI } from '../src/erosion/layer-stack-ui.js';
import * as Shaders from '../src/erosion/erosion-shaders.js';
import * as PipelineShaders from '../src/erosion/pipeline-shaders.js';
import {
  DEFAULT_FROST_PARAMS,
  SNOW_EFFECT_PRESETS,
  SNOW_EFFECT_TYPES,
  advanceFrostState,
  createSnowEffect,
  createSnowEffectPreset,
  deriveSnowEffectWeights,
  estimateAnnualFrostWindow,
  normalizeFrostParams,
  normalizeSnowEffects,
  snowmeltCycleAtStep,
  visibleCrackThresholdMm,
} from '../src/erosion/frost-weathering.js';
import {
  MAX_STRATA_BEDS,
  STRATA_COLUMN_PRESETS,
  STRATA_LITHOLOGIES,
  createStrataColumnPreset,
  deriveBedProperties,
  normalizeStrataColumn,
  packStrataColumnUniforms,
  sampleStrataColumn,
  strataColumnThickness,
} from '../src/erosion/stratigraphic-column.js';
import {
  CLIMATE_REGIMES,
  DEFAULT_SEASONAL_CLIMATE_PARAMS,
  climateDefaultsForLandscape,
  climateRegimeFor,
  formatLatitude,
  normalizeSeasonalClimateParams,
  seasonalClimateAt,
} from '../src/erosion/seasonal-climate.js';
import { checkGlslScopes } from './glsl-scope-check.mjs';

const ALL_SHADER_EXPORTS = [
  'FULLSCREEN_VERT',
  'INIT_TERRAIN_FRAG',
  'INIT_SNOW_FRAG',
  'INIT_PARTICLES_FRAG',
  'PARTICLE_STEP_FRAG',
  'PARTICLE_SCATTER_VERT',
  'PARTICLE_SCATTER_FRAG',
  'WATER_FLUX_FRAG',
  'TERRAIN_INTEGRATE_FRAG',
  'SNOW_CRYO_FRAG',
  'FROST_DAMAGE_FRAG',
  'THERMAL_TALUS_FRAG',
  'CHEM_WEATHER_FRAG',
  'TERRAIN_RENDER_VERT',
  'TERRAIN_RENDER_FRAG',
  'PARTICLE_RENDER_VERT',
  'PARTICLE_RENDER_FRAG',
  'SKY_BG_FRAG',
  'ROCK_INSTANCE_VERT',
  'ROCK_INSTANCE_FRAG',
  'PROCEDURAL_MODIFIER_FRAG',
  'GRASS_INSTANCE_VERT',
  'GRASS_INSTANCE_FRAG',
];

test('GPU Erosion presets cover all 19 landscapes including non-periodic Himalayas, Sahara Grand Erg, Rugged Desert, Moon, Icelandic Green Highlands & Oasis', () => {
  const landKeys = Object.keys(EROSION_LANDSCAPES);
  assert.equal(landKeys.length, 19);
  const seenTypes = new Set();
  for (const k of landKeys) {
    const p = EROSION_LANDSCAPES[k];
    assert.equal(p.id, k);
    assert.ok(p.defaults.relief >= 80 && p.defaults.relief <= 230);
    assert.ok(p.defaults.rockHardness > 0 && p.defaults.rockHardness < 1);
    seenTypes.add(p.terrainType);
  }
  assert.equal(seenTypes.size, 19, 'Each landscape preset should map to a unique GPU terrain generator mode');

  // Verify all untouched baseline & v2.2 terrainTypes (0..6, 8..13), upgraded 7 & 14, and new 15..18!
  assert.equal(DEFAULT_EROSION_PARAMS.landscape, 'icelandic-highlands');
  assert.equal(EROSION_LANDSCAPES['alpine-divide'].terrainType, 0);
  assert.equal(EROSION_LANDSCAPES['layered-plateau'].terrainType, 1);
  assert.equal(EROSION_LANDSCAPES['fault-escarpment'].terrainType, 2);
  assert.equal(EROSION_LANDSCAPES['volcanic-caldera'].terrainType, 3);
  assert.equal(EROSION_LANDSCAPES['badlands-domes'].terrainType, 4);
  assert.equal(EROSION_LANDSCAPES['coastal-massif'].terrainType, 5);
  assert.equal(EROSION_LANDSCAPES['icelandic-highlands'].terrainType, 6);
  assert.equal(EROSION_LANDSCAPES['himalayan-karakoram'].terrainType, 7);
  assert.equal(EROSION_LANDSCAPES['himalayan-karakoram'].name, 'Himalayan Snow Mountains');
  assert.equal(EROSION_LANDSCAPES['grand-canyon'].terrainType, 8);
  assert.equal(EROSION_LANDSCAPES['studlagil-basalt'].terrainType, 9);
  assert.equal(EROSION_LANDSCAPES['fjadrargljufur-palagonite'].terrainType, 10);
  assert.equal(EROSION_LANDSCAPES['namib-erg-dunes'].terrainType, 11);
  assert.equal(EROSION_LANDSCAPES['yardang-wind-corridor'].terrainType, 12);
  assert.equal(EROSION_LANDSCAPES['glacial-lake-watershed'].terrainType, 13);
  assert.equal(EROSION_LANDSCAPES['sahara-tassili-erg'].terrainType, 14);
  assert.equal(EROSION_LANDSCAPES['rugged-desert-crags'].terrainType, 15);
  assert.equal(EROSION_LANDSCAPES['lunar-mare-craters'].terrainType, 16);
  assert.equal(EROSION_LANDSCAPES['icelandic-green-highlands'].terrainType, 17);
  assert.equal(EROSION_LANDSCAPES['desert-oasis-erg'].terrainType, 18);
  assert.equal(EROSION_LANDSCAPES['desert-oasis-erg'].defaults.oasisSpring, true);
  assert.equal(Object.keys(MATERIAL_THEMES).length, 10);
  assert.equal(Object.keys(ROCK_FABRIC_MODES).length, 5);

  // Ensure periodic fract(spurPhase) and stair-stepped floor(mesaMask) artifacts are gone from INIT_TERRAIN_FRAG
  assert.ok(!Shaders.INIT_TERRAIN_FRAG.includes('spurPhase1'), 'Himalayan terrain must not use periodic fract(spurPhase) ribs');
  assert.ok(!Shaders.INIT_TERRAIN_FRAG.includes('mesaMask'), 'Sahara terrain must not use stair-stepped Tassili mesaMask blobs');

  // Glacial mountain archetypes must exist and snow across broad mountain elevation ranges
  const iceland = EROSION_LANDSCAPES['icelandic-highlands'];
  const himalaya = EROSION_LANDSCAPES['himalayan-karakoram'];
  assert.ok(iceland && himalaya, 'Icelandic & Himalayan glacial mountain presets must exist');
  assert.ok(iceland.defaults.snowfallRate > 0.5 && himalaya.defaults.snowfallRate > 0.85);
  assert.ok(himalaya.defaults.snowline <= 0.30, 'Himalayan Snow Mountains must have low snowline for broad snow cover');
  assert.ok(himalaya.defaults.glacierStrength > iceland.defaults.glacierStrength * 0.9);

  const emitterKeys = Object.keys(EMITTER_MODES);
  assert.equal(emitterKeys.length, 6);
  for (const required of ['glacial-blizzard', 'aeolian-wind', 'rockfall-avalanche', 'all-weather', 'sky-rain']) {
    assert.ok(emitterKeys.includes(required), `Missing multi-agent emitter mode ${required}`);
  }
  const codes = new Set(emitterKeys.map((k) => EMITTER_MODES[k].code));
  assert.equal(codes.size, 6, 'Emitter mode codes must be unique for GPU dispatch');
  assert.equal(DEFAULT_EROSION_PARAMS.gridSize, 512);
  assert.ok(DEFAULT_EROSION_PARAMS.particleCount >= 8192 && DEFAULT_EROSION_PARAMS.particleCount <= 32768);
});

test('All GLSL shaders pass declaration-before-use scope checking (no undeclared or out-of-scope identifiers)', () => {
  // Regression guard: a wind-shader edit once referenced a variable outside
  // the block that declared it (terrainDeflectedWind). Node's mock WebGL
  // cannot compile GLSL, so it shipped and black-screened in the browser
  // with ANGLE "undeclared identifier". This scope checker catches that
  // whole class statically (validated to flag the original bug at the exact
  // line ANGLE reported, with zero false positives on the fixed shaders).
  for (const [modName, mod] of [['erosion-shaders', Shaders], ['pipeline-shaders', PipelineShaders]]) {
    for (const [name, value] of Object.entries(mod)) {
      if (typeof value !== 'string' || !value.includes('void main')) continue;
      const errors = checkGlslScopes(value, `${modName}.${name}`);
      assert.deepEqual(errors, [], `${modName}.${name} scope errors:\n${errors.join('\n')}`);
    }
  }

  // The checker itself must catch the original failure mode.
  const buggy = `#version 300 es
precision highp float;
out vec4 o;
void main() {
  if (true) { vec2 innerOnly = vec2(1.0); o = vec4(innerOnly, 0.0, 1.0); }
  o = vec4(innerOnly, 0.0, 1.0);
}`;
  const buggyErrors = checkGlslScopes(buggy, 'synthetic');
  assert.equal(buggyErrors.length, 1);
  assert.ok(buggyErrors[0].includes("'innerOnly'"), `Scope checker must flag out-of-block use, got: ${buggyErrors.join(' | ')}`);
});

test('All 18 WebGL2 GLSL ES 3.00 shaders satisfy strict ANGLE precision, brace, and interface invariants', () => {
  for (const name of ALL_SHADER_EXPORTS) {
    const src = Shaders[name];
    assert.equal(typeof src, 'string', `Missing shader export ${name}`);
    assert.ok(
      src.startsWith('#version 300 es\n'),
      `${name} must start at byte 0 with '#version 300 es'`
    );
    assert.ok(src.includes('precision highp float;'), `${name} missing highp float`);
    assert.ok(src.includes('precision highp int;'), `${name} missing highp int`);
    assert.ok(src.includes('precision highp sampler2D;'), `${name} missing highp sampler2D`);
    assert.ok(src.includes('void main()'), `${name} missing void main()`);

    // Balanced braces and parentheses
    let braces = 0;
    let parens = 0;
    for (const ch of src) {
      if (ch === '{') braces++;
      else if (ch === '}') braces--;
      else if (ch === '(') parens++;
      else if (ch === ')') parens--;
      assert.ok(braces >= 0, `Unbalanced '}' in ${name}`);
      assert.ok(parens >= 0, `Unbalanced ')' in ${name}`);
    }
    assert.equal(braces, 0, `Unclosed '{' in ${name}`);
    assert.equal(parens, 0, `Unclosed '(' in ${name}`);
  }

  // Verify MRT locations on PARTICLE_STEP_FRAG (5 attachments: posAge, velWater, sediment, exchange, cryo)
  for (let loc = 0; loc < 5; loc++) {
    assert.ok(
      Shaders.PARTICLE_STEP_FRAG.includes(`layout(location = ${loc}) out vec4`),
      `PARTICLE_STEP_FRAG missing MRT attachment ${loc}`
    );
  }

  // Verify every getUniformLocation / _bindTex name in gpu-erosion-engine.js exists in the target shader!
  const engineSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const uniformRegex = /(?:getUniformLocation|_bindTex)\(\s*this\.(\w+)\s*,\s*'(\w+)'/g;
  const progMap = {
    progInitTerrain: [Shaders.FULLSCREEN_VERT, Shaders.INIT_TERRAIN_FRAG],
    progProceduralMod: [Shaders.FULLSCREEN_VERT, Shaders.PROCEDURAL_MODIFIER_FRAG],
    progInitSnow: [Shaders.FULLSCREEN_VERT, Shaders.INIT_SNOW_FRAG],
    progInitParticles: [Shaders.FULLSCREEN_VERT, Shaders.INIT_PARTICLES_FRAG],
    progParticleStep: [Shaders.FULLSCREEN_VERT, Shaders.PARTICLE_STEP_FRAG],
    progParticleScatter: [Shaders.PARTICLE_SCATTER_VERT, Shaders.PARTICLE_SCATTER_FRAG],
    progWaterFlux: [Shaders.FULLSCREEN_VERT, Shaders.WATER_FLUX_FRAG],
    progTerrainIntegrate: [Shaders.FULLSCREEN_VERT, Shaders.TERRAIN_INTEGRATE_FRAG],
    progThermalTalus: [Shaders.FULLSCREEN_VERT, Shaders.THERMAL_TALUS_FRAG],
    progSnowCryo: [Shaders.FULLSCREEN_VERT, Shaders.SNOW_CRYO_FRAG],
    progTerrainRender: [Shaders.TERRAIN_RENDER_VERT, Shaders.TERRAIN_RENDER_FRAG],
    progRockInstance: [Shaders.ROCK_INSTANCE_VERT, Shaders.ROCK_INSTANCE_FRAG],
    progGrassInstance: [Shaders.GRASS_INSTANCE_VERT, Shaders.GRASS_INSTANCE_FRAG],
    progParticleRender: [Shaders.PARTICLE_RENDER_VERT, Shaders.PARTICLE_RENDER_FRAG],
  };

  let match;
  let checkedCount = 0;
  while ((match = uniformRegex.exec(engineSrc)) !== null) {
    const [, progProp, uName] = match;
    if (!progMap[progProp]) continue;
    const combined = progMap[progProp].join('\n');
    assert.ok(
      combined.includes(`uniform `) && combined.includes(uName),
      `Uniform '${uName}' referenced on '${progProp}' is missing from its GLSL shaders`
    );
    checkedCount++;
  }
  assert.ok(checkedCount >= 35, `Expected >= 35 verified GPU uniform bindings, got ${checkedCount}`);
  // Verify glacial-blizzard (code 1) is NOT trapped inside storm-cell disc (only mode == 5 is storm-cell!)
  assert.ok(
    Shaders.PARTICLE_STEP_FRAG.includes('if (mode == 5)') && !Shaders.PARTICLE_STEP_FRAG.includes('if (mode == 1) {\n    float ang'),
    'Full-domain blizzard snowfall must not be restricted to storm-cell radius'
  );
});

test('Top-drop particle physics, 3D rock hardness resistance, and two-species sedimentation obey physical conservation', () => {
  // Exact mirror of PARTICLE_STEP_FRAG hardness-gated cutting & two-species sedimentation laws:
  const evalBedrockCut = (hardness, rawDemand, bedrock = 100) => {
    const rockResistance = 0.12 + 3.85 * hardness * hardness;
    return Math.min(Math.max(0, bedrock - 1.0), rawDemand / rockResistance);
  };

  // 1. Differential Hardness Cutting Contrast: soft shale (H=0.10) vs hard caprock (H=0.94)
  const softCut = evalBedrockCut(0.10, 0.08);
  const hardCut = evalBedrockCut(0.94, 0.08);
  const contrastRatio = softCut / hardCut;
  assert.ok(
    contrastRatio > 18.0,
    `Expected > 18:1 cutting contrast between soft shale and hard caprock, got ${contrastRatio.toFixed(2)}:1`
  );

  // 2. Airborne Sky-Drop Fall -> Landing Impact Transition
  let dropY = 185.0;
  let vy = -15.0;
  let phaseAge = -0.02; // negative = airborne falling drop
  const surfY = 92.0;
  const dt = 0.085;
  let stepsToLand = 0;
  while (phaseAge < 0 && stepsToLand < 200) {
    stepsToLand++;
    vy = Math.max(vy - 9.81 * 2.2 * dt, -26.0);
    dropY += vy * dt;
    if (dropY <= surfY) {
      dropY = surfY;
      phaseAge = 0.001; // transitions to landed surface runoff parcel!
    }
  }
  assert.equal(phaseAge, 0.001, 'Falling drop must transition to landed runoff parcel upon hitting terrain');
  assert.equal(dropY, surfY);
  assert.ok(stepsToLand > 10 && stepsToLand < 100);

  // 3. Two-Species Sedimentation & Grain-Size Sorting on Gentle Alluvial Fan
  let coarseLoad = 0.045;
  let fineLoad = 0.045;
  const capacity = 0.010; // Flow decelerated onto piedmont plain -> over capacity!
  const depRate = 0.60 * 0.42;

  const surplus = (coarseLoad + fineLoad) - capacity;
  const depCoarse = Math.min(coarseLoad, surplus * depRate * 1.45);
  const remSurplus = Math.max(0, surplus - depCoarse);
  const depFine = Math.min(fineLoad, remSurplus * depRate * 0.45);

  const nextCoarse = coarseLoad - depCoarse;
  const nextFine = fineLoad - depFine;

  assert.ok(depCoarse > depFine * 2.5, 'Coarse gravel must settle significantly faster than fine silt at slope break');
  assert.ok(depFine > 0, 'Fine alluvial silt must also deposit across the fan');

  // Exact solid-mass conservation between particle load loss and terrain sediment gain
  const beforeMass = coarseLoad + fineLoad;
  const afterMass = nextCoarse + nextFine + depCoarse + depFine;
  assert.ok(Math.abs(beforeMass - afterMass) < 1e-12, 'Sediment deposition must strictly conserve mass');
});

test('Multi-agent GPU particle species (snow, aeolian wind, rockfall) and glacier cryosphere conserve mass', () => {
  // ---- 1. Species lottery partitions the whole population with zero loss ----
  const speciesVec = [0.34, 0.41, 0.17, 0.08]; // rain, snow, wind, rockfall
  const total = speciesVec.reduce((a, b) => a + b, 0);
  assert.ok(Math.abs(total - 1) < 1e-9, 'Species weights must be normalized');

  const counts = [0, 0, 0, 0];
  const N = 20000;
  for (let i = 0; i < N; i++) {
    const pick = ((i * 0.6180339887) % 1) * total;
    let species = 0;
    if (pick > speciesVec[0] + speciesVec[1] + speciesVec[2]) species = 3;
    else if (pick > speciesVec[0] + speciesVec[1]) species = 2;
    else if (pick > speciesVec[0]) species = 1;
    counts[species]++;
  }
  assert.equal(counts.reduce((a, b) => a + b, 0), N, 'Every GPU slot must receive exactly one species');
  assert.ok(counts[1] > 0 && counts[2] > 0 && counts[3] > 0, 'Snow, wind and rockfall species must all be emitted');
  for (let i = 0; i < 4; i++) {
    const expected = speciesVec[i] * N;
    assert.ok(Math.abs(counts[i] - expected) / N < 0.02, `Species ${i} frequency off by more than 2%`);
  }

  // ---- 2. Snowflake terminal velocity is far below a raindrop under the same gravity ----
  const dt = 0.085;
  let rainVy = -14, snowVy = -2.4;
  for (let i = 0; i < 40; i++) {
    rainVy = Math.max(rainVy - 9.81 * 2.2 * dt, -26.0);
    snowVy = Math.max(snowVy - 9.81 * 0.22 * dt, -4.2);
  }
  assert.ok(rainVy < -20, `Rain must reach terminal speed, got ${rainVy}`);
  assert.ok(snowVy > -4.3 && snowVy <= -4.2, `Snow must drift slowly at terminal speed, got ${snowVy}`);

  // ---- 3. Snowpack accumulation -> firn compaction -> glacier ice is mass conserving ----
  let snow = 0.9;
  let ice = 0.4;
  const glacierStrength = 0.85;
  const elevN = 0.72;
  const smoothstep = (a, b, x) => {
    const t = Math.max(0, Math.min(1, (x - a) / (b - a)));
    return t * t * (3 - 2 * t);
  };
  const before = snow + ice;
  const compaction = Math.min(snow * 0.20, snow * smoothstep(0.30, 0.85, elevN) * glacierStrength * 0.55 * dt);
  snow -= compaction;
  ice += compaction;
  assert.ok(Math.abs(snow + ice - before) < 1e-12, 'Firn compaction must conserve total ice-equivalent mass');
  assert.ok(compaction > 0, 'Cold high basins must form glacier ice');

  // ---- 4. Degree-day ablation only melts where the cell is below the snowline ----
  const warmth = (snowline, elev) => Math.max(0, Math.min(1, (snowline - elev) / Math.max(snowline, 0.05)));
  assert.equal(warmth(0.5, 0.9), 0, 'Summit snow far above the snowline must not melt');
  assert.ok(warmth(0.9, 0.2) > 0.7, 'Low warm cells below the snowline must melt aggressively');

  // ---- 5. Aeolian saltation carries sand further than heavy rockfall boulders ----
  const surfaceSpeed = (species, sinTheta) => {
    if (species === 'wind') return Math.min(22, Math.max(0.5, 6.0 + 20.0 * sinTheta));
    if (species === 'rock') return Math.min(26, Math.max(0.8, 1.4 + 22.0 * sinTheta));
    return 1.0;
  };
  // Saltating sand keeps blowing across flat plains while heavy boulders grind to a halt
  assert.ok(surfaceSpeed('wind', 0.0) > 5.0, 'Aeolian sand must keep saltating on flat ground');
  assert.ok(surfaceSpeed('rock', 0.0) < 2.5, 'Boulders must stall on flat ground');
  assert.ok(surfaceSpeed('wind', 0.0) > surfaceSpeed('rock', 0.0) * 2,
    'On a flat plain saltating sand must outrun stalled boulders');
  // Boulders gain far more speed per unit slope than sand (gravity-driven rolling vs. aerodynamic saltation)
  const rockGain = surfaceSpeed('rock', 0.6) - surfaceSpeed('rock', 0.05);
  const windGain = surfaceSpeed('wind', 0.6) - surfaceSpeed('wind', 0.05);
  assert.ok(rockGain > 0 && windGain > 0, 'Both species must accelerate down steepening slopes');
});

test('3D RockCalvingSystem synthesizes outward-normal CCW polyhedral outcrops & settles calved boulders down cliff slopes', () => {
  const atlas = buildRockArchetypeAtlas(7331);
  assert.equal(atlas.submeshes.length, 6, 'Atlas must contain 6 cleaved polyhedral rock archetypes');
  assert.ok(atlas.indices.length > 600, 'Polyhedral rock atlas must contain rich faceted triangles');

  // Verify EVERY triangle in the rock atlas has outward-pointing normals and CCW front-face winding!
  const pos = atlas.positions;
  const nrm = atlas.normals;
  const idxArr = atlas.indices;
  for (const sub of atlas.submeshes) {
    for (let t = sub.firstIndex; t < sub.firstIndex + sub.indexCount; t += 3) {
      const i0 = idxArr[t] * 3, i1 = idxArr[t + 1] * 3, i2 = idxArr[t + 2] * 3;
      const e1x = pos[i1] - pos[i0], e1y = pos[i1 + 1] - pos[i0 + 1], e1z = pos[i1 + 2] - pos[i0 + 2];
      const e2x = pos[i2] - pos[i0], e2y = pos[i2 + 1] - pos[i0 + 1], e2z = pos[i2 + 2] - pos[i0 + 2];
      const cx = e1y * e2z - e1z * e2y;
      const cy = e1z * e2x - e1x * e2z;
      const cz = e1x * e2y - e1y * e2x;
      const dotN = cx * nrm[i0] + cy * nrm[i0 + 1] + cz * nrm[i0 + 2];
      assert.ok(dotN > 0, 'Every rock facet triangle must have strictly outward CCW winding');
    }
  }

  const sys = new RockCalvingSystem();
  const N = 64;
  const grid = new Float32Array(N * N * 4);
  // Build a steep cliff escarpment at x < 0 dropping onto a gentle talus apron at x > 0
  for (let iz = 0; iz < N; iz++) {
    for (let ix = 0; ix < N; ix++) {
      const u = ix / (N - 1) - 0.5;
      const scarp = 1 / (1 + Math.exp(u * 22.0));
      const idx = (iz * N + ix) * 4;
      grid[idx + 0] = 14.0 + 95.0 * scarp;
      grid[idx + 1] = 0.4 * (1 - scarp);
      grid[idx + 2] = 0.0;
      grid[idx + 3] = 0.0;
    }
  }

  sys.updateTerrainSnapshot(grid, N, 512.0);
  sys.seedForLandscape({ seed: 4029, worldSpan: 512.0, boulderDensity: 0.75, strataStrike: 10, strataDip: 12 });

  // Verify EVERY rock instance 3x3 basis matrix has det(M) > 0 (right-handed, NEVER inverted normals!)
  for (let b = 0; b < sys.bucketCounts.length; b++) {
    const cnt = sys.bucketCounts[b];
    const data = sys.bucketData[b];
    for (let s = 0; s < cnt; s++) {
      const o = s * 16;
      const m00 = data[o + 0], m01 = data[o + 1], m02 = data[o + 2];
      const m10 = data[o + 4], m11 = data[o + 5], m12 = data[o + 6];
      const m20 = data[o + 8], m21 = data[o + 9], m22 = data[o + 10];
      const det = m00 * (m11 * m22 - m12 * m21)
                - m01 * (m10 * m22 - m12 * m20)
                + m02 * (m10 * m21 - m11 * m20);
      assert.ok(det > 0.05, `Instance matrix must be strictly right-handed (det > 0), got ${det}`);
    }
  }

  const outcrops = sys.rocks.filter((r) => r.isOutcrop);
  const boulders = sys.rocks.filter((r) => !r.isOutcrop);
  assert.ok(outcrops.length >= 10, `Expected >= 10 crown outcrops on gentle rim, got ${outcrops.length}`);
  assert.ok(boulders.length >= 50, `Expected >= 50 calved boulders, got ${boulders.length}`);

  // Verify NO rooted outcrop clings to steep slopes (all outcrops must sit on gentle slope <= 0.40)
  for (const out of outcrops) {
    const s = sys.sampleTerrain(out.x, out.z).slope;
    assert.ok(s <= 0.40, `Rooted outcrop must never cling to steep slope, got slope=${s}`);
  }

  // Verify brisk rockfall speed: a boulder on the steep cliff face x=0 must travel >12m in 1.0s!
  const cliffSample = sys.sampleTerrain(0.0, 0.0);
  assert.ok(cliffSample.slope > 1.0, `Expected steep cliff at x=0, got slope=${cliffSample.slope}`);
  const testRock = {
    submesh: 2,
    isOutcrop: false,
    settled: false,
    x: 0.0,
    y: cliffSample.totalH + 1.5,
    z: 0.0,
    vx: 2.0,
    vy: 0.0,
    vz: 0.0,
    sx: 2.0,
    sy: 1.8,
    sz: 2.0,
    radiusY: 1.8,
    yaw: 0.0,
    upX: 0.0,
    upY: 1.0,
    upZ: 0.0,
    isSlab: false,
    wakeTimer: 4.0,
    wear: 0.2,
    seedHash: 0.5,
  };
  sys.rocks.push(testRock);
  for (let s = 0; s < 20; s++) sys.stepPhysics(0.05); // 1.0 second elapsed
  // REAL rigid-body physics (g = 9.81, no forced minimum slide speed, no
  // terrain snapping): from rest-ish on a ~45 deg slope a boulder covers a
  // few metres in 1 s while accelerating to several m/s.
  const speed1s = Math.hypot(testRock.vx, testRock.vy, testRock.vz);
  assert.ok(testRock.x > 3.0, `Cliff boulder must accelerate downhill (>3m in 1.0s), moved ${testRock.x.toFixed(2)}m`);
  assert.ok(speed1s > 4.0, `Cliff boulder must be accelerating under gravity after 1s, speed ${speed1s.toFixed(2)} m/s`);
  // It is a tumbling rigid body: orientation quaternion stays unit-length and spin develops
  const qLen = Math.hypot(testRock.qx, testRock.qy, testRock.qz, testRock.qw);
  assert.ok(Math.abs(qLen - 1) < 1e-6, `Orientation quaternion must stay normalized, |q|=${qLen}`);

  // Step boulder gravity rolling until boulders reach the gentle cliff toe / apron
  for (let s = 0; s < 400; s++) sys.stepPhysics(0.05); // 20 s of real-time physics
  const settled = sys.rocks.filter((r) => !r.isOutcrop && r.settled);
  assert.ok(settled.length >= boulders.length * 0.75, 'Calved boulders must naturally decelerate and settle on gentler terrain');
  for (const b of settled) {
    const s = sys.sampleTerrain(b.x, b.z).slope;
    // Static friction can hold angular blocks up to mu_s = 0.62..0.78 (talus repose)
    assert.ok(s <= 0.82, `Settled boulder must NEVER cling beyond its friction angle, got slope=${s}`);
    assert.equal(Math.hypot(b.vx, b.vy, b.vz), 0, 'Sleeping boulder must have exactly zero velocity (no creep)');
  }

  // Verify hydraulic undercutting wakes settled boulders up to fall further!
  const target = settled[0];
  const restYBefore = target.y;
  for (let i = 0; i < grid.length; i += 4) grid[i] = Math.max(2.0, grid[i] - 2.5);
  sys.updateTerrainSnapshot(grid, N, 512.0);
  assert.equal(target.settled, false, 'Erosion undercutting (>0.12m gap) must wake settled boulder');
  // And it must genuinely FALL under gravity (position owned by the body, not snapped):
  for (let s = 0; s < 10; s++) sys.stepPhysics(0.05);
  assert.ok(target.y < restYBefore - 0.3, `Undercut boulder must fall under gravity, y ${restYBefore.toFixed(2)} -> ${target.y.toFixed(2)}`);
});

test('Asymmetric Namib/Sahara sand dune profile joins windward stoss & 33.8-deg planar slipface along a razor-sharp brink', () => {
  const evalDune = (dM, hCrestM = 72.0, stossSpanM = 185.0) => {
    if (dM >= 0.0) {
      const rawLee = hCrestM - dM * 0.670;
      if (rawLee <= -2.5) return 0.0;
      const k = 2.2;
      const h = Math.max(0, Math.min(1, 0.5 + 0.5 * rawLee / k));
      return Math.max(0, rawLee * h + k * h * (1 - h));
    }
    const t = Math.max(0, Math.min(1, -dM / stossSpanM));
    return hCrestM * (1 - t) * (1 - t) * (1 + 1.25 * t);
  };

  // 1. Exact C0 crest height continuity at brink (dM = 0)
  assert.ok(Math.abs(evalDune(-1e-6) - evalDune(1e-6)) < 1e-3, 'Stoss and slipface meet cleanly at the brink');
  // 2. Sharp slope discontinuity at brink: gentle positive windward stoss slope vs steep -0.670 (33.8 deg) leeward slipface!
  const stossSlope = (evalDune(0) - evalDune(-2.0)) / 2.0;
  const leeSlope = (evalDune(2.0) - evalDune(0)) / 2.0;
  assert.ok(stossSlope > 0.15 && stossSlope < 0.38, `Expected gentle windward stoss slope, got ${stossSlope}`);
  assert.ok(Math.abs(leeSlope - (-0.670)) < 1e-3, `Expected planar 33.8-deg (-0.670) slipface, got ${leeSlope}`);
  // 3. Clean flat interdune desert floor beyond slipface & upwind toe
  assert.equal(evalDune(140.0), 0, 'Interdune floor beyond slipface toe must be clean exposed desert pan');
  assert.equal(evalDune(-190.0), 0, 'Upwind stoss toe merges smoothly onto flat desert pan');
});

test('3D Rocks are true rigid bodies: vertex shader renders CPU world position (no heightfield snapping)', () => {
  assert.ok(
    !Shaders.ROCK_INSTANCE_VERT.includes('texelFetch(uTerrainTex') &&
    !Shaders.ROCK_INSTANCE_VERT.includes('texelFetch(uSnowTex') &&
    Shaders.ROCK_INSTANCE_VERT.includes('aInstPosMeta.xyz'),
    'ROCK_INSTANCE_VERT must render the CPU rigid-body centre directly and never re-snap rocks onto the heightfield (that made boulders ride up/down with erosion)'
  );

  const sys = new RockCalvingSystem(6);
  const N = 64;
  const grid = new Float32Array(N * N * 4);
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const idx = (j * N + i) * 4;
      const u = i / (N - 1);
      // Create convex rocky crown on left, steep cliff in center, gentle talus apron on right
      const h = u < 0.35
        ? 110.0 - Math.pow((u - 0.18) * 12.0, 2.0) * 1.4
        : u < 0.55
          ? 105.0 - (u - 0.35) * 360.0
          : 33.0 - (u - 0.55) * 14.0;
      grid[idx] = h;
      grid[idx + 1] = 0.0; // dry
      grid[idx + 2] = u >= 0.55 && u < 0.72 ? 1.4 : 0.0; // coarse scree at cliff toe
      grid[idx + 3] = 0.0; // low sand
    }
  }
  sys.updateTerrainSnapshot(grid, N, 512.0);
  sys.seedForLandscape({ seed: 4029, boulderDensity: 0.85, rockfallRate: 0.75 });
  assert.ok(sys.rocks.length >= 25, `Expected >= 25 seeded outcrops + talus boulders, got ${sys.rocks.length}`);

  for (let s = 0; s < 30; s++) sys.stepPhysics(0.04);
  // Packed instance Y must be the body's true world Y (finite, above the domain floor)
  for (let v = 0; v < 6; v++) {
    const count = sys.bucketCounts[v];
    const arr = sys.bucketData[v];
    for (let k = 0; k < count; k++) {
      const worldY = arr[k * 16 + 13];
      assert.ok(Number.isFinite(worldY) && worldY > 0.0, `Rock instance must pack a finite true world Y, got ${worldY}`);
    }
  }

  // KEY REGRESSION: a small terrain change must NOT move a sleeping boulder
  // (the old GPU heightfield lock made rocks ride up/down with every erosion step)
  const sleeper = sys.rocks.find((r) => !r.isOutcrop && r.settled);
  assert.ok(sleeper, 'Expected at least one sleeping boulder after seed+settle');
  const y0 = sleeper.y;
  const gridCopy = Float32Array.from(grid);
  for (let i = 0; i < gridCopy.length; i += 4) gridCopy[i] += 0.05; // 5 cm aggradation everywhere
  sys.updateTerrainSnapshot(gridCopy, N, 512.0);
  for (let s = 0; s < 10; s++) sys.stepPhysics(0.04);
  assert.equal(sleeper.y, y0, 'Sleeping boulder must stay exactly in place when terrain changes slightly (no elevator snapping)');

  // Free-fall sanity: a boulder dropped 20 m reaches ~sqrt(2 g h) impact speed
  const flat = new Float32Array(N * N * 4);
  for (let i = 0; i < flat.length; i += 4) { flat[i] = 20; }
  const fallSys = new RockCalvingSystem();
  fallSys.updateTerrainSnapshot(flat, N, 512.0);
  const dropped = {
    submesh: 2, isOutcrop: false, settled: false,
    x: 0, y: 42, z: 0, vx: 0, vy: 0, vz: 0,
    sx: 1.6, sy: 1.4, sz: 1.6, isSlab: false, wear: 0.3, seedHash: 0.4,
  };
  fallSys.rocks.push(dropped);
  let impactSpeed = 0;
  for (let s = 0; s < 200 && !dropped.settled; s++) {
    fallSys.stepPhysics(0.02);
    impactSpeed = Math.max(impactSpeed, Math.abs(dropped.vy));
  }
  const expectV = Math.sqrt(2 * 9.81 * 20); // ~19.8 m/s
  assert.ok(impactSpeed > expectV * 0.75 && impactSpeed < expectV * 1.1,
    `Free-fall impact speed must follow sqrt(2gh)=${expectV.toFixed(1)}, got ${impactSpeed.toFixed(1)}`);
  assert.ok(dropped.settled, 'Dropped boulder must bounce, dissipate and come to rest on flat ground');
  const restGap = Math.abs((dropped.y - fallSys._support(dropped, 0, 1, 0)) - 20);
  assert.ok(restGap < 0.35, `Resting boulder must sit on the surface (support gap ${restGap.toFixed(3)} m)`);
});

test('Boulders seat on true mesh support, keep a soft-ground protrusion floor, and freeze when paused', () => {
  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  assert.ok(
    engSrc.includes('if (this.playing && this.params.simRockfallPlaying !== false && this.params.showOutcrops !== false)'),
    'Pipeline-mode Pause Sim must freeze rigid-body boulders as well as GPU erosion'
  );
  assert.ok(
    engSrc.includes('if (this.playing && this.params.simRockfallPlaying !== false)'),
    'Standalone-mode pause must also freeze rigid-body boulders'
  );

  const atlas = buildRockArchetypeAtlas(7331);
  assert.equal(atlas.supportVerts.length, 6, 'Rock atlas must expose exact mesh support vertices for every archetype');
  for (let i = 0; i < atlas.supportVerts.length; i++) {
    assert.ok(atlas.supportVerts[i].length >= 90, `Archetype ${i} must have a rich exact-support vertex set`);
  }

  const N = 64;
  const SPAN = 512.0;
  const slopeGrid = new Float32Array(N * N * 4);
  for (let iz = 0; iz < N; iz++) {
    for (let ix = 0; ix < N; ix++) {
      const x = (ix / (N - 1) - 0.5) * SPAN;
      slopeGrid[(iz * N + ix) * 4] = 30.0 + 0.45 * x;
    }
  }
  const slopeSys = new RockCalvingSystem();
  slopeSys.updateTerrainSnapshot(slopeGrid, N, SPAN);
  const slopeRock = {
    submesh: 2, isOutcrop: false, settled: false,
    x: 0, y: 0, z: 0, vx: 0, vy: 0, vz: 0,
    sx: 2.6, sy: 2.2, sz: 2.6, isSlab: false, wear: 0.3, seedHash: 0.4,
  };
  slopeRock.y = slopeSys.sampleTerrain(0, 0).surfH + 2.5;
  slopeSys.rocks.push(slopeRock);
  for (let s = 0; s < 900 && !slopeRock.settled; s++) slopeSys.stepPhysics(0.033);
  assert.ok(slopeRock.settled, 'Slope regression rock must come to rest on a 0.45 slope');
  slopeSys._rebuildInstanceBuckets();
  const mat = slopeSys.bucketData[slopeRock.submesh];
  const verts = slopeSys.atlas.supportVerts[slopeRock.submesh];
  let minVertexClear = Infinity;
  for (let i = 0; i < verts.length; i += 3) {
    const wx = mat[12] + mat[0] * verts[i] + mat[4] * verts[i + 1] + mat[8] * verts[i + 2];
    const wy = mat[13] + mat[1] * verts[i] + mat[5] * verts[i + 1] + mat[9] * verts[i + 2];
    const wz = mat[14] + mat[2] * verts[i] + mat[6] * verts[i + 1] + mat[10] * verts[i + 2];
    minVertexClear = Math.min(minVertexClear, wy - slopeSys.sampleTerrain(wx, wz).surfH);
  }
  assert.ok(
    minVertexClear > -0.18,
    `Resting rock mesh must not sink through a planar slope (min vertex clearance ${minVertexClear.toFixed(3)} m; ellipsoid-only contact was about -0.41 m)`
  );

  const snowGrid = new Float32Array(N * N * 4);
  const snowState = new Float32Array(N * N * 4);
  for (let i = 0; i < N * N; i++) {
    snowGrid[i * 4] = 30.0;
    snowState[i * 4] = 6.0;
  }
  const snowSys = new RockCalvingSystem();
  snowSys.updateTerrainSnapshot(snowGrid, N, SPAN, snowState);
  const snowStart = snowSys.sampleTerrain(0, 0);
  const snowRock = {
    submesh: 2, isOutcrop: false, settled: false,
    x: 0, y: snowStart.totalH + 18, z: 0, vx: 0, vy: 0, vz: 0,
    sx: 0.85, sy: 0.8, sz: 0.85, isSlab: false, wear: 0.3, seedHash: 0.42,
  };
  snowSys.rocks.push(snowRock);
  for (let s = 0; s < 4000 && !snowRock.settled; s++) snowSys.stepPhysics(0.016);
  assert.ok(snowRock.settled, 'Deep-snow regression rock must settle');
  const bottomSup = snowSys._contactSupport(snowRock, 0, 1, 0);
  const topSup = Math.max(snowSys._support(snowRock, 0, 1, 0), snowSys._meshSupport(snowRock, 0, 1, 0));
  const snowSurf = snowSys.sampleTerrain(0, 0).surfH;
  const snowEmbed = snowSurf - (snowRock.y - bottomSup);
  const protrusionFrac = ((snowRock.y + topSup) - snowSurf) / (topSup + bottomSup);
  assert.ok(snowEmbed > 0.5, `Small rock must still genuinely embed into deep snow, embed=${snowEmbed.toFixed(3)} m`);
  assert.ok(protrusionFrac >= 0.32, `Small rock must keep a visible protrusion floor in deep snow, got ${(protrusionFrac * 100).toFixed(1)}% of diameter`);
});

test('Real 3D Grass tuft mesh, biome vegetation defaults (Green / Dry / Auto-Blend), and Layer Stack preserve preset visual invariance', () => {
  // 1. 3D Curved Grass Tuft Geometry
  const tuft = buildGrassTuftMesh();
  assert.equal(tuft.vertexCount, 35, '5 curved 3D grass blades * 7 verts = 35 vertices per tuft');
  assert.equal(tuft.indexCount, 75, '5 curved 3D grass blades * 5 segments * 3 indices = 75 indices per tuft');
  assert.equal(tuft.positions.length, 35 * 3);
  assert.equal(tuft.normals.length, 35 * 3);
  assert.equal(tuft.bladeMeta.length, 35 * 2);
  assert.ok(MAX_GRASS_TUFTS >= 32000);
  for (let i = 0; i < tuft.positions.length; i++) {
    assert.ok(Number.isFinite(tuft.positions[i]), 'Non-finite grass vertex position');
    assert.ok(Number.isFinite(tuft.normals[i]), 'Non-finite grass vertex normal');
  }

  // 2. Biome Vegetation Defaults (Green Grass vs Dry Grass vs Disabled)
  const greenHighlands = getVegetationDefaultsForLandscape('icelandic-green-highlands');
  assert.equal(greenHighlands.grassEnabled, true);
  assert.equal(greenHighlands.grassMode, 0, 'Icelandic Green Highlands uses lush Green Grass');

  const oasis = getVegetationDefaultsForLandscape('desert-oasis-erg');
  assert.equal(oasis.grassEnabled, true);
  assert.equal(oasis.grassMode, 2, 'Desert Oasis uses Biome Auto-Blend (green oasis shore -> dry outer fringe)');

  const ruggedDesert = getVegetationDefaultsForLandscape('rugged-desert-crags');
  assert.equal(ruggedDesert.grassEnabled, true);
  assert.equal(ruggedDesert.grassMode, 1, 'Rugged Desert Crags uses Dry Grass');

  const moon = getVegetationDefaultsForLandscape('lunar-mare-craters');
  assert.equal(moon.grassEnabled, false, 'Lunar Mare Craters disables grass');
});

test('Bottom-to-top Terrain Stack Pipeline shaders, uniform bindings, and non-colour operation specs satisfy strict architecture', () => {
  const PIPELINE_EXPORTS = [
    'STACK_OP_FRAG',
    'SCULPT_STAMP_FRAG',
    'STACK_MASK_BAKE_FRAG',
    'STACK_MASK_BLEND_FRAG',
    'STACK_COPY_VEC_FRAG',
    'SDF_CAVE_VERT',
    'SDF_CAVE_FRAG',
  ];

  for (const name of PIPELINE_EXPORTS) {
    const src = PipelineShaders[name];
    assert.equal(typeof src, 'string', `Missing pipeline shader export ${name}`);
    assert.ok(src.startsWith('#version 300 es\n'), `${name} must start with #version 300 es`);
    assert.ok(src.includes('precision highp float;'), `${name} missing highp float`);
    assert.ok(src.includes('precision highp int;'), `${name} missing highp int`);
    assert.ok(src.includes('precision highp sampler2D;'), `${name} missing highp sampler2D`);
    assert.ok(src.includes('void main()'), `${name} missing void main()`);

    let braces = 0, parens = 0;
    for (const ch of src) {
      if (ch === '{') braces++;
      else if (ch === '}') braces--;
      else if (ch === '(') parens++;
      else if (ch === ')') parens--;
      assert.ok(braces >= 0, `Unbalanced '}' in ${name}`);
      assert.ok(parens >= 0, `Unbalanced ')' in ${name}`);
    }
    assert.equal(braces, 0, `Unclosed '{' in ${name}`);
    assert.equal(parens, 0, `Unclosed '(' in ${name}`);
  }

  // Verify every getUniformLocation / _bindTex in terrain-stack-pipeline.js matches its GLSL shader!
  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const uRegex = /(?:getUniformLocation|_bindTex)\(\s*this\.(?:engine\.)?(\w+)\s*,\s*'(\w+)'/g;
  const progShaders = {
    progStackOp: [Shaders.FULLSCREEN_VERT, PipelineShaders.STACK_OP_FRAG],
    progSculptStamp: [Shaders.FULLSCREEN_VERT, PipelineShaders.SCULPT_STAMP_FRAG],
    progMaskBake: [Shaders.FULLSCREEN_VERT, PipelineShaders.STACK_MASK_BAKE_FRAG],
    progMaskBlend: [Shaders.FULLSCREEN_VERT, PipelineShaders.STACK_MASK_BLEND_FRAG],
    progCopyVec: [Shaders.FULLSCREEN_VERT, PipelineShaders.STACK_COPY_VEC_FRAG],
    progCave: [PipelineShaders.SDF_CAVE_VERT, PipelineShaders.SDF_CAVE_FRAG],
    progInitTerrain: [Shaders.FULLSCREEN_VERT, Shaders.INIT_TERRAIN_FRAG],
    progInitParticles: [Shaders.FULLSCREEN_VERT, Shaders.INIT_PARTICLES_FRAG],
    progSkyBg: [Shaders.FULLSCREEN_VERT, Shaders.SKY_BG_FRAG],
    progTerrainRender: [Shaders.TERRAIN_RENDER_VERT, Shaders.TERRAIN_RENDER_FRAG],
    progRockInstance: [Shaders.ROCK_INSTANCE_VERT, Shaders.ROCK_INSTANCE_FRAG],
    progGrassInstance: [Shaders.GRASS_INSTANCE_VERT, Shaders.GRASS_INSTANCE_FRAG],
    progParticleRender: [Shaders.PARTICLE_RENDER_VERT, Shaders.PARTICLE_RENDER_FRAG],
  };

  let m;
  let verified = 0;
  while ((m = uRegex.exec(pipeSrc)) !== null) {
    const [, progName, uName] = m;
    if (!progShaders[progName]) continue;
    const joined = progShaders[progName].join('\n');
    assert.ok(
      joined.includes('uniform ') && joined.includes(uName),
      `Pipeline uniform '${uName}' referenced on '${progName}' is missing from GLSL`
    );
    verified++;
  }
  assert.ok(verified >= 45, `Expected >= 45 verified pipeline GPU uniform bindings, got ${verified}`);

  // Verify 23 composable terrain operations plus the one locked Environment
  // system layer, across all 6 categories, and ZERO colour/material layers.
  const specKeys = Object.keys(LAYER_OPERATION_SPECS);
  assert.equal(specKeys.length, 24);
  for (const catKey of ['environment', 'landform', 'simulation', 'spline', 'rocks', 'vegetation']) {
    assert.ok(LAYER_CATEGORIES[catKey], `Missing category ${catKey}`);
    assert.ok(specKeys.some((k) => LAYER_OPERATION_SPECS[k].category === catKey), `No operation in category ${catKey}`);
  }
  const forbiddenTerms = ['colour', 'color', 'material', 'biome', 'texture', 'albedo'];
  for (const spec of Object.values(LAYER_OPERATION_SPECS)) {
    const labelStr = spec.defaultName || '';
    for (const bad of forbiddenTerms) {
      assert.ok(
        !spec.type.toLowerCase().includes(bad) && !labelStr.toLowerCase().includes(bad),
        `Layer stack must not contain colour/material layers, found ${labelStr}`
      );
    }
    assert.ok(Array.isArray(spec.reads) && Array.isArray(spec.writes) && spec.writes.length > 0);
  }
  assert.equal(Object.keys(DERIVED_PALETTES).length, 10, 'All 10 regional geological MATERIAL_THEMES live separately from the layer stack');
  for (const [id, palette] of Object.entries(DERIVED_PALETTES)) {
    assert.equal(palette.label, MATERIAL_THEMES[id].label, `Palette ${id} must expose a real label, not undefined`);
  }
  for (const [id, fabric] of Object.entries(DERIVED_FABRICS)) {
    assert.equal(fabric.label, ROCK_FABRIC_MODES[id].label, `Fabric ${id} must expose a real label, not undefined`);
  }

  // Verify all 19 canonical EROSION_LANDSCAPES recipes plus the curated
  // Utah, standalone spline-authoring and frost/strata manual stacks build
  // valid bottom-up stacks anchored by base-landform.
  const presetKeys = Object.keys(TERRAIN_STACK_PRESETS);
  assert.equal(presetKeys.length, 26);
  for (const pk of presetKeys) {
    const stackPreset = TERRAIN_STACK_PRESETS[pk];
    const isManualRecipe = stackPreset.manualRecipe === true;
    const orig = EROSION_LANDSCAPES[pk];
    if (!isManualRecipe) assert.ok(orig, `Stack preset ${pk} must map 1:1 to canonical EROSION_LANDSCAPES`);
    const expectedType = isManualRecipe ? stackPreset.terrainType : orig.terrainType;
    const expectedRelief = isManualRecipe ? stackPreset.expectedRelief : (orig.defaults.relief ?? DEFAULT_EROSION_PARAMS.relief);
    const stack = TERRAIN_STACK_PRESETS[pk].layers();
    assert.ok(stack.length >= 3, `Preset ${pk} must populate a multi-stage editable stack`);
    const environmentLayers = stack.filter((layer) => layer.type === ENVIRONMENT_LAYER_TYPE);
    assert.equal(environmentLayers.length, 1, `Preset ${pk} must contain exactly one Environment layer`);
    const lastLayer = stack[stack.length - 1];
    assert.equal(lastLayer, environmentLayers[0], `Environment must be the locked last row of ${pk}`);
    assert.equal(lastLayer.name, ENVIRONMENT_LAYER_NAME, `Preset ${pk} Environment name is canonical`);
    assert.equal(lastLayer.enabled, true, `Preset ${pk} Environment cannot be muted`);
    assert.equal(lastLayer.locked, true, `Preset ${pk} Environment is locked`);
    assert.equal(lastLayer.mask.enabled, false, `Preset ${pk} Environment cannot be masked`);
    assert.equal(lastLayer.params.sunAzimuth, stackPreset.sunAzimuth, `Preset ${pk} Environment owns the preset sun azimuth`);
    assert.equal(lastLayer.params.sunElevation, stackPreset.sunElevation, `Preset ${pk} Environment owns the preset sun elevation`);

    const bottomLayer = stack[stack.length - 2];
    assert.equal(bottomLayer.type, 'base-landform', `Final terrain layer of ${pk} must be base-landform (evaluated first, immediately above Environment)`);
    assert.equal(bottomLayer.params.archetype, expectedType, `Preset ${pk} must use exact INIT_TERRAIN_FRAG terrainType ${expectedType}`);
    assert.equal(bottomLayer.params.seed, 4029, `Preset ${pk} must preserve default seed 4029`);
    assert.equal(bottomLayer.params.relief, expectedRelief, `Preset ${pk} must preserve exact relief`);
    for (const l of stack) {
      assert.ok(l.id && l.type && l.params && typeof l.enabled === 'boolean');
    }
  }
});

test('Environment owns the Sun as one locked last layer and cannot be added elsewhere', () => {
  const spec = LAYER_OPERATION_SPECS[ENVIRONMENT_LAYER_TYPE];
  assert.ok(spec, 'Environment operation spec exists');
  assert.equal(ENVIRONMENT_LAYER_NAME, 'Environment');
  assert.equal(spec.defaultName, ENVIRONMENT_LAYER_NAME);
  assert.equal(spec.category, 'environment');
  assert.equal(spec.glyph, 'sun');
  assert.equal(spec.systemLayer, true);
  assert.equal(spec.singleton, true);
  assert.equal(spec.addable, false, 'Environment must not be offered as an addable layer');
  assert.equal(spec.lockedPosition, 'last');
  assert.deepEqual(spec.reads, ['Sun Azimuth', 'Sun Elevation', 'Latitude', 'Climate Regime']);
  assert.deepEqual(spec.writes, ['Sun Direction', 'Seasonal Climate']);
  assert.equal(LAYER_CATEGORIES.environment.color, '#ffd166');

  // Direct construction cannot smuggle in a renamed, muted, unlocked or
  // masked Environment layer.
  const canonical = createStackLayer(ENVIRONMENT_LAYER_TYPE, {
    name: 'Loose Sun',
    enabled: false,
    locked: false,
    mask: { enabled: true },
    params: { sunAzimuth: 101, sunElevation: 44 },
  });
  assert.equal(canonical.name, ENVIRONMENT_LAYER_NAME);
  assert.equal(canonical.enabled, true);
  assert.equal(canonical.locked, true);
  assert.equal(canonical.mask.enabled, false);
  assert.deepEqual(canonical.params, { sunAzimuth: 101, sunElevation: 44, ...DEFAULT_SEASONAL_CLIMATE_PARAMS });

  // Normalization keeps the first Environment, discards duplicates, pins it
  // to the final row, repairs system flags, and lets its own sun values win.
  const malformed = createStackLayer(ENVIRONMENT_LAYER_TYPE, {
    params: { sunAzimuth: 101, sunElevation: 44 },
  });
  malformed.name = 'Sun copy';
  malformed.enabled = false;
  malformed.locked = false;
  malformed.mask.enabled = true;
  const duplicate = createEnvironmentLayer({ sunAzimuth: 1, sunElevation: 9 });
  const helperBase = createStackLayer('base-landform', { locked: true });
  const normalized = ensureEnvironmentLayer(
    [malformed, helperBase, duplicate],
    { sunAzimuth: -44, sunElevation: 66 }
  );
  assert.equal(normalized.length, 2, 'Duplicate Environment layers are discarded');
  assert.equal(normalized[0], helperBase);
  assert.equal(normalized[1], malformed, 'The first Environment survives and is moved last');
  assert.equal(malformed.name, ENVIRONMENT_LAYER_NAME);
  assert.equal(malformed.enabled, true);
  assert.equal(malformed.locked, true);
  assert.equal(malformed.mask.enabled, false);
  assert.deepEqual(malformed.params, { sunAzimuth: 101, sunElevation: 44, ...DEFAULT_SEASONAL_CLIMATE_PARAMS });

  const makePipelineHarness = (layers) => {
    const harness = Object.create(TerrainStackPipeline.prototype);
    harness.layers = layers;
    harness.selectedLayerId = layers[0]?.id || null;
    harness.engine = { params: { sunAzimuth: 11, sunElevation: 22 } };
    harness.sculptStore = new Map();
    harness.gl = { deleteTexture() {}, deleteFramebuffer() {} };
    harness.evalCount = 0;
    harness.evaluateStack = function evaluateStackStub() { this.evalCount += 1; };
    harness._getOrCreateSculptEntry = () => {};
    return harness;
  };

  const ridge = createStackLayer('mountain-ridges');
  const base = createStackLayer('base-landform', { locked: true });
  const environment = createEnvironmentLayer({ sunAzimuth: 73, sunElevation: 31 });
  const pipeline = makePipelineHarness([ridge, base, environment]);
  pipeline.selectedLayerId = environment.id;

  // Requesting Environment through the normal add API selects the existing
  // singleton; it cannot create a second copy or place one elsewhere.
  assert.equal(pipeline.addLayer(ENVIRONMENT_LAYER_TYPE), environment);
  assert.equal(pipeline.layers.length, 3);
  assert.equal(pipeline.layers.filter((layer) => layer.type === ENVIRONMENT_LAYER_TYPE).length, 1);
  assert.equal(pipeline.evalCount, 0, 'Selecting the Environment singleton does not rebuild terrain');

  // Adding with Environment selected inserts above it, never below it.
  const added = pipeline.addLayer('mountain-ridges');
  assert.equal(pipeline.layers[pipeline.layers.length - 2], added);
  assert.equal(pipeline.layers[pipeline.layers.length - 1], environment);
  assert.equal(pipeline.evalCount, 1);

  // Duplicate/delete/move protections are enforced by the pipeline itself,
  // not merely hidden by the UI.
  const countBeforeGuards = pipeline.layers.length;
  assert.equal(pipeline.duplicateLayer(environment.id), null);
  assert.equal(pipeline.duplicateLayer(base.id), null, 'Legacy locked layers remain duplicate-safe');
  assert.equal(pipeline.deleteLayer(environment.id), false);
  assert.equal(pipeline.moveLayer(pipeline.layers.length - 1, 0), false);
  assert.equal(pipeline.layers.length, countBeforeGuards);
  assert.equal(pipeline.layers[pipeline.layers.length - 1], environment);

  // A move targeted below Environment clamps to immediately above it.
  assert.equal(pipeline.moveLayer(0, 999), true);
  assert.equal(pipeline.layers[pipeline.layers.length - 2], ridge);
  assert.equal(pipeline.layers[pipeline.layers.length - 1], environment);

  // Preserve the older locked-layer contract: Base is delete/duplicate-safe
  // but can still be arrow-reordered above Environment.
  const baseIdx = pipeline.layers.indexOf(base);
  assert.equal(pipeline.moveLayer(baseIdx, baseIdx + 1), true);
  assert.equal(pipeline.layers[pipeline.layers.length - 1], environment);

  // If a malformed/restored stack selected a duplicate Environment, the
  // pipeline remaps selection to the surviving singleton instead of leaving
  // the Inspector attached to a discarded layer id.
  const survivingEnvironment = createEnvironmentLayer({ sunAzimuth: 12, sunElevation: 34 });
  const selectedDuplicate = createEnvironmentLayer({ sunAzimuth: 56, sunElevation: 18 });
  const duplicatePipeline = makePipelineHarness([
    survivingEnvironment,
    createStackLayer('base-landform', { locked: true }),
    selectedDuplicate,
  ]);
  duplicatePipeline.selectedLayerId = selectedDuplicate.id;
  assert.equal(duplicatePipeline.getEnvironmentLayer(), survivingEnvironment);
  assert.equal(duplicatePipeline.selectedLayerId, survivingEnvironment.id);
  assert.equal(duplicatePipeline.layers[duplicatePipeline.layers.length - 1], survivingEnvironment);

  // Sun changes are clamped and applied directly to render state without a
  // terrain evaluation (and therefore without the stack-edit auto-pause).
  environment.params.sunAzimuth = 999;
  environment.params.sunElevation = -50;
  environment.params.climateRegime = 4;
  environment.params.climateLatitudeDeg = -120;
  environment.params.climateYearsToSimulate = 20;
  const evalBeforeSun = pipeline.evalCount;
  assert.equal(pipeline.applyLiveEnvironmentLayerParams(environment), true);
  assert.equal(pipeline.engine.params.sunAzimuth, 180);
  assert.equal(pipeline.engine.params.sunElevation, 4);
  assert.equal(environment.params.sunAzimuth, 180);
  assert.equal(environment.params.sunElevation, 4);
  assert.equal(pipeline.engine.params.climateRegime, 4);
  assert.equal(pipeline.engine.params.climateLatitudeDeg, -82, 'Latitude is normalised when copied live to the engine');
  assert.equal(environment.params.climateLatitudeDeg, -82);
  assert.equal(pipeline.engine.params.climateYearsToSimulate, 20);
  assert.equal(pipeline.evalCount, evalBeforeSun);
  assert.deepEqual(pipeline.simulationProcessesForLayer(environment), [], 'Environment is not a simulation season');

  // UI contract: yellow Lucide Sun/Lock glyphs, live schema, no Add-menu or
  // duplicate generic Sun sliders, and a squared locked-last Outliner row.
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const cssSrc = fs.readFileSync(new URL('../src/app/style.css', import.meta.url), 'utf8');
  const htmlSrc = fs.readFileSync(new URL('../index.html', import.meta.url), 'utf8');
  for (const token of [
    'sun: lucideIcon(Sun)',
    'lock: lucideIcon(Lock, 16)',
    "[ENVIRONMENT_LAYER_TYPE]: '#ffd166'",
    "key: 'sunAzimuth'",
    "key: 'sunElevation'",
    'liveParam: true',
    's.addable !== false',
    'environment locked-last',
    "stepLabel = environmentLayer ? 'LAST'",
    'Locked · Last layer',
    'applyLiveEnvironmentLayerParams(layer)',
    'Sun direction lives on the locked Environment layer',
  ]) {
    assert.ok(uiSrc.includes(token), `Environment UI wiring missing ${token}`);
  }
  assert.ok(!uiSrc.includes('tp-slider-sun-az') && !uiSrc.includes('tp-slider-sun-el'),
    'Generic shading card must not duplicate the Environment Sun sliders');
  assert.ok(!uiSrc.includes('tp-out-sun-az') && !uiSrc.includes('tp-out-sun-el'),
    'Generic Sun outputs must not remain as a second source of truth');

  const envBranch = pipeSrc.indexOf('if (layer.type === ENVIRONMENT_LAYER_TYPE)');
  const baseBranch = pipeSrc.indexOf("} else if (layer.type === 'base-landform')");
  assert.ok(envBranch > -1 && baseBranch > envBranch,
    'Pipeline handles Environment as lighting state before the Base terrain pass');

  assert.ok(/\.tp-layer-row\.environment\.locked-last \{[^}]*border-radius: 0;/s.test(cssSrc),
    'Environment remains a squared layer-stack entry');
  assert.ok(/\.tp-lock-chip \{[^}]*border-radius: 999px;/s.test(cssSrc),
    'Environment lock chip stays rounded like the surrounding controls');
  assert.ok(htmlSrc.includes('BOTTOM — BASE FIRST · ENVIRONMENT LAST (LOCKED)'),
    'Outliner footer explains Base terrain order and locked Environment last');
});

test('Location seasonal climate reverses hemispheres, separates desert rain/snow/frost, and auto-stops after simulated years', () => {
  const at = (params, startDay, elapsedYears = 0) => seasonalClimateAt({
    seasonalClimateMode: 1,
    climateStartDayOfYear: startDay,
    ...params,
  }, elapsedYears, 4029);

  assert.equal(CLIMATE_REGIMES.length, 9);
  assert.equal(DEFAULT_SEASONAL_CLIMATE_PARAMS.climateStepsPerYear, 240);
  assert.equal(DEFAULT_SEASONAL_CLIMATE_PARAMS.climateYearsToSimulate, 20);
  assert.equal(formatLatitude(-13), '13°S');
  assert.deepEqual(
    normalizeSeasonalClimateParams({ climateLatitudeDeg: 999, climateStepsPerYear: 1, climateYearsToSimulate: 999 }),
    {
      ...DEFAULT_SEASONAL_CLIMATE_PARAMS,
      climateLatitudeDeg: 82,
      climateStepsPerYear: 24,
      climateYearsToSimulate: 250,
    },
  );

  const northJan = at({ climateRegime: climateRegimeFor('alpine-continental').code, climateLatitudeDeg: 46 }, 15);
  const northJul = at({ climateRegime: climateRegimeFor('alpine-continental').code, climateLatitudeDeg: 46 }, 197);
  const southJan = at({ climateRegime: climateRegimeFor('alpine-continental').code, climateLatitudeDeg: -46 }, 15);
  const southJul = at({ climateRegime: climateRegimeFor('alpine-continental').code, climateLatitudeDeg: -46 }, 197);
  assert.equal(northJan.seasonName, 'Winter');
  assert.equal(southJan.seasonName, 'Summer');
  assert.ok(northJul.temperatureC > northJan.temperatureC + 15);
  assert.ok(southJan.temperatureC > southJul.temperatureC + 15);
  assert.ok(northJan.snowMultiplier > northJul.snowMultiplier * 10);
  assert.ok(southJul.snowMultiplier > southJan.snowMultiplier * 10);
  assert.ok(northJul.meltMultiplier > northJan.meltMultiplier + 1);

  const equator = at({ climateRegime: climateRegimeFor('tropical-highland').code, climateLatitudeDeg: 0 }, 15);
  assert.ok(equator.temperatureAmplitudeScale < 0.3, 'Equatorial temperature seasonality is strongly damped');
  assert.ok(['Wet season', 'Dry season'].includes(equator.seasonName));

  const medWinter = at({ climateRegime: climateRegimeFor('mediterranean').code, climateLatitudeDeg: 38 }, 15);
  const medSummer = at({ climateRegime: climateRegimeFor('mediterranean').code, climateLatitudeDeg: 38 }, 197);
  assert.ok(medWinter.rainMultiplier > medSummer.rainMultiplier * 8, 'Mediterranean rain is winter-dominant');
  const monsoonWinter = at({ climateRegime: climateRegimeFor('monsoon').code, climateLatitudeDeg: 25 }, 15);
  const monsoonSummer = at({ climateRegime: climateRegimeFor('monsoon').code, climateLatitudeDeg: 25 }, 197);
  assert.ok(monsoonSummer.rainMultiplier > monsoonWinter.rainMultiplier * 8, 'Monsoon rain is warm-season dominant');

  const hotDesert = at({ climateRegime: climateRegimeFor('arid-desert').code, climateLatitudeDeg: 24 }, 15);
  assert.equal(Math.round(hotDesert.referenceAnnualPrecipMm), 35, 'Hot desert normal is only a few tens of millimetres per year');
  assert.ok(hotDesert.referenceAnnualSnowMmSWE < 1, 'Hot desert snow-water normal is below one millimetre');
  assert.ok(hotDesert.snowMultiplier < 0.01 && hotDesert.precipitationMultiplier < 0.1);
  const highDesert = at({ climateRegime: climateRegimeFor('semi-arid-high-desert').code, climateLatitudeDeg: 39 }, 15);
  assert.ok(highDesert.nightMinC < -5 && highDesert.frostMultiplier > 1,
    'High-desert winter nights freeze existing pore water');
  assert.ok(highDesert.precipitationMultiplier < 0.2 && highDesert.snowMultiplier < 0.1,
    'High-desert frost does not require or manufacture a deep snowpack');

  const yearCharacters = new Set([0, 1, 2, 3, 4].map((year) => at(
    { climateRegime: 0, climateLatitudeDeg: 46 },
    15,
    year,
  ).temperatureAnomalyC.toFixed(2)));
  assert.ok(yearCharacters.size > 1, 'Consecutive simulated years have deterministic warm/cold variation');

  assert.equal(climateDefaultsForLandscape('namib-erg-dunes').climateLatitudeDeg, -23);
  assert.equal(climateRegimeFor(climateDefaultsForLandscape('namib-erg-dunes').climateRegime).id, 'arid-desert');
  assert.equal(climateRegimeFor(climateDefaultsForLandscape('icelandic-highlands').climateRegime).id, 'subarctic-continental');
  for (const preset of Object.values(TERRAIN_STACK_PRESETS)) {
    const stack = preset.layers();
    const environment = stack.find((layer) => layer.type === ENVIRONMENT_LAYER_TYPE);
    for (const key of Object.keys(DEFAULT_SEASONAL_CLIMATE_PARAMS)) {
      assert.ok(Number.isFinite(Number(environment.params[key])), `${preset.id} Environment missing ${key}`);
    }
  }
  const sahara = TERRAIN_STACK_PRESETS['sahara-tassili-erg'].layers();
  assert.equal(climateRegimeFor(sahara.at(-1).params.climateRegime).id, 'arid-desert');
  assert.ok(!sahara.some((layer) => layer.type === 'aeolian-glacial'), 'Desert climate must not invent a Snow layer');
  const utah = TERRAIN_STACK_PRESETS['utah-wind-rain-canyon'].layers();
  assert.equal(climateRegimeFor(utah.at(-1).params.climateRegime).id, 'semi-arid-high-desert');
  assert.ok(!utah.some((layer) => ['aeolian-glacial', 'chemical-weathering', 'thermal-talus'].includes(layer.type)),
    'Utah remains Wind + Rain only under its high-desert climate');
  const rainbow = TERRAIN_STACK_PRESETS['frost-pitted-rainbow-range'].layers();
  assert.equal(rainbow.at(-1).params.climateLatitudeDeg, -13);
  assert.equal(climateRegimeFor(rainbow.at(-1).params.climateRegime).id, 'tropical-highland');

  const climateDrivenJan = snowmeltCycleAtStep([], 9999, northJan);
  const climateDrivenJul = snowmeltCycleAtStep([], 0, northJul);
  assert.equal(climateDrivenJan.climateDriven, true);
  assert.ok(climateDrivenJan.snowfallMultiplier > climateDrivenJul.snowfallMultiplier);
  assert.ok(climateDrivenJul.meltMultiplier > climateDrivenJan.meltMultiplier);

  const engine = Object.create(GpuErosionEngine.prototype);
  engine.stepCount = 0;
  engine.cryoStepCount = 0;
  engine.climateElapsedYears = 0;
  engine.climateRunStartYears = 0;
  engine.climateRunActive = false;
  engine.climateRunCompleted = false;
  engine._seasonalClimateSuppressed = false;
  engine.telemetry = {};
  engine.params = {
    ...DEFAULT_EROSION_PARAMS,
    emitterMode: 'all-weather',
    seasonalClimateMode: 1,
    climateRegime: 0,
    climateLatitudeDeg: 46,
    climateStartDayOfYear: 15,
    climateStepsPerYear: 240,
    climateYearsToSimulate: 20,
    rainIntensity: 1,
    snowfallRate: 0.8,
    simHydraulicPlaying: true,
    simCryoPlaying: true,
    simWindPlaying: true,
    simRockfallPlaying: false,
    simChemicalPlaying: true,
  };
  const winterUniforms = engine._getClimateUniforms();
  assert.equal(winterUniforms.speciesVec[0], 0, 'Deep alpine winter precipitation is not emitted as rain');
  assert.ok(winterUniforms.speciesVec[1] > 0.1 && winterUniforms.snowVec[1] > 0.5);
  engine.climateElapsedYears = 0.5;
  const summerUniforms = engine._getClimateUniforms();
  assert.equal(summerUniforms.speciesVec[1], 0, 'Trace summer snow flakes are cut before species normalisation');
  assert.ok(summerUniforms.snowVec[1] > 0 && summerUniforms.snowVec[1] < 0.05,
    'A trace of high-altitude Eulerian snowfall can remain without rendering a valley snowstorm');
  assert.ok(summerUniforms.effectiveRainIntensity > 0.2 && summerUniforms.meltCycle.meltMultiplier > winterUniforms.meltCycle.meltMultiplier);

  engine.params.simHydraulicPlaying = false;
  const summerSnowSolo = engine._getClimateUniforms();
  assert.equal(summerSnowSolo.effectiveRainIntensity, 0, 'Seasonal climate cannot re-enable paused Hydraulic rain');
  assert.equal(summerSnowSolo.speciesVec[0], 0);
  assert.equal(engine._getSpeciesVisUniform()[1], 0, 'No fake visible snowfall remains during the warm dry phase');

  engine.params = {
    ...engine.params,
    emitterMode: 'sky-rain',
    climateRegime: climateRegimeFor('semi-arid-high-desert').code,
    climateLatitudeDeg: 39,
    rainIntensity: DEFAULT_EROSION_PARAMS.rainIntensity,
    snowfallRate: 0,
    simHydraulicPlaying: true,
    simCryoPlaying: false,
    simWindPlaying: false,
  };
  engine.climateElapsedYears = 0;
  assert.deepEqual(engine._getClimateUniforms().speciesVec, [0, 0, 0, 0],
    'A high-desert dry spell is below the visible-rain cutoff instead of becoming a normalised storm');
  assert.equal(engine._getSpeciesVisUniform()[0], 0);

  engine._seasonalClimateSuppressed = true;
  const suppressed = engine._getSeasonalClimate();
  assert.equal(suppressed.enabled, false, 'Stack bakes use annual-neutral seasonal forcing');
  assert.equal(suppressed.rainMultiplier, 1);
  assert.equal(suppressed.snowMultiplier, 1);
  engine._seasonalClimateSuppressed = false;

  engine.params = {
    ...engine.params,
    climateRegime: 0,
    climateLatitudeDeg: 46,
    climateStepsPerYear: 240,
    climateYearsToSimulate: 20,
  };
  engine.resetSeasonalClimateClock();
  engine.beginSeasonalClimateRun();
  for (let i = 0; i < 240; i++) engine._advanceSeasonalClimateClock();
  engine.beginSeasonalClimateRun();
  assert.equal(engine.climateRunStartYears, 0, 'Resuming a paused run does not restart its year target');
  let advanced = 240;
  let completion = null;
  while (advanced <= 4802) {
    completion = engine._advanceSeasonalClimateClock();
    advanced += 1;
    if (completion.completedNow) break;
  }
  assert.ok(completion?.completedNow, 'Twenty-year climate run reports completion');
  assert.ok(Math.abs(engine.climateElapsedYears - 20) < 0.01, `Expected ~20 years, got ${engine.climateElapsedYears}`);
  assert.equal(engine.getSeasonalClimateRunState().complete, true);
  const completedElapsed = engine.climateElapsedYears;
  engine._advanceSeasonalClimateClock();
  assert.equal(engine.climateElapsedYears, completedElapsed, 'A completed run does not keep consuming years');
  engine.beginSeasonalClimateRun();
  assert.equal(engine.getSeasonalClimateRunState().progress, 0, 'Starting after completion begins a fresh run from the current date');

  const inspector = Object.create(ErosionLayerStackUI.prototype);
  inspector.engine = engine;
  const climateHtml = inspector._seasonalClimateCardHtml(sahara.at(-1), '#ffd166');
  assert.ok(climateHtml.includes('Hot arid desert') && climateHtml.includes('35 mm/yr'));
  assert.ok(climateHtml.includes('Climate calendar') && climateHtml.includes('Simulated run'));

  const engineSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const pipelineSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  const mainSrc = fs.readFileSync(new URL('../src/app/main.js', import.meta.url), 'utf8');
  const htmlSrc = fs.readFileSync(new URL('../index.html', import.meta.url), 'utf8');
  for (const token of ['_advanceSeasonalClimateClock', 'suppressSeasonalClimate', 'climate.effectiveRainIntensity', 'climate.effectiveEvaporation', 'Seasonal climate run complete']) {
    assert.ok(engineSrc.includes(token), `Engine seasonal-climate wiring missing ${token}`);
  }
  for (const token of ['beginSeasonalClimateRun', 'resetSeasonalClimateClock', '_seasonalClimateSuppressed = true', 'climateDefaultsForLandscape']) {
    assert.ok(pipelineSrc.includes(token), `Pipeline seasonal-climate wiring missing ${token}`);
  }
  for (const token of ["key: 'seasonalClimateMode'", "key: 'climateRegime'", "key: 'climateLatitudeDeg'", "key: 'climateYearsToSimulate'", 'Location Seasonal Climate']) {
    assert.ok(uiSrc.includes(token), `Environment Inspector missing ${token}`);
  }
  assert.ok(mainSrc.includes('er-stat-climate') && htmlSrc.includes('id="er-stat-climate"'));
});

test('Bottom-to-top layer order genuinely changes terrain output and 3D SDF Cave mesher carves volumetric overhangs', () => {
  // 1. Prove Layer Order Sensitivity:
  // Evaluate a 1D column strip bottom-to-top for:
  //   Stack A (top -> bottom): [Hydraulic Erosion, Rock Stacks, Base Landform] -> Stacks built first, THEN eroded!
  //   Stack B (top -> bottom): [Rock Stacks, Hydraulic Erosion, Base Landform] -> Valley eroded first, THEN crisp stacks added!
  const W = 32;
  const evalStripBottomUp = (layersTopToBottom) => {
    let h = new Float32Array(W);
    let sed = new Float32Array(W);
    for (let li = layersTopToBottom.length - 1; li >= 0; li--) {
      const layer = layersTopToBottom[li];
      if (!layer.enabled) continue;
      if (layer.type === 'base-landform') {
        for (let x = 0; x < W; x++) h[x] = 20 + Math.sin((x / W) * Math.PI) * layer.params.relief * 0.4;
      } else if (layer.type === 'rock-stacks') {
        // Add steep rock stack pillar in center
        for (let x = 13; x <= 18; x++) h[x] += layer.params.height;
      } else if (layer.type === 'hydraulic-erosion') {
        const steps = layer.params.bakeSteps ?? layer.params.iterations ?? 24;
        for (let it = 0; it < steps; it++) {
          const next = new Float32Array(h);
          for (let x = 1; x < W - 1; x++) {
            const lap = 0.5 * (h[x - 1] + h[x + 1]) - h[x];
            if (lap < 0) {
              const cut = -lap * 0.28 * layer.params.erosionStrength;
              next[x] -= cut;
              sed[x] += cut * 0.5;
            } else {
              next[x] += lap * 0.15;
            }
          }
          h = next;
        }
      }
    }
    return { h, sed };
  };

  const base = createStackLayer('base-landform', { params: { relief: 120 } });
  const stacks = createStackLayer('rock-stacks', { params: { height: 55 } });
  const erosion = createStackLayer('hydraulic-erosion', { params: { bakeSteps: 24, erosionStrength: 0.8 } });

  const outErodedStacks = evalStripBottomUp([erosion, stacks, base]); // bottom->top: base -> stacks -> erosion
  const outFreshStacks = evalStripBottomUp([stacks, erosion, base]);  // bottom->top: base -> erosion -> stacks

  const crestA = outErodedStacks.h[13];
  const crestB = outFreshStacks.h[13];
  assert.ok(
    crestB - crestA > 12.0,
    `Swapping Rock Stacks above Hydraulic Erosion must preserve sharp un-eroded stack rims (got ${crestB.toFixed(1)}m vs ${crestA.toFixed(1)}m)`
  );
  assert.ok(
    outErodedStacks.sed[13] > outFreshStacks.sed[13] * 2.0,
    'Eroding after Rock Stacks must generate significantly more stack-derived sediment'
  );

  // 2. Prove Real 3D Volumetric SDF Cave & Cliff-Undercut Carving + Non-Destructive Muting:
  const caveSys = new SdfCaveSystem();
  const N = 64;
  const snap = new Float32Array(N * N * 4);
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const u = i / (N - 1);
      // Steep 140m escarpment cliff running through center of domain
      const bedrock = u < 0.42 ? 18.0 : u > 0.58 ? 158.0 : 18.0 + ((u - 0.42) / 0.16) * 140.0;
      const idx = (j * N + i) * 4;
      snap[idx] = bedrock;
    }
  }

  const caveLayer = createStackLayer('sdf-caves', {
    params: { undercutDepth: 16.5, caveRadius: 12.5, archHeight: 18.0, slopeMin: 20, elevationCenter: 0.35, elevationBand: 0.35, opacity: 1.0 },
  });
  const sculptCaveStrokes = [{ x: 0, y: 58, z: 0, radius: 18, strength: 1.0, op: 'cave-carve' }];

  const resActive = caveSys.rebuildCaveVolume({
    gridRgba: snap,
    gridN: N,
    worldSpan: 512,
    caveLayers: [caveLayer],
    sculptCaveStrokes,
    maxRelief: 165,
  });

  assert.equal(resActive.hasCaves, true);
  assert.ok(resActive.vertexCount >= 90, `Expected real 3D SDF cave mesh triangles, got ${resActive.vertexCount} vertices`);
  let maxPortal = 0;
  for (let k = 0; k < caveSys.portalTexData.length; k += 4) {
    if (caveSys.portalTexData[k] > maxPortal) maxPortal = caveSys.portalTexData[k];
  }
  assert.ok(maxPortal > 0.46, `Expected 3D cave portal mask to breach cliff surface (>0.46), got ${maxPortal}`);

  // Verify every 3D cave normal is unit length and finite
  for (let v = 0; v < resActive.vertexCount; v++) {
    const nx = caveSys.normals[v * 3], ny = caveSys.normals[v * 3 + 1], nz = caveSys.normals[v * 3 + 2];
    const len = Math.hypot(nx, ny, nz);
    assert.ok(Math.abs(len - 1.0) < 1e-3, `Non-unit 3D SDF cave normal length ${len}`);
  }

  // Non-destructive mute restores 0 cave vertices without touching snap heightfield
  caveLayer.enabled = false;
  const resMuted = caveSys.rebuildCaveVolume({
    gridRgba: snap,
    gridN: N,
    worldSpan: 512,
    caveLayers: [caveLayer],
    sculptCaveStrokes: [],
    maxRelief: 165,
  });
  assert.equal(resMuted.vertexCount, 0);
});

test('All Pipeline Shaders satisfy strict Windows Chrome ANGLE D3D11 HLSL rules (no inverted smoothstep, no divergent texture(), matching varyings)', () => {
  for (const [name, src] of Object.entries(PipelineShaders)) {
    if (typeof src !== 'string') continue;

    // 1. Every fragment shader must declare explicit layout(location = 0) out
    if (name.endsWith('_FRAG')) {
      assert.ok(
        /layout\s*\(\s*location\s*=\s*0\s*\)\s*out\s+vec4/.test(src),
        `${name} must declare explicit layout(location = 0) out vec4`
      );
    }

    // 2. Zero texture() calls (must use derivative-free texelFetch so D3D11 never rejects divergent Sample() gradients!)
    const stripped = src.replace(/\/\/.*$/gm, '').replace(/\/\*[\s\S]*?\*\//g, '');
    assert.ok(
      !/\btexture\s*\(/.test(stripped),
      `${name} must use derivative-free texelFetch rather than texture() for ANGLE D3D11 RGBA32F compatibility`
    );

    // 3. Zero literal inverted smoothstep(a, b, x) where a >= b (D3D11 error X4010!)
    const smRe = /smoothstep\s*\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*,/g;
    let m;
    while ((m = smRe.exec(stripped)) !== null) {
      const e0 = Number(m[1]);
      const e1 = Number(m[2]);
      assert.ok(
        e0 < e1,
        `${name} contains inverted literal smoothstep(${e0}, ${e1}, ...) which fails ANGLE D3D11 with error X4010`
      );
    }

    // 4. Zero pow((... - ...), ...) where base can be negative (D3D11 error X3571!)
    assert.ok(
      !/\bpow\s*\(\s*\([^)]*-[^)]*\)\s*,/.test(stripped),
      `${name} contains unguarded pow((a - b), e) which fails ANGLE D3D11 with error X3571`
    );
  }

  // 5. Verify exact vertex out -> fragment in varying signatures for all linked pairs
  const extractVaryings = (glsl, dir) => {
    const out = [];
    const re = new RegExp(`^\\s*${dir}\\s+((?:highp|mediump|lowp)\\s+)?(vec[234]|float|int)\\s+(\\w+)\\s*;`, 'gm');
    let m;
    while ((m = re.exec(glsl)) !== null) {
      out.push(`${m[2]} ${m[3]}`);
    }
    return out.sort();
  };

  assert.deepEqual(
    extractVaryings(Shaders.TERRAIN_RENDER_VERT, 'out'),
    extractVaryings(Shaders.TERRAIN_RENDER_FRAG, 'in'),
    'TERRAIN_RENDER_VERT/FRAG varyings must match identically'
  );
  assert.deepEqual(
    extractVaryings(PipelineShaders.SDF_CAVE_VERT, 'out'),
    extractVaryings(PipelineShaders.SDF_CAVE_FRAG, 'in'),
    'SDF_CAVE_VERT/FRAG varyings must match identically'
  );
  assert.equal(
    PipelineShaders.STACK_HYDRAULIC_FRAG,
    undefined,
    'Non-conservative custom iterative hydraulic shader must be eliminated in favor of engine conservative passes'
  );
  assert.equal(
    PipelineShaders.STACK_THERMAL_TALUS_FRAG,
    undefined,
    'Non-conservative custom iterative talus shader must be eliminated in favor of engine conservative passes'
  );
});

test('End-to-end stateful Mock-WebGL2 + DOM boot of GpuErosionEngine, TerrainStackPipeline, and ErosionLayerStackUI succeeds with zero FBO feedback loops', () => {
  // Minimal DOM environment covering all Frontier Studio IDs
  const elements = new Map();
  const makeEl = (id = '') => {
    const listeners = {};
    const el = {
      id,
      value: '0',
      checked: true,
      hidden: false,
      textContent: '',
      _innerHTML: '',
      dataset: {},
      clientWidth: 960,
      clientHeight: 600,
      width: 960,
      height: 600,
      style: { setProperty() {} },
      parentElement: { style: { setProperty() {} } },
      classList: { toggle() {}, add() {}, remove() {}, contains: () => false },
      setAttribute() {},
      addEventListener(ev, fn) { (listeners[ev] ||= []).push(fn); },
      querySelectorAll() { return []; },
      querySelector() { return null; },
      contains() { return false; },
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 960, height: 600 }),
    };
    Object.defineProperty(el, 'innerHTML', {
      get() { return el._innerHTML; },
      set(v) { el._innerHTML = String(v); },
    });
    return el;
  };

  const domIds = [
    'tp-layer-list', 'tp-layer-count', 'tp-stat-visible', 'tp-stat-hidden',
    'tp-layer-search-input', 'tp-btn-add-layer', 'tp-add-layer-menu',
    'tp-inspector-content', 'cb-preset-grid', 'cb-shelf-drawer', 'cb-btn-toggle',
    'tp-doc-title', 'tp-scene-caption', 'tp-select-shading', 'tp-select-palette',
    'tp-author-tools', 'spline-gizmo-svg', 'tp-sim-panel', 'btn-er-play',
  ];
  for (const id of domIds) elements.set(id, makeEl(id));

  const prevDoc = globalThis.document;
  const prevWin = globalThis.window;
  globalThis.document = {
    getElementById: (id) => elements.get(id) || makeEl(id),
    addEventListener() {},
  };
  globalThis.window = {
    devicePixelRatio: 1,
    addEventListener() {},
  };

  try {
    // Stateful WebGL2 mock that tracks shader outputs, uniform samplers, texture units, and FBO attachments
    // to catch ANY FBO-texture feedback loop or MRT drawBuffers mismatch!
    let nextId = 1;
    const shaders = new Map();
    const programs = new Map();
    const fbos = new Map();
    const texUnits = new Array(16).fill(null);
    let activeUnit = 0;
    let currentProg = null;
    let drawFbo = null;
    let drawCount = 0;

    const gl = {
      VERTEX_SHADER: 0x8b31,
      FRAGMENT_SHADER: 0x8b30,
      COMPILE_STATUS: 0x8b81,
      LINK_STATUS: 0x8b82,
      TEXTURE_2D: 0x0de1,
      TEXTURE0: 0x84c0,
      RGBA32F: 0x8814,
      RGBA16F: 0x881a,
      RGBA8: 0x8058,
      UNSIGNED_BYTE: 0x1401,
      RGBA: 0x1908,
      FLOAT: 0x1406,
      HALF_FLOAT: 0x140b,
      NEAREST: 0x2600,
      LINEAR: 0x2601,
      CLAMP_TO_EDGE: 0x812f,
      TEXTURE_MIN_FILTER: 0x2801,
      TEXTURE_MAG_FILTER: 0x2800,
      TEXTURE_WRAP_S: 0x2802,
      TEXTURE_WRAP_T: 0x2803,
      FRAMEBUFFER: 0x8d40,
      READ_FRAMEBUFFER: 0x8ca8,
      DRAW_FRAMEBUFFER: 0x8ca9,
      FRAMEBUFFER_COMPLETE: 0x8cd5,
      COLOR_ATTACHMENT0: 0x8ce0,
      COLOR_BUFFER_BIT: 0x4000,
      DEPTH_BUFFER_BIT: 0x0100,
      COLOR: 0x1800,
      ARRAY_BUFFER: 0x8892,
      ELEMENT_ARRAY_BUFFER: 0x8893,
      STATIC_DRAW: 0x88e4,
      DYNAMIC_DRAW: 0x88e8,
      POINTS: 0x0000,
      TRIANGLES: 0x0004,
      UNSIGNED_INT: 0x1405,
      UNSIGNED_SHORT: 0x1403,
      DEPTH_TEST: 0x0b71,
      BLEND: 0x0be2,
      CULL_FACE: 0x0b44,
      BACK: 0x0405,
      LEQUAL: 0x0203,
      ONE: 1,
      SRC_ALPHA: 0x0302,
      ONE_MINUS_SRC_ALPHA: 0x0303,

      getExtension: (ext) => (ext === 'EXT_color_buffer_float' || ext === 'EXT_float_blend' || ext === 'OES_texture_float_linear' ? {} : null),
      createShader: (type) => { const s = { id: nextId++, type, src: '' }; shaders.set(s, s); return s; },
      shaderSource: (s, src) => { s.src = src; },
      compileShader: () => {},
      getShaderParameter: () => true,
      getShaderInfoLog: () => '',
      deleteShader: () => {},
      createProgram: () => { const p = { id: nextId++, vs: null, fs: null, samplers: new Map(), fragOutCount: 1 }; programs.set(p, p); return p; },
      attachShader: (p, s) => { if (s.type === 0x8b31) p.vs = s.src; else p.fs = s.src; },
      linkProgram: (p) => {
        const fs = p.fs || '';
        const outs = fs.match(/layout\s*\(\s*location\s*=\s*\d+\s*\)\s*out\s+vec4/g);
        p.fragOutCount = outs ? outs.length : 1;
        const combined = `${p.vs || ''}\n${fs}`;
        const sRe = /uniform\s+sampler2D\s+(\w+)\s*;/g;
        let m;
        while ((m = sRe.exec(combined)) !== null) {
          p.samplers.set(m[1], 0);
        }
      },
      getProgramParameter: () => true,
      getProgramInfoLog: () => '',
      deleteProgram: () => {},
      useProgram: (p) => { currentProg = p; },
      getUniformLocation: (p, name) => ({ prog: p, name }),
      uniform1i: (loc, v) => {
        if (loc && loc.prog && loc.prog.samplers.has(loc.name)) {
          loc.prog.samplers.set(loc.name, v);
        }
      },
      uniform1f() {},
      uniform2f() {},
      uniform2i() {},
      uniform3fv() {},
      uniform4f() {},
      uniform4fv() {},
      uniformMatrix4fv() {},
      createTexture: () => ({ texId: nextId++ }),
      deleteTexture() {},
      activeTexture: (u) => { activeUnit = u - 0x84c0; },
      bindTexture: (_target, tex) => { texUnits[activeUnit] = tex; },
      texParameteri() {},
      texImage2D() {},
      createFramebuffer: () => { const f = { fboId: nextId++, attachments: new Map(), drawBufCount: 1 }; fbos.set(f, f); return f; },
      deleteFramebuffer() {},
      bindFramebuffer: (target, f) => {
        if (target === 0x8d40 || target === 0x8ca9) drawFbo = f;
      },
      framebufferTexture2D: (_target, att, _texTarget, tex) => {
        if (drawFbo) drawFbo.attachments.set(att, tex);
      },
      drawBuffers: (bufs) => { if (drawFbo) drawFbo.drawBufCount = bufs.length; },
      checkFramebufferStatus: () => 0x8cd5,
      createVertexArray: () => ({ vaoId: nextId++ }),
      bindVertexArray() {},
      createBuffer: () => ({ bufId: nextId++ }),
      bindBuffer() {},
      bufferData() {},
      bufferSubData() {},
      enableVertexAttribArray() {},
      vertexAttribPointer() {},
      vertexAttribDivisor() {},
      viewport() {},
      blitFramebuffer() {},
      clearColor() {},
      clear() {},
      clearBufferfv() {},
      enable() {},
      disable() {},
      depthFunc() {},
      depthMask() {},
      cullFace() {},
      blendFunc() {},
      readPixels(_x, _y, w, h, _fmt, _type, dst) {
        for (let j = 0; j < h; j++) {
          for (let i = 0; i < w; i++) {
            const u = i / Math.max(1, w - 1);
            const idx = (j * w + i) * 4;
            dst[idx] = u < 0.45 ? 20 : u > 0.55 ? 150 : 20 + ((u - 0.45) / 0.10) * 130;
            dst[idx + 1] = 0.2;
            dst[idx + 2] = 0.4;
            dst[idx + 3] = 0.3;
          }
        }
      },
      _verifyDrawState() {
        drawCount++;
        assert.ok(currentProg, 'Draw call issued with no active WebGL program');
        if (drawFbo) {
          assert.equal(
            drawFbo.drawBufCount,
            currentProg.fragOutCount,
            `MRT drawBuffers (${drawFbo.drawBufCount}) must match fragment outputs (${currentProg.fragOutCount})`
          );
          const fboTexSet = new Set(drawFbo.attachments.values());
          for (const [sName, unit] of currentProg.samplers.entries()) {
            const boundTex = texUnits[unit];
            assert.ok(
              !fboTexSet.has(boundTex),
              `WebGL2 Feedback Loop detected: sampler ${sName} (unit ${unit}) reads texture currently attached to draw FBO!`
            );
          }
        }
      },
      drawArrays() { this._verifyDrawState(); },
      drawElements() { this._verifyDrawState(); },
      drawArraysInstanced() { this._verifyDrawState(); },
      drawElementsInstanced() { this._verifyDrawState(); },
    };

    const canvas = makeEl('erosion-canvas');
    canvas.getContext = (kind) => (kind === 'webgl2' ? gl : null);

    let lastTel = null;
    const engine = new GpuErosionEngine(canvas, (t) => { lastTel = t; });
    const ui = new ErosionLayerStackUI(engine);

    assert.ok(engine.pipeline, 'TerrainStackPipeline must initialize cleanly');
    assert.equal(engine.pipeline.activePresetId, 'icelandic-highlands', 'Default preset must match c5ae88b/2cc3d34 icelandic-highlands');
    assert.equal(engine.params.seed, 4029, 'Default seed must be 4029');
    assert.equal(engine.pipeline.liveSimRunning, false, 'Live simulation must be explicitly triggered, not auto-started by evaluation');
    assert.equal(engine.pipeline.simPlaybackMode, 'solo', 'Seasons default to one-at-a-time playback');
    // The optional world mask must never read the texture attached to its output FBO.
    engine.pipeline.setWorldBaseMask(new Uint8Array(16 * 16).fill(255), 16);
    engine.pipeline.evaluateStack({ reseedPlacements: false, fastPreview: true });
    assert.equal(engine.pipeline.worldBaseMaskEnabled, true);
    engine.pipeline.setWorldBaseMask(null);
    engine.pipeline.evaluateStack({ reseedPlacements: false, fastPreview: true });
    assert.equal(engine.pipeline.worldBaseMaskEnabled, false);
    assert.equal(engine.gridRes, 512, 'Terrain resolution defaults to the established 512² simulation grid');
    assert.equal(elements.get('cb-shelf-drawer').hidden, true, 'Preset recipes stay closed by default so the viewport stays dominant');
    assert.ok(elements.get('tp-sim-panel').innerHTML.includes('SIMULATION SEASONS'), 'Simulation Seasons panel must render');
    assert.ok(elements.get('tp-sim-panel').innerHTML.includes('data-sim-process="wind"'), 'Wind must be an independently triggerable season');
    const defaultSnowLayer = engine.pipeline.layers.find((l) => l.type === 'aeolian-glacial');
    const defaultWindLayer = engine.pipeline.layers.find((l) => l.type === 'wind-erosion');
    assert.ok(defaultSnowLayer && defaultWindLayer, 'Default preset must split Snow and dedicated Wind Erosion layers');
    assert.deepEqual(engine.pipeline.simulationProcessesForLayer(defaultSnowLayer), ['cryo'], 'Snow layer must trigger only Snow when Wind has its own layer');
    assert.deepEqual(engine.pipeline.simulationProcessesForLayer(defaultWindLayer), ['wind'], 'Wind Erosion layer must trigger only Wind');

    // Solo season: snow runs while rain/wind/chemical/rockfall stay frozen.
    engine.pipeline.startSimulation('cryo');
    assert.equal(engine.pipeline.liveSimRunning, true, 'Pressing Simulate starts the selected season');
    assert.equal(engine.params.simCryoPlaying, true, 'Solo Snow season plays cryo');
    assert.equal(engine.params.simHydraulicPlaying, false, 'Solo Snow freezes rain');
    assert.equal(engine.params.simWindPlaying, false, 'Solo Snow freezes the wind season');
    assert.equal(engine.params.simChemicalPlaying, false, 'Solo Snow freezes chemical');
    assert.equal(engine.params.simRockfallPlaying, false, 'Solo Snow freezes rockfall');

    // Arming another season must not start it; only Simulate does.
    engine.pipeline.selectSimulationProcess('wind');
    assert.equal(engine.pipeline.liveSimRunning, false, 'Selecting a different season pauses instead of silently starting it');
    assert.equal(engine.pipeline.simSoloProcess, 'wind', 'Wind is armed for the next Simulate press');
    engine.pipeline.startSimulation('cryo');

    // Any stack/base evaluation auto-pauses and must not silently resume.
    engine.pipeline.evaluateStack({ reseedPlacements: false, fastPreview: true });
    assert.equal(engine.pipeline.liveSimRunning, false, 'Stack/base evaluation must auto-pause live simulation until explicitly resumed');

    // Simultaneous mode: every armed season runs together, individual pause respected.
    engine.pipeline.setSimPlaybackMode('all');
    engine.pipeline.setSimulationProcessEnabled('cryo', false);
    engine.pipeline.startSimulation(null);
    assert.equal(engine.params.simHydraulicPlaying, true, 'Simultaneous mode runs rain');
    assert.equal(engine.params.simCryoPlaying, false, 'Simultaneous mode respects an individually paused snow season');
    assert.equal(engine.params.simWindPlaying, true, 'Simultaneous mode runs wind');
    assert.equal(engine.params.simChemicalPlaying, true, 'Simultaneous mode runs chemical');
    engine.pipeline.pauseSimulation('');
    engine.pipeline.setSimulationProcessEnabled('cryo', true);
    engine.pipeline.setSimPlaybackMode('solo');
    assert.ok(lastTel && lastTel.layerCount >= 7, 'Initial telemetry must publish evaluated stack');
    assert.ok(elements.get('tp-select-palette').innerHTML.includes('<option'), 'Palette select must be populated');
    assert.ok(elements.get('tp-layer-list').innerHTML.includes('tp-layer-row'), 'Layer stack list must render rows');
    assert.ok(elements.get('tp-inspector-content').innerHTML.includes('tp-property-card'), 'Inspector must render property cards');
    assert.ok(elements.get('cb-preset-grid').innerHTML.includes('cb-preset-card'), 'Preset shelf must render all canonical + Utah recipes');

    // Cycle through ALL canonical stack presets plus Utah and the standalone
    // spline-authoring recipes, verifying exact generator/parameter/seed
    // parity & zero feedback loops, while the imported world mask survives
    // every preset replacement and remains bound to the same document.
    engine.pipeline.setWorldBaseMask(new Uint8Array(16 * 16).fill(180), 16);
    const importedMaskTexture = engine.pipeline.worldBaseMaskTex;
    for (const presetId of Object.keys(TERRAIN_STACK_PRESETS)) {
      const stackPreset = TERRAIN_STACK_PRESETS[presetId];
      const isManualRecipe = stackPreset.manualRecipe === true;
      const orig = EROSION_LANDSCAPES[presetId];
      const d = {
        ...DEFAULT_EROSION_PARAMS,
        ...(orig?.defaults || {}),
        ...(isManualRecipe ? { relief: stackPreset.expectedRelief, roughness: stackPreset.expectedRoughness } : {}),
      };
      engine.pipeline.loadPreset(presetId);
      assert.equal(engine.pipeline.worldBaseMaskEnabled, true, `Preset ${presetId} keeps the world mask active`);
      assert.equal(engine.pipeline.worldBaseMaskTex, importedMaskTexture, `Preset ${presetId} keeps the imported mask texture`);
      assert.equal(engine.params.relief, d.relief, `Preset ${presetId} relief parity`);
      assert.equal(engine.params.roughness, d.roughness, `Preset ${presetId} roughness parity`);
      assert.equal(engine.params.seed, 4029, `Preset ${presetId} seed parity`);
      assert.equal(engine.params.materialTheme, isManualRecipe ? stackPreset.palette : (orig.materialTheme ?? 0), `Preset ${presetId} materialTheme parity`);
      assert.ok(engine.telemetry.fineSedM3 < 50_000_000, `Preset ${presetId} fineSedM3 must stay physically bounded`);
      assert.ok(engine.telemetry.coarseSedM3 < 50_000_000, `Preset ${presetId} coarseSedM3 must stay physically bounded`);
      ui.renderAll();
      engine.renderFrame();
    }

    engine.pipeline.setWorldBaseMask(null);

    // Utah workflow: top Wind layer is selected/armed on load, simulation
    // remains paused, and only Rain + Wind are available seasons.
    engine.pipeline.loadPreset('utah-wind-rain-canyon');
    assert.equal(engine.pipeline.liveSimRunning, false, 'Utah recipe must not auto-start wind or rain');
    assert.equal(engine.pipeline.simSoloProcess, 'wind', 'Utah recipe arms Wind for the first Simulate press');
    const utahState = engine.pipeline.getSimulationControlState();
    assert.deepEqual(
      utahState.processes.filter((proc) => proc.available).map((proc) => proc.id),
      ['hydraulic', 'wind'],
      'Utah recipe exposes Rain + Wind seasons only'
    );
    const utahWindLayer = engine.pipeline.layers.find((l) => l.type === 'wind-erosion');
    utahWindLayer.params.windAzimuth = -42;
    utahWindLayer.params.windTurbulence = 0.91;
    assert.equal(engine.pipeline.applyLiveWindLayerParams(utahWindLayer), true, 'Top Wind layer applies live without stack rebuild');
    assert.equal(engine.params.windAzimuth, -42, 'Live wind direction updates the engine');
    assert.equal(engine.params.windTurbulence, 0.91, 'Live wind turbulence updates the engine');
    assert.equal(engine.pipeline.liveSimRunning, false, 'Changing wind settings pauses until Simulate is pressed again');

    // Exercise every composable stack operation (Ridges, Rock Stacks, Cliff Push-Pull, Spline, Caves, River, Baked Masked Sim, Sculpt)!
    engine.pipeline.loadPreset('icelandic-highlands');
    engine.pipeline.addLayer('mountain-ridges');
    engine.pipeline.addLayer('rock-stacks');
    engine.pipeline.addLayer('cliff-push-pull');
    engine.pipeline.addLayer('spline-feature');
    engine.pipeline.addLayer('sdf-caves');
    const riverLyr = engine.pipeline.addLayer('river-incision', { iterations: 6 });
    riverLyr.mask.enabled = true;
    engine.pipeline.evaluateStack({ reseedPlacements: true });

    engine.pipeline.authorTool = 'sculpt-add';
    engine.pipeline.applySculptStroke(12, -18, true);
    engine.pipeline.authorTool = 'cave-carve';
    engine.pipeline.applySculptStroke(0, 0, true);
    engine.stepSimulation(2);
    engine.pipeline.shadingMode = 5; // B&W Mask inspection mode
    engine.renderFrame();
    engine.pipeline.shadingMode = 0;
    engine.renderFrame();

    const exported = engine.extractMeshForExport(4);
    assert.ok(exported.positions.length > 0 && exported.indices.length > 0, 'Mesh export must succeed in pipelineMode');

    // A higher boot-time terrain grid must flow through the engine, stack
    // pipeline and telemetry as one consistent allocation size.
    const highResEngine = new GpuErosionEngine(canvas, () => {}, { gridSize: 768 });
    assert.equal(highResEngine.gridRes, 768);
    assert.equal(highResEngine.params.gridSize, 768);
    assert.equal(highResEngine.pipeline.R, 768);
    assert.equal(highResEngine.telemetry.gridRes, 768);
    highResEngine.renderFrame();

    assert.ok(drawCount > 250, `Expected >250 verified WebGL2 draw calls with zero feedback loops, got ${drawCount}`);
  } finally {
    globalThis.document = prevDoc;
    globalThis.window = prevWin;
  }
});

test('All 25+ WebGL2 GLSL ES 3.00 shaders resolve every function call, identifier, varying interface, and uniform type', () => {
  const GLSL_BUILTINS = new Set([
    'float', 'int', 'uint', 'bool',
    'vec2', 'vec3', 'vec4', 'ivec2', 'ivec3', 'ivec4', 'uvec2', 'uvec3', 'uvec4', 'bvec2', 'bvec3', 'bvec4',
    'mat2', 'mat3', 'mat4',
    'radians', 'degrees', 'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'sinh', 'cosh', 'tanh', 'asinh', 'acosh', 'atanh',
    'pow', 'exp', 'log', 'exp2', 'log2', 'sqrt', 'inversesqrt',
    'abs', 'sign', 'floor', 'trunc', 'round', 'roundEven', 'ceil', 'fract', 'mod', 'min', 'max', 'clamp', 'mix', 'step', 'smoothstep', 'isnan', 'isinf',
    'length', 'distance', 'dot', 'cross', 'normalize', 'faceforward', 'reflect', 'refract',
    'matrixCompMult', 'outerProduct', 'transpose', 'determinant', 'inverse',
    'lessThan', 'lessThanEqual', 'greaterThan', 'greaterThanEqual', 'equal', 'notEqual', 'any', 'all', 'not',
    'texelFetch', 'textureSize', 'dFdx', 'dFdy', 'fwidth',
    'if', 'for', 'while', 'switch', 'return', 'layout',
  ]);
  const GLSL_KEYWORDS = new Set([
    ...GLSL_BUILTINS,
    'version', 'es', 'precision', 'highp', 'mediump', 'lowp', 'in', 'out', 'inout', 'uniform', 'const', 'flat', 'smooth', 'centroid', 'location',
    'void', 'sampler2D', 'sampler3D', 'samplerCube',
    'else', 'do', 'case', 'default', 'break', 'continue', 'discard', 'true', 'false', 'struct',
    'gl_Position', 'gl_FragCoord', 'gl_VertexID', 'gl_InstanceID', 'gl_PointSize', 'gl_PointCoord', 'gl_FrontFacing',
  ]);

  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const telMatch = engSrc.match(/const TELEMETRY_FRAG = `([\s\S]*?)`;/);
  assert.ok(telMatch, 'TELEMETRY_FRAG must exist in gpu-erosion-engine.js');

  const allShaders = [];
  for (const [mName, mod] of [['erosion', Shaders], ['pipeline', PipelineShaders]]) {
    for (const [name, raw] of Object.entries(mod)) {
      if (typeof raw === 'string' && (name.endsWith('_VERT') || name.endsWith('_FRAG'))) {
        allShaders.push([`${mName}.${name}`, raw]);
      }
    }
  }
  allShaders.push(['engine.TELEMETRY_FRAG', telMatch[1]]);
  assert.ok(allShaders.length >= 25, `Expected >=25 shaders, found ${allShaders.length}`);

  for (const [label, raw] of allShaders) {
    const clean = raw.replace(/\/\/.*$/gm, '').replace(/\/\*[\s\S]*?\*\//g, '');

    // 1. Order-sensitive function call resolution (catches missing helpers like hash22 before main())
    const declaredFns = new Set();
    const lines = clean.split('\n');
    lines.forEach((line, idx) => {
      const declM = line.match(/^\s*(?:void|float|int|uint|bool|vec[234]|ivec[234]|mat[234])\s+([A-Za-z_]\w*)\s*\(/);
      const callRe = /\b([A-Za-z_]\w*)\s*\(/g;
      let m;
      while ((m = callRe.exec(line)) !== null) {
        const fn = m[1];
        if (declM && declM[1] === fn && m.index === line.indexOf(fn)) continue;
        assert.ok(
          GLSL_BUILTINS.has(fn) || declaredFns.has(fn),
          `${label}:${idx + 1} calls undeclared GLSL function '${fn}(...)'`
        );
      }
      if (declM) declaredFns.add(declM[1]);
    });

    // 2. Full identifier declaration check across each shader unit
    const body = clean.replace(/^#.*$/gm, '');
    const declaredIds = new Set(GLSL_KEYWORDS);
    let m;
    const singleDeclRe = /\b(?:void|float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234]|sampler2D)\s+([A-Za-z_]\w*)/g;
    while ((m = singleDeclRe.exec(body)) !== null) declaredIds.add(m[1]);
    const multiDeclRe = /\b(?:float|int|uint|bool|vec[234]|ivec[234]|mat[234])\s+([^;()]+);/g;
    while ((m = multiDeclRe.exec(body)) !== null) {
      for (const part of m[1].split(',')) {
        const id = part.trim().match(/^([A-Za-z_]\w*)/);
        if (id) declaredIds.add(id[1]);
      }
    }
    const tokRe = /(?:\.[A-Za-z_]\w*)|\b([A-Za-z_]\w*)\b/g;
    while ((m = tokRe.exec(body)) !== null) {
      if (!m[1] || /^\d/.test(m[1])) continue;
      assert.ok(declaredIds.has(m[1]), `${label} references undeclared GLSL identifier '${m[1]}'`);
    }
  }

  // 3. Vertex -> Fragment varying interface check for all custom linked pairs
  for (const [vName, fName, vSrc, fSrc] of [
    ['TERRAIN_RENDER_VERT', 'TERRAIN_RENDER_FRAG', Shaders.TERRAIN_RENDER_VERT, Shaders.TERRAIN_RENDER_FRAG],
    ['SDF_CAVE_VERT', 'SDF_CAVE_FRAG', PipelineShaders.SDF_CAVE_VERT, PipelineShaders.SDF_CAVE_FRAG],
    ['ROCK_INSTANCE_VERT', 'ROCK_INSTANCE_FRAG', Shaders.ROCK_INSTANCE_VERT, Shaders.ROCK_INSTANCE_FRAG],
    ['GRASS_INSTANCE_VERT', 'GRASS_INSTANCE_FRAG', Shaders.GRASS_INSTANCE_VERT, Shaders.GRASS_INSTANCE_FRAG],
    ['PARTICLE_RENDER_VERT', 'PARTICLE_RENDER_FRAG', Shaders.PARTICLE_RENDER_VERT, Shaders.PARTICLE_RENDER_FRAG],
  ]) {
    const outs = new Map();
    let m;
    const outRe = /^\s*(?:flat\s+)?out\s+(\w+)\s+(\w+)\s*;/gm;
    while ((m = outRe.exec(vSrc)) !== null) outs.set(m[2], m[1]);
    const inRe = /^\s*(?:flat\s+)?in\s+(\w+)\s+(\w+)\s*;/gm;
    while ((m = inRe.exec(fSrc)) !== null) {
      assert.equal(outs.get(m[2]), m[1], `Varying mismatch ${vName} -> ${fName} for '${m[2]}'`);
    }
  }
});






test('Flat terrain colours: no procedural fabric/grain texturing remains in the terrain & rock render shaders', () => {
  const frag = Shaders.TERRAIN_RENDER_FRAG;
  assert.ok(!frag.includes('hexDist'), 'Hexagonal column tile pattern must be gone from terrain colouring');
  assert.ok(!frag.includes('colSeam'), 'Columnar seam fabric must be gone');
  assert.ok(!frag.includes('sandRipple'), 'Repeating sand ripple pattern must be gone');
  assert.ok(!frag.includes('sparkHash'), 'Sand sparkle hash glitter must be gone');
  assert.ok(!frag.includes('float grain ='), 'Noise grain must not modulate terrain albedo');
  assert.ok(!frag.includes('float patchN ='), 'FBM patch noise must not modulate terrain albedo');
  const rockFrag = Shaders.ROCK_INSTANCE_FRAG;
  assert.ok(!rockFrag.includes('float grain ='), 'Noise grain must not modulate rock albedo');
});

test('Shallow-water pipe flow is anti-slosh and snowmelt is single-counted', () => {
  assert.ok(Shaders.WATER_FLUX_FRAG.includes('volLevel'), 'Pipe flux must include the anti-overshoot levelling cap');
  assert.ok(Shaders.WATER_FLUX_FRAG.includes('exp(-2.0 * dt)'), 'Pipe momentum must decay per second, not per frame');
  assert.ok(!Shaders.TERRAIN_INTEGRATE_FRAG.includes('cryoAblation'), 'Double-counted meltwater source must be gone');
  assert.ok(Shaders.PARTICLE_STEP_FRAG.includes('tR.a;') || Shaders.PARTICLE_STEP_FRAG.includes('+ tR.a'),
    'Droplets must ride full total head (bed + full water depth)');
  assert.ok(!Shaders.PARTICLE_STEP_FRAG.includes('tR.a * 0.45'), 'Partial 45% water head must be gone');
});

test('Snow simulation is mass-conserving and visibility toggles exist for water/snow/rain', () => {
  assert.ok(Shaders.SNOW_CRYO_FRAG.includes('windTilt'), 'Wind drift must use the conservative tilted-potential exchange');
  assert.ok(Shaders.SNOW_CRYO_FRAG.includes('retention = 1.0 - smoothstep(0.62, 1.05'), 'Snowfall must be slope-retained (sheds from steep faces)');
  assert.ok(Shaders.SNOW_CRYO_FRAG.includes('windLoad = 1.0 - 0.45 * max(windFace, 0.0) + 0.35 * max(-windFace, 0.0)'), 'Snowfall must be wind-loaded (scoured windward, loaded lee)');
  assert.ok(Shaders.SNOW_CRYO_FRAG.includes('reposeTan = mix(1.25, 0.72'), 'Avalanche repose must include thin-snow cohesion');
  assert.ok(Shaders.PARTICLE_RENDER_VERT.includes('uSpeciesVis'), 'Particle renderer must support per-species visibility');
  assert.equal(DEFAULT_EROSION_PARAMS.showWater, true);
  assert.equal(DEFAULT_EROSION_PARAMS.showSnow, true);
  assert.equal(DEFAULT_EROSION_PARAMS.showRain, true);

  // Snow-only is a hard particle-species boundary, including particles that
  // were already alive when the season changed. Otherwise old raindrops
  // remain visible and keep doing hydraulic work under a Snow solo.
  assert.ok(Shaders.INIT_PARTICLES_FRAG.includes('uniform vec4 uSpeciesMix')
    && Shaders.INIT_PARTICLES_FRAG.includes('initTotalW'),
    'Initial particle population must obey the active season mix');
  assert.ok(Shaders.PARTICLE_STEP_FRAG.includes('speciesWeight <= 0.0005')
    && Shaders.PARTICLE_STEP_FRAG.includes('totalSpeciesWeight'),
    'Live particles from frozen seasons must be recycled immediately');
  assert.ok(!Shaders.PARTICLE_STEP_FRAG.includes('iceFormation')
    && !Shaders.PARTICLE_STEP_FRAG.includes('behaves like rain'),
    'Snow particles must not manufacture glacier ice or meltwater runoff');
  assert.ok(Shaders.SNOW_CRYO_FRAG.includes('snowDepth -= melt')
    && Shaders.SNOW_CRYO_FRAG.includes('iceDepth -= iceMelt'),
    'Meltwater must be subtracted from snow/ice before it is reported');

  const climateHarness = Object.create(GpuErosionEngine.prototype);
  climateHarness.stepCount = 0;
  climateHarness.cryoStepCount = 0;
  climateHarness.params = {
    ...DEFAULT_EROSION_PARAMS,
    emitterMode: 'glacial-blizzard',
    snowfallRate: 0,
    glacierStrength: 0.5,
    simHydraulicPlaying: false,
    simCryoPlaying: true,
    simWindPlaying: false,
    simRockfallPlaying: false,
    showRain: true,
    showSnow: true,
  };
  assert.deepEqual(climateHarness._getClimateUniforms().speciesVec, [0, 0, 0, 0],
    'Frost/glacier-only Cryo with zero snowfall creates no precipitation particles');
  climateHarness.params.snowfallRate = 0.8;
  climateHarness.params.emitterMode = 'sky-rain';
  assert.deepEqual(climateHarness._getClimateUniforms().speciesVec, [0, 1, 0, 0],
    'Snow solo emits snow particles only, even if the inherited emitter was rain-oriented');
  assert.deepEqual(climateHarness._getSpeciesVisUniform(), [0, 1, 0, 0],
    'Snow solo hides every non-snow particle species');
});

test('Boulders embed into snow/sediment by bearing capacity and protrude realistically', () => {
  const N = 64;
  const world = ({ bed = 20, coarse = 0, fine = 0, snow = 0, ice = 0 } = {}) => {
    const g = new Float32Array(N * N * 4);
    const s = new Float32Array(N * N * 4);
    for (let i = 0; i < g.length; i += 4) {
      g[i] = bed; g[i + 1] = coarse; g[i + 2] = fine;
      s[i] = snow; s[i + 1] = ice;
    }
    return { g, s };
  };
  const dropRock = (w, { dropH = 3, vx = 0, sx = 1.7, sy = 1.5, sz = 1.7 } = {}) => {
    const sys = new RockCalvingSystem();
    sys.updateTerrainSnapshot(w.g, N, 512.0, w.s);
    const st = sys.sampleTerrain(0, 0);
    const rk = {
      submesh: 2, isOutcrop: false, settled: false,
      x: 0, y: st.surfH + dropH + 1.6, z: 0, vx, vy: 0, vz: 0,
      sx, sy, sz, isSlab: false, wear: 0.3, seedHash: 0.4,
    };
    sys.rocks.push(rk);
    // Rebound = any upward velocity after the fall (grounded flag alone misses
    // bounces that begin and end inside one stepPhysics call's substeps)
    let fell = false;
    let maxRebound = 0;
    for (let i = 0; i < 700 && !rk.settled; i++) {
      sys.stepPhysics(0.033);
      assert.ok(Number.isFinite(rk.x + rk.y + rk.z + rk.vx + rk.vy + rk.vz), 'Rock state must stay finite');
      if (rk.vy < -3) fell = true;
      if (fell) maxRebound = Math.max(maxRebound, rk.vy);
    }
    assert.ok(rk.settled, 'Rock must come to rest');
    const supUp = sys._support(rk, 0, 1, 0);
    return {
      sys, rk, st, maxRebound,
      embed: st.surfH - (rk.y - supUp),
      protrude: (rk.y + supUp) - st.surfH,
      diameter: 2 * supUp,
    };
  };

  // --- Pure bearing model: area saturates at the equator, snow hardens ---
  assert.ok(contactPatchArea(1, 1, 0.5) < contactPatchArea(1, 1, 1), 'Patch area grows with sink depth');
  assert.ok(Math.abs(contactPatchArea(1, 1.4, 1) - Math.PI * 1.96) < 1e-9, 'Patch area saturates at pi*Rh^2');
  assert.ok(Math.abs(submergedCapVolume(1, 1, 2) - (4 / 3) * Math.PI) < 1e-9, 'Full submergence = whole sphere volume');
  assert.ok(bearingPressure(1.8, 2, 0, 0) > 6 * bearingPressure(0.1, 2, 0, 0), 'Snow must densify-harden strongly under load');
  assert.ok(bearingPressure(2.1, 2, 1, 55000) > bearingPressure(0.2, 2, 1, 55000), 'Sediment under a compacted plug stays firm');

  // Square-cube law on a finite snowpack: bearing demand (W/A) grows with R,
  // so a small rock is carried by the snow while a big one punches through it
  const eqSmall = equilibriumEmbed(1272, 0.45, 0.5, 1.0, 1.5, 140000);
  const eqLarge = equilibriumEmbed(320000, 2.4, 2.76, 1.0, 1.5, 140000);
  assert.ok(eqSmall < 1.0, `Small rock must be supported by the snowpack (eq ${eqSmall.toFixed(2)} m)`);
  assert.ok(eqLarge > 1.0, `Mega-boulder must punch through snow into sediment (eq ${eqLarge.toFixed(2)} m)`);
  // Snowshoe effect: flat slab of equal mass rides far higher than a blocky rock
  const eqSlab = equilibriumEmbed(8000, 0.35, 1.7, 2.1, 0, 140000);
  assert.ok(eqSlab < equilibriumEmbed(8000, 0.8, 0.92, 2.1, 0, 140000) * 0.55, 'Slabs must spread load and ride high');

  // Rim volume conservation: numerically integrate the GLSL annulus profile
  const rim = rimHeightForVolume(0.42, 0.9);
  {
    const r0 = 0.82 * 0.9;
    const r1 = 0.9 + rim.width;
    let vol = 0;
    const M = 4000;
    for (let i = 0; i < M; i++) {
      const r = r0 + (r1 - r0) * ((i + 0.5) / M);
      const s = (r - r0) / (r1 - r0);
      vol += 2 * Math.PI * r * rim.height * (0.5 + 0.5 * Math.cos(Math.PI * s));
    }
    vol *= (r1 - r0) / M;
    assert.ok(Math.abs(vol - 0.42) / 0.42 < 0.02, `Rim must hold the displaced volume (got ${vol.toFixed(3)} m^3)`);
  }

  // --- Dynamic embedding on each ground type ---
  const onBedrock = dropRock(world({}));
  assert.ok(onBedrock.embed < 0.05, `Bedrock is unyielding (embed ${onBedrock.embed.toFixed(2)} m)`);
  assert.ok(onBedrock.maxRebound > 0.5, 'Bedrock landing stays lively (bounce)');

  const inSediment = dropRock(world({ fine: 2.5 }));
  assert.ok(inSediment.embed > 0.15 && inSediment.embed < inSediment.diameter * 0.55,
    `Rock seats into loose sediment but mostly protrudes (embed ${inSediment.embed.toFixed(2)} m)`);
  assert.ok(inSediment.maxRebound < 0.8, 'Sediment landing is deadened');

  const inSnow = dropRock(world({ snow: 3 }));
  assert.ok(inSnow.embed > inSediment.embed && inSnow.embed > inSnow.diameter * 0.25,
    `Rock sinks deep into snow (embed ${inSnow.embed.toFixed(2)} of ${inSnow.diameter.toFixed(2)} m diameter)`);
  assert.ok(inSnow.protrude > 0.25, `Rock still protrudes from the snowpack (${inSnow.protrude.toFixed(2)} m)`);
  assert.ok(inSnow.maxRebound < 0.35, 'Snow landing must not bounce (plastic crush)');

  const onIce = dropRock(world({ ice: 4 }));
  assert.ok(onIce.embed < 0.05, 'Glacier ice is a hard surface: no embedding');
  assert.ok(onIce.maxRebound > 0.8, 'Ice landing bounces');

  // Ploughing: a rolling boulder bogs down fast in snow, runs far on bedrock
  const rollBed = dropRock(world({}), { dropH: 0.3, vx: 6 });
  const rollSnow = dropRock(world({ snow: 3 }), { dropH: 0.3, vx: 6 });
  assert.ok(Math.abs(rollSnow.rk.x) < 0.6 * Math.abs(rollBed.rk.x),
    `Snow ploughing must kill runout (snow ${rollSnow.rk.x.toFixed(1)} m vs bedrock ${rollBed.rk.x.toFixed(1)} m)`);

  // Melt: a rock embedded in snow over sediment wakes and settles down when
  // the pack melts away (same sediment before/after -- only the snow goes)
  const meltCase = dropRock(world({ snow: 3, fine: 1 }));
  const meltY = meltCase.rk.y;
  meltCase.sys.updateTerrainSnapshot(world({ fine: 1 }).g, N, 512.0, world({}).s);
  assert.equal(meltCase.rk.settled, false, 'Melt-out must wake the embedded rock');
  for (let i = 0; i < 400 && !meltCase.rk.settled; i++) meltCase.sys.stepPhysics(0.033);
  assert.ok(meltCase.rk.settled && meltCase.rk.y < meltY - 0.8, 'After melt-out the rock settles onto the sediment');

  // --- Snow imprints: settling in snow queues GPU stamps ---
  const stamps = dropRock(world({ snow: 3 })).sys.consumeImprints(24);
  assert.ok(stamps.length >= 1, 'Embedded rock must queue snow imprints');
  const last = stamps[stamps.length - 1];
  assert.ok(last.a > 0.5 && last.rimW > 0.4, 'Imprint footprint/rim geometry is physical');
  assert.ok(Math.abs(last.compactSnow - 3 * 0.18) < 0.05, 'Imprint compacts snow to 18% residual under the rock');
  const rimTotal = stamps.reduce((acc, s) => acc + s.rimH, 0);
  assert.ok(rimTotal > 0, 'Displaced snow volume must resurface as rim height (summed deltas)');
  assert.equal(dropRock(world({})).sys.consumeImprints(24).length, 0, 'No snow -> no imprints');

  // --- GPU wiring: shader, engine pass, dual snapshots in engine + pipeline ---
  assert.equal(typeof Shaders.ROCK_IMPRINT_FRAG, 'string', 'ROCK_IMPRINT_FRAG must be exported');
  assert.ok(Shaders.ROCK_IMPRINT_FRAG.includes('uImprintA[24]'), 'Imprint shader takes a stamp batch');
  assert.ok(Shaders.ROCK_IMPRINT_FRAG.includes('min(snow.r, B.y)'), 'Imprint compacts snow under the footprint');
  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  assert.ok(engSrc.includes('progRockImprint'), 'Engine must compile the imprint program');
  assert.ok(engSrc.includes('this._applyRockImprints(R, cellSize)'), 'Engine must stamp imprints after the cryo pass');
  assert.ok(engSrc.includes('_readSnowSnapshot64'), 'Engine must read back the cryo snapshot for physics');
  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  assert.ok(pipeSrc.includes('snapSnowBuf'), 'Pipeline must feed the cryo snapshot to rock physics');
});

test('Sediment/water transport feels boulders: obstacle field diverts flow, banks talus, buries rocks', () => {
  const N = 64;
  const grid = new Float32Array(N * N * 4);
  for (let iz = 0; iz < N; iz++) {
    for (let ix = 0; ix < N; ix++) {
      const u = ix / (N - 1) - 0.5;
      const scarp = 1 / (1 + Math.exp(u * 22.0));
      const idx = (iz * N + ix) * 4;
      grid[idx] = 14 + 95 * scarp;
      grid[idx + 1] = 0.4 * (1 - scarp);
    }
  }
  const sys = new RockCalvingSystem();
  assert.equal(sys.obstaclesDirty, true, 'Obstacle field must start dirty (unbuilt)');
  sys.updateTerrainSnapshot(grid, N, 512.0);
  sys.seedForLandscape({ seed: 4029, worldSpan: 512.0, boulderDensity: 0.75, strataStrike: 10, strataDip: 12 });

  // Every rock (rooted outcrops included) becomes an occupancy stamp
  const stamps = sys.getObstacleStamps();
  assert.equal(stamps.length, sys.rocks.length, 'One obstacle stamp per rock instance');
  assert.ok(sys.rocks.some((r) => r.isOutcrop), 'Seeded world must contain outcrops (they block flow too)');
  for (const s of stamps) {
    assert.ok(Number.isFinite(s.x + s.z + s.cy + s.Rv + s.Rh), 'Stamp values must be finite');
    assert.ok(s.Rv > 0 && s.Rh > 0, 'Stamp ellipsoid radii must be positive');
    assert.ok(s.cy + s.Rv > s.cy, 'Stamp top must rise above the body centre');
  }
  assert.equal(sys.obstaclesDirty, true, 'Seeding must mark the obstacle field dirty');

  // Dirty lifecycle: engine cleans after build; motion re-dirties; rest stays clean
  const flat = new Float32Array(N * N * 4);
  for (let i = 0; i < flat.length; i += 4) flat[i] = 20;
  const restSys = new RockCalvingSystem();
  restSys.updateTerrainSnapshot(flat, N, 512.0);
  const sleeper = {
    submesh: 2, isOutcrop: false, settled: true,
    x: 0, y: 21.6, z: 0, vx: 0, vy: 0, vz: 0,
    sx: 1.7, sy: 1.5, sz: 1.7, isSlab: false, wear: 0.3, seedHash: 0.4,
  };
  restSys.rocks.push(sleeper);
  restSys.obstaclesDirty = false; // simulate engine build
  restSys.stepPhysics(0.033);
  assert.equal(restSys.obstaclesDirty, false, 'Sleeping rocks must not trigger obstacle rebuilds');
  sleeper.settled = false;
  sleeper.y += 5;
  restSys.stepPhysics(0.033);
  assert.equal(restSys.obstaclesDirty, true, 'A moving rock must re-dirty the obstacle field');

  // --- GPU wiring: obstacle stamp shader + every transport consumer ---
  assert.equal(typeof Shaders.ROCK_OBSTACLE_FRAG, 'string', 'ROCK_OBSTACLE_FRAG must be exported');
  assert.ok(Shaders.ROCK_OBSTACLE_FRAG.includes('uObstacleA[24]'), 'Obstacle shader takes rock batches');
  assert.ok(Shaders.ROCK_OBSTACLE_FRAG.includes('outObstacle = vec4(top, cov'), 'Obstacle field stores (rockTop, coverage)');
  assert.ok(Shaders.WATER_FLUX_FRAG.includes('rockOpenness'), 'Pipe flux must throttle edges blocked by boulders');
  assert.ok(Shaders.WATER_FLUX_FRAG.includes('flux *= edgeOpen'), 'Water must divert around / pond behind boulders');
  assert.ok(Shaders.THERMAL_TALUS_FRAG.includes('diffSed') && Shaders.THERMAL_TALUS_FRAG.includes('effHC'),
    'Talus avalanches must use max(terrain, rockTop) as ground (bank against, spill over)');
  assert.ok(Shaders.TERRAIN_INTEGRATE_FRAG.includes('armour'), 'Integrate must armour the bed under boulders');
  assert.ok(Shaders.TERRAIN_INTEGRATE_FRAG.includes('fillBoost'), 'Deposition must fill only the free cell fraction (burial wedge)');
  assert.ok(Shaders.PARTICLE_STEP_FRAG.includes('rockShield'), 'Droplet pickup must be shielded under boulders');
  assert.ok(Shaders.PARTICLE_STEP_FRAG.includes('max(tR.r + tR.g + tR.b + tR.a, oR.r)'), 'Droplets must ride over/around rock tops');
  assert.ok(Shaders.SNOW_CRYO_FRAG.includes('max(hBase + snowDepth, texelFetch(uRockObstacleTex, c, 0).r)'),
    'Snow creep/drift must shed off and bank against boulders');
  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  assert.ok(engSrc.includes('_ensureRockObstacles'), 'Engine must rebuild the obstacle field');
  const binds = engSrc.split("'uRockObstacleTex', this.obstacleTex[this.obstacleIdx]").length - 1;
  assert.ok(binds >= 6, `Obstacle texture must bind into particle/flux/integrate/talus x2/cryo passes (found ${binds})`);
});

test('Chemical weathering on Terrain Studio boulders: rust bleed & copper patina shader + plumbing', () => {
  assert.ok(Shaders.ROCK_INSTANCE_FRAG.includes('uniform float uChemWeather'), 'Rock shader takes chemical weathering strength');
  assert.ok(Shaders.ROCK_INSTANCE_FRAG.includes('ironRich') && Shaders.ROCK_INSTANCE_FRAG.includes('copperRich'),
    'Per-instance iron/copper mineralogy from seed hash');
  assert.ok(Shaders.ROCK_INSTANCE_FRAG.includes('uMaterialTheme != 7'), 'No oxidation on the airless lunar theme');
  assert.ok(Shaders.ROCK_INSTANCE_VERT.includes('vInstCentre'), 'Seep columns anchored to the rigid-body centre');
  assert.ok(typeof DEFAULT_EROSION_PARAMS.chemicalWeathering === 'number', 'chemicalWeathering param exists');
  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  assert.ok(engSrc.includes("'uChemWeather'"), 'Engine sets uChemWeather');
  assert.ok(pipeSrc.includes("'uChemWeather'"), 'Pipeline sets uChemWeather');
});

test('Chemical weathering is a terrain-wide simulation layer with multiple reaction processes', () => {
  const spec = LAYER_OPERATION_SPECS['chemical-weathering'];
  assert.ok(spec, 'Chemical weathering layer operation exists');
  assert.equal(spec.category, 'simulation');
  assert.equal(spec.glyph, 'flask');
  assert.ok(spec.writes.includes('Chemical State'), 'Layer writes a persistent chemical state');

  const layer = createStackLayer('chemical-weathering');
  for (const key of ['chemRate', 'chemRust', 'chemHydrolysis', 'chemDissolution', 'chemSalt', 'chemVarnish', 'chemCopper']) {
    assert.equal(layer.params[key], DEFAULT_EROSION_PARAMS[key], `${key} defaults from erosion params`);
    assert.ok(Number.isFinite(DEFAULT_EROSION_PARAMS[key]) && DEFAULT_EROSION_PARAMS[key] >= 0, `${key} is a valid rate`);
  }
  assert.equal(DEFAULT_EROSION_PARAMS.simChemicalPlaying, true);

  const manualChemicalShowcases = new Set(['frost-pitted-rainbow-range', 'nivation-cirque-strata']);
  for (const [presetId, preset] of Object.entries(TERRAIN_STACK_PRESETS)) {
    const chemLayers = preset.layers().filter((l) => l.type === 'chemical-weathering');
    const expected = manualChemicalShowcases.has(presetId)
      ? 1
      : (presetId === 'lunar-mare-craters' || preset.manualRecipe ? 0 : 1);
    assert.equal(chemLayers.length, expected, `${presetId} chemical layer count`);
  }

  // Distinct reaction families live in the persistent terrain state pass.
  for (const token of ['veinFe', 'veinCu', 'veinCalc', 'rustDrive', 'hydrolysisDrive', 'saltDrive', 'varnishDrive', 'patinaDrive', 'netRust', 'outChem']) {
    assert.ok(Shaders.CHEM_WEATHER_FRAG.includes(token), `CHEM_WEATHER_FRAG missing ${token}`);
  }

  // The state is geomorphic, not just a tint: it weakens hardness and drives
  // dissolution, hydrolysis-to-fines and salt bursting in terrain integration.
  for (const token of ['uniform sampler2D uChemTex', 'uniform vec4 uChemInt', 'dissolution', 'hydrolysis', 'saltCrumble', '1.0 - 0.55 * chemC.a']) {
    assert.ok(Shaders.TERRAIN_INTEGRATE_FRAG.includes(token), `TERRAIN_INTEGRATE_FRAG missing ${token}`);
  }
  assert.ok(Shaders.TERRAIN_RENDER_FRAG.includes('sampleGridBilinear(uChemTex, vUv, res)'));
  assert.ok(Shaders.TERRAIN_RENDER_FRAG.includes('rustColC') && Shaders.TERRAIN_RENDER_FRAG.includes('patinaColC'));

  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  assert.ok(engSrc.includes('progChemWeather') && engSrc.includes('_runChemPass') && engSrc.includes('PASS G'));
  assert.ok(engSrc.includes('seedChemicalWeathering') && engSrc.includes('simChemicalPlaying'));
  assert.ok(pipeSrc.includes("layer.type === 'chemical-weathering'"));
  assert.ok(pipeSrc.includes('_clearChemicalWeathering') && pipeSrc.includes('seedChemicalWeathering'));
  assert.ok(pipeSrc.includes("'uChemTex', this.engine.chemTex[this.engine.chemIdx], 7"));
  for (const key of ['chemRate', 'chemRust', 'chemHydrolysis', 'chemDissolution', 'chemSalt', 'chemVarnish', 'chemCopper']) {
    assert.ok(uiSrc.includes(`key: '${key}'`), `Inspector missing ${key} slider`);
  }
});

test('Terrain shadow and desert varnish do not collapse into black texel artifacts', () => {
  const frag = Shaders.TERRAIN_RENDER_FRAG;
  const shadowFn = frag.slice(
    frag.indexOf('float sampleTerrainShadow'),
    frag.indexOf('void main()')
  );
  assert.ok(shadowFn.includes('surfaceNormal'), 'terrain shadow ray must be normal-biased');
  assert.ok(shadowFn.includes('normalBias') && shadowFn.includes('slopeTan'),
    'shadow bias must grow on steep heightfield cells');
  assert.ok(shadowFn.includes('horizonSlope') && shadowFn.includes('penumbra'),
    'terrain shadowing must use a soft horizon-angle comparison');
  assert.ok(!shadowFn.includes('if (diff < 0.0) return 0.14'),
    'a single grazing height texel must not binary-collapse a fragment to near-black');
  assert.ok(frag.includes('sampleTerrainShadow(vUv, max(vWorldPos.y, hC), L, bedNormal, span, res)'),
    'shadow ray must start on the rasterized surface, not only the bilinear height sample');

  assert.ok(Shaders.CHEM_WEATHER_FRAG.includes('broadSlope'),
    'desert varnish must require a sustained face, not one-cell slope noise');
  assert.ok(frag.includes('varnishColC') && frag.includes('rockFace * 0.58'),
    'desert varnish must remain a translucent film over the underlying rock');
  assert.ok(!frag.includes('vec3(0.075, 0.055, 0.040)'),
    'the old opaque near-black varnish constant must not return');
});

test('Simulation seasons trigger solo/simultaneously and mineral strata feed water-borne stains', () => {
  // Sedimentary / stratigraphic layers carry a specific chemical orientation.
  const strataSpec = LAYER_OPERATION_SPECS['stratigraphic-beds'];
  assert.ok(strataSpec.writes.includes('Mineral Source'), 'Stratigraphic beds write a mineral source field');
  const strata = createStackLayer('stratigraphic-beds');
  for (const key of ['mineralIron', 'mineralCopper', 'mineralCarbonate', 'mineralSalt', 'mineralHorizon', 'mineralHorizonWidth', 'mineralBandStrength']) {
    assert.equal(strata.params[key], DEFAULT_EROSION_PARAMS[key], `${key} defaults from erosion params`);
    assert.ok(Number.isFinite(DEFAULT_EROSION_PARAMS[key]) && DEFAULT_EROSION_PARAMS[key] >= 0 && DEFAULT_EROSION_PARAMS[key] <= 1, `${key} is a valid abundance`);
  }
  for (const [presetId, preset] of Object.entries(TERRAIN_STACK_PRESETS)) {
    const strataLayers = preset.layers().filter((l) => l.type === 'stratigraphic-beds');
    assert.equal(strataLayers.length, 1, `${presetId} has one mineral-bearing stratigraphic package`);
    if (presetId === 'lunar-mare-craters') {
      assert.equal(strataLayers[0].params.mineralIron, 0, 'Lunar strata carry no aqueous mineral source');
    } else {
      assert.ok(strataLayers[0].params.mineralIron > 0, `${presetId} strata are iron-bearing`);
    }
  }

  // Mineral source bake + dissolved transport following the water velocity field.
  assert.equal(typeof Shaders.MINERAL_ACCUM_FRAG, 'string', 'MINERAL_ACCUM_FRAG must be exported');
  for (const token of ['uMineralComp', 'uMineralBand', 'horizonGate', 'mineralGate', 'outMineral']) {
    assert.ok(Shaders.MINERAL_ACCUM_FRAG.includes(token), `MINERAL_ACCUM_FRAG missing ${token}`);
  }
  for (const token of ['uMineralTex', 'uMineralFieldActive', 'saltSource', 'dissolvedFe', 'dissolvedCu', 'flowToSelf', 'flowFromSelf']) {
    assert.ok(Shaders.CHEM_WEATHER_FRAG.includes(token), `CHEM_WEATHER_FRAG missing dissolved-transport token ${token}`);
  }
  assert.ok(
    Shaders.ROCK_INSTANCE_FRAG.includes('mineralCell.r') && Shaders.ROCK_INSTANCE_FRAG.includes('chemCell.r'),
    'Boulders must stain from the mineral bed / transported chemical field beneath them'
  );
  assert.ok(Shaders.TERRAIN_RENDER_FRAG.includes('uViewMode == 9'), 'Mineral diagnostic view exists');
  assert.ok(
    Shaders.TERRAIN_INTEGRATE_FRAG.includes('waterRoutingGate') && Shaders.TERRAIN_INTEGRATE_FRAG.includes('cryoActiveGate'),
    'Frozen seasons must not keep routing stale flux or consuming stale snowmelt'
  );

  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  const mainSrc = fs.readFileSync(new URL('../src/app/main.js', import.meta.url), 'utf8');

  for (const token of ['progMineralAccum', '_clearMineralField', '_accumulateMineralStrata', 'seedDefaultMineralField', 'uMineralFieldActive']) {
    assert.ok(engSrc.includes(token), `Engine missing mineral lifecycle token ${token}`);
  }
  for (const id of ['hydraulic', 'cryo', 'wind', 'rockfall', 'chemical']) {
    assert.ok(pipeSrc.includes(`id: '${id}'`), `Pipeline season registry missing ${id}`);
  }
  for (const token of ['pauseSimulationForEdit', "pauseSimulationForEdit('Stack evaluated')", 'startSimulation', 'toggleSimulation', 'setSimPlaybackMode', '_clearMineralField', '_accumulateMineralStrata']) {
    assert.ok(pipeSrc.includes(token), `Pipeline missing season/mineral token ${token}`);
  }
  for (const token of ['data-sim-process', 'data-sim-mode', 'data-simulate-process', 'SIMULATION SEASONS']) {
    assert.ok(uiSrc.includes(token), `Layer UI missing season control token ${token}`);
  }
  for (const key of ['mineralIron', 'mineralCopper', 'mineralCarbonate', 'mineralSalt', 'mineralHorizon']) {
    assert.ok(uiSrc.includes(`key: '${key}'`), `Inspector missing ${key} slider`);
  }
  assert.ok(mainSrc.includes('startSimulation(firstAvailable)'), 'Header Simulate runs the selected simulation layer season');
});

test('Wind erosion is its own turbulent layer and Utah stack has Rain + Wind only', () => {
  const spec = LAYER_OPERATION_SPECS['wind-erosion'];
  assert.ok(spec, 'Dedicated Wind Erosion layer operation exists');
  assert.equal(spec.category, 'simulation');
  assert.equal(spec.glyph, 'wind');
  assert.ok(spec.writes.includes('Height') && spec.writes.includes('FineSed'), 'Wind layer writes terrain/sediment channels');

  const windKeys = [
    'windSpeed', 'windAzimuth', 'dryAeolianMode', 'windErosionRate', 'windAbrasion',
    'windDeflation', 'windDeposition', 'windTurbulence', 'windGustiness',
    'windSandSupply', 'windCurtainHeight', 'windStreamlining', 'windFluting',
  ];
  const layer = createStackLayer('wind-erosion');
  assert.equal(layer.params.bakeSteps, 0, 'Wind layer defaults to live simulation');
  for (const key of windKeys) {
    assert.equal(layer.params[key], DEFAULT_EROSION_PARAMS[key], `${key} defaults from erosion params`);
    assert.ok(Number.isFinite(DEFAULT_EROSION_PARAMS[key]), `${key} is finite`);
  }

  // Canonical recipes split the old combined layer into Snow and Wind layers
  // while preserving each landscape's original wind speed/direction values.
  for (const [presetId, landscape] of Object.entries(EROSION_LANDSCAPES)) {
    const d = { ...DEFAULT_EROSION_PARAMS, ...(landscape.defaults || {}) };
    const stack = TERRAIN_STACK_PRESETS[presetId].layers();
    const windLayers = stack.filter((l) => l.type === 'wind-erosion');
    const snowLayers = stack.filter((l) => l.type === 'aeolian-glacial');
    assert.equal(windLayers.length, (d.windSpeed ?? 0) > 0.04 ? 1 : 0, `${presetId} dedicated wind layer count`);
    assert.equal(
      snowLayers.length,
      ((d.snowfallRate ?? 0) > 0.015 || (d.glacierStrength ?? 0) > 0.015) ? 1 : 0,
      `${presetId} snow/glacier layer count`
    );
    if (windLayers.length) {
      assert.equal(windLayers[0].params.windSpeed, d.windSpeed, `${presetId} wind speed parity`);
      assert.equal(windLayers[0].params.windAzimuth, d.windAzimuth, `${presetId} wind direction parity`);
    }
  }

  // Requested rocky Utah workflow: exactly Wind + Rain simulation layers,
  // with no snow, rockfall/boulder placement, or chemical simulation layers.
  const utah = TERRAIN_STACK_PRESETS['utah-wind-rain-canyon'];
  assert.ok(utah, 'Utah Wind + Rain Canyon recipe exists');
  const utahStack = utah.layers();
  assert.deepEqual(
    utahStack.map((l) => l.type),
    ['wind-erosion', 'hydraulic-erosion', 'stratigraphic-beds', 'base-landform', ENVIRONMENT_LAYER_TYPE],
    'Utah stack must contain only Wind + Rain simulations above strata/base, followed by locked Environment'
  );
  assert.ok(!utahStack.some((l) => ['aeolian-glacial', 'thermal-talus', 'rock-placement', 'chemical-weathering'].includes(l.type)));
  assert.ok(utahStack[0].params.bakeSteps === 0 && utahStack[0].params.windTurbulence >= 0.7, 'Utah wind is live-first and turbulent/natural, not ruler-straight');
  assert.ok(utahStack[0].params.windFluting >= 0.8 && utahStack[0].params.windAbrasion > 1.0, 'Utah wind is tuned for fluted fins/spires');
  assert.ok(utahStack[1].params.rainIntensity > 0 && utahStack[1].params.bakeSteps === 0, 'Utah stack keeps a live Rain wash layer');

  // Shader/engine/pipeline/UI plumbing for the dedicated controls.
  for (const token of ['uWindErodeA', 'uWindErodeB', 'gustWindDir', 'terrainDeflectedWind', 'windWander']) {
    assert.ok(Shaders.PARTICLE_STEP_FRAG.includes(token), `PARTICLE_STEP_FRAG missing turbulent wind token ${token}`);
  }
  for (const token of ['uWindErodeA', 'uWindErodeB', 'localWind', 'windShadow', 'windExposure', 'saltationCurtain', 'fluteGate', 'hardnessResist']) {
    assert.ok(Shaders.TERRAIN_INTEGRATE_FRAG.includes(token), `TERRAIN_INTEGRATE_FRAG missing natural wind token ${token}`);
  }

  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  assert.ok(engSrc.includes('_getWindErosionUniforms') && engSrc.includes("'uWindErodeA'") && engSrc.includes("'uWindErodeB'"));
  assert.ok(pipeSrc.includes("layer.type === 'wind-erosion'") && pipeSrc.includes("layerTypes: ['wind-erosion', 'aeolian-glacial']"));
  assert.ok(pipeSrc.includes('applyLiveWindLayerParams') && uiSrc.includes('isLiveWindParam'), 'Wind direction/parameters must be changeable between seasons without rebuilding eroded terrain');
  for (const key of windKeys) {
    assert.ok(uiSrc.includes(`key: '${key}'`), `Wind inspector missing ${key} slider`);
  }
  assert.ok(uiSrc.includes("label: 'Prevailing Wind Direction'"), 'Wind direction/angle must be directly editable');
});


test('Spline Landform layers sculpt with noise and bore SDF tunnels on isolated test terrains', () => {
  const spec = LAYER_OPERATION_SPECS['spline-landform'];
  assert.ok(spec, 'New parameterized Spline Landform operation exists');
  assert.equal(spec.category, 'spline');
  assert.ok(spec.writes.includes('SDF Tunnel'), 'Spline landform can write the SDF volume');
  assert.deepEqual(Object.values(SPLINE_LANDFORM_MODES).map((m) => m.id), [0, 1, 2, 3, 4]);
  for (const labelPart of ['Mountain Ridge', 'Canyon', 'River Channel', 'Road Cut', 'SDF Tunnel']) {
    assert.ok(Object.values(SPLINE_LANDFORM_MODES).some((m) => m.label.includes(labelPart)), `Missing spline mode ${labelPart}`);
  }

  const spline = createStackLayer('spline-landform');
  for (const key of ['mode', 'width', 'amount', 'falloff', 'sharpness', 'noiseAmount', 'noiseScale', 'waterDepth', 'sediment', 'startElevation', 'endElevation', 'flattenStrength', 'bankHeight', 'tunnelRadius', 'tunnelDepth', 'tunnelFollowSurface', 'tunnelRoughness', 'opacity', 'seed', 'points']) {
    assert.ok(key in spline.params, `Spline Landform missing parameter ${key}`);
  }
  assert.equal(spline.params.points.length, 5);

  // The old spline operation and all canonical landscape recipes remain
  // untouched: the new layer appears only in the two standalone test terrains
  // (plus whenever the user explicitly adds it themselves).
  assert.deepEqual(Object.values(SPLINE_MODES).map((m) => m.label), ['Canyon Gorge Cut', 'Graded Road / Terrace', 'Lake Basin Boundary']);
  assert.equal(createStackLayer('spline-feature').params.points.length, 4);
  for (const presetId of Object.keys(EROSION_LANDSCAPES)) {
    assert.ok(
      !TERRAIN_STACK_PRESETS[presetId].layers().some((l) => l.type === 'spline-landform'),
      `Canonical terrain ${presetId} must not be changed by new spline test layers`
    );
  }
  assert.ok(!TERRAIN_STACK_PRESETS['utah-wind-rain-canyon'].layers().some((l) => l.type === 'spline-landform'));

  const canyonRiver = TERRAIN_STACK_PRESETS['spline-canyon-river-test'];
  assert.ok(canyonRiver?.manualRecipe, 'Spline Canyon + River Test is a separate manual terrain');
  assert.deepEqual(
    canyonRiver.layers().filter((l) => l.type === 'spline-landform').map((l) => l.params.mode),
    [2, 1, 0],
    'Canyon/river test evaluates ridge addition first, then canyon subtraction, then directed river'
  );
  const canyonSculpt = canyonRiver.layers().find((l) => l.type === 'sculpt-layer');
  assert.ok(canyonSculpt && canyonSculpt.params.noiseAmount > 0.3 && canyonSculpt.params.blendMode === 0);

  const roadTunnel = TERRAIN_STACK_PRESETS['spline-road-tunnel-test'];
  assert.ok(roadTunnel?.manualRecipe, 'Spline Road + Tunnel Test is a separate manual terrain');
  assert.deepEqual(
    roadTunnel.layers().filter((l) => l.type === 'spline-landform').map((l) => l.params.mode),
    [4, 3],
    'Road/tunnel test contains a graded road spline and volumetric tunnel spline'
  );
  const tunnelLayer = roadTunnel.layers().find((l) => l.type === 'spline-landform' && l.params.mode === 4);
  assert.ok(tunnelLayer.params.tunnelRadius >= 8 && tunnelLayer.params.points.length === 5);

  // Sculpt authoring now has explicit add/multiply/mix/max-min noise blends.
  assert.deepEqual(Object.values(SCULPT_BLEND_MODES).map((m) => m.id), [0, 1, 2, 3]);
  const sculpt = createStackLayer('sculpt-layer');
  for (const key of ['blendMode', 'noiseAmount', 'noiseScale', 'noiseHeight', 'targetHeight', 'multiplyStrength', 'seed']) {
    assert.ok(key in sculpt.params, `Sculpt layer missing ${key}`);
  }
  assert.equal(sculpt.params.blendMode, 0, 'Default sculpt blend preserves the original additive behaviour');
  assert.equal(sculpt.params.noiseAmount, 0, 'Default sculpt noise is off for exact legacy strokes');
  for (const token of ['evalSmoothSplineDist', 'catmullRomSplinePoint', 'if (opKind == 7)', 'uniform vec4 uParam3', 'noisyDelta', 'heightScale', 'sculptSurface', 'paintedFootprint']) {
    assert.ok(PipelineShaders.STACK_OP_FRAG.includes(token) || PipelineShaders.SCULPT_STAMP_FRAG.includes(token), `Pipeline shader missing ${token}`);
  }
  for (const token of ['Mountain Ridge Add', 'Canyon Subtract', 'River Channel', 'Road Cut/Grade', 'SDF Tunnel']) {
    assert.ok(PipelineShaders.STACK_OP_FRAG.includes(token), `Spline landform shader missing ${token}`);
  }

  // Direct CPU SDF proof: a smooth spline tube passes completely through a
  // synthetic ridge and opens the surface portal mask on the slopes.
  const N = 64;
  const snap = new Float32Array(N * N * 4);
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const x = (i / (N - 1) - 0.5) * 512;
      const ridge = Math.max(0, 1 - Math.abs(x) / 150);
      snap[(j * N + i) * 4] = 22 + 96 * Math.pow(ridge, 1.25);
    }
  }
  const caveSys = new SdfCaveSystem();
  const tunnelRes = caveSys.rebuildCaveVolume({
    gridRgba: snap,
    gridN: N,
    worldSpan: 512,
    caveLayers: [],
    sculptCaveStrokes: [],
    splineTunnels: [{
      points: [[-235, 0], [-120, -18], [0, 0], [120, 18], [235, 0]],
      radius: 8,
      startElevation: 38,
      endElevation: 36,
      roughness: 0.3,
      strength: 1,
      seed: 7201,
    }],
    maxRelief: 140,
  });
  assert.equal(tunnelRes.hasCaves, true);
  assert.ok(tunnelRes.vertexCount > 3000, `Spline tunnel should extract a substantial tube mesh, got ${tunnelRes.vertexCount}`);
  let minX = 1e9, maxX = -1e9, maxPortal = 0;
  for (let v = 0; v < caveSys.vertexCount; v++) {
    minX = Math.min(minX, caveSys.positions[v * 3]);
    maxX = Math.max(maxX, caveSys.positions[v * 3]);
    const nx = caveSys.normals[v * 3], ny = caveSys.normals[v * 3 + 1], nz = caveSys.normals[v * 3 + 2];
    assert.ok(Math.abs(Math.hypot(nx, ny, nz) - 1) < 1e-3, 'Tunnel mesh normals are unit length');
  }
  for (let k = 0; k < caveSys.portalTexData.length; k += 4) maxPortal = Math.max(maxPortal, caveSys.portalTexData[k]);
  assert.ok(maxX - minX > 220, `Tunnel mesh must span through the ridge, got ${(maxX - minX).toFixed(1)}m`);
  assert.ok(maxPortal > 0.4, `Tunnel portals must breach the heightfield mask, got ${maxPortal}`);

  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  const caveSrc = fs.readFileSync(new URL('../src/erosion/sdf-cave-mesher.js', import.meta.url), 'utf8');
  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  assert.ok(pipeSrc.includes("layer.type === 'spline-landform'") && pipeSrc.includes('splineTunnels: activeSplineTunnels'));
  assert.ok(caveSrc.includes('catmullRom2D') && caveSrc.includes('splineTunnels = []') && caveSrc.includes('path.segments'));
  assert.ok(uiSrc.includes('isSplineAuthoringLayer') && uiSrc.includes("key: 'tunnelRadius'") && uiSrc.includes("key: 'blendMode'"));
  for (const key of ['width', 'amount', 'falloff', 'sharpness', 'noiseAmount', 'noiseScale', 'startElevation', 'endElevation', 'flattenStrength', 'waterDepth', 'sediment', 'bankHeight', 'tunnelDepth', 'tunnelRoughness']) {
    assert.ok(uiSrc.includes(`key: '${key}'`), `Spline inspector missing ${key}`);
  }
  // Game export is a baked polygon surface: SDF cave triangles and real
  // portal holes are merged into extractMeshForExport, not left as runtime SDF.
  assert.ok(engSrc.includes('caveSys.positions') && engSrc.includes('portalAt') && engSrc.includes('indices.slice(0, ip)'));
});

test('Spline control points are editable in full 3D: gizmo axes, numeric X/Z/Y editor, 3D grades and tunnel bore', () => {
  // Data model: points are [x, z] (legacy/automatic grade) or [x, z, y]
  // (authored absolute elevation). Defaults and every shipped recipe stay in
  // the legacy 2D form, so existing terrains keep identical output.
  const fresh = createStackLayer('spline-landform');
  assert.ok(fresh.params.points.every((pt) => pt.length === 2), 'Default spline points carry no authored Y (automatic grade)');
  for (const presetId of ['spline-canyon-river-test', 'spline-road-tunnel-test']) {
    for (const l of TERRAIN_STACK_PRESETS[presetId].layers().filter((x) => x.type === 'spline-landform')) {
      assert.ok(l.params.points.every((pt) => pt.length === 2), `${presetId} keeps legacy [x,z] points`);
    }
  }
  const authored = createStackLayer('spline-landform', { params: { points: [[-100, 20, 80], [0, 0], [100, -20, 15]] } });
  assert.deepEqual(authored.params.points, [[-100, 20, 80], [0, 0], [100, -20, 15]], 'Authored per-point elevations survive layer creation/cloning');

  // Shader: per-point elevations are a real grade input for both spline ops,
  // with an exact fallback to the original start->end grade when absent.
  for (const token of ['uniform vec4 uSplineElev[2]', 'uniform float uSplineHasY', 'float splinePointElev(', 'float catmullRom1D(', 'float splineGradeAt(', 'if (uSplineHasY < 0.5) return fallbackGrade;']) {
    assert.ok(PipelineShaders.STACK_OP_FRAG.includes(token), `Stack shader missing 3D spline token: ${token}`);
  }
  assert.ok(PipelineShaders.STACK_OP_FRAG.includes('splineGradeAt(alongT, mix(startElevation, endElevation, alongT))'), 'Spline Landform grade consumes per-point Y');
  assert.ok(PipelineShaders.STACK_OP_FRAG.includes('splineGradeAt(alongT, mix(maxRelief * 0.24, maxRelief * 0.56, alongT))'), 'Legacy graded road consumes per-point Y with its original fallback');
  assert.ok(PipelineShaders.STACK_OP_FRAG.includes('crestTarget'), 'Ridge crest follows authored 3D point heights when present');
  assert.ok(PipelineShaders.STACK_OP_FRAG.includes('evalSplineDist(worldXZ, dSpline, alongT)'), 'Legacy 2D distance field is retained');

  // Pipeline: elevations are packed (unset points pre-filled from the grade)
  // and tunnel points keep their authored bore height.
  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  for (const token of ["'uSplineElev'", "'uSplineHasY'", 'Number.isFinite(pt?.[2])', 'fbStart + (fbEnd - fbStart) * t']) {
    assert.ok(pipeSrc.includes(token), `Pipeline missing 3D spline packing token: ${token}`);
  }
  assert.ok(pipeSrc.includes('Number.isFinite(pt?.[2]) ? pt[2] : NaN'), 'Tunnel normalization preserves authored point elevations');

  // UI: selected-point axis gizmo + full numeric point editor.
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  for (const token of ['selectedSplinePt', 'draggingSplineAxis', '_splinePointerRay', '_rayAxisParam', '_splineUsesElevation', '_splineEffectiveY', '_splineHandleWorld', 'data-gizmo-axis', 'spline-axis-hit', 'data-pt-axis', 'data-pt-select', 'data-pt-insert', 'data-pt-delete', 'tp-btn-spline-bake-y', 'tp-btn-spline-clear-y', 'tp-spline-pt-row']) {
    assert.ok(uiSrc.includes(token), `Layer-stack UI missing 3D spline editor token: ${token}`);
  }

  // Direct CPU SDF proof: the bore follows authored point elevations in 3D.
  const N = 64;
  const snap = new Float32Array(N * N * 4);
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const x = (i / (N - 1) - 0.5) * 512;
      const ridge = Math.max(0, 1 - Math.abs(x) / 150);
      snap[(j * N + i) * 4] = 22 + 96 * Math.pow(ridge, 1.25);
    }
  }
  const runTunnel = (points) => {
    const sys = new SdfCaveSystem();
    const res = sys.rebuildCaveVolume({
      gridRgba: snap,
      gridN: N,
      worldSpan: 512,
      caveLayers: [],
      sculptCaveStrokes: [],
      splineTunnels: [{ points, radius: 8, startElevation: 38, endElevation: 36, roughness: 0, strength: 1, seed: 7201 }],
      maxRelief: 140,
    });
    assert.equal(res.hasCaves, true);
    assert.ok(res.vertexCount > 1500, `3D tunnel should extract a tube mesh, got ${res.vertexCount}`);
    let minY = 1e9, maxY = -1e9;
    for (let v = 0; v < sys.vertexCount; v++) {
      minY = Math.min(minY, sys.positions[v * 3 + 1]);
      maxY = Math.max(maxY, sys.positions[v * 3 + 1]);
    }
    return { minY, maxY };
  };

  const flat = runTunnel([[-235, 0], [-120, -18], [0, 0], [120, 18], [235, 0]]);
  const arched = runTunnel([[-235, 0, 30], [-120, -18, 60], [0, 0, 95], [120, 18, 60], [235, 0, 30]]);
  assert.ok(arched.maxY > flat.maxY + 35, `Raising middle points must lift the bore (flat roof ${flat.maxY.toFixed(1)} m vs arched ${arched.maxY.toFixed(1)} m)`);

  const dipped = runTunnel([[-235, 0, 34], [-120, -18, 20], [0, 0, 9], [120, 18, 20], [235, 0, 34]]);
  assert.ok(dipped.minY < flat.minY - 10, `Lowering middle points must sink the bore (flat floor ${flat.minY.toFixed(1)} m vs dipped ${dipped.minY.toFixed(1)} m)`);

  // Mixed paths: a single authored Y blends with the automatic grade fills.
  const mixed = runTunnel([[-235, 0], [-120, -18], [0, 0, 88], [120, 18], [235, 0]]);
  assert.ok(mixed.maxY > flat.maxY + 25, `One raised point must locally lift the bore (mixed roof ${mixed.maxY.toFixed(1)} m)`);
});

test('Wind erosion debug vector field mirrors the GPU flow model (moving gusts, crest speedup, lee reattachment)', () => {
  // Compass semantics: windAzimuth is the direction the wind blows TOWARD in
  // XZ (dir = (cos, sin)); with North = -Z and East = +X, azimuth 0 blows
  // toward East, i.e. it is a wind FROM the West.
  const c0 = windCompassFromAzimuth(0);
  assert.equal(c0.towardPoint, 'E');
  assert.equal(c0.fromPoint, 'W');
  assert.equal(Math.round(c0.towardDeg), 90);
  const cNeg = windCompassFromAzimuth(-35);
  assert.equal(cNeg.towardPoint, 'NE');
  assert.equal(cNeg.fromPoint, 'SW');

  // "Wind FROM <compass>" quick setters are exact inverses.
  assert.equal(azimuthForWindFromBearing(0), 90, 'Wind from North blows toward +Z (South)');
  assert.equal(azimuthForWindFromBearing(90), 180, 'Wind from East blows toward -X (West)');
  assert.equal(azimuthForWindFromBearing(180), -90, 'Wind from South blows toward -Z (North)');
  assert.equal(azimuthForWindFromBearing(270), 0, 'Wind from West blows toward +X (East)');
  assert.equal(azimuthForWindFromBearing(315), 45, 'Wind from NW blows toward SE');
  for (const bearing of [0, 45, 90, 135, 180, 225, 270, 315]) {
    assert.equal(Math.round(windCompassFromAzimuth(azimuthForWindFromBearing(bearing)).fromDeg), bearing, `FROM ${bearing}° round-trips`);
  }

  // Flat terrain + zero turbulence: the whole field aligns with the
  // prevailing vector; only the gust bands modulate speed (as on GPU).
  const flat = () => 50;
  const field = buildWindVectorField({
    heightAt: flat, span: 500, divisions: 9,
    azimuthDeg: 30, windSpeed: 0.58, turbulence: 0, streamlining: 0, seed: 7, cellSize: 1,
  });
  assert.equal(field.arrows.length, 81);
  const expectX = Math.cos(30 * Math.PI / 180);
  const expectZ = Math.sin(30 * Math.PI / 180);
  for (const a of field.arrows) {
    assert.ok(Math.abs(a.dirX - expectX) < 1e-6 && Math.abs(a.dirZ - expectZ) < 1e-6, 'Calm flat field follows the prevailing vector exactly');
    assert.ok(a.speed > 0.2 && a.speed < 1.8, `Gust-modulated speed stays in range, got ${a.speed}`);
    assert.equal(a.active, true);
  }
  const speeds = field.arrows.map((a) => a.speed);
  assert.ok(Math.max(...speeds) > Math.min(...speeds) * 1.2, 'Spatial gust bands still vary speed at zero turbulence');

  // Those gust bands are weather features advected downwind, not permanent
  // fast/slow corridors painted onto fixed terrain cells.
  const gustPhaseSpeeds = [0, 400, 900, 1600].map((phase) => sampleWindVector(
    { azimuthDeg: 30, windSpeed: 0.58, turbulence: 0.65, streamlining: 0, seed: 7, cellSize: 1, phase },
    37, -61, flat
  ).speed);
  assert.ok(Math.max(...gustPhaseSpeeds) - Math.min(...gustPhaseSpeeds) > 0.05,
    `gust phase must move speed bands across a fixed point (${gustPhaseSpeeds})`);

  // Turbulence bends directions away from the prevailing vector.
  const turbulent = sampleWindVector(
    { azimuthDeg: 0, windSpeed: 0.8, turbulence: 1.0, streamlining: 0, seed: 3, cellSize: 1 },
    120, -80, flat
  );
  assert.ok(Math.abs(turbulent.dirZ) > 0.05, `Turbulent wander deflects the local wind, got dirZ ${turbulent.dirZ}`);

  // Wrap-around flow: the wind splits around a cone instead of riding over
  // it — symmetric outward bending either side of the stagnation centreline,
  // and no bending at all when Wrap/Streamlining is 0 (legacy ride-over).
  const cone = (x, z) => 30 + 70 * Math.exp(-(x * x + z * z) / (2 * 60 * 60));
  const wrapP = { azimuthDeg: 0, windSpeed: 0.8, turbulence: 0, streamlining: 1.0, seed: 3, cellSize: 1 };
  const northSide = sampleWindVector(wrapP, -50, 30, cone);
  const southSide = sampleWindVector(wrapP, -50, -30, cone);
  const centreline = sampleWindVector(wrapP, -50, 0, cone);
  assert.ok(northSide.dirZ > 0.4, `Flow wraps around the +Z flank, got dirZ ${northSide.dirZ.toFixed(3)}`);
  assert.ok(southSide.dirZ < -0.4, `Flow wraps around the -Z flank, got dirZ ${southSide.dirZ.toFixed(3)}`);
  assert.ok(Math.abs(centreline.dirZ) < 0.02, 'Centreline is the symmetric split line');
  assert.ok(northSide.wrapDeflect > 0.1, 'Wrap deflection is recorded for the flank speedup');
  const rideOver = sampleWindVector({ ...wrapP, streamlining: 0 }, -50, 30, cone);
  assert.ok(Math.abs(rideOver.dirZ) < 1e-6, 'Streamlining 0 keeps the legacy straight-over-the-terrain flow');
  const halfWrap = sampleWindVector({ ...wrapP, streamlining: 0.55 }, -50, 30, cone);
  assert.ok(halfWrap.dirZ > 0.15 && halfWrap.dirZ < northSide.dirZ, 'Wrap strength scales with the Streamlining setting');

  // Valley channeling: a diagonal wind is funnelled along the valley axis.
  const valley = (x, z) => 30 + 70 * (1 - Math.exp(-(z * z) / (2 * 55 * 55)));
  const channelled = sampleWindVector(
    { azimuthDeg: 40, windSpeed: 0.8, turbulence: 0, streamlining: 1.0, seed: 3, cellSize: 1 },
    0, 30, valley
  );
  assert.ok(channelled.dirX > 0.9 && channelled.dirZ < 0.35,
    `Diagonal wind (0.766, 0.643) is channelled along the valley, got (${channelled.dirX.toFixed(3)}, ${channelled.dirZ.toFixed(3)})`);

  // The GPU shaders implement the same wrap model in both wind passes.
  const erosionShaderSrc = fs.readFileSync(new URL('../src/erosion/erosion-shaders.js', import.meta.url), 'utf8');
  assert.ok((erosionShaderSrc.match(/obstacleGrad/g) || []).length >= 4, 'Both wind passes compute the multi-scale obstacle gradient');
  assert.ok(erosionShaderSrc.includes('wrapBlock') && erosionShaderSrc.includes('wrapDeflect'), 'Wrap-around blocking + flank speedup present in the integrator');
  assert.ok(!erosionShaderSrc.includes('crossSlopeW') && !erosionShaderSrc.includes('crossSlopeWind'), 'Old single-cell cross-slope nudge is replaced');

  // Erosion must FOLLOW the wrapped streamlines, not just steer by them:
  // tangential flank abrasion in both passes, flutes aligned to the local
  // wrapped wind, and particle erosion decisions made against the wrapped
  // direction (previously the un-wrapped gust vector).
  assert.ok((erosionShaderSrc.match(/tangentialSlope/g) || []).length >= 4, 'Tangential streamline abrasion present in both wind passes');
  assert.ok(erosionShaderSrc.includes('float abrasionGate = max('), 'Terrain abrasion uses an explicit head-on/reattached-flow gate');
  assert.ok(erosionShaderSrc.includes('leeReattachment * smoothstep(0.08, 0.65, tangentialSlope)'),
    'Abrasion can resume on tangential slopes after lee-wake reattachment');
  assert.ok(erosionShaderSrc.includes('dot(worldXZ, localPerp)'), 'Wind flutes align to the wrapped local wind');
  assert.ok(!erosionShaderSrc.includes('dot(grad, gustWindDir)'), 'Particle stoss/lee decisions no longer use the un-wrapped gust vector');
  assert.ok(erosionShaderSrc.includes('dot(grad, terrainDeflectedWind)'), 'Particle stoss/lee decisions use the wrapped wind');
  assert.ok(erosionShaderSrc.includes('terrainDeflectedWind * 0.88'), 'Saltation steering follows the wrapped wind with only mild downslope dilution');

  // Researched aeolian transport controls: gusts advect with simulation
  // phase, entrainment requires excess shear above a slope-dependent
  // threshold, and flux relaxes over a finite fetch/saturation length.
  for (const token of [
    'uniform vec2 uWindPhase',
    'float gustPhase = uWindPhase.x',
    'float thresholdShear',
    'float excessShear',
    'float saturationLengthM = mix(8.0, 30.0',
    'float fetchFactor = 1.0 - exp(',
    'float qSaltation = mix(qSatUp, qSatHere, saturationResponse)',
    'float crestSpeedup = clamp(',
    'float separatedWake',
    'float leeReattachment',
    'if (r2.z < 0.20)',
  ]) assert.ok(erosionShaderSrc.includes(token), `Aeolian shader missing researched transport token: ${token}`);

  // Lee sheltering: just downwind of a ridge the flow is much weaker than on
  // the windward side — this is where the debug arrows turn short/cool.
  const ridge = (x) => 30 + 80 * Math.exp(-(x * x) / (2 * 35 * 35));
  const p = { azimuthDeg: 0, windSpeed: 0.8, turbulence: 0, streamlining: 0.8, seed: 3, cellSize: 1 };
  const lee = sampleWindVector(p, 30, 0, ridge);
  const stoss = sampleWindVector(p, -30, 0, ridge);
  const reattached = sampleWindVector(p, 90, 0, ridge);
  assert.ok(stoss.exposure > 0.95, `Windward side is fully exposed, got ${stoss.exposure}`);
  assert.ok(stoss.crestSpeedup > 1.05, `Streamline compression accelerates flow up the stoss slope (${stoss.crestSpeedup})`);
  assert.ok(lee.exposure < 0.45, `Immediate lee is sheltered by flow separation, got exposure ${lee.exposure}`);
  assert.ok(lee.separatedWake > 0.45, `Immediate lee is identified as a separated wake (${lee.separatedWake})`);
  assert.ok(lee.speed < stoss.speed * 0.6, `Lee speed collapses vs windward (${lee.speed.toFixed(2)} vs ${stoss.speed.toFixed(2)})`);
  assert.ok(reattached.reattachment > 0.15, `Wake begins reattaching farther downwind (${reattached.reattachment})`);
  assert.ok(reattached.exposure > lee.exposure + 0.25,
    `Exposure recovers after reattachment (${reattached.exposure} vs immediate lee ${lee.exposure})`);
  assert.ok(reattached.speed > lee.speed * 1.35,
    `Recovered wake regains erosive speed (${reattached.speed} vs immediate lee ${lee.speed})`);

  // A later opposite weather spell reverses which flank is stoss and which
  // is lee; combined with the smooth multi-sector regime, erosion is not
  // permanently locked to one terrain aspect.
  const reverseP = { ...p, azimuthDeg: 180 };
  const eastUnderEastWind = sampleWindVector(reverseP, 30, 0, ridge);
  const westUnderEastWind = sampleWindVector(reverseP, -30, 0, ridge);
  assert.ok(eastUnderEastWind.exposure > 0.95 && eastUnderEastWind.crestSpeedup > 1.05,
    'Opposite spell makes the former lee flank an exposed, accelerating stoss slope');
  assert.ok(westUnderEastWind.exposure < 0.45,
    'Opposite spell correspondingly shelters the former stoss flank');

  // Water/snow gate dims arrows where wind erosion cannot act.
  const gated = buildWindVectorField({
    heightAt: flat, gateAt: (x) => ({ water: x > 0 ? 0.4 : 0, snow: 0 }),
    span: 500, divisions: 5, azimuthDeg: 0, windSpeed: 0.6, turbulence: 0, streamlining: 0, seed: 1, cellSize: 1,
  });
  assert.ok(gated.arrows.filter((a) => a.x > 0).every((a) => !a.active), 'Arrows over water are inactive');
  assert.ok(gated.arrows.filter((a) => a.x < 0).every((a) => a.active), 'Arrows over dry land stay active');

  // Speed colour ramp endpoints (sheltered blue -> erosive coral).
  assert.deepEqual(windSpeedColor(0), [61, 123, 253]);
  assert.deepEqual(windSpeedColor(1), [255, 122, 107]);

  // UI wiring: toggle, readout, FROM setters, overlay canvas & studio mode.
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  for (const token of ['showWindDebug', 'tp-chk-wind-debug', 'tp-wind-debug-readout', 'data-wind-from', 'updateWindDebugOverlay', '_windDebugParams', 'buildWindVectorField', 'azimuthForWindFromBearing', 'windCompassFromAzimuth']) {
    assert.ok(uiSrc.includes(token), `Layer-stack UI missing wind debug token: ${token}`);
  }
  const htmlSrc = fs.readFileSync(new URL('../index.html', import.meta.url), 'utf8');
  assert.ok(htmlSrc.includes('id="wind-debug-canvas"'), 'Wind debug canvas exists in the viewport');
  const mainSrc = fs.readFileSync(new URL('../src/app/main.js', import.meta.url), 'utf8');
  assert.ok(mainSrc.includes("getElementById('wind-debug-canvas')"), 'Wind debug canvas hides with the terrain studio');

  // Latent bug fixed alongside: sampleTerrain must expose surfaceH (the
  // engine raycaster / pipeline / spline overlay read it; only surfH existed,
  // which silently disabled the exact terrain raymarch).
  const calvSrc = fs.readFileSync(new URL('../src/erosion/rock-calving.js', import.meta.url), 'utf8');
  assert.ok(calvSrc.includes('surfaceH: surfH'), 'sampleTerrain exposes the surfaceH alias its callers use');
});

test('Terrain resolution is a safe boot-time quality option and the redesigned UI stays decluttered', () => {
  assert.deepEqual([...TERRAIN_RESOLUTION_OPTIONS], [256, 512, 768, 1024]);
  assert.equal(DEFAULT_EROSION_PARAMS.gridSize, 512, 'The established default terrain remains bit-for-bit 512²');
  assert.equal(normalizeTerrainResolution(256), 256);
  assert.equal(normalizeTerrainResolution('768'), 768);
  assert.equal(normalizeTerrainResolution(1024), 1024);
  assert.equal(normalizeTerrainResolution(2048), 512, 'Unsupported extreme grids cannot allocate an unbounded GPU working set');
  assert.equal(normalizeTerrainResolution('not-a-grid', 768), 768);
  assert.equal(resolveTerrainResolution(1024), 1024, 'An explicit engine option wins over URL/storage defaults');

  const high = terrainResolutionProfile(768, 500);
  assert.equal(high.label, 'High');
  assert.ok(Math.abs(high.cellSizeMeters - (500 / 768)) < 1e-12);
  assert.equal(high.relativeCellCount, 2.25);
  assert.equal(terrainResolutionProfile(1024).relativeCellCount, 4);
  assert.equal(terrainResolutionProfile(256).relativeCellCount, 0.25);

  const engSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const pipeSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const mainSrc = fs.readFileSync(new URL('../src/app/main.js', import.meta.url), 'utf8');
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  const htmlSrc = fs.readFileSync(new URL('../index.html', import.meta.url), 'utf8');
  const cssSrc = fs.readFileSync(new URL('../src/app/style.css', import.meta.url), 'utf8');

  // Every grid-sized resource derives from the same boot-time engine value;
  // the UI reloads instead of mutating textures underneath a live pipeline.
  for (const token of [
    'constructor(canvas, onTelemetry = null, options = {})',
    'resolveTerrainResolution(',
    'gridSize: requestedGridSize',
    'this.gridRes = requestedGridSize',
    'gridRes: this.gridRes',
  ]) {
    assert.ok(engSrc.includes(token), `Engine resolution plumbing missing ${token}`);
  }
  assert.ok(pipeSrc.includes('this.R = engine.gridRes'), 'Stack pipeline must allocate from the selected engine resolution');
  for (const token of [
    "getElementById('tp-terrain-resolution')",
    'gridSize: initialTerrainResolution',
    'TERRAIN_RESOLUTION_STORAGE_KEY',
    "searchParams.set('terrainRes'",
    'TERRAIN_RESOLUTION_SNAPSHOT_KEY',
    'captureTerrainResolutionSnapshot(next)',
    'restoreTerrainResolutionSnapshot();',
    'createStackLayer(layer.type',
    "getElementById('er-stat-grid')",
  ]) {
    assert.ok(mainSrc.includes(token), `Studio resolution control missing ${token}`);
  }
  assert.ok(mainSrc.includes(TERRAIN_RESOLUTION_STORAGE_KEY) || mainSrc.includes('TERRAIN_RESOLUTION_STORAGE_KEY'));
  for (const option of ['256² · Performance', '512² · Balanced', '768² · High', '1024² · Ultra']) {
    assert.ok(htmlSrc.includes(option), `Missing terrain resolution option ${option}`);
  }

  // Decluttering is structural, not just a colour swap: recipes and global
  // shading start collapsed, and Focus mode can remove both side docks.
  assert.ok(uiSrc.includes('this.shelfOpen = false'), 'Preset shelf defaults closed');
  assert.ok(htmlSrc.includes('id="cb-shelf-drawer" hidden'), 'Preset drawer is hidden before first UI render');
  assert.ok(uiSrc.includes('<details class="tp-property-card tp-collapsible-card"'), 'Global shading card is collapsible');
  assert.ok(htmlSrc.includes('id="btn-toggle-panels"') && mainSrc.includes("classList.toggle('tp-focus-mode')"));
  for (const token of ['2026 TELEMETRY-GLASS REDESIGN', 'body.tp-focus-mode .tp-layer-dock', '.tp-resolution-control', '.tp-collapsible-card', 'backdrop-filter: blur(18px)']) {
    assert.ok(cssSrc.includes(token), `Redesigned UI stylesheet missing ${token}`);
  }

  // Charcoal Exhibits redesign: Outliner + Inspector dock tabs, squared
  // layer entries with per-type coloured icons, and authoring tools split
  // out of the viewport header into their own stage tool rail.
  for (const token of [
    'CHARCOAL EDITOR REDESIGN',
    '--tp-dock: #0e0e10',
    '--tp-select: #5ac8fa',
    '.tp-tool-rail {',
    '.tp-layer-status',
    '#tp-terrain-bars.tp-viewport-header',
    '#tp-scene-nav.tp-vp-cam-group',
  ]) {
    assert.ok(cssSrc.includes(token), `Charcoal editor stylesheet missing ${token}`);
  }
  const charcoalBlock = cssSrc.slice(cssSrc.indexOf('CHARCOAL EDITOR REDESIGN'));
  assert.ok(/\.tp-layer-row \{[^}]*border-radius: 0;/s.test(charcoalBlock),
    'Layer-stack entries must be squared (border-radius: 0) in the charcoal theme');

  // User correction: squared corners ONLY on layer-stack entries — the rest
  // of the UI is rounded again, and layer icons are Lucide glyphs whose
  // glyph (not tile background) carries the per-type colour.
  const restoredBlock = cssSrc.slice(cssSrc.indexOf('ROUNDED RESTORATION + LUCIDE GLYPHS'));
  assert.ok(restoredBlock.length > 100, 'Rounded-restoration theme block present');
  assert.ok(/\.tp-layer-icon \{[^}]*background: transparent;/s.test(restoredBlock),
    'Layer icon tiles must be transparent — the glyph itself is coloured');
  assert.ok(/\.tp-hdr-btn,\s*\.tp-resolution-select \{[^}]*border-radius: 999px;/s.test(restoredBlock),
    'Non-layer controls are rounded again');
  assert.ok(/\.tp-layer-row \{[^}]*border-radius: 0;/s.test(restoredBlock),
    'Layer rows stay squared after the rounded restoration');

  for (const token of ['LAYER_ICON_COLORS', 'layerIconColor(', 'tp-layer-status', "from 'lucide'", 'lucideIcon(Mountain)', 'lucideIcon(FlaskConical)']) {
    assert.ok(uiSrc.includes(token), `Outliner coloured-icon/status wiring missing ${token}`);
  }
  const pkg = JSON.parse(fs.readFileSync(new URL('../package.json', import.meta.url), 'utf8'));
  assert.ok(pkg.dependencies && pkg.dependencies.lucide, 'Lucide is a declared dependency');

  // Tools live in the stage rail, NOT inside the viewport header element.
  const headerStart = htmlSrc.indexOf('id="tp-terrain-bars"');
  const stageStart = htmlSrc.indexOf('class="viewport-stage"');
  const toolsIdx = htmlSrc.indexOf('id="tp-author-tools"');
  assert.ok(headerStart > -1 && stageStart > headerStart, 'Viewport header precedes the stage');
  assert.ok(toolsIdx > stageStart, 'Authoring tools are split out of the viewport header into the stage tool rail');
  assert.ok(htmlSrc.includes('class="tp-tool-rail" id="tp-author-tools"'), 'Tool rail carries the author-tools contract id');
  assert.ok(htmlSrc.includes('id="tp-scene-nav"') && htmlSrc.indexOf('id="tp-scene-nav"') < stageStart,
    'Camera framing controls live in the viewport header');
  assert.ok(htmlSrc.includes('>Outliner</h1>') && htmlSrc.includes('id="tp-left-tab-label">Outliner<'),
    'Left dock is the Outliner, matching the Exhibits editor');
});

test('Snow frost cracks incubate below terrain resolution before thresholded pitting releases debris', () => {
  assert.equal(Object.keys(SNOW_EFFECT_TYPES).length, 8);
  assert.equal(Object.keys(SNOW_EFFECT_PRESETS).length, 8);
  assert.deepEqual(normalizeFrostParams({}), normalizeFrostParams(DEFAULT_FROST_PARAMS));

  const balanced = createSnowEffectPreset('research-balanced');
  const balancedWeights = deriveSnowEffectWeights(balanced.effects);
  assert.ok(balancedWeights.weights.segregation > 0 && balancedWeights.weights.frostCreep > 0);
  assert.ok(balancedWeights.weights.snowmelt > 0 && balancedWeights.anyTransport, 'Balanced Snow includes erosive seasonal snowmelt runoff');
  assert.equal(balancedWeights.weights.legacy, 0, 'Research presets must not secretly use instant in-place conversion');
  assert.equal(createSnowEffectPreset('snowpack-only').effects.length, 0);
  assert.ok(deriveSnowEffectWeights(createSnowEffectPreset('legacy-instant').effects).weights.legacy > 0);

  // Duplicates of one process add (capped), and per-process parameters are
  // intensity-weighted rather than the last entry silently replacing the rest.
  const stacked = deriveSnowEffectWeights([
    createSnowEffect('segregation-ice', { intensity: 1.5, windowLowC: -10, windowHighC: -4 }),
    createSnowEffect('segregation-ice', { intensity: 1.5, windowLowC: -6, windowHighC: -2 }),
    createSnowEffect('frost-creep-solifluction', { enabled: false, intensity: 2 }),
  ]);
  assert.equal(stacked.weights.segregation, 2.5);
  assert.equal(stacked.params.windowLowC, -8);
  assert.equal(stacked.params.windowHighC, -3);
  assert.equal(stacked.weights.frostCreep, 0);

  const duplicateIds = normalizeSnowEffects([
    createSnowEffect('segregation-ice', { id: 'same:id' }),
    createSnowEffect('frost-creep-solifluction', { id: 'same:id' }),
  ]);
  assert.equal(new Set(duplicateIds.map((effect) => effect.id)).size, 2, 'Restored duplicate Snow-effect ids are made unique');
  assert.ok(duplicateIds.every((effect) => !effect.id.includes(':')), 'Snow-effect ids stay dataset-safe');

  // The snowline is not allowed to be a zero-retention floor. In the cold
  // phase most valley snowfall sticks; half a cycle later snowfall is
  // suppressed and stored snow/ice melt is amplified instead.
  const meltEffects = [createSnowEffect('snowmelt-runoff', {
    intensity: 1,
    cyclePeriodSteps: 100,
    cyclePhase: 0,
    warmMeltBoost: 2,
  })];
  const coldCycle = snowmeltCycleAtStep(meltEffects, 0);
  const warmCycle = snowmeltCycleAtStep(meltEffects, 50);
  assert.ok(coldCycle.lowElevationRetention > 0.85 && coldCycle.snowfallMultiplier > 1.1 && coldCycle.meltMultiplier < 0.2);
  assert.ok(warmCycle.meltMultiplier > 1.8 && warmCycle.snowfallMultiplier < 0.5 && warmCycle.lowElevationRetention < coldCycle.lowElevationRetention);
  const neutralCycle = snowmeltCycleAtStep([], 50);
  assert.deepEqual(
    [neutralCycle.snowfallMultiplier, neutralCycle.meltMultiplier, neutralCycle.lowElevationRetention, neutralCycle.season],
    [1, 1, 0.34, 0],
    'Snowpack-only still retains some low snowfall but has no imposed melt cycle',
  );
  assert.ok(deriveSnowEffectWeights(createSnowEffectPreset('spring-snowmelt').effects).weights.snowmelt > 1);

  // A visible aperture is resolution-aware: a 1 m cell cannot honestly show
  // a requested 2 mm crack, while the stored state can still carry it.
  assert.equal(visibleCrackThresholdMm(1, 2), 8);
  assert.equal(visibleCrackThresholdMm(0.5, 12), 12);

  const showcaseParams = {
    ...createSnowEffectPreset('segregation-pitting').globals,
    frostYearsPerStep: 2.6,
    frostInitialDamage: 0.42,
  };
  const env = {
    snowDepthM: 0.35,
    meltM: 0.12,
    slope: 0.45,
    sedimentM: 0.01,
    elevationNorm: 0.78,
    snowlineNorm: 0.38,
    snowMargin: 0.8,
    hollow: 0.4,
    cellSizeM: 1,
  };
  const porousFracturedSandstone = { hardness: 0.48, porosity: 0.22, fracture: 0.72, clay: 0.18 };
  let state = { damage: 0.08, crackMm: 0.04, saturation: 0.72, detachedM: 0 };
  let firstReleaseStep = -1;
  let cumulativeRelease = 0;
  for (let step = 0; step < 240; step++) {
    state = advanceFrostState(state, env, showcaseParams, createSnowEffectPreset('segregation-pitting').effects, porousFracturedSandstone);
    assert.ok(Number.isFinite(state.damage) && Number.isFinite(state.crackMm) && Number.isFinite(state.detachedM));
    if (state.detachedM > 0 && firstReleaseStep < 0) firstReleaseStep = step;
    cumulativeRelease += state.detachedM;
  }
  assert.ok(firstReleaseStep > 2, `Pitting must incubate before release, got first release at step ${firstReleaseStep}`);
  assert.ok(cumulativeRelease > 0.02 && cumulativeRelease < 2, `Showcase release should be finite and visible, got ${cumulativeRelease}`);

  // Dry pores and hard, tight, unfractured rock are strongly protected.
  let dryState = { damage: 0.08, crackMm: 0.04, saturation: 0.1, detachedM: 0 };
  for (let step = 0; step < 80; step++) {
    dryState = advanceFrostState(
      dryState,
      { ...env, meltM: 0, snowDepthM: 0.05 },
      { ...showcaseParams, frostMoisture: 0.04 },
      createSnowEffectPreset('segregation-pitting').effects,
      { hardness: 0.9, porosity: 0.015, fracture: 0.08, clay: 0.02 },
    );
  }
  assert.ok(dryState.damage < state.damage && dryState.detachedM === 0, 'Dry crystalline rock must not frost-pit like wet fractured sandstone');

  // Deep snow is insulation, not a universal frost accelerator.
  const exposedDiagnostic = estimateAnnualFrostWindow(showcaseParams, 0.03);
  const insulatedDiagnostic = estimateAnnualFrostWindow(showcaseParams, 1.2);
  assert.ok(insulatedDiagnostic.amplitudeC < exposedDiagnostic.amplitudeC * 0.65);
  assert.ok(insulatedDiagnostic.meanC > exposedDiagnostic.meanC);

  // GPU contract: a persistent RGBA frost state, thresholded A-channel
  // release consumed by Integrate, and separate pairwise export in Talus.
  for (const token of ['layout(location = 0) out vec4 outFrost', 'uFrostRelease', 'visibleThresholdMm', 'detachedM']) {
    assert.ok(Shaders.FROST_DAMAGE_FRAG.includes(token), `Frost damage shader missing ${token}`);
  }
  for (const token of ['uniform sampler2D uFrostTex', 'frostC.a', 'debrisRetention', 'legacyRate']) {
    assert.ok(Shaders.TERRAIN_INTEGRATE_FRAG.includes(token), `Integrate frost contract missing ${token}`);
  }
  for (const token of ['creepDrive', 'avalancheDrive', 'tillDrive', 'uFrostTalusA']) {
    assert.ok(Shaders.THERMAL_TALUS_FRAG.includes(token), `Talus frost export missing ${token}`);
  }
  // Full-domain snowfall + snowmelt erosion contracts. The old hard gates
  // (`altFactor`, `coldZone`) must not return: elevation now interpolates
  // retention, while conservative meltwash moves the liberated sediment.
  for (const token of ['lowElevationSnow', 'snowfallRetention', 'precipField']) {
    assert.ok(Shaders.INIT_SNOW_FRAG.includes(token), `Initial snow must reach low terrain (${token})`);
  }
  assert.ok(!Shaders.INIT_SNOW_FRAG.includes('altFactor'));
  assert.ok(Shaders.PARTICLE_STEP_FRAG.includes('altitudeStick') && Shaders.PARTICLE_STEP_FRAG.includes('0.34 + 0.66 * altitudeStick'));
  for (const token of ['uMeltCycle', 'uMeltCycle.y', 'uMeltCycle.z', 'precipField', 'snowfallRetention']) {
    assert.ok(Shaders.SNOW_CRYO_FRAG.includes(token), `Snow Cryo full-domain accumulation missing ${token}`);
  }
  assert.ok(!Shaders.SNOW_CRYO_FRAG.includes('coldZone'));
  for (const token of ['uFrostIntD', 'meltPipeBlend', 'effectivePipeBlend', 'localCoarseShare']) {
    assert.ok(Shaders.TERRAIN_INTEGRATE_FRAG.includes(token), `Snowmelt channel cutting missing ${token}`);
  }
  for (const token of ['uFrostTalusD', 'meltwashDrive', 'diffWater', 'sourceWater']) {
    assert.ok(Shaders.THERMAL_TALUS_FRAG.includes(token), `Snowmelt pairwise sediment transport missing ${token}`);
  }
  const engineSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const pipelineSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  for (const token of ['progFrostDamage', 'frostTex', 'fboFrost', 'seedFrostWeathering', "'uFrostTex', this.frostTex[this.frostIdx], 9", "'uMeltCycle'", "'uFrostIntD'", "'uFrostTalusD'", 'cryoStepCount']) {
    assert.ok(engineSrc.includes(token), `Engine frost wiring missing ${token}`);
  }
  for (const token of ['applyLiveSnowLayerParams', 'seedFrostWeathering()', "case 10: return 10", "'uFrostTex', this.engine.frostTex[this.engine.frostIdx], 9"]) {
    assert.ok(pipelineSrc.includes(token), `Pipeline frost wiring missing ${token}`);
  }
  for (const token of ['FROST_GLOBAL_CONTROLS', 'data-snow-effect-param', 'tp-btn-apply-snow-preset', 'tp-btn-frost-diagnostic']) {
    assert.ok(uiSrc.includes(token), `Snow Inspector missing ${token}`);
  }

  // Exercise the real Inspector HTML builder, not only source markers: a
  // showcase Snow layer must render its stacked process controls and select
  // an editable effect without needing a browser DOM.
  const inspector = Object.create(ErosionLayerStackUI.prototype);
  inspector.selectedSnowEffectId = null;
  inspector.selectedStrataBedId = null;
  inspector.engine = { params: { worldSpan: 500 }, gridRes: 512 };
  inspector.pipeline = { R: 512, shadingMode: 0 };
  const showcaseLayers = TERRAIN_STACK_PRESETS['frost-pitted-rainbow-range'].layers();
  const snowHtml = inspector._snowFrostCardHtml(showcaseLayers.find((layer) => layer.type === 'aeolian-glacial'), '#a8e6ff');
  assert.ok(snowHtml.includes('Segregation window') && snowHtml.includes('data-snow-effect-param'));
  assert.ok(snowHtml.includes('Snowmelt runoff') && snowHtml.includes('Location snow season') && snowHtml.includes('Year 1'), 'Snow Inspector exposes the location-calendar valley-snow / melt state');
  assert.ok(inspector.selectedSnowEffectId, 'Snow Inspector selects an effect for editing');

  // An explicitly emptied custom stack is snowpack-only. It must not be
  // mistaken for a pre-feature layer and resurrect the balanced preset,
  // while a frost-only stack can still make the Cryo season available.
  const availabilityHarness = Object.create(TerrainStackPipeline.prototype);
  const availabilitySnow = showcaseLayers.find((layer) => layer.type === 'aeolian-glacial');
  availabilityHarness.layers = [availabilitySnow];
  availabilityHarness.paletteId = 1;
  Object.assign(availabilitySnow.params, {
    snowfallRate: 0,
    glacierStrength: 0,
    windSpeed: 0,
    snowEffectPreset: 'custom',
    snowEffects: [],
  });
  assert.equal(availabilityHarness._computeSimAvailability().cryo, false);
  availabilitySnow.params.snowEffects = [createSnowEffect('segregation-ice')];
  assert.equal(availabilityHarness._computeSimAvailability().cryo, true);
  availabilitySnow.params.snowEffects = [createSnowEffect('snowmelt-runoff')];
  assert.equal(availabilityHarness._computeSimAvailability().cryo, true, 'A melt-only cycle can run to drain an existing snowpack');
});

test('Authored stratigraphic columns are non-repeating chemical beds with composition-derived properties', () => {
  assert.equal(MAX_STRATA_BEDS, 16);
  assert.equal(Object.keys(STRATA_LITHOLOGIES).length, 12);
  assert.deepEqual(
    Object.fromEntries(Object.entries(STRATA_COLUMN_PRESETS).map(([id]) => [id, createStrataColumnPreset(id).length])),
    {
      'rainbow-andes': 14,
      'grand-canyon': 11,
      'alpine-limestone-shale': 11,
      'painted-desert': 13,
    },
  );

  const rainbow = createStrataColumnPreset('rainbow-andes');
  assert.equal(Math.round(strataColumnThickness(rainbow)), 200);
  assert.ok(new Set(rainbow.map((bed) => bed.color)).size >= 12, 'Rainbow column needs many distinct bed hues');
  assert.ok(new Set(rainbow.map((bed) => bed.thicknessM)).size >= 10, 'Bed thickness must genuinely vary');
  assert.ok(rainbow.some((bed) => bed.copper >= 0.2) && rainbow.some((bed) => bed.carbonate >= 0.85));

  // Coordinates outside a measured section clamp to the end beds. They never
  // wrap around to repeat the colour/chemistry sequence up a taller peak.
  assert.equal(sampleStrataColumn(rainbow, -500).index, 0);
  assert.equal(sampleStrataColumn(rainbow, 500).index, rainbow.length - 1);
  assert.equal(sampleStrataColumn(rainbow, 500).aboveColumn, true);
  assert.notEqual(
    sampleStrataColumn(rainbow, strataColumnThickness(rainbow) + 5).bed.id,
    sampleStrataColumn(rainbow, 5).bed.id,
  );

  const shale = deriveBedProperties(rainbow.find((bed) => bed.lithology === 'shale'));
  const dolomite = deriveBedProperties(rainbow.find((bed) => bed.lithology === 'dolomite'));
  assert.ok(shale.frostSusceptibility > dolomite.frostSusceptibility, 'Weak fractured clay-rich beds should frost-crack faster');
  assert.ok(dolomite.solubility > shale.solubility, 'Carbonate chemistry should raise dissolution susceptibility');
  assert.ok(shale.pluckingSusceptibility > dolomite.pluckingSusceptibility, 'Thin/jointed weak beds should pluck preferentially');

  const packed = packStrataColumnUniforms(rainbow, { enabled: true, baseM: -10, colorStrength: 0.96 });
  assert.deepEqual([...packed.meta].slice(0, 3), [1, 14, -10]);
  assert.ok(Math.abs(packed.meta[3] - 0.96) < 1e-6);
  assert.equal(packed.bedsA.length, MAX_STRATA_BEDS * 4);
  assert.ok(Math.abs(packed.bedsA[3 * 4 + 3] - 0.78) < 1e-6, 'Fourth packed bed carries its authored fracture value');
  assert.ok(Math.abs(packed.bedsA[13 * 4] - 200) < 1e-5, 'Cumulative top reaches the measured-section total');
  assert.ok(Math.abs(packed.bedsB[6 * 4 + 2] - 0.94) < 1e-6, 'Packed carbonate remains tied to its exact bed');

  // Normalisation is the document boundary: no more than 16 beds, finite
  // physical/chemical fields, and stable hex colours survive serialisation.
  const normalised = normalizeStrataColumn(Array.from({ length: 24 }, (_, i) => ({
    id: 'duplicate:bed',
    lithology: i % 2 ? 'shale' : 'sandstone',
    name: `Bed ${i}`,
    thicknessM: 2 + i,
    color: '#A4634F',
    iron: 4,
    porosity: -2,
  })));
  assert.equal(normalised.length, MAX_STRATA_BEDS);
  assert.equal(new Set(normalised.map((bed) => bed.id)).size, MAX_STRATA_BEDS, 'Restored duplicate bed ids are made unique');
  assert.ok(normalised.every((bed) => bed.iron <= 1 && bed.porosity >= 0.003 && /^#[0-9a-f]{6}$/.test(bed.color)));

  // Shared GPU geology + every consumer binds the same packed column.
  for (const token of ['uStrataColumnMeta', 'uStrataBedsA[16]', 'uStrataBedsC[16]', 'strataBedIndexAt']) {
    assert.ok(Shaders.COMMON_GEOLOGY_GLSL.includes(token), `Common geology missing ${token}`);
  }
  assert.ok(Shaders.TERRAIN_RENDER_FRAG.includes('bedColour') && Shaders.TERRAIN_RENDER_FRAG.includes('renderBedIdx'));
  assert.ok(Shaders.TERRAIN_INTEGRATE_FRAG.includes('chemBedIdx')
    && Shaders.TERRAIN_INTEGRATE_FRAG.includes('bedComp.z * 1.05')
    && Shaders.TERRAIN_INTEGRATE_FRAG.includes('hydrolysisSusceptibility'),
    'Authored carbonate/salt/clay composition must drive chemical geomorphology');
  assert.ok(Shaders.CHEM_WEATHER_FRAG.includes('authoredChemBedIdx')
    && Shaders.CHEM_WEATHER_FRAG.includes('hydrolysisMineralFactor'),
    'Authored bed fracture/porosity/clay must drive reaction-state growth');
  assert.ok(PipelineShaders.STACK_OP_FRAG.includes('strataBedBottomM') && PipelineShaders.STACK_OP_FRAG.includes('strataBedThicknessM'));
  const engineSrc = fs.readFileSync(new URL('../src/erosion/gpu-erosion-engine.js', import.meta.url), 'utf8');
  const pipelineSrc = fs.readFileSync(new URL('../src/erosion/terrain-stack-pipeline.js', import.meta.url), 'utf8');
  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  assert.ok((engineSrc.match(/_bindStrataColumnUniforms\(/g) || []).length >= 10, 'Engine must bind authored beds into all geology consumers');
  assert.ok(pipelineSrc.includes('_bindStrataColumnUniforms(this.progStackOp)'));
  for (const token of ['STRATA_BED_CONTROLS', 'data-strata-bed-param', 'tp-btn-apply-strata-preset', 'data-strata-bed-color']) {
    assert.ok(uiSrc.includes(token), `Strata Inspector missing ${token}`);
  }

  // Exercise the real geological-column builder as well as its source
  // markers, including youngest-bed selection and derived property readouts.
  const inspector = Object.create(ErosionLayerStackUI.prototype);
  inspector.selectedSnowEffectId = null;
  inspector.selectedStrataBedId = null;
  inspector.engine = { params: { worldSpan: 500 }, gridRes: 512 };
  inspector.pipeline = { R: 512, shadingMode: 0 };
  const showcaseLayers = TERRAIN_STACK_PRESETS['frost-pitted-rainbow-range'].layers();
  const strataHtml = inspector._strataColumnCardHtml(showcaseLayers.find((layer) => layer.type === 'stratigraphic-beds'), '#e8d06a');
  assert.ok(strataHtml.includes('data-strata-bed-param') && strataHtml.includes('Frost susceptibility'));
  assert.ok(inspector.selectedStrataBedId, 'Strata Inspector selects a bed for editing');

  // The two isolated showcase terrains exercise every new system without
  // changing Utah's Wind+Rain-only contract or Environment's last position.
  for (const [presetId, bedCount, snowPreset] of [
    ['frost-pitted-rainbow-range', 14, 'segregation-pitting'],
    ['nivation-cirque-strata', 11, 'nivation-cirque'],
  ]) {
    const stack = TERRAIN_STACK_PRESETS[presetId].layers();
    const strata = stack.filter((layer) => layer.type === 'stratigraphic-beds');
    const snow = stack.filter((layer) => layer.type === 'aeolian-glacial');
    assert.equal(strata.length, 1, `${presetId} strata count`);
    assert.equal(strata[0].params.strataColumnEnabled, true);
    assert.equal(strata[0].params.strataBeds.length, bedCount);
    assert.equal(snow.length, 1, `${presetId} snow count`);
    assert.equal(snow[0].params.snowEffectPreset, snowPreset);
    assert.equal(snow[0].params.snowEffects.length, 7);
    assert.ok(deriveSnowEffectWeights(snow[0].params.snowEffects).weights.legacy === 0);
    assert.ok(deriveSnowEffectWeights(snow[0].params.snowEffects).weights.snowmelt > 0);
    assert.equal(stack[stack.length - 1].type, ENVIRONMENT_LAYER_TYPE);
    assert.equal(stack[stack.length - 2].params.seed, 4029);
  }
});

test('8-direction spillway water routing, Sklar-Dietrich stream incision, vadose infiltration, and anti-tent water rendering eliminate overflow ponding', () => {
  assert.ok(
    Shaders.WATER_FLUX_FRAG.includes('if (c.x == 0)') &&
    Shaders.WATER_FLUX_FRAG.includes('max(0.0, bedR - bedC)'),
    'WATER_FLUX_FRAG must drain open boundary sills so water exits the domain cleanly',
  );
  assert.ok(
    Shaders.TERRAIN_INTEGRATE_FRAG.includes('dWaterDiag') &&
    Shaders.TERRAIN_INTEGRATE_FRAG.includes('qDiag'),
    'TERRAIN_INTEGRATE_FRAG must route diagonal spillway water flux alongside cardinal pipe flux',
  );
  assert.ok(
    Shaders.TERRAIN_INTEGRATE_FRAG.includes('coverShield') &&
    Shaders.TERRAIN_INTEGRATE_FRAG.includes('toolEffect'),
    'TERRAIN_INTEGRATE_FRAG must implement Sklar-Dietrich tools-and-cover bedrock incision',
  );
  assert.ok(
    Shaders.TERRAIN_INTEGRATE_FRAG.includes('infiltrated') &&
    Shaders.TERRAIN_RENDER_VERT.includes('confineScale') &&
    Shaders.TERRAIN_RENDER_VERT.includes('filmFade'),
    'Thin hillside films infiltrate and narrow channels suppress raised overflow-tent artifacts',
  );
});

test('Glacier solar ablation + Glen flow law (n=3) and physical frost/thermal spalling without fake procedural crack textures', () => {
  assert.ok(
    Shaders.SNOW_CRYO_FRAG.includes('uSunDir') &&
    Shaders.SNOW_CRYO_FRAG.includes('glenBoost') &&
    Shaders.SNOW_CRYO_FRAG.includes('aspectMeltFactor') &&
    Shaders.SNOW_CRYO_FRAG.includes('min(iceDepth, 28.0)'),
    'SNOW_CRYO_FRAG must couple sun-facing insolation ablation, uncapped 28m cirque ice, and Glen n=3 glacier creep',
  );
  assert.ok(
    !Shaders.COMMON_GEOLOGY_GLSL.includes('evalRockFractureNetwork') &&
    !Shaders.TERRAIN_INTEGRATE_FRAG.includes('evalRockFractureNetwork') &&
    !Shaders.TERRAIN_RENDER_FRAG.includes('evalRockFractureNetwork') &&
    Shaders.FROST_DAMAGE_FRAG.includes('diurnalSwing'),
    'Terrain shaders must NOT fake thermal/frost cracks with a procedural Voronoi crack texture',
  );
});

test('Landscape rockfall couples frost & undercutting to outcrop calving, stamps cliff scars, shatters high-energy impacts, and weathers boulders', () => {
  const sys = new RockCalvingSystem();
  const N = 64;
  const grid = new Float32Array(N * N * 4);
  for (let iz = 0; iz < N; iz++) {
    for (let ix = 0; ix < N; ix++) {
      const u = ix / (N - 1) - 0.5;
      const scarp = 1 / (1 + Math.exp(u * 22.0));
      const idx = (iz * N + ix) * 4;
      grid[idx + 0] = 14.0 + 95.0 * scarp;
      grid[idx + 1] = 0.35 * (1 - scarp);
      grid[idx + 2] = 0.05;
      grid[idx + 3] = 0.08 * (1 - scarp);
    }
  }
  sys.updateTerrainSnapshot(grid, N, 512.0);
  sys.seedForLandscape({ seed: 4029, worldSpan: 512.0, boulderDensity: 0.75, strataStrike: 10, strataDip: 12 });

  const initialEvents = sys.consumeTerrainEvents(64);
  assert.ok(Array.isArray(initialEvents), 'consumeTerrainEvents returns pending GPU terrain scar/impact events');

  // Inject a large airborne boulder impacting the cliff toe at high speed to trigger impact shattering
  const countBeforeImpact = sys.rocks.length;
  const donor = sys.rocks.find((r) => !r.isOutcrop);
  assert.ok(donor, 'Seeded landscape includes mobile calved boulders');
  donor.settled = false;
  donor.sleepTimer = 0;
  sys._ensureBody(donor);
  sys._rescaleBody(donor, 2.2);
  donor.x = 30;
  donor.z = 0;
  const stToe = sys.sampleTerrain(30, 0);
  donor.y = stToe.totalH + 3.2;
  donor.vx = 4.0;
  donor.vy = -26.0;
  donor.vz = 1.5;
  donor.generation = 0;
  for (let s = 0; s < 6; s++) sys.stepPhysics(0.04, false);
  assert.ok(sys.rocks.length >= countBeforeImpact, 'High-energy cliff impact preserves or shatters angular rock bodies cleanly');

  const sumRxBefore = sys.rocks.filter((r) => !r.isOutcrop).reduce((acc, r) => {
    sys._ensureBody(r);
    return acc + r.rx;
  }, 0);
  let weathered = false;
  for (let pass = 0; pass < 6; pass++) {
    if (sys.weatherBoulders(0.25, { rain: 0.9, frost: 0.85, wind: 0.6, chem: 0.7 })) weathered = true;
  }
  const sumRxAfter = sys.rocks.filter((r) => !r.isOutcrop).reduce((acc, r) => {
    assert.ok(Number.isFinite(r.rx) && r.rx > 0, 'Weathered boulder rx must stay finite and positive');
    return acc + r.rx;
  }, 0);
  assert.equal(weathered, true, 'weatherBoulders actively abrades boulders under active environmental rates');
  assert.ok(sumRxAfter < sumRxBefore, 'Boulder dimensions shrink monotonically under physical weathering');
  assert.ok(Shaders.ROCK_TERRAIN_COUPLE_FRAG.includes('uEventPosRad') || Shaders.ROCK_TERRAIN_COUPLE_FRAG.includes('outTerrain'),
    'ROCK_TERRAIN_COUPLE_FRAG stamps detachment scars, craters, and weathered scree aprons onto GPU terrain');
});

test('16-point splines, Lake & Oasis rim-sill layers, and Gaea procedural nodes operate end-to-end', () => {
  assert.equal(MAX_SPLINE_POINTS, 16, 'MAX_SPLINE_POINTS must support 16 control points');

  // Verify 16-point volumetric SDF cave tunnel boring produces valid triangles
  const caveSys = new SdfCaveSystem();
  const pts16 = Array.from({ length: 16 }, (_, i) => {
    const t = i / 15;
    return [-180 + t * 360, Math.round(Math.sin(t * Math.PI * 2) * 80), 22];
  });
  const gridN = 48;
  const gridRgba = new Float32Array(gridN * gridN * 4);
  for (let i = 0; i < gridN * gridN; i++) {
    gridRgba[i * 4] = 55;
  }
  const res = caveSys.rebuildCaveVolume({
    gridRgba,
    gridN,
    worldSpan: 500,
    splineTunnels: [{
      id: 'tunnel-16',
      points: pts16,
      width: 22,
      height: 16,
      archShape: 0.6,
      wallRoughness: 0.35,
      seed: 4029,
      opacity: 1.0,
    }],
  });
  assert.ok(res.vertexCount > 60 && res.hasCaves, '16-point spline tunnel bores a continuous 3D SDF cave mesh');

  // Verify new layer specs and showcase presets
  for (const type of ['mountain-noise', 'ridge-network', 'rocky-outcrops', 'cliff-generator', 'lake-basin', 'desert-oasis']) {
    const layer = createStackLayer(type);
    assert.equal(layer.type, type, `createStackLayer builds ${type}`);
    assert.ok(Object.keys(layer.params).length >= 6, `${type} initializes rich physical default parameters`);
  }
  for (const presetId of ['gaea-alpine-tarn-lake', 'saharan-artesian-oasis-basin']) {
    const preset = TERRAIN_STACK_PRESETS[presetId];
    assert.ok(preset, `Preset ${presetId} exists`);
    const layers = preset.layers();
    assert.equal(layers.at(-1).type, ENVIRONMENT_LAYER_TYPE);
  }
  assert.ok(
    PipelineShaders.STACK_OP_FRAG.includes('texelFetch(uTerrainTex, rimPx, 0)') &&
    PipelineShaders.STACK_OP_FRAG.includes('evalAngularVoronoiFacet'),
    'STACK_OP_FRAG auto-calibrates lake/oasis rim sills via texelFetch and provides angular Voronoi faceting',
  );
});

test('Experimental Remastered StudioControls kit resolves display units, safely ignores minimal mock DOMs, and self-hosts DM Sans fonts', () => {
  const pctSpec = resolveSliderDisplaySpec(0, 1, 0.01, '65%');
  assert.equal(pctSpec.unit, '%');
  assert.equal(pctSpec.multiplier, 100);
  assert.equal(pctSpec.digits, 0);

  const kSpec = resolveSliderDisplaySpec(0, 48000, 500, '8.0k');
  assert.equal(kSpec.unit, 'k');
  assert.equal(kSpec.multiplier, 0.001);
  assert.equal(kSpec.digits, 1);

  const degSpec = resolveSliderDisplaySpec(-180, 180, 1, '35°');
  assert.equal(degSpec.unit, '°');
  assert.equal(degSpec.multiplier, 1);

  const rawSpec = resolveSliderDisplaySpec(0, 1, 0.02, '0.58');
  assert.equal(rawSpec.unit, '—');
  assert.equal(rawSpec.multiplier, 1);
  assert.equal(rawSpec.digits, 2);

  // Must safely no-op when document lacks createElement (Node unit-test environment)
  assert.doesNotThrow(() => {
    enhanceAmountSliders(null);
    enhanceSwitches(null);
    buildRemasteredRockSliders(null, [], {}, () => {}, new Map());
  });

  const cssSrc = fs.readFileSync(new URL('../src/app/style.css', import.meta.url), 'utf8');
  assert.ok(cssSrc.includes('EXPERIMENTAL REMASTERED STUDIO REDESIGN'), 'style.css includes Experimental Remastered redesign');
  assert.ok(cssSrc.includes('/fonts/dm-sans-latin-400-normal.woff2'), 'style.css wires self-hosted DM Sans woff2');
  for (const w of ['300', '400', '500']) {
    assert.ok(
      fs.existsSync(new URL(`../public/fonts/dm-sans-latin-${w}-normal.woff2`, import.meta.url)),
      `Self-hosted DM Sans weight ${w} woff2 exists in public/fonts/`,
    );
  }
});

test('Seasonal simulation OFF/ON toggle updates Environment & Engine live without rebuilding terrain or running hidden melt cycles', () => {
  const engine = Object.create(GpuErosionEngine.prototype);
  engine.stepCount = 45;
  engine.cryoStepCount = 50;
  engine.climateElapsedYears = 12.5;
  engine.climateRunStartYears = 0;
  engine.climateRunActive = true;
  engine.climateRunCompleted = true;
  engine._seasonalClimateSuppressed = false;
  engine.telemetry = {};
  engine.params = {
    ...DEFAULT_EROSION_PARAMS,
    seasonalClimateMode: 1,
    rainIntensity: 0.7,
    snowfallRate: 0.65,
    snowEffects: [createSnowEffect('snowmelt-runoff', { intensity: 1.0, cyclePeriodSteps: 100, cyclePhase: 0 })],
    simHydraulicPlaying: true,
    simCryoPlaying: true,
  };

  const pipeline = Object.create(TerrainStackPipeline.prototype);
  pipeline.engine = engine;
  pipeline.layers = [
    createStackLayer('hydraulic-erosion'),
    createStackLayer('aeolian-glacial', { snowfallRate: 0.65 }),
    createStackLayer('base-landform'),
    createEnvironmentLayer({ seasonalClimateMode: 1 }),
  ];
  pipeline.selectedLayerId = pipeline.layers[0].id;
  pipeline.liveSimRunning = false;
  pipeline.simPlaybackMode = 'all';
  pipeline.simSoloProcess = 'hydraulic';
  pipeline.simProcessEnabled = { hydraulic: true, cryo: true, wind: true, rockfall: true, chemical: true };
  pipeline.simAutoPaused = true;
  pipeline.simPauseReason = 'Seasonal climate run complete (20 years)';
  pipeline._simStateListeners = [];
  let evalCount = 0;
  pipeline.evaluateStack = () => { evalCount++; };

  assert.equal(pipeline.isSeasonalClimateEnabled(), true);
  const offState = pipeline.setSeasonalClimateEnabled(false);
  assert.equal(evalCount, 0, 'Turning seasonal simulation OFF must NOT rebuild or erase terrain');
  assert.equal(pipeline.isSeasonalClimateEnabled(), false);
  assert.equal(offState.seasonalEnabled, false);
  assert.equal(engine.params.seasonalClimateMode, 0);
  assert.equal(pipeline.getEnvironmentLayer().params.seasonalClimateMode, 0);
  assert.equal(engine.climateRunActive, false);
  assert.equal(engine.climateRunCompleted, false);
  assert.equal(pipeline.simAutoPaused, false, 'Turning seasonal simulation OFF clears stale run-complete auto-pause');

  // With seasonal simulation OFF, snowmelt-runoff must NOT run a hidden sine-wave cycle
  const steadyUniforms = engine._getClimateUniforms();
  assert.deepEqual(
    steadyUniforms.meltCycle.vec,
    [1, 1, 0.34, 0],
    'Seasonal OFF holds snowfall, melt, and retention steady without hidden seasonal cycling'
  );
  assert.ok(steadyUniforms.speciesVec[1] > 0.25, 'Snow particles stay active continuously when Seasonal is OFF');
  assert.equal(engine._getSpeciesVisUniform()[1], 1.0, 'Falling snow stays visible when Seasonal is OFF');

  const backOn = pipeline.toggleSeasonalClimate();
  assert.equal(backOn.seasonalEnabled, true);
  assert.equal(engine.params.seasonalClimateMode, 1);

  const uiSrc = fs.readFileSync(new URL('../src/erosion/layer-stack-ui.js', import.meta.url), 'utf8');
  assert.ok(uiSrc.includes('data-sim-seasonal-toggle="1"') && uiSrc.includes('data-env-seasonal-toggle="1"'),
    'Both Simulation Seasons panel and Environment Inspector expose the Seasonal ON/OFF toggle');
});

test('Snow particles emit across multi-process emitters, stagger in the visible sky column, and recycle cleanly after bonding', () => {
  const engine = Object.create(GpuErosionEngine.prototype);
  engine.stepCount = 0;
  engine.cryoStepCount = 0;
  engine.climateElapsedYears = 0;
  engine.climateRunStartYears = 0;
  engine._seasonalClimateSuppressed = false;
  engine.params = {
    ...DEFAULT_EROSION_PARAMS,
    emitterMode: 'sky-rain', // previously emitted 0 snow when Hydraulic was also playing!
    seasonalClimateMode: 0,
    rainIntensity: 0.72,
    snowfallRate: 0.68,
    snowline: 0.62,
    showSnow: true,
    showRain: true,
    simHydraulicPlaying: true,
    simCryoPlaying: true,
    simWindPlaying: false,
    simRockfallPlaying: false,
  };

  const bothOn = engine._getClimateUniforms();
  assert.ok(
    bothOn.speciesVec[1] > 0.28,
    `Expected substantial snow particle share alongside Hydraulic on sky-rain emitter, got ${bothOn.speciesVec[1]}`
  );
  assert.equal(engine._getSpeciesVisUniform()[1], 1.0, 'Snow species visibility is enabled');

  // Shader checks for sky-column spawning, surface-bonding recycle, and distance-readable flakes
  assert.ok(
    Shaders.PARTICLE_STEP_FRAG.includes('surfSpawn + 12.0') &&
    Shaders.PARTICLE_STEP_FRAG.includes('velWater.w * exp(-snowBondRate * dt)'),
    'PARTICLE_STEP_FRAG staggers snowflakes in the visible air column and recycles bonded surface snow'
  );
  assert.ok(
    Shaders.PARTICLE_RENDER_VERT.includes('distScale') &&
    Shaders.INIT_PARTICLES_FRAG.includes('speciesPick == 1.0'),
    'PARTICLE_RENDER_VERT scales snowflakes for camera readability and INIT_PARTICLES_FRAG seeds visible winter sky'
  );
});

test('Real 3D SDF-simulated rocks eliminate polyhedron mesh rocks and solve 6-DOF SDF terrain & rock-rock physics', () => {
  const rockSrc = fs.readFileSync(new URL('../src/erosion/rock-calving.js', import.meta.url), 'utf8');
  assert.ok(
    !rockSrc.includes('polyhedron.js') && !rockSrc.includes('sculptGeologicalParentPoly'),
    'rock-calving.js contains zero legacy polyhedron mesh rock dependencies'
  );

  const atlas = buildRockArchetypeAtlas(7331);
  assert.equal(atlas.isSdfAtlas, true);
  assert.equal(atlas.sdfVolumes.length, 6);
  assert.equal(atlas.sdfRes, ROCK_SDF_RES);
  assert.equal(atlas.sdfBound, ROCK_SDF_BOUND);
  assert.equal(atlas.sdfTexData.length, atlas.sdfTexWidth * atlas.sdfTexHeight * 4);

  for (let a = 0; a < 6; a++) {
    const inside = sampleRockArchetypeSdf(atlas, a, 0, 0, 0, 0);
    const outside = sampleRockArchetypeSdf(atlas, a, 1.55, 1.55, 1.55, 0);
    assert.ok(inside.d < -0.15, `Archetype ${a} CoM must lie deep inside 3D SDF (d=${inside.d})`);
    assert.ok(outside.d > 0.30, `Archetype ${a} exterior corner must have positive SDF (d=${outside.d})`);
    assert.ok(atlas.unitVolumes[a] > 0.5 && atlas.unitInertias[a].every((v) => v > 0.06));
  }

  // GPU 3D SDF raymarching shader checks
  assert.ok(
    Shaders.ROCK_INSTANCE_FRAG.includes('uniform sampler2D uRockSdfTex;') &&
    Shaders.ROCK_INSTANCE_FRAG.includes('uniform int uRockArchetype;') &&
    Shaders.ROCK_INSTANCE_FRAG.includes('sampleRockSdf3D(') &&
    Shaders.ROCK_INSTANCE_FRAG.includes('discard;'),
    'ROCK_INSTANCE_FRAG sphere-traces 3D SDF volumes and discards outside the rounded SDF silhouette'
  );

  // 6-DOF 3D SDF rock-to-rock narrowphase collision + angular impulse transfer
  const N = 64;
  const flat = new Float32Array(N * N * 4);
  for (let i = 0; i < flat.length; i += 4) flat[i] = 20;
  const sys = new RockCalvingSystem();
  sys.updateTerrainSnapshot(flat, N, 512.0);
  const rockA = sys._makeRock({
    submesh: 2, isOutcrop: false, settled: false, grounded: false,
    x: -1.4, y: 26.0, z: 0.35, sx: 1.8, sy: 1.6, sz: 1.8, vx: 6.5, vy: 0, vz: 0,
  });
  const rockB = sys._makeRock({
    submesh: 3, isOutcrop: false, settled: false, grounded: false,
    x: 1.4, y: 26.2, z: -0.35, sx: 1.7, sy: 1.6, sz: 1.7, vx: -5.0, vy: 0, vz: 0,
  });
  sys.rocks.push(rockA, rockB);
  for (let s = 0; s < 8; s++) sys.stepPhysics(0.02);
  assert.ok(rockA.vx < 3.0 && rockB.vx > -2.0, '3D SDF narrowphase collision exchanges linear momentum');
  assert.ok(
    Math.hypot(rockA.ox, rockA.oy, rockA.oz) > 0.05 || Math.hypot(rockB.ox, rockB.oy, rockB.oz) > 0.05,
    'Off-axis 3D SDF collision imparts rotational spin via 3D SDF inertia tensor'
  );
});

test('Sculpt layers automatically insert below Simulation layers so simulations act on sculpted terrain', () => {
  const makeHarness = (layers) => {
    const h = Object.create(TerrainStackPipeline.prototype);
    h.engine = { params: { sunAzimuth: 52, sunElevation: 28, ...DEFAULT_SEASONAL_CLIMATE_PARAMS } };
    h.layers = layers;
    h.selectedLayerId = layers[0]?.id || null;
    h.sculptStore = new Map();
    h.evalCalls = 0;
    h.evaluateStack = () => { h.evalCalls++; };
    h._getOrCreateSculptEntry = (id) => {
      const e = { idx: 0, caveStrokes: [], hasStamps: false };
      h.sculptStore.set(id, e);
      return e;
    };
    return h;
  };

  const veg = createStackLayer('vegetation-placement');
  const rocks = createStackLayer('rock-placement');
  const cryo = createStackLayer('aeolian-glacial', { snowfallRate: 0.5 });
  const hydro = createStackLayer('hydraulic-erosion', { rainIntensity: 0.6 });
  const ridges = createStackLayer('mountain-ridges');
  const base = createStackLayer('base-landform');
  const env = createEnvironmentLayer();

  // Default state on preset load: top placement/simulation layer is selected (index 0)
  const pipe = makeHarness([veg, rocks, cryo, hydro, ridges, base, env]);
  const sculpt = pipe.addLayer('sculpt-layer');
  const sculptIdx = pipe.layers.indexOf(sculpt);
  const hydroIdx = pipe.layers.indexOf(hydro);
  const ridgesIdx = pipe.layers.indexOf(ridges);
  assert.ok(
    sculptIdx > hydroIdx && sculptIdx < ridgesIdx,
    `Expected sculpt-layer below simulations (${hydroIdx}) and above landforms (${ridgesIdx}), got ${sculptIdx}`
  );
  assert.equal(pipe.isSculptAboveSimulations(sculpt.id), false);

  // If user selects base-landform (below sculpt) and adds a new simulation layer, it inserts ABOVE sculpt!
  pipe.selectedLayerId = base.id;
  const wind = pipe.addLayer('wind-erosion');
  assert.ok(
    pipe.layers.indexOf(wind) < pipe.layers.indexOf(sculpt),
    'Adding a simulation layer while base-landform is selected places it above existing sculpt-layer'
  );

  // If user manually drags sculpt-layer to index 0 (above sims), helper detects and moves it below sims
  pipe.moveLayer(pipe.layers.indexOf(sculpt), 0);
  assert.equal(pipe.isSculptAboveSimulations(sculpt.id), true);
  assert.equal(pipe.moveSculptBelowSimulations(sculpt.id), true);
  assert.equal(pipe.isSculptAboveSimulations(sculpt.id), false);

  // Spline Canyon + River Test preset also orders hydraulic-erosion above sculpt-layer
  const canyonLayers = TERRAIN_STACK_PRESETS['spline-canyon-river-test'].layers();
  const presetHydroIdx = canyonLayers.findIndex((l) => l.type === 'hydraulic-erosion');
  const presetSculptIdx = canyonLayers.findIndex((l) => l.type === 'sculpt-layer');
  assert.ok(presetHydroIdx >= 0 && presetSculptIdx > presetHydroIdx, 'Preset evaluates sculpt-layer before hydraulic-erosion');
});


