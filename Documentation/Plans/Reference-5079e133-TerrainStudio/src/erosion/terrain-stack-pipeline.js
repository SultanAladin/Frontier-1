/**
 * Non-Destructive Bottom-to-Top GPU Terrain Stack Evaluator & Renderer.
 *
 * Evaluates an ordered stack of Landform, SDF Geometry, Sculpt, Spline, Simulation,
 * Rock-Placement, and Vegetation-Placement layers from bottom (last index) to top (index 0)
 * directly on `GpuErosionEngine`'s native textures (`terrainTex`, `snowTex`, `fluxTex`,
 * `velocityFieldTex`) using the exact `c5ae88b` / `2cc3d34` generators (`INIT_TERRAIN_FRAG`
 * `terrainType` 0..18, `seed: 4029`), conservative simulation shaders, and `TERRAIN_RENDER`
 * geological shading!
 */

import { DEFAULT_EROSION_PARAMS, EMITTER_MODES } from './erosion-presets.js';
import { FULLSCREEN_VERT } from './erosion-shaders.js';
import { MAX_GRASS_TUFTS } from './grass-system.js';
import {
  SCULPT_STAMP_FRAG,
  SDF_CAVE_FRAG,
  SDF_CAVE_VERT,
  STACK_COPY_VEC_FRAG,
  STACK_MASK_BAKE_FRAG,
  STACK_MASK_BLEND_FRAG,
  STACK_OP_FRAG,
} from './pipeline-shaders.js';
import { SdfCaveSystem } from './sdf-cave-mesher.js';
import {
  ENVIRONMENT_LAYER_TYPE,
  LAYER_OPERATION_SPECS,
  MAX_SPLINE_POINTS,
  TERRAIN_STACK_PRESETS,
  createStackLayer,
  ensureEnvironmentLayer,
} from './terrain-stack-presets.js';
import {
  DEFAULT_FROST_PARAMS,
  createSnowEffectPreset,
  deriveSnowEffectWeights,
  normalizeFrostParams,
  normalizeSnowEffects,
} from './frost-weathering.js';
import {
  normalizeStrataColumn,
  strataColumnThickness,
} from './stratigraphic-column.js';
import {
  DEFAULT_SEASONAL_CLIMATE_PARAMS,
  climateDefaultsForLandscape,
  normalizeSeasonalClimateParams,
} from './seasonal-climate.js';

// The optional world-map mask is applied to the base landform before the
// remainder of the stack and simulation run. Ordinary Terrain Studio is unchanged.
const WORLD_BASE_MASK_FRAG = `#version 300 es
precision highp float;
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uTerrain;
uniform sampler2D uWorldMask;
out vec4 outTerrain;
void main() {
  vec4 terrain = texture(uTerrain, vUv);
  float weight = texture(uWorldMask, vUv).r;
  outTerrain = vec4(terrain.rgb * weight, terrain.a * weight);
}`;

const PART_TEX_W = 256;
const PART_TEX_H = 128;
const MAX_GPU_PARTICLES = PART_TEX_W * PART_TEX_H;

function compile(gl, type, src, label) {
  const sh = gl.createShader(type);
  gl.shaderSource(sh, src);
  gl.compileShader(sh);
  if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
    const info = gl.getShaderInfoLog(sh);
    gl.deleteShader(sh);
    throw new Error(`[Pipeline ${label}] ${info}`);
  }
  return sh;
}

function link(gl, vsSrc, fsSrc, label) {
  const vs = compile(gl, gl.VERTEX_SHADER, vsSrc, `${label}:VS`);
  const fs = compile(gl, gl.FRAGMENT_SHADER, fsSrc, `${label}:FS`);
  const p = gl.createProgram();
  gl.attachShader(p, vs);
  gl.attachShader(p, fs);
  gl.linkProgram(p);
  gl.deleteShader(vs);
  gl.deleteShader(fs);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
    const info = gl.getProgramInfoLog(p);
    gl.deleteProgram(p);
    throw new Error(`[Pipeline ${label}] Link error: ${info}`);
  }
  return p;
}

function makeFloatTex(gl, w, h, linear = false) {
  const tex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, linear ? gl.LINEAR : gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, linear ? gl.LINEAR : gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, w, h, 0, gl.RGBA, gl.FLOAT, null);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return tex;
}

function makeFbo(gl, colorAttachments) {
  const fbo = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
  const bufs = [];
  for (let i = 0; i < colorAttachments.length; i++) {
    const att = gl.COLOR_ATTACHMENT0 + i;
    gl.framebufferTexture2D(gl.FRAMEBUFFER, att, gl.TEXTURE_2D, colorAttachments[i], 0);
    bufs.push(att);
  }
  gl.drawBuffers(bufs);
  const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  if (status !== gl.FRAMEBUFFER_COMPLETE) {
    throw new Error(`[Pipeline FBO] Incomplete framebuffer: 0x${status.toString(16)}`);
  }
  return fbo;
}

const MASK_MODE_CODE = {
  height: 0,
  slope: 1,
  flow: 2,
  concavity: 3,
  radial: 4,
  noise: 5,
};

const SNOW_LIVE_SCALARS = [
  'snowEffectPreset',
  ...Object.keys(DEFAULT_FROST_PARAMS),
];

function snowEffectsForLayer(layer) {
  // An explicit array is authoritative even when empty: deleting every
  // effect (or editing a snowpack-only stack into "custom") must not
  // resurrect the balanced preset. Only pre-feature layers with no array
  // at all fall back to their named preset.
  if (Array.isArray(layer?.params?.snowEffects)) {
    return normalizeSnowEffects(layer.params.snowEffects);
  }
  return createSnowEffectPreset(layer?.params?.snowEffectPreset ?? DEFAULT_EROSION_PARAMS.snowEffectPreset).effects;
}

export const SIM_PROCESS_DEFS = [
  { id: 'hydraulic', flag: 'simHydraulicPlaying', label: 'Rain & runoff', short: 'Rain', layerTypes: ['hydraulic-erosion'] },
  { id: 'cryo', flag: 'simCryoPlaying', label: 'Snow & glaciers', short: 'Snow', layerTypes: ['aeolian-glacial'] },
  { id: 'wind', flag: 'simWindPlaying', label: 'Wind erosion & saltation', short: 'Wind', layerTypes: ['wind-erosion', 'aeolian-glacial'] },
  { id: 'rockfall', flag: 'simRockfallPlaying', label: 'Talus, rockfall & boulders', short: 'Rockfall', layerTypes: ['thermal-talus', 'rock-placement'] },
  { id: 'chemical', flag: 'simChemicalPlaying', label: 'Chemical weathering', short: 'Chemical', layerTypes: ['chemical-weathering'] },
];

export class TerrainStackPipeline {
  constructor(engine) {
    this.engine = engine;
    this.gl = engine.gl;
    this.R = engine.gridRes;

    // Document state — defaults to `icelandic-highlands` (`seed: 4029`), matching `c5ae88b` / `2cc3d34`!
    this.activePresetId = 'icelandic-highlands';
    const initPreset = TERRAIN_STACK_PRESETS[this.activePresetId];
    this.projectTitle = initPreset.name;
    this.paletteId = initPreset.palette ?? 1;
    this.fabricMode = initPreset.fabricMode ?? 0;
    this.shadingMode = 0; // 0=Derived Geology, 1=AO & Protrusions, 2=Hardness, 3=Flow, 4=Sedimentation, 5=Active Mask B&W, 6=Clay, 7=Cryosphere, 8=Hydrology, 9=Mineral, 10=Frost damage
    this.authorTool = 'navigate'; // 'navigate' | 'sculpt-add' | 'sculpt-remove' | 'sculpt-smooth' | 'cave-carve' | 'cave-fill' | 'spline-edit'
    this.layers = [];
    this.selectedLayerId = null;
    this.lastEvalMs = 0;
    // Live simulation is explicitly triggered by the user (Simulate button /
    // a season chip), never auto-started by stack evaluation. Default mode is
    // one process at a time ("seasons": snow, then rain, then chemical...);
    // simultaneous mode remains available in the Simulation panel.
    this.liveSimRunning = false;
    this.simPlaybackMode = 'solo'; // 'solo' | 'all'
    this.simSoloProcess = 'hydraulic';
    this.simProcessEnabled = { hydraulic: true, cryo: true, wind: true, rockfall: true, chemical: true };
    this.simAvailable = { hydraulic: false, cryo: false, wind: false, rockfall: false, chemical: false };
    this.simAutoPaused = true;
    this.simPauseReason = 'Press Simulate to run a season';
    this._simStateListeners = [];
    this.hasActiveVecDisp = false;

    // Per-sculpt-layer GPU delta textures: layerId -> { tex: [t0, t1], fbo: [f0, f1], idx: 0, caveStrokes: [] }
    this.sculptStore = new Map();

    // 3D SDF Cave & Undercut Volumetric System
    this.caveSystem = new SdfCaveSystem();

    this._initGpuResources();
    this.loadPreset(this.activePresetId);
  }

  _initGpuResources() {
    const gl = this.gl;
    const R = this.R;

    this.progStackOp = link(gl, FULLSCREEN_VERT, STACK_OP_FRAG, 'StackOp');
    this.progSculptStamp = link(gl, FULLSCREEN_VERT, SCULPT_STAMP_FRAG, 'SculptStamp');
    this.progMaskBake = link(gl, FULLSCREEN_VERT, STACK_MASK_BAKE_FRAG, 'StackMaskBake');
    this.progMaskBlend = link(gl, FULLSCREEN_VERT, STACK_MASK_BLEND_FRAG, 'StackMaskBlend');
    this.progCopyVec = link(gl, FULLSCREEN_VERT, STACK_COPY_VEC_FRAG, 'StackCopyVec');
    this.progCave = link(gl, SDF_CAVE_VERT, SDF_CAVE_FRAG, 'SdfCave');

    // 3D Lateral Overhang Vector Displacement ping-pong textures + scratch textures
    this.vecDispTex = [makeFloatTex(gl, R, R, false), makeFloatTex(gl, R, R, false)];
    this.vecIdx = 0;
    this.maskTex = makeFloatTex(gl, R, R, false);
    this.prevTerrainScratchTex = makeFloatTex(gl, R, R, false);
    this.emptySculptTex = makeFloatTex(gl, R, R, false);

    // 2-target MRT FBOs writing directly to `(engine.terrainTex[i], this.vecDispTex[i])`!
    this.fboStackOp = [
      makeFbo(gl, [this.engine.terrainTex[0], this.vecDispTex[0]]),
      makeFbo(gl, [this.engine.terrainTex[1], this.vecDispTex[1]]),
    ];
    this.fboVecOnly = [makeFbo(gl, [this.vecDispTex[0]]), makeFbo(gl, [this.vecDispTex[1]])];
    this.fboMask = makeFbo(gl, [this.maskTex]);
    this.fboPrevScratch = makeFbo(gl, [this.prevTerrainScratchTex]);

    // Clear emptySculptTex & vecDispTex once
    for (const f of [this.fboVecOnly[0], this.fboVecOnly[1], makeFbo(gl, [this.emptySculptTex])]) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, f);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    // Downsampled 64x64 readback buffer
    this.snapN = 64;
    this.snapBuf = new Float32Array(this.snapN * this.snapN * 4);
    this.snapSnowBuf = new Float32Array(this.snapN * this.snapN * 4); // cryo twin for rock embedding
    this.snapFrostBuf = new Float32Array(this.snapN * this.snapN * 4); // frost/crack twin for cliff calving
    this.lastViewProj = null;

    // 128x128 Cave Portal Cutout Texture
    const P = this.caveSystem.portalMaskSize;
    this.cavePortalTex = makeFloatTex(gl, P, P, false);

    // 3D Volumetric SDF Cave VAO & dynamic buffers
    this.caveVao = gl.createVertexArray();
    gl.bindVertexArray(this.caveVao);
    this.cavePosBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.cavePosBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);

    this.caveNrmBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.caveNrmBuf);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);

    this.caveAttBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.caveAttBuf);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 4, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
  }

  _bindTex(prog, name, tex, unit) {
    const gl = this.gl;
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.uniform1i(gl.getUniformLocation(prog, name), unit);
  }

  _copyTex(srcTex, dstFbo, w = this.R, h = this.R) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, dstFbo);
    gl.viewport(0, 0, w, h);
    gl.useProgram(this.progCopyVec);
    gl.bindVertexArray(this.engine.emptyVao);
    this._bindTex(this.progCopyVec, 'uSrcTex', srcTex, 0);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  /** Set the cropped world-shape mask for the active terrain document. */
  setWorldBaseMask(mask, size = 256) {
    const gl = this.gl;
    if (!mask) {
      this.worldBaseMaskEnabled = false;
      return;
    }
    if (mask.length !== size * size) throw new Error('Invalid world mask');
    if (!this.progWorldBaseMask) {
      this.progWorldBaseMask = link(gl, FULLSCREEN_VERT, WORLD_BASE_MASK_FRAG, 'WorldBaseMask');
      this.worldBaseMaskTex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, this.worldBaseMaskTex);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    }
    const rgba = new Uint8Array(size * size * 4);
    for (let i = 0; i < mask.length; i++) rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = mask[i];
    gl.bindTexture(gl.TEXTURE_2D, this.worldBaseMaskTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, size, size, 0, gl.RGBA, gl.UNSIGNED_BYTE, rgba);
    gl.bindTexture(gl.TEXTURE_2D, null);
    this.worldBaseMaskEnabled = true;
  }

  _applyWorldBaseMask() {
    if (!this.worldBaseMaskEnabled) return;
    const gl = this.gl, current = this.engine.terrainIdx, next = 1 - current;
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.engine.fboTerrainSingle[next]);
    gl.viewport(0, 0, this.R, this.R);
    gl.useProgram(this.progWorldBaseMask);
    gl.bindVertexArray(this.engine.emptyVao);
    this._bindTex(this.progWorldBaseMask, 'uTerrain', this.engine.terrainTex[current], 0);
    this._bindTex(this.progWorldBaseMask, 'uWorldMask', this.worldBaseMaskTex, 1);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    this.engine.terrainIdx = next;
  }

  _getOrCreateSculptEntry(layerId) {
    if (this.sculptStore.has(layerId)) return this.sculptStore.get(layerId);
    const gl = this.gl;
    const R = this.R;
    const t0 = makeFloatTex(gl, R, R, false);
    const t1 = makeFloatTex(gl, R, R, false);
    const f0 = makeFbo(gl, [t0]);
    const f1 = makeFbo(gl, [t1]);
    for (const f of [f0, f1]) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, f);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const entry = { tex: [t0, t1], fbo: [f0, f1], idx: 0, caveStrokes: [], hasStamps: false };
    this.sculptStore.set(layerId, entry);
    return entry;
  }

  clearSculptLayer(layerId) {
    const entry = this.sculptStore.get(layerId);
    const layer = this.layers.find((l) => l.id === layerId);
    if (entry) {
      const gl = this.gl;
      for (const f of entry.fbo) {
        gl.bindFramebuffer(gl.FRAMEBUFFER, f);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      entry.caveStrokes.length = 0;
      entry.hasStamps = false;
    }
    if (layer) layer.params.strokeCount = 0;
    this.evaluateStack({ reseedPlacements: true });
  }

  loadPreset(presetId) {
    const preset = TERRAIN_STACK_PRESETS[presetId] || TERRAIN_STACK_PRESETS['icelandic-highlands'];
    this.activePresetId = preset.id;
    this.projectTitle = preset.name;
    this.paletteId = preset.palette ?? 0;
    this.fabricMode = preset.fabricMode ?? 0;

    // Apply preset's non-layer visual & emitter configuration cleanly
    const ep = this.engine.params;
    ep.landscape = preset.id;
    ep.materialTheme = this.paletteId;
    ep.fabricMode = this.fabricMode;
    ep.emitterMode = preset.emitterMode ?? DEFAULT_EROSION_PARAMS.emitterMode;
    ep.particleCount = preset.particleCount ?? DEFAULT_EROSION_PARAMS.particleCount;
    ep.particleInertia = preset.particleInertia ?? DEFAULT_EROSION_PARAMS.particleInertia;
    ep.pipeFlowBlend = preset.pipeFlowBlend ?? DEFAULT_EROSION_PARAMS.pipeFlowBlend;
    ep.sunAzimuth = preset.sunAzimuth ?? DEFAULT_EROSION_PARAMS.sunAzimuth;
    ep.sunElevation = preset.sunElevation ?? DEFAULT_EROSION_PARAMS.sunElevation;
    Object.assign(ep, climateDefaultsForLandscape(preset.id));
    ep.showWater = preset.showWater ?? true;
    ep.showParticles = preset.showParticles ?? true;

    // Clear old sculpt buffers and populate fresh editable stack layers
    for (const [, entry] of this.sculptStore) {
      for (const t of entry.tex) this.gl.deleteTexture(t);
      for (const f of entry.fbo) this.gl.deleteFramebuffer(f);
    }
    this.sculptStore.clear();

    this.layers = preset.layers();
    this._ensureEnvironmentLayer({
      sunAzimuth: ep.sunAzimuth,
      sunElevation: ep.sunElevation,
    });
    // Select the top meaningful terrain layer, never the locked final
    // Environment row just because a malformed stack contained only it.
    this.selectedLayerId = this.layers.find((layer) => layer.type !== ENVIRONMENT_LAYER_TYPE)?.id
      || this.layers[0]?.id
      || null;
    const selectedProcesses = this.simulationProcessesForLayer(this.getSelectedLayer());
    if (selectedProcesses.length && this.simPlaybackMode === 'solo') {
      this.simSoloProcess = selectedProcesses[0];
    }
    this.evaluateStack({ reseedPlacements: true });
    this.engine.camera.target[1] = (ep.relief || 168) * 0.30;
  }

  _ensureEnvironmentLayer(sunDefaults = {}) {
    const ep = this.engine?.params || {};
    const selectedWasEnvironment = Array.isArray(this.layers)
      && this.layers.find((layer) => layer?.id === this.selectedLayerId)?.type === ENVIRONMENT_LAYER_TYPE;
    const seasonalDefaults = {};
    for (const key of Object.keys(DEFAULT_SEASONAL_CLIMATE_PARAMS)) {
      const value = sunDefaults[key] ?? sunDefaults.params?.[key] ?? ep[key];
      if (Number.isFinite(Number(value))) seasonalDefaults[key] = Number(value);
    }
    this.layers = ensureEnvironmentLayer(this.layers, {
      sunAzimuth: sunDefaults.sunAzimuth ?? ep.sunAzimuth,
      sunElevation: sunDefaults.sunElevation ?? ep.sunElevation,
      ...seasonalDefaults,
    });
    const environment = this.layers[this.layers.length - 1] || null;
    // If normalization discarded a selected duplicate, keep the Inspector
    // attached to the surviving canonical Environment layer.
    if (selectedWasEnvironment && environment) this.selectedLayerId = environment.id;
    return environment;
  }

  getEnvironmentLayer() {
    return this._ensureEnvironmentLayer();
  }

  /**
   * Apply Environment sun angles and seasonal-climate location directly to
   * live render/simulation state. Environment is not a terrain operation,
   * so changing it neither rebuilds the stack nor erases the terrain.
   */
  applyLiveEnvironmentLayerParams(layer) {
    if (!layer || layer.type !== ENVIRONMENT_LAYER_TYPE) return false;
    const environment = this._ensureEnvironmentLayer({
      sunAzimuth: this.engine.params.sunAzimuth,
      sunElevation: this.engine.params.sunElevation,
      ...normalizeSeasonalClimateParams(this.engine.params),
    });
    if (!environment || environment.id !== layer.id) return false;
    const p = environment.params || {};
    const ep = this.engine.params;
    const az = Number(p.sunAzimuth);
    const el = Number(p.sunElevation);
    ep.sunAzimuth = Number.isFinite(az)
      ? Math.max(-180, Math.min(180, az))
      : DEFAULT_EROSION_PARAMS.sunAzimuth;
    ep.sunElevation = Number.isFinite(el)
      ? Math.max(4, Math.min(85, el))
      : DEFAULT_EROSION_PARAMS.sunElevation;
    environment.params.sunAzimuth = ep.sunAzimuth;
    environment.params.sunElevation = ep.sunElevation;
    const seasonal = normalizeSeasonalClimateParams(p);
    Object.assign(ep, seasonal);
    Object.assign(environment.params, seasonal);
    if (seasonal.seasonalClimateMode !== 1) {
      this.engine.climateRunActive = false;
      this.engine.climateRunCompleted = false;
      if (this.simAutoPaused && /seasonal climate run complete/i.test(this.simPauseReason || '')) {
        this.simAutoPaused = false;
        this.simPauseReason = '';
      }
    }
    if (typeof this.engine._syncSeasonalClimateTelemetry === 'function') {
      this.engine._syncSeasonalClimateTelemetry();
    }
    return true;
  }

  /** Returns whether location-based seasonal climate cycling is currently ON. */
  isSeasonalClimateEnabled() {
    const env = this.getEnvironmentLayer();
    const mode = Number(env?.params?.seasonalClimateMode ?? this.engine?.params?.seasonalClimateMode ?? 1);
    return mode === 1;
  }

  /**
   * Turns Seasonal Simulation ON (`seasonalClimateMode = 1`) or OFF (`0` =
   * constant non-seasonal forcing) live without rebuilding or erasing terrain.
   */
  setSeasonalClimateEnabled(enabled) {
    const env = this.getEnvironmentLayer();
    const nextMode = enabled ? 1 : 0;
    if (env) {
      env.params.seasonalClimateMode = nextMode;
      this.applyLiveEnvironmentLayerParams(env);
    } else if (this.engine?.params) {
      this.engine.params.seasonalClimateMode = nextMode;
    }
    if (enabled && this.liveSimRunning && typeof this.engine.beginSeasonalClimateRun === 'function') {
      this.engine.beginSeasonalClimateRun();
    }
    return this._emitSimulationState();
  }

  toggleSeasonalClimate() {
    return this.setSeasonalClimateEnabled(!this.isSeasonalClimateEnabled());
  }

  /**
   * Computes the natural bottom-to-top insertion slot (`0` = top/evaluated last,
   * `layers.length - 2` = bottom `base-landform`/evaluated first) so that:
   * - Adding a `sculpt-layer` when a Simulation / Rocks / Vegetation layer (or
   *   nothing) is selected places the Sculpt layer directly BELOW the upper
   *   simulation & placement stack — ensuring all simulations run AFTER sculpting!
   * - Adding a Simulation layer when `base-landform` is selected below an
   *   existing `sculpt-layer` places the Simulation layer ABOVE the Sculpt layer!
   */
  _resolveInsertIndexForType(type) {
    const environmentIdx = Math.max(0, this.layers.length - 1);
    const selIdx = this.layers.findIndex((l) => l.id === this.selectedLayerId);
    const selLayer = selIdx >= 0 ? this.layers[selIdx] : null;
    const selCat = selLayer ? LAYER_OPERATION_SPECS[selLayer.type]?.category : null;
    const newCat = LAYER_OPERATION_SPECS[type]?.category || null;
    const isUpperSimOrPlacement = (cat) => cat === 'simulation' || cat === 'rocks' || cat === 'vegetation';

    if (selLayer?.type === ENVIRONMENT_LAYER_TYPE) {
      return environmentIdx;
    }

    if (type === 'sculpt-layer') {
      if (selIdx < 0 || isUpperSimOrPlacement(selCat)) {
        let k = 0;
        while (k < environmentIdx && isUpperSimOrPlacement(LAYER_OPERATION_SPECS[this.layers[k]?.type]?.category)) {
          k++;
        }
        return k;
      }
      return Math.min(selIdx, environmentIdx);
    }

    if (newCat === 'simulation') {
      const topSculptIdx = this.layers.findIndex((l) => l.type === 'sculpt-layer');
      if (topSculptIdx >= 0 && (selIdx < 0 || selIdx > topSculptIdx)) {
        return topSculptIdx;
      }
    }

    return selIdx >= 0 ? Math.min(selIdx, environmentIdx) : 0;
  }

  /** Returns true if a sculpt layer is currently sitting above any enabled simulation layer. */
  isSculptAboveSimulations(layerId) {
    const idx = this.layers.findIndex((l) => l.id === layerId && l.type === 'sculpt-layer');
    if (idx < 0) return false;
    for (let i = idx + 1; i < this.layers.length; i++) {
      const l = this.layers[i];
      if (l?.enabled && LAYER_OPERATION_SPECS[l.type]?.category === 'simulation') {
        return true;
      }
    }
    return false;
  }

  /** Moves a sculpt layer directly below all simulation layers so simulations act on sculpted terrain. */
  moveSculptBelowSimulations(layerId) {
    this._ensureEnvironmentLayer();
    const fromIdx = this.layers.findIndex((l) => l.id === layerId && l.type === 'sculpt-layer');
    if (fromIdx < 0) return false;
    let lowestSimIdx = -1;
    for (let i = 0; i < this.layers.length - 1; i++) {
      if (LAYER_OPERATION_SPECS[this.layers[i]?.type]?.category === 'simulation') {
        lowestSimIdx = i;
      }
    }
    if (lowestSimIdx <= fromIdx) return false;
    return this.moveLayer(fromIdx, lowestSimIdx);
  }

  addLayer(type, overrides = {}) {
    this._ensureEnvironmentLayer();
    if (type === ENVIRONMENT_LAYER_TYPE) {
      const environment = this.layers[this.layers.length - 1];
      this.selectedLayerId = environment?.id || this.selectedLayerId;
      return environment || null;
    }
    const newLayer = createStackLayer(type, overrides);
    const insertAt = this._resolveInsertIndexForType(type);
    this.layers.splice(insertAt, 0, newLayer);
    this._ensureEnvironmentLayer();
    this.selectedLayerId = newLayer.id;
    if (type === 'sculpt-layer') {
      this._getOrCreateSculptEntry(newLayer.id);
    }
    this.evaluateStack({ reseedPlacements: true });
    return newLayer;
  }

  duplicateLayer(layerId) {
    this._ensureEnvironmentLayer();
    const idx = this.layers.findIndex((l) => l.id === layerId);
    if (idx < 0) return null;
    const src = this.layers[idx];
    if (src.locked || src.type === ENVIRONMENT_LAYER_TYPE) return null;
    const copy = createStackLayer(src.type, {
      name: `${src.name} copy`,
      enabled: src.enabled,
      mask: structuredClone(src.mask),
      params: structuredClone(src.params),
    });
    this.layers.splice(idx, 0, copy);
    this._ensureEnvironmentLayer();
    this.selectedLayerId = copy.id;
    this.evaluateStack({ reseedPlacements: true });
    return copy;
  }

  deleteLayer(layerId) {
    this._ensureEnvironmentLayer();
    if (this.layers.length <= 1) return false;
    const idx = this.layers.findIndex((l) => l.id === layerId);
    if (idx < 0 || this.layers[idx].locked || this.layers[idx].type === ENVIRONMENT_LAYER_TYPE) return false;
    this.layers.splice(idx, 1);
    this._ensureEnvironmentLayer();
    const sc = this.sculptStore.get(layerId);
    if (sc) {
      for (const t of sc.tex) this.gl.deleteTexture(t);
      for (const f of sc.fbo) this.gl.deleteFramebuffer(f);
      this.sculptStore.delete(layerId);
    }
    if (this.selectedLayerId === layerId) {
      const fallbackIdx = Math.min(idx, this.layers.length - 1);
      this.selectedLayerId = this.layers[fallbackIdx]?.type === ENVIRONMENT_LAYER_TYPE && fallbackIdx > 0
        ? this.layers[fallbackIdx - 1]?.id
        : this.layers[fallbackIdx]?.id || null;
    }
    this.evaluateStack({ reseedPlacements: true });
    return true;
  }

  moveLayer(fromIdx, toIdx) {
    this._ensureEnvironmentLayer();
    if (fromIdx < 0 || fromIdx >= this.layers.length) return false;
    const movedCandidate = this.layers[fromIdx];
    // Preserve the existing `locked` contract (delete/duplicate/drag-safe,
    // but arrow-reorderable for Base/Spline layers). Only Environment has a
    // position lock and refuses every move.
    if (!movedCandidate || movedCandidate.type === ENVIRONMENT_LAYER_TYPE) return false;
    // Environment is pinned to the final slot; ordinary layers may move
    // anywhere above it but never below it.
    const maxIdx = Math.max(0, this.layers.length - 2);
    const clamped = Math.max(0, Math.min(maxIdx, toIdx));
    if (fromIdx === clamped) return false;
    const [moved] = this.layers.splice(fromIdx, 1);
    this.layers.splice(clamped, 0, moved);
    this._ensureEnvironmentLayer();
    this.evaluateStack({ reseedPlacements: true });
    return true;
  }

  getSelectedLayer() {
    return this.layers.find((l) => l.id === this.selectedLayerId) || null;
  }

  addSimulationStateListener(fn) {
    if (typeof fn === 'function') this._simStateListeners.push(fn);
  }

  _emitSimulationState() {
    const state = this.getSimulationControlState();
    for (const fn of this._simStateListeners || []) {
      try { fn(state); } catch { /* UI listeners must never break evaluation */ }
    }
    return state;
  }

  _hasDedicatedWindLayer() {
    return this.layers.some((l) => l.enabled && l.type === 'wind-erosion');
  }

  _computeSimAvailability() {
    const avail = { hydraulic: false, cryo: false, wind: false, rockfall: false, chemical: false };
    const hasDedicatedWind = this._hasDedicatedWindLayer();
    for (const layer of this.layers) {
      if (!layer.enabled) continue;
      const p = layer.params || {};
      if (layer.type === 'hydraulic-erosion') {
        avail.hydraulic = true;
      } else if (layer.type === 'thermal-talus' || layer.type === 'rock-placement') {
        avail.rockfall = true;
      } else if (layer.type === 'chemical-weathering') {
        if (this.paletteId !== 7 && (p.chemRate ?? DEFAULT_EROSION_PARAMS.chemRate) > 0.001) avail.chemical = true;
      } else if (layer.type === 'wind-erosion') {
        if ((p.windSpeed ?? 0) > 0.03 && (p.windErosionRate ?? DEFAULT_EROSION_PARAMS.windErosionRate) > 0.001) avail.wind = true;
      } else if (layer.type === 'aeolian-glacial') {
        const frostWeights = deriveSnowEffectWeights(snowEffectsForLayer(layer)).weights;
        const frostActive = (frostWeights.segregation + frostWeights.volumetric
          + frostWeights.nivation + frostWeights.frostCreep + frostWeights.avalanche
          + frostWeights.glacierPlucking + frostWeights.legacy + frostWeights.snowmelt) > 0.001;
        if ((p.snowfallRate ?? 0) > 0.015 || (p.glacierStrength ?? 0) > 0.015 || frostActive) avail.cryo = true;
        // Legacy combined stacks may still drive wind from the snow layer,
        // but a dedicated Wind Erosion layer is the sole wind authority.
        if (!hasDedicatedWind && (p.windSpeed ?? 0) > 0.03) avail.wind = true;
      }
    }
    this.simAvailable = avail;
    return avail;
  }

  /**
   * Applies Wind Erosion settings to the live engine without rebuilding the
   * stack. This lets the user run a Wind season, pause, change direction or
   * abrasion/turbulence, then resume on the already-eroded terrain (essential
   * for multi-direction natural windwork). Only the topmost enabled wind
   * layer is the live authority, matching bottom-to-top evaluation order.
   */
  applyLiveWindLayerParams(layer, reason = 'Wind settings changed') {
    if (!layer || layer.type !== 'wind-erosion' || !layer.enabled) return false;
    const authority = this.layers.find((l) => l.enabled && l.type === 'wind-erosion');
    if (!authority || authority.id !== layer.id) return false;
    const ep = this.engine.params;
    const p = layer.params || {};
    const keys = [
      'windSpeed', 'windAzimuth', 'windShiftMode', 'windShiftRange',
      'windShiftPeriod', 'dryAeolianMode', 'windErosionRate',
      'windAbrasion', 'windDeflation', 'windDeposition', 'windTurbulence',
      'windGustiness', 'windSandSupply', 'windCurtainHeight',
      'windStreamlining', 'windFluting',
    ];
    for (const key of keys) ep[key] = p[key] ?? DEFAULT_EROSION_PARAMS[key];
    ep.simWindPlaying = ep.windSpeed > 0.03 && ep.windErosionRate > 0.001;
    this.pauseSimulationForEdit(reason);
    this._applySimulationPlayback(true);
    return true;
  }

  /**
   * Applies Snow/frost effect-stack edits to the live engine without
   * rebuilding terrain or clearing accumulated sub-resolution cracks.
   * Only the topmost enabled Snow layer is the live authority (layers[0]
   * side first), matching the Wind authority rule. Snowpack amount and
   * bake count are deliberately NOT live: they rebuild/seeds on evaluate.
   */
  applyLiveSnowLayerParams(layer, reason = 'Snow / frost settings changed') {
    if (!layer || layer.type !== 'aeolian-glacial' || !layer.enabled) return false;
    const authority = this.layers.find((l) => l.enabled && l.type === 'aeolian-glacial');
    if (!authority || authority.id !== layer.id) return false;
    const ep = this.engine.params;
    const p = layer.params || {};
    for (const key of SNOW_LIVE_SCALARS) {
      if (Object.prototype.hasOwnProperty.call(p, key)) ep[key] = p[key];
      else if (Object.prototype.hasOwnProperty.call(DEFAULT_EROSION_PARAMS, key)) ep[key] = DEFAULT_EROSION_PARAMS[key];
      else if (Object.prototype.hasOwnProperty.call(DEFAULT_FROST_PARAMS, key)) ep[key] = DEFAULT_FROST_PARAMS[key];
    }
    Object.assign(ep, normalizeFrostParams(ep));
    ep.snowEffects = snowEffectsForLayer(layer);
    this.pauseSimulationForEdit(reason);
    this._applySimulationPlayback(true);
    return true;
  }

  simulationProcessesForLayer(layer) {
    if (!layer) return [];
    if (layer.type === 'wind-erosion') return ['wind'];
    if (layer.type === 'aeolian-glacial') return ['cryo'];
    return SIM_PROCESS_DEFS.filter((d) => d.layerTypes.includes(layer.type)).map((d) => d.id);
  }

  _applySimulationPlayback(emit = true) {
    const ep = this.engine.params;
    const avail = this._computeSimAvailability();
    if (this.simPlaybackMode === 'solo' && !avail[this.simSoloProcess]) {
      const first = SIM_PROCESS_DEFS.find((d) => avail[d.id]);
      if (first) this.simSoloProcess = first.id;
    }
    for (const def of SIM_PROCESS_DEFS) {
      const selected = this.simPlaybackMode === 'all'
        ? this.simProcessEnabled[def.id] !== false
        : def.id === this.simSoloProcess;
      ep[def.flag] = Boolean(avail[def.id] && selected);
    }
    this.engine.playing = Boolean(this.liveSimRunning);
    if (emit) this._emitSimulationState();
    return this.getSimulationControlState();
  }

  getSimulationControlState() {
    const ep = this.engine.params;
    const avail = this.simAvailable || this._computeSimAvailability();
    return {
      running: Boolean(this.liveSimRunning),
      mode: this.simPlaybackMode,
      soloProcess: this.simSoloProcess,
      autoPaused: Boolean(this.simAutoPaused),
      pauseReason: this.simPauseReason || '',
      seasonalEnabled: this.isSeasonalClimateEnabled(),
      climate: typeof this.engine.getSeasonalClimateState === 'function'
        ? this.engine.getSeasonalClimateState()
        : null,
      processes: SIM_PROCESS_DEFS.map((def) => ({
        ...def,
        available: Boolean(avail[def.id]),
        enabled: this.simProcessEnabled[def.id] !== false,
        solo: def.id === this.simSoloProcess,
        playing: Boolean(this.liveSimRunning && ep[def.flag] !== false && avail[def.id]),
      })),
    };
  }

  startSimulation(processId = null) {
    if (processId && SIM_PROCESS_DEFS.some((d) => d.id === processId)) {
      this.simPlaybackMode = 'solo';
      this.simSoloProcess = processId;
      this.simProcessEnabled[processId] = true;
    }
    this.liveSimRunning = true;
    this.simAutoPaused = false;
    this.simPauseReason = '';
    if (typeof this.engine.beginSeasonalClimateRun === 'function') {
      this.engine.beginSeasonalClimateRun();
    }
    return this._applySimulationPlayback(true);
  }

  pauseSimulation(reason = '') {
    this.liveSimRunning = false;
    this.engine.playing = false;
    this.simAutoPaused = Boolean(reason);
    this.simPauseReason = reason || 'Paused';
    return this._emitSimulationState();
  }

  /** Editing the stack (especially base landform/shape) freezes every live
   * process. It stays frozen until the user explicitly presses Simulate. */
  pauseSimulationForEdit(reason = 'Stack edited') {
    this.liveSimRunning = false;
    this.engine.playing = false;
    this.simAutoPaused = true;
    this.simPauseReason = `${reason} — press Simulate to resume`;
    return this._emitSimulationState();
  }

  toggleSimulation(processId = null) {
    if (processId) {
      if (this.liveSimRunning && this.simPlaybackMode === 'solo' && this.simSoloProcess === processId) {
        return this.pauseSimulation('');
      }
      return this.startSimulation(processId);
    }
    return this.liveSimRunning ? this.pauseSimulation('') : this.startSimulation(null);
  }

  setSimPlaybackMode(mode) {
    this.simPlaybackMode = mode === 'all' ? 'all' : 'solo';
    return this._applySimulationPlayback(true);
  }

  /** Selects (but does not start) the one-at-a-time season. */
  selectSimulationProcess(processId) {
    if (!SIM_PROCESS_DEFS.some((d) => d.id === processId)) return this.getSimulationControlState();
    const switchingSeason = this.liveSimRunning && this.simPlaybackMode === 'solo' && this.simSoloProcess !== processId;
    this.simPlaybackMode = 'solo';
    this.simSoloProcess = processId;
    this.simProcessEnabled[processId] = true;
    if (switchingSeason) {
      this.liveSimRunning = false;
      this.simAutoPaused = true;
      this.simPauseReason = 'Season selected — press Simulate to run it';
    }
    return this._applySimulationPlayback(true);
  }

  setSimulationProcessEnabled(processId, enabled) {
    if (!SIM_PROCESS_DEFS.some((d) => d.id === processId)) return this.getSimulationControlState();
    this.simProcessEnabled[processId] = Boolean(enabled);
    return this._applySimulationPlayback(true);
  }

  _packMaskUniforms(mask) {
    if (!mask || !mask.enabled) {
      return {
        cfg1: [0, 0, 0, 1],
        cfg2: [0.15, 0, 2.0, 1337],
      };
    }
    return {
      cfg1: [1, MASK_MODE_CODE[mask.mode] ?? 0, mask.min ?? 0.2, mask.max ?? 0.85],
      cfg2: [mask.feather ?? 0.15, mask.invert ? 1 : 0, mask.scale ?? 2.2, mask.seed ?? 1337],
    };
  }

  _maxReliefEstimate() {
    let r = 150;
    for (let i = this.layers.length - 1; i >= 0; i--) {
      const l = this.layers[i];
      if (!l.enabled) continue;
      if (l.type === 'base-landform') r = Math.max(55, l.params.relief || 150);
      else if (l.type === 'mountain-ridges' && l.params.combineMode === 0) r += (l.params.amplitude || 40) * 0.55;
      else if (l.type === 'rock-stacks') r += (l.params.height || 40) * 0.5;
      else if (l.type === 'sculpt-layer') {
        const entry = this.sculptStore.get(l.id);
        if (entry?.hasStamps || (l.params.strokeCount ?? 0) > 0) {
          r = Math.max(r, (l.params.targetHeight || 80) * 1.15);
          r += 24;
        }
      }
    }
    return Math.min(280, r);
  }

  applySculptStroke(worldX, worldZ, isFirstSampleOfStroke = false) {
    let target = this.getSelectedLayer();
    if (!target || target.type !== 'sculpt-layer') {
      target = this.layers.find((l) => l.type === 'sculpt-layer' && l.enabled && !l.locked);
      if (!target) {
        target = this.addLayer('sculpt-layer', { name: 'SDF sculpt layer' });
      }
    }
    if (target.locked) return;

    const entry = this._getOrCreateSculptEntry(target.id);
    const tool = this.authorTool;
    const radius = target.params.brushRadius ?? 34;
    const strength = target.params.brushStrength ?? 0.72;

    if (tool === 'cave-carve' || tool === 'cave-fill') {
      const st = this.engine.rockCalving.sampleTerrain(worldX, worldZ);
      const centerY = Math.max(4, st.surfaceH - (target.params.caveHeightOffset ?? 8.0));
      const last = entry.caveStrokes[entry.caveStrokes.length - 1];
      if (!last || Math.hypot(last.x - worldX, last.z - worldZ) > radius * 0.32) {
        entry.caveStrokes.push({
          x: worldX,
          y: centerY,
          z: worldZ,
          radius: radius * 0.65,
          strength,
          op: tool,
        });
        entry.hasStamps = true;
        target.params.strokeCount = (target.params.strokeCount || 0) + 1;
        this._sculptPendingReseed = true;
        this.evaluateStack({ reseedPlacements: false });
      }
      return;
    }

    const gl = this.gl;
    const R = this.R;
    const next = 1 - entry.idx;
    const opCode = tool === 'sculpt-remove' ? 1 : tool === 'sculpt-smooth' ? 2 : 0;

    gl.bindFramebuffer(gl.FRAMEBUFFER, entry.fbo[next]);
    gl.viewport(0, 0, R, R);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.useProgram(this.progSculptStamp);
    gl.bindVertexArray(this.engine.emptyVao);
    this._bindTex(this.progSculptStamp, 'uSculptTex', entry.tex[entry.idx], 0);
    gl.uniform4f(
      gl.getUniformLocation(this.progSculptStamp, 'uBrushPosRad'),
      worldX,
      worldZ,
      radius,
      this.engine.params.worldSpan
    );
    gl.uniform4f(
      gl.getUniformLocation(this.progSculptStamp, 'uBrushOpStr'),
      opCode,
      strength,
      0,
      0
    );
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    entry.idx = next;
    entry.hasStamps = true;

    if (isFirstSampleOfStroke || !target.params.strokeCount) {
      target.params.strokeCount = (target.params.strokeCount || 0) + 1;
    }
    this._sculptPendingReseed = true;
    this.evaluateStack({ reseedPlacements: false });
  }

  /**
   * Finalizes a painted sculpt/cave stroke on pointerup so 3D SDF outcrops,
   * boulders, obstacles, snowpack, and particles re-sync to the sculpted relief.
   */
  finishSculptStroke() {
    if (!this._sculptPendingReseed) return false;
    this._sculptPendingReseed = false;
    this.evaluateStack({ reseedPlacements: true });
    return true;
  }

  _runStackOpPass(opKind, layer, u0, u1, u2, maxRelief, u3 = [0, 0, 0, 0]) {
    const gl = this.gl;
    const R = this.R;
    const span = this.engine.params.worldSpan;
    const curT = this.engine.terrainIdx;
    const nextT = 1 - curT;

    if (this.vecIdx === nextT) {
      this._copyTex(this.vecDispTex[this.vecIdx], this.fboVecOnly[curT]);
      this.vecIdx = curT;
    }

    const { cfg1, cfg2 } = this._packMaskUniforms(layer.mask);
    let sculptTex = this.emptySculptTex;
    if (layer.type === 'sculpt-layer') {
      const entry = this._getOrCreateSculptEntry(layer.id);
      sculptTex = entry.tex[entry.idx];
    }

    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboStackOp[nextT]);
    gl.viewport(0, 0, R, R);
    gl.useProgram(this.progStackOp);
    gl.bindVertexArray(this.engine.emptyVao);
    this._bindTex(this.progStackOp, 'uTerrainTex', this.engine.terrainTex[curT], 0);
    this._bindTex(this.progStackOp, 'uVecDispTex', this.vecDispTex[this.vecIdx], 1);
    this._bindTex(this.progStackOp, 'uSculptTex', sculptTex, 2);
    this._bindTex(this.progStackOp, 'uVelocityFieldTex', this.engine.velocityFieldTex, 3);

    gl.uniform4f(gl.getUniformLocation(this.progStackOp, 'uDomain'), span, R, maxRelief, opKind);
    gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uParam0'), u0);
    gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uParam1'), u1);
    gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uParam2'), u2);
    gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uParam3'), u3);
    gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uMaskCfg1'), cfg1);
    gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uMaskCfg2'), cfg2);
    // No-ops safely on programs without the uniforms (mock links return null
    // locations). opKind 2 consumes this for real variable-bed benching.
    this.engine._bindStrataColumnUniforms(this.progStackOp);

    if (opKind === 6 || opKind === 7) {
      const pts = Array.isArray(layer.params.points) ? layer.params.points : [];
      const count = Math.min(MAX_SPLINE_POINTS, pts.length);
      const flat = new Float32Array(32);
      const elev = new Float32Array(16);
      // Points are [x, z] or [x, z, y]. A finite third element is an authored
      // absolute elevation: the spline becomes a true 3D control path whose
      // height profile drives the river/road/canyon grade (and ridge crest).
      // Unset points are pre-filled from the same start->end grade the 2D
      // path used, and with no authored Y anywhere the shader keeps its
      // original grade bit-for-bit.
      const hasY = pts.slice(0, count).some((pt) => Number.isFinite(pt?.[2]));
      const fbStart = opKind === 7 ? (u2[0] ?? 78) : maxRelief * 0.24;
      const fbEnd = opKind === 7 ? (u2[1] ?? 24) : maxRelief * 0.56;
      for (let k = 0; k < count; k++) {
        const pt = pts[k];
        flat[k * 2 + 0] = Number.isFinite(pt?.[0]) ? pt[0] : 0;
        flat[k * 2 + 1] = Number.isFinite(pt?.[1]) ? pt[1] : 0;
        const t = count > 1 ? k / (count - 1) : 0;
        elev[k] = Number.isFinite(pt?.[2]) ? pt[2] : fbStart + (fbEnd - fbStart) * t;
      }
      gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uSplinePts'), flat.subarray(0, 16));
      gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uSplinePtsHi'), flat.subarray(16, 32));
      gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uSplineElev'), elev.subarray(0, 8));
      gl.uniform4fv(gl.getUniformLocation(this.progStackOp, 'uSplineElevHi'), elev.subarray(8, 16));
      gl.uniform1f(gl.getUniformLocation(this.progStackOp, 'uSplineHasY'), hasY ? 1 : 0);
      gl.uniform1i(gl.getUniformLocation(this.progStackOp, 'uSplineCount'), count);
    }

    gl.drawArrays(gl.TRIANGLES, 0, 3);
    this.engine.terrainIdx = nextT;
    this.vecIdx = nextT;
  }

  _bakeMaskTexture(mask, maxRelief) {
    const gl = this.gl;
    const R = this.R;
    const span = this.engine.params.worldSpan;
    const { cfg1, cfg2 } = this._packMaskUniforms(mask);

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboMask);
    gl.viewport(0, 0, R, R);
    gl.useProgram(this.progMaskBake);
    gl.bindVertexArray(this.engine.emptyVao);
    this._bindTex(this.progMaskBake, 'uTerrainTex', this.engine.terrainTex[this.engine.terrainIdx], 0);
    this._bindTex(this.progMaskBake, 'uVelocityFieldTex', this.engine.velocityFieldTex, 1);
    gl.uniform4f(gl.getUniformLocation(this.progMaskBake, 'uBakeDomain'), span, R, maxRelief, 0);
    gl.uniform4fv(gl.getUniformLocation(this.progMaskBake, 'uMaskCfg1'), cfg1);
    gl.uniform4fv(gl.getUniformLocation(this.progMaskBake, 'uMaskCfg2'), cfg2);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  _runMaskedSimulation(layer, maxRelief, runFn) {
    const hasMask = Boolean(layer.mask && layer.mask.enabled);
    if (hasMask) {
      this._copyTex(this.engine.terrainTex[this.engine.terrainIdx], this.fboPrevScratch);
      this._bakeMaskTexture(layer.mask, maxRelief);
    }

    runFn();

    if (hasMask) {
      const gl = this.gl;
      const curT = this.engine.terrainIdx;
      const nextT = 1 - curT;
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.engine.fboTerrainSingle[nextT]);
      gl.viewport(0, 0, this.R, this.R);
      gl.useProgram(this.progMaskBlend);
      gl.bindVertexArray(this.engine.emptyVao);
      this._bindTex(this.progMaskBlend, 'uPrevTex', this.prevTerrainScratchTex, 0);
      this._bindTex(this.progMaskBlend, 'uNextTex', this.engine.terrainTex[curT], 1);
      this._bindTex(this.progMaskBlend, 'uMaskTex', this.maskTex, 2);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      this.engine.terrainIdx = nextT;
    }
  }

  /**
   * FULL BOTTOM-TO-TOP PIPELINE EVALUATION!
   * Iterates from `layers.length - 1` (bottom) up to `0` (top), executing each enabled
   * Landform, Sculpt, Spline, Cave, Simulation, Rock-Placement, and Grass-Placement stage!
   */
  evaluateStack({ reseedPlacements = true, fastPreview = false, keepLiveSim = false } = {}) {
    const t0 = performance.now();
    const gl = this.gl;
    const R = this.R;
    const ep = this.engine.params;
    const previousSeasonalSuppressed = this.engine._seasonalClimateSuppressed;
    // Stack evaluation and its fixed bake passes are not elapsed live time:
    // use annual-neutral climate forcing inside the bake, then restart the
    // location calendar at the Environment layer's configured start date.
    this.engine._seasonalClimateSuppressed = true;
    // Every evaluatable stack—including restored snapshots and stacks built
    // by external callers—gets the same locked Environment singleton last.
    this._ensureEnvironmentLayer({
      sunAzimuth: ep.sunAzimuth,
      sunElevation: ep.sunElevation,
    });
    const span = ep.worldSpan;
    const maxRelief = this._maxReliefEstimate();

    ep.materialTheme = this.paletteId;
    ep.fabricMode = this.fabricMode;

    // Rebuilding the stack changes the ground under every live process, so
    // snow/rain/wind/chemical/rockfall all freeze here and stay frozen until
    // the user explicitly presses Simulate again (no surprise snowfall while
    // dragging base-landform sliders).
    if (!keepLiveSim) this.pauseSimulationForEdit('Stack evaluated');

    // Reset per-layer active simulation & placement flags before bottom-up walk
    ep.simHydraulicPlaying = false;
    ep.simCryoPlaying = false;
    ep.simWindPlaying = false;
    ep.simRockfallPlaying = false;
    ep.simChemicalPlaying = false;
    ep.strataLayerEnabled = false;
    ep.snowLayerEnabled = false;
    ep.showOutcrops = false;
    ep.grassEnabled = false;
    ep.rainIntensity = 0.0;
    ep.headwaterFlow = 0.0;
    ep.oasisSpring = false;
    ep.snowfallRate = 0.0;
    ep.glacierStrength = 0.0;
    ep.windSpeed = 0.0;
    ep.windAzimuth = DEFAULT_EROSION_PARAMS.windAzimuth;
    // A shifting regime is authored by a Wind layer; snow-only/legacy stacks
    // keep their historical fixed drift direction until such a layer sets it.
    ep.windShiftMode = 0;
    ep.windShiftRange = DEFAULT_EROSION_PARAMS.windShiftRange;
    ep.windShiftPeriod = DEFAULT_EROSION_PARAMS.windShiftPeriod;
    ep.dryAeolianMode = 0.0;
    ep.windErosionRate = DEFAULT_EROSION_PARAMS.windErosionRate;
    ep.windAbrasion = DEFAULT_EROSION_PARAMS.windAbrasion;
    ep.windDeflation = DEFAULT_EROSION_PARAMS.windDeflation;
    ep.windDeposition = DEFAULT_EROSION_PARAMS.windDeposition;
    ep.windTurbulence = DEFAULT_EROSION_PARAMS.windTurbulence;
    ep.windGustiness = DEFAULT_EROSION_PARAMS.windGustiness;
    ep.windSandSupply = DEFAULT_EROSION_PARAMS.windSandSupply;
    ep.windCurtainHeight = DEFAULT_EROSION_PARAMS.windCurtainHeight;
    ep.windStreamlining = DEFAULT_EROSION_PARAMS.windStreamlining;
    ep.windFluting = DEFAULT_EROSION_PARAMS.windFluting;
    // Authored stratigraphy/frost are opt-in stack state. Reset them before
    // the bottom-up walk so deleting/disabling those layers actually removes
    // the measured section and cannot leave damage acting invisibly.
    ep.strataColumnEnabled = false;
    ep.snowEffects = [];
    this.engine._clearFrostWeathering();

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);
    gl.bindVertexArray(this.engine.emptyVao);

    // 1. Clear working buffers at bottom of stack
    this.engine.terrainIdx = 0;
    this.engine.snowIdx = 0;
    this.engine.fluxIdx = 0;
    this.vecIdx = 0;
    this.hasActiveVecDisp = false;

    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboStackOp[0]);
    gl.viewport(0, 0, R, R);
    gl.clearBufferfv(gl.COLOR, 0, new Float32Array([8.0, 0.0, 0.0, 0.0]));
    gl.clearBufferfv(gl.COLOR, 1, new Float32Array([0.0, 0.0, 0.0, 0.0]));

    for (let i = 0; i < 2; i++) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.engine.fboFlux[i]);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.engine.fboSnowClear[i]);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }

    this.engine._clearChemicalWeathering();
    this.engine._clearMineralField();

    // Determine bottom seed early so particle cloud initializes deterministically
    const baseLayer = [...this.layers].reverse().find((l) => l.type === 'base-landform' && l.enabled);
    if (baseLayer) {
      ep.seed = baseLayer.params.seed ?? DEFAULT_EROSION_PARAMS.seed;
      ep.relief = baseLayer.params.relief ?? DEFAULT_EROSION_PARAMS.relief;
      ep.roughness = baseLayer.params.roughness ?? DEFAULT_EROSION_PARAMS.roughness;
    }

    const emitter = EMITTER_MODES[ep.emitterMode] || EMITTER_MODES['sky-rain'];
    gl.viewport(0, 0, PART_TEX_W, PART_TEX_H);
    gl.useProgram(this.engine.progInitParticles);
    gl.uniform4f(
      gl.getUniformLocation(this.engine.progInitParticles, 'uSpawnBox'),
      span,
      ep.cloudHeight,
      ep.seed,
      emitter.code
    );
    gl.uniform4f(gl.getUniformLocation(this.engine.progInitParticles, 'uSpeciesMix'), 1, 0, 0, 0);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.engine.fboInitParticles[0]);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    this.engine.partIdx = 0;
    this.engine.stepCount = 0;
    this.engine.cryoStepCount = 0;
    this.engine._obstBuiltStep = -999;
    this.engine.rockCalving.obstaclesDirty = true;
    if (typeof this.engine.resetSeasonalClimateClock === 'function') {
      this.engine.resetSeasonalClimateClock();
    }

    const activeCaveLayers = [];
    const activeSculptCaveStrokes = [];
    const activeSplineTunnels = [];
    let activeRockLayer = null;
    let activeTalusLayer = null;
    const hasExplicitRockLayer = this.layers.some((l) => l.type === 'rock-placement');

    // 2. Walk strictly BOTTOM -> TOP (`i = layers.length - 1` down to `0`)!
    for (let i = this.layers.length - 1; i >= 0; i--) {
      const layer = this.layers[i];
      if (!layer.enabled) continue;
      const p = layer.params;

      if (layer.type === ENVIRONMENT_LAYER_TYPE) {
        // Environment is live scene/climate state, not a GPU terrain
        // operation. Its final-row position makes the Outliner explicit
        // without adding a pass or invalidating placements when it changes.
        this.applyLiveEnvironmentLayerParams(layer);
      } else if (layer.type === 'base-landform') {
        ep.relief = p.relief ?? DEFAULT_EROSION_PARAMS.relief;
        ep.roughness = p.roughness ?? DEFAULT_EROSION_PARAMS.roughness;
        ep.seed = p.seed ?? DEFAULT_EROSION_PARAMS.seed;

        gl.bindFramebuffer(gl.FRAMEBUFFER, this.engine.fboTerrainSingle[this.engine.terrainIdx]);
        gl.viewport(0, 0, R, R);
        gl.useProgram(this.engine.progInitTerrain);
        gl.bindVertexArray(this.engine.emptyVao);
        gl.uniform4f(
          gl.getUniformLocation(this.engine.progInitTerrain, 'uInitParams'),
          ep.relief,
          ep.roughness,
          p.archetype ?? 6,
          ep.seed
        );
        gl.uniform1f(gl.getUniformLocation(this.engine.progInitTerrain, 'uWorldSpan'), span);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        this._applyWorldBaseMask();
      } else if (layer.type === 'stratigraphic-beds') {
        ep.strataLayerEnabled = true;
        ep.rockHardness = p.rockHardness ?? DEFAULT_EROSION_PARAMS.rockHardness;
        ep.strataStrength = (p.strataStrength ?? DEFAULT_EROSION_PARAMS.strataStrength) * (p.opacity ?? 1.0);
        ep.strataFreq = p.strataFreq ?? DEFAULT_EROSION_PARAMS.strataFreq;
        ep.strataDip = p.strataDip ?? DEFAULT_EROSION_PARAMS.strataDip;
        ep.strataStrike = p.strataStrike ?? DEFAULT_EROSION_PARAMS.strataStrike;
        ep.faultWeakness = p.faultWeakness ?? DEFAULT_EROSION_PARAMS.faultWeakness;
        // Authored measured section: bottom->top non-repeating beds. The last
        // evaluated (topmost) enabled section owns live hardness/mineral/frost
        // lookup, while each stratigraphic layer still accumulates its bake.
        ep.strataColumnEnabled = (p.strataColumnEnabled ?? DEFAULT_EROSION_PARAMS.strataColumnEnabled) !== false;
        ep.strataColumnPreset = p.strataColumnPreset ?? DEFAULT_EROSION_PARAMS.strataColumnPreset;
        ep.strataColumnBaseM = p.strataColumnBaseM ?? DEFAULT_EROSION_PARAMS.strataColumnBaseM;
        ep.strataColumnColorStrength = p.strataColumnColorStrength ?? DEFAULT_EROSION_PARAMS.strataColumnColorStrength;
        ep.strataBeds = Array.isArray(p.strataBeds) ? normalizeStrataColumn(p.strataBeds) : [];
        if (!ep.strataBeds.length) ep.strataColumnEnabled = false;

        const terrace = p.terraceCarve ?? 0.0;
        const overhang = p.ledgeOverhang ?? 0.0;
        if (terrace > 0.001 || overhang > 0.001) {
          if (overhang > 0.005) this.hasActiveVecDisp = true;
          this._runStackOpPass(
            2,
            layer,
            [ep.strataFreq, terrace, ep.strataStrength, ep.strataDip],
            [ep.strataStrike, 0.25, overhang, p.opacity ?? 1.0],
            [p.seed ?? ep.seed, ep.strataColumnEnabled ? 1 : 0, ep.strataColumnBaseM ?? 0, strataColumnThickness(ep.strataBeds)],
            maxRelief
          );
        }

        // Bake THIS sedimentary layer's chemical orientation (Fe/Cu/carbonate/
        // salt horizon) into the mineral source field. Multiple stratigraphic
        // layers accumulate, so specific added beds become specific ore /
        // evaporite horizons for the later chemical season to leach.
        ep.mineralIron = p.mineralIron ?? DEFAULT_EROSION_PARAMS.mineralIron;
        ep.mineralCopper = p.mineralCopper ?? DEFAULT_EROSION_PARAMS.mineralCopper;
        ep.mineralCarbonate = p.mineralCarbonate ?? DEFAULT_EROSION_PARAMS.mineralCarbonate;
        ep.mineralSalt = p.mineralSalt ?? DEFAULT_EROSION_PARAMS.mineralSalt;
        ep.mineralHorizon = p.mineralHorizon ?? DEFAULT_EROSION_PARAMS.mineralHorizon;
        ep.mineralHorizonWidth = p.mineralHorizonWidth ?? DEFAULT_EROSION_PARAMS.mineralHorizonWidth;
        ep.mineralBandStrength = p.mineralBandStrength ?? DEFAULT_EROSION_PARAMS.mineralBandStrength;
        {
          const { hardnessVec, strataVec } = this.engine._getHardnessUniforms();
          this.engine._accumulateMineralStrata(
            { iron: ep.mineralIron, copper: ep.mineralCopper, carbonate: ep.mineralCarbonate, salt: ep.mineralSalt },
            { horizon: ep.mineralHorizon, horizonWidth: ep.mineralHorizonWidth, bandStrength: ep.mineralBandStrength, opacity: p.opacity ?? 1.0 },
            hardnessVec,
            strataVec,
            p.seed ?? ep.seed
          );
        }
      } else if (layer.type === 'mountain-ridges') {
        this._runStackOpPass(
          1,
          layer,
          [p.generator ?? 0, p.combineMode ?? 0, p.amplitude ?? 48, p.scale ?? 2.2],
          [p.sharpness ?? 0.60, p.azimuth ?? 28, p.opacity ?? 1.0, 0],
          [p.seed ?? ep.seed, 0, 0, 0],
          maxRelief
        );
      } else if (layer.type === 'mountain-noise') {
        this._runStackOpPass(
          10,
          layer,
          [p.combineMode ?? 0, p.amplitude ?? 64, p.scale ?? 1.85, p.sharpness ?? 0.68],
          [p.erosionFold ?? 0.58, p.ridgeMix ?? 0.54, p.azimuth ?? 22, p.opacity ?? 1.0],
          [p.seed ?? ep.seed, p.screeAmount ?? 0.42, 0, 0],
          maxRelief
        );
      } else if (layer.type === 'ridge-network') {
        this._runStackOpPass(
          11,
          layer,
          [p.combineMode ?? 0, p.amplitude ?? 56, p.scale ?? 1.7, p.sharpness ?? 0.74],
          [p.spurDensity ?? 0.62, p.colDepth ?? 0.36, p.flankConcavity ?? 0.64, p.azimuth ?? -18],
          [p.opacity ?? 1.0, p.seed ?? ep.seed, 0, 0],
          maxRelief
        );
      } else if (layer.type === 'rocky-outcrops') {
        if ((p.angularity ?? 0.82) > 0.15 && (p.cragHeight ?? 14.0) > 0.1) {
          this.hasActiveVecDisp = true;
        }
        this._runStackOpPass(
          12,
          layer,
          [p.cragHeight ?? 14.0, p.cragScale ?? 2.1, p.angularity ?? 0.82, p.slopeMin ?? 18],
          [p.screeAmount ?? 0.55, p.bedTilt ?? 14, p.azimuth ?? 26, p.opacity ?? 1.0],
          [p.seed ?? ep.seed, p.valleyTors ?? 0.12, 0, 0],
          maxRelief
        );
      } else if (layer.type === 'cliff-generator') {
        if ((p.overhang ?? 0.48) > 0.01) {
          this.hasActiveVecDisp = true;
        }
        this._runStackOpPass(
          13,
          layer,
          [p.terraceSteps ?? 4, p.steepness ?? 0.84, p.angularity ?? 0.78, p.slopeMin ?? 20],
          [p.chuteDepth ?? 0.42, p.overhang ?? 0.48, p.talusToe ?? 0.56, p.opacity ?? 1.0],
          [p.seed ?? ep.seed, p.featureScale ?? 1.8, 0, 0],
          maxRelief
        );
      } else if (layer.type === 'lake-basin') {
        this._runStackOpPass(
          8,
          layer,
          [p.centerX ?? 0, p.centerZ ?? 0, p.radiusX ?? 78, p.radiusZ ?? 62],
          [p.azimuth ?? 18, p.depth ?? 24, p.waterLevelOffset ?? -1.5, p.shoreShelf ?? 0.62],
          [p.irregularity ?? 0.44, p.spillwayNotch ?? 0.48, p.sedimentRim ?? 0.52, p.opacity ?? 1.0],
          maxRelief,
          [p.seed ?? ep.seed, 0, 0, 0]
        );
      } else if (layer.type === 'desert-oasis') {
        if ((p.springDischarge ?? 0.55) > 0.05) {
          ep.oasisSpring = true;
        }
        this._runStackOpPass(
          9,
          layer,
          [p.centerX ?? 0, p.centerZ ?? 0, p.radius ?? 56, p.basinDepth ?? 16],
          [p.poolFill ?? 0.72, p.shelterRim ?? 8.5, p.wetHaloWidth ?? 0.68, p.irregularity ?? 0.38],
          [p.windAzimuth ?? 35, p.seed ?? ep.seed, p.opacity ?? 1.0, p.springDischarge ?? 0.55],
          maxRelief,
          [0, 0, 0, 0]
        );
      } else if (layer.type === 'rock-stacks') {
        if ((p.caprockOverhang ?? 0.45) > 0.005 || (p.lean ?? 0.28) > 0.005) {
          this.hasActiveVecDisp = true;
        }
        this._runStackOpPass(
          3,
          layer,
          [p.height ?? 48, p.spacing ?? 68, p.radius ?? 15, p.taper ?? 0.42],
          [p.tiers ?? 5, p.lean ?? 0.28, p.leanAzimuth ?? 35, p.caprockOverhang ?? 0.45],
          [p.opacity ?? 1.0, p.seed ?? ep.seed, 0, 0],
          maxRelief
        );
      } else if (layer.type === 'cliff-push-pull') {
        if ((p.displacement ?? 10.5) > 0.01) {
          this.hasActiveVecDisp = true;
        }
        this._runStackOpPass(
          4,
          layer,
          [p.displacement ?? 10.5, p.featureScale ?? 2.6, p.slopeThreshold ?? 28, p.ledgeStrength ?? 0.62],
          [p.overhangBias ?? 0.52, p.verticalCrease ?? 0.58, p.opacity ?? 1.0, p.seed ?? ep.seed],
          [0, 0, 0, 0],
          maxRelief
        );
      } else if (layer.type === 'sculpt-layer') {
        const entry = this.sculptStore.get(layer.id);
        if (entry) {
          for (const cs of entry.caveStrokes) activeSculptCaveStrokes.push(cs);
          if (entry.hasStamps || (p.strokeCount ?? 0) > 0) {
            ep.relief = Math.max(ep.relief || 50, maxRelief);
            this._runStackOpPass(
              5,
              layer,
              [p.opacity ?? 1.0, p.blendMode ?? 0, p.noiseAmount ?? 0, p.noiseScale ?? 1.6],
              [p.targetHeight ?? 70, p.seed ?? ep.seed, p.noiseHeight ?? 6, p.multiplyStrength ?? 0.65],
              [0, 0, 0, 0],
              maxRelief
            );
          }
        }
      } else if (layer.type === 'spline-feature') {
        this._runStackOpPass(
          6,
          layer,
          [p.mode ?? 0, p.width ?? 44, p.depth ?? 48, p.wallSteepness ?? 0.74],
          [p.meanderAmp ?? 0.32, p.waterFill ?? 0.28, p.bankFalloff ?? 0.55, p.opacity ?? 1.0],
          [0, 0, 0, 0],
          maxRelief
        );
      } else if (layer.type === 'spline-landform') {
        const mode = Math.max(0, Math.min(4, Math.round(p.mode ?? 0)));
        if (mode === 4) {
          // Tunnel mode is deliberately volumetric: the heightfield is left
          // intact and the smooth spline becomes an SDF tube below.
          activeSplineTunnels.push({
            // Third coordinate (when finite) is the authored bore elevation:
            // the tunnel follows the true 3D spline through those heights.
            points: (Array.isArray(p.points) ? p.points : []).map((pt) => [Number(pt?.[0]) || 0, Number(pt?.[1]) || 0, Number.isFinite(pt?.[2]) ? pt[2] : NaN]),
            radius: Math.max(1.5, p.tunnelRadius ?? 7.5),
            startElevation: p.startElevation ?? 48,
            endElevation: p.endElevation ?? 44,
            depth: Math.max(4, p.tunnelDepth ?? 22),
            followSurface: (p.tunnelFollowSurface ?? 0) > 0.5,
            roughness: Math.max(0, Math.min(1, p.tunnelRoughness ?? 0.32)),
            strength: Math.max(0.05, Math.min(1, p.opacity ?? 1.0)),
            seed: p.seed ?? ep.seed,
          });
        } else {
          this._runStackOpPass(
            7,
            layer,
            [mode, p.width ?? 72, p.amount ?? 42, p.falloff ?? 0.62],
            [p.sharpness ?? 0.62, p.noiseAmount ?? 0.38, p.noiseScale ?? 1.7, p.waterDepth ?? 1.2],
            [p.startElevation ?? 78, p.endElevation ?? 24, p.seed ?? ep.seed, p.opacity ?? 1.0],
            maxRelief,
            [p.sediment ?? 0.35, p.flattenStrength ?? 0.78, p.bankHeight ?? 1.6, 0]
          );
        }
      } else if (layer.type === 'sdf-caves') {
        activeCaveLayers.push(layer);
      } else if (layer.type === 'river-incision') {
        const steps = Math.max(1, Math.min(80, Math.round((p.iterations ?? 20) * (fastPreview ? 0.5 : 1.0))));
        this._runMaskedSimulation(layer, maxRelief, () => {
          const prevFlags = {
            h: ep.simHydraulicPlaying,
            c: ep.simCryoPlaying,
            w: ep.simWindPlaying,
            r: ep.simRockfallPlaying,
            chem: ep.simChemicalPlaying,
            rain: ep.rainIntensity,
            erod: ep.erosionStrength,
            hw: ep.headwaterFlow,
            lake: ep.lakeRetention,
          };
          ep.simHydraulicPlaying = true;
          ep.simCryoPlaying = false;
          ep.simWindPlaying = false;
          ep.simRockfallPlaying = false;
          ep.simChemicalPlaying = false;
          ep.rainIntensity = p.headwaterRain ?? 0.68;
          ep.erosionStrength = p.incisionStrength ?? 0.85;
          ep.headwaterFlow = Math.max(ep.headwaterFlow, (p.headwaterRain ?? 0.68) * 0.65);
          ep.lakeRetention = p.basinPooling ?? 0.45;
          this.engine.stepSimulation(steps, { skipTelemetry: true });
          ep.simHydraulicPlaying = prevFlags.h;
          ep.simCryoPlaying = prevFlags.c;
          ep.simWindPlaying = prevFlags.w;
          ep.simRockfallPlaying = prevFlags.r;
          ep.simChemicalPlaying = prevFlags.chem;
          ep.rainIntensity = prevFlags.rain;
          ep.erosionStrength = prevFlags.erod;
          ep.headwaterFlow = prevFlags.hw;
          ep.lakeRetention = prevFlags.lake;
        });
      } else if (layer.type === 'hydraulic-erosion') {
        ep.rainIntensity = p.rainIntensity ?? DEFAULT_EROSION_PARAMS.rainIntensity;
        ep.erosionStrength = p.erosionStrength ?? DEFAULT_EROSION_PARAMS.erosionStrength;
        ep.sedimentCapacity = p.sedimentCapacity ?? DEFAULT_EROSION_PARAMS.sedimentCapacity;
        ep.depositionRate = p.depositionRate ?? DEFAULT_EROSION_PARAMS.depositionRate;
        ep.evaporation = p.evaporation ?? DEFAULT_EROSION_PARAMS.evaporation;
        ep.headwaterFlow = p.headwaterFlow ?? 0;
        ep.lakeRetention = p.lakeRetention ?? 0.4;
        ep.oasisSpring = Boolean(p.oasisSpring);
        ep.simHydraulicPlaying = true;

        const bakeSteps = Math.max(0, Math.min(120, Math.round((p.bakeSteps ?? 0) * (fastPreview ? 0.5 : 1.0))));
        if (bakeSteps > 0) {
          this._runMaskedSimulation(layer, maxRelief, () => {
            const prevC = ep.simCryoPlaying, prevW = ep.simWindPlaying, prevR = ep.simRockfallPlaying, prevChem = ep.simChemicalPlaying;
            ep.simCryoPlaying = false;
            ep.simWindPlaying = false;
            ep.simRockfallPlaying = false;
            ep.simChemicalPlaying = false;
            this.engine.stepSimulation(bakeSteps, { skipTelemetry: true });
            ep.simCryoPlaying = prevC;
            ep.simWindPlaying = prevW;
            ep.simRockfallPlaying = prevR;
            ep.simChemicalPlaying = prevChem;
          });
        }
      } else if (layer.type === 'thermal-talus') {
        activeTalusLayer = layer;
        ep.talusAngle = p.talusAngle ?? DEFAULT_EROSION_PARAMS.talusAngle;
        ep.rockfallRate = p.rockfallRate ?? DEFAULT_EROSION_PARAMS.rockfallRate;
        ep.simRockfallPlaying = true;

        const bakePasses = Math.max(0, Math.min(80, Math.round((p.bakePasses ?? 0) * (fastPreview ? 0.5 : 1.0))));
        if (bakePasses > 0) {
          this._runMaskedSimulation(layer, maxRelief, () => {
            this.engine.stepThermalTalusOnly(bakePasses);
          });
        }
      } else if (layer.type === 'chemical-weathering') {
        ep.chemRate = p.chemRate ?? DEFAULT_EROSION_PARAMS.chemRate;
        ep.chemRust = p.chemRust ?? DEFAULT_EROSION_PARAMS.chemRust;
        ep.chemHydrolysis = p.chemHydrolysis ?? DEFAULT_EROSION_PARAMS.chemHydrolysis;
        ep.chemDissolution = p.chemDissolution ?? DEFAULT_EROSION_PARAMS.chemDissolution;
        ep.chemSalt = p.chemSalt ?? DEFAULT_EROSION_PARAMS.chemSalt;
        ep.chemVarnish = p.chemVarnish ?? DEFAULT_EROSION_PARAMS.chemVarnish;
        ep.chemCopper = p.chemCopper ?? DEFAULT_EROSION_PARAMS.chemCopper;
        ep.simChemicalPlaying = true;

        // Pre-age rust/patina/salt/varnish/alteration on the terrain built so far.
        this.engine.seedChemicalWeathering();

        const bakeSteps = Math.max(0, Math.min(160, Math.round((p.bakeSteps ?? 0) * (fastPreview ? 0.5 : 1.0))));
        if (bakeSteps > 0) {
          this._runMaskedSimulation(layer, maxRelief, () => {
            const prevH = ep.simHydraulicPlaying, prevC = ep.simCryoPlaying, prevW = ep.simWindPlaying, prevR = ep.simRockfallPlaying;
            // Water transports reactants/products during the chemical bake;
            // unrelated snow, wind and rockfall processes stay frozen.
            ep.simHydraulicPlaying = true;
            ep.simCryoPlaying = false;
            ep.simWindPlaying = false;
            ep.simRockfallPlaying = false;
            this.engine.stepSimulation(bakeSteps, { skipTelemetry: true });
            ep.simHydraulicPlaying = prevH;
            ep.simCryoPlaying = prevC;
            ep.simWindPlaying = prevW;
            ep.simRockfallPlaying = prevR;
          });
        }
      } else if (layer.type === 'wind-erosion') {
        ep.windSpeed = p.windSpeed ?? DEFAULT_EROSION_PARAMS.windSpeed;
        ep.windAzimuth = p.windAzimuth ?? DEFAULT_EROSION_PARAMS.windAzimuth;
        ep.windShiftMode = p.windShiftMode ?? DEFAULT_EROSION_PARAMS.windShiftMode;
        ep.windShiftRange = p.windShiftRange ?? DEFAULT_EROSION_PARAMS.windShiftRange;
        ep.windShiftPeriod = p.windShiftPeriod ?? DEFAULT_EROSION_PARAMS.windShiftPeriod;
        ep.dryAeolianMode = p.dryAeolianMode ?? DEFAULT_EROSION_PARAMS.dryAeolianMode;
        ep.windErosionRate = p.windErosionRate ?? DEFAULT_EROSION_PARAMS.windErosionRate;
        ep.windAbrasion = p.windAbrasion ?? DEFAULT_EROSION_PARAMS.windAbrasion;
        ep.windDeflation = p.windDeflation ?? DEFAULT_EROSION_PARAMS.windDeflation;
        ep.windDeposition = p.windDeposition ?? DEFAULT_EROSION_PARAMS.windDeposition;
        ep.windTurbulence = p.windTurbulence ?? DEFAULT_EROSION_PARAMS.windTurbulence;
        ep.windGustiness = p.windGustiness ?? DEFAULT_EROSION_PARAMS.windGustiness;
        ep.windSandSupply = p.windSandSupply ?? DEFAULT_EROSION_PARAMS.windSandSupply;
        ep.windCurtainHeight = p.windCurtainHeight ?? DEFAULT_EROSION_PARAMS.windCurtainHeight;
        ep.windStreamlining = p.windStreamlining ?? DEFAULT_EROSION_PARAMS.windStreamlining;
        ep.windFluting = p.windFluting ?? DEFAULT_EROSION_PARAMS.windFluting;
        ep.simWindPlaying = ep.windSpeed > 0.03 && ep.windErosionRate > 0.001;

        const bakeSteps = Math.max(0, Math.min(160, Math.round((p.bakeSteps ?? 0) * (fastPreview ? 0.5 : 1.0))));
        if (bakeSteps > 0) {
          this._runMaskedSimulation(layer, maxRelief, () => {
            const prevH = ep.simHydraulicPlaying, prevC = ep.simCryoPlaying, prevR = ep.simRockfallPlaying, prevChem = ep.simChemicalPlaying;
            ep.simHydraulicPlaying = false;
            ep.simCryoPlaying = false;
            ep.simRockfallPlaying = false;
            ep.simChemicalPlaying = false;
            ep.simWindPlaying = true;
            this.engine.stepSimulation(bakeSteps, { skipTelemetry: true });
            ep.simHydraulicPlaying = prevH;
            ep.simCryoPlaying = prevC;
            ep.simRockfallPlaying = prevR;
            ep.simChemicalPlaying = prevChem;
            ep.simWindPlaying = ep.windSpeed > 0.03 && ep.windErosionRate > 0.001;
          });
        }
      } else if (layer.type === 'aeolian-glacial') {
        ep.snowline = p.snowline ?? DEFAULT_EROSION_PARAMS.snowline;
        ep.snowfallRate = p.snowfallRate ?? 0.0;
        ep.glacierStrength = p.glacierStrength ?? 0.0;
        ep.iceMeltRate = p.iceMeltRate ?? DEFAULT_EROSION_PARAMS.iceMeltRate;
        // Snow owns a real effect stack (segregation / freeze-thaw / nivation
        // / creep / avalanche / plucking / explicit legacy), plus global
        // climate/saturation/release scalars. Clone-normalise it so engine
        // runs can never mutate the document layer by accident.
        ep.snowEffects = snowEffectsForLayer(layer);
        Object.assign(ep, normalizeFrostParams(p));
        ep.snowEffectPreset = p.snowEffectPreset ?? DEFAULT_EROSION_PARAMS.snowEffectPreset;
        if (!this._hasDedicatedWindLayer()) {
          ep.windSpeed = p.windSpeed ?? 0.0;
          ep.windAzimuth = p.windAzimuth ?? DEFAULT_EROSION_PARAMS.windAzimuth;
          ep.windShiftMode = p.windShiftMode ?? 0;
          ep.windShiftRange = p.windShiftRange ?? DEFAULT_EROSION_PARAMS.windShiftRange;
          ep.windShiftPeriod = p.windShiftPeriod ?? DEFAULT_EROSION_PARAMS.windShiftPeriod;
          ep.dryAeolianMode = p.dryAeolianMode ?? 0.0;
        }
        ep.snowLayerEnabled = true;
        ep.simCryoPlaying = ep.snowfallRate > 0.015 || ep.glacierStrength > 0.015
          || deriveSnowEffectWeights(ep.snowEffects).anyWeathering;
        ep.simWindPlaying = ep.windSpeed > 0.03 && (ep.windErosionRate ?? DEFAULT_EROSION_PARAMS.windErosionRate) > 0.001;

        // Seed snowpack & valley glaciers (`progInitSnow`) on the terrain constructed up to this stage!
        this.engine.seedInitialSnow();
        // Seed only SUB-RESOLUTION frost damage/cracks + pore saturation.
        // No geometry changes here: visible pitting waits for thresholded
        // detached material during simulation steps.
        this.engine.seedFrostWeathering();

        const bakePasses = Math.max(0, Math.min(64, Math.round((p.bakePasses ?? 0) * (fastPreview ? 0.5 : 1.0))));
        if (bakePasses > 0) {
          this._runMaskedSimulation(layer, maxRelief, () => {
            const prevH = ep.simHydraulicPlaying, prevR = ep.simRockfallPlaying, prevChem = ep.simChemicalPlaying;
            ep.simHydraulicPlaying = false;
            ep.simRockfallPlaying = false;
            ep.simChemicalPlaying = false;
            this.engine.stepSimulation(bakePasses, { skipTelemetry: true });
            ep.simHydraulicPlaying = prevH;
            ep.simRockfallPlaying = prevR;
            ep.simChemicalPlaying = prevChem;
          });
        }
      } else if (layer.type === 'rock-placement') {
        activeRockLayer = layer;
      } else if (layer.type === 'vegetation-placement') {
        ep.grassEnabled = true;
        ep.grassMode = p.species ?? 0;
        ep.grassDensity = p.density ?? 0.55;
        ep.grassHeight = p.bladeHeight ?? 1.15;
        ep.grassSlopeMax = p.slopeMax ?? 0.62;
        ep.grassElevMax = p.elevationMax ?? 0.52;
        ep.grassPatchiness = p.patchiness ?? 0.58;
        ep.grassWindSway = p.windSway ?? 0.65;
        ep.grassDryBlend = p.dryBlend ?? (p.species === 1 ? 0.85 : 0.14);
      }
    }

    // Higher landform operations can extend beyond the flat base. Clip the
    // evaluated ground to the authored shape before snapshots and live erosion.
    this._applyWorldBaseMask();

    // 3. Copy evaluated initial terrain into `engine.initialTerrainTex` (ANGLE-safe shader copy!)
    this._copyTex(this.engine.terrainTex[this.engine.terrainIdx], this.engine.fboInitialTerrain);

    // 4. Bake Selected Layer's B&W Mask Preview Texture (`uViewMode == 7`)
    const sel = this.getSelectedLayer();
    const previewMask = sel?.mask?.enabled
      ? sel.mask
      : { enabled: true, ...(sel?.mask || { mode: 'height', min: 0.2, max: 0.85, feather: 0.15 }) };
    this._bakeMaskTexture(previewMask, maxRelief);

    // 5. Read back fast 64x64 snapshot for 3D SDF Caves, Rock Placement & Telemetry!
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.engine.fboTelemetry);
    gl.viewport(0, 0, this.snapN, this.snapN);
    gl.useProgram(this.engine.progTelemetry);
    gl.bindVertexArray(this.engine.emptyVao);
    this._bindTex(this.engine.progTelemetry, 'uTerrainTex', this.engine.terrainTex[this.engine.terrainIdx], 0);
    gl.uniform1f(gl.getUniformLocation(this.engine.progTelemetry, 'uGridRes'), R);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.readPixels(0, 0, this.snapN, this.snapN, gl.RGBA, gl.FLOAT, this.snapBuf);
    // Cryosphere & frost twin snapshots so placed/calved rocks embed into snow and couple to rock fractures
    this._bindTex(this.engine.progTelemetry, 'uTerrainTex', this.engine.snowTex[this.engine.snowIdx], 0);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.readPixels(0, 0, this.snapN, this.snapN, gl.RGBA, gl.FLOAT, this.snapSnowBuf);
    this._bindTex(this.engine.progTelemetry, 'uTerrainTex', this.engine.frostTex[this.engine.frostIdx], 0);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.readPixels(0, 0, this.snapN, this.snapN, gl.RGBA, gl.FLOAT, this.snapFrostBuf);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this.engine.initialSnap64.set(this.snapBuf);

    // 6. Rebuild 3D Volumetric SDF Caves & Cliff Undercuts + Upload Portal Mask Texture
    const caveRes = this.caveSystem.rebuildCaveVolume({
      gridRgba: this.snapBuf,
      gridN: this.snapN,
      worldSpan: span,
      caveLayers: activeCaveLayers,
      sculptCaveStrokes: activeSculptCaveStrokes,
      splineTunnels: activeSplineTunnels,
      maxRelief,
    });
    const P = this.caveSystem.portalMaskSize;
    gl.bindTexture(gl.TEXTURE_2D, this.cavePortalTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, P, P, 0, gl.RGBA, gl.FLOAT, this.caveSystem.portalTexData);
    gl.bindTexture(gl.TEXTURE_2D, null);

    if (caveRes.vertexCount > 0) {
      gl.bindBuffer(gl.ARRAY_BUFFER, this.cavePosBuf);
      gl.bufferData(gl.ARRAY_BUFFER, this.caveSystem.positions, gl.DYNAMIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.caveNrmBuf);
      gl.bufferData(gl.ARRAY_BUFFER, this.caveSystem.normals, gl.DYNAMIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.caveAttBuf);
      gl.bufferData(gl.ARRAY_BUFFER, this.caveSystem.attribs, gl.DYNAMIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, null);
    }

    // 7. Update Rock Placement / Active Rockfall Talus Layer & Telemetry
    this.engine.rockCalving.updateTerrainSnapshot(this.snapBuf, this.snapN, span, this.snapSnowBuf, this.snapFrostBuf);
    const autoRockfallBoulders = !hasExplicitRockLayer && activeTalusLayer && (activeTalusLayer.params.rockfallRate ?? 0.35) > 0.05;
    if (activeRockLayer && reseedPlacements) {
      ep.showOutcrops = true;
      ep.boulderDensity = activeRockLayer.params.density ?? DEFAULT_EROSION_PARAMS.boulderDensity;
      ep.rockfallRate = activeRockLayer.params.talusAffinity ?? ep.rockfallRate;
      const prevSeed = ep.seed;
      ep.seed = activeRockLayer.params.seed ?? prevSeed;
      this.engine.rockCalving.seedForLandscape(ep);
      ep.seed = prevSeed;
      const settle = Math.max(0, Math.min(60, Math.round(activeRockLayer.params.settleSteps ?? 0)));
      for (let s = 0; s < settle; s++) {
        this.engine.rockCalving.stepPhysics(0.04, true);
      }
    } else if (autoRockfallBoulders && reseedPlacements) {
      ep.showOutcrops = true;
      ep.boulderDensity = Math.max(0.22, Math.min(0.75, (activeTalusLayer.params.rockfallRate ?? 0.45) * 0.72));
      this.engine.rockCalving.seedForLandscape(ep);
    } else if (activeRockLayer || autoRockfallBoulders) {
      ep.showOutcrops = true;
    } else {
      ep.showOutcrops = false;
    }

    gl.bindVertexArray(null);
    this.engine._seasonalClimateSuppressed = previousSeasonalSuppressed;
    // Evaluation used the sim flags as bake/availability scratch; publish the
    // user's actual season selection (solo or simultaneous) for live ticking.
    this._applySimulationPlayback(false);

    // Re-seed GPU sky particles AFTER the full stack + playback flags are
    // resolved so initial particle species (including falling snow!) match the
    // active climate/seasons from frame 0 instead of pre-stack defaults.
    {
      const liveEmitter = EMITTER_MODES[ep.emitterMode] || EMITTER_MODES['sky-rain'];
      const liveSpeciesMix = this.engine._getClimateUniforms().speciesVec;
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.BLEND);
      gl.viewport(0, 0, PART_TEX_W, PART_TEX_H);
      gl.useProgram(this.engine.progInitParticles);
      gl.bindVertexArray(this.engine.emptyVao);
      gl.uniform4f(
        gl.getUniformLocation(this.engine.progInitParticles, 'uSpawnBox'),
        span,
        ep.cloudHeight,
        ep.seed,
        liveEmitter.code
      );
      gl.uniform4fv(gl.getUniformLocation(this.engine.progInitParticles, 'uSpeciesMix'), liveSpeciesMix);
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.engine.fboInitParticles[0]);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.bindVertexArray(null);
      this.engine.partIdx = 0;
    }

    this.engine._obstBuiltStep = -999;
    this.engine.rockCalving.obstaclesDirty = true;
    this.lastEvalMs = Math.max(0.1, performance.now() - t0);
    this._publishTelemetry();
    this._emitSimulationState();
  }

  _publishTelemetry() {
    const cellArea = Math.pow(this.engine.params.worldSpan / this.snapN, 2);
    let coarseM3 = 0, fineM3 = 0, waterM3 = 0;
    for (let i = 0; i < this.snapBuf.length; i += 4) {
      coarseM3 += Math.max(0, this.snapBuf[i + 1]) * cellArea;
      fineM3 += Math.max(0, this.snapBuf[i + 2]) * cellArea;
      waterM3 += Math.max(0, this.snapBuf[i + 3]) * cellArea;
    }
    this.engine.telemetry.steps = this.engine.stepCount;
    this.engine.telemetry.erodedVolumeM3 = 0;
    this.engine.telemetry.coarseSedM3 = Math.round(coarseM3);
    this.engine.telemetry.fineSedM3 = Math.round(fineM3);
    this.engine.telemetry.waterVolumeM3 = Math.round(waterM3);
    this.engine.telemetry.boulders = this.engine.rockCalving.rocks.length;
    this.engine.telemetry.particles = this.engine.params.particleCount;
    this.engine.telemetry.evalMs = Number(this.lastEvalMs.toFixed(1));
    this.engine.telemetry.layerCount = this.layers.length;
    this.engine.telemetry.caveTris = Math.round(this.caveSystem.vertexCount / 3);
    if (typeof this.engine._syncSeasonalClimateTelemetry === 'function') {
      this.engine._syncSeasonalClimateTelemetry();
    }
    if (this.engine.onTelemetry) this.engine.onTelemetry(this.engine.telemetry);
  }

  projectWorldToScreen(wx, wy, wz) {
    const m = this.lastViewProj;
    if (!m) return null;
    const cx = m[0] * wx + m[4] * wy + m[8] * wz + m[12];
    const cy = m[1] * wx + m[5] * wy + m[9] * wz + m[13];
    const cw = m[3] * wx + m[7] * wy + m[11] * wz + m[15];
    if (cw <= 0.1) return null;
    const ndcX = cx / cw;
    const ndcY = cy / cw;
    return {
      u: ndcX * 0.5 + 0.5,
      v: 0.5 - ndcY * 0.5,
    };
  }

  _resolveEngineViewCode() {
    // Maps toolbar shadingMode -> `TERRAIN_RENDER_FRAG` `uViewMode`
    switch (this.shadingMode) {
      case 0: return 0; // Derived Geology (Natural)
      case 1: return 8; // AO & Ridge Protrusions
      case 2: return 2; // 3D Rock Hardness & Strata
      case 3: return 3; // Drainage Flow & Velocity
      case 4: return 1; // Sedimentation & Cut/Fill
      case 5: return 7; // Selected Layer B&W Mask
      case 6: return 4; // Pure Sculptor Clay
      case 7: return 5; // Cryosphere (Snow & Glacier Ice)
      case 8: return 6; // 2.5D Hydrology & Basins
      case 9: return 9; // Mineral Strata & Dissolved Stain
      case 10: return 10; // Frost damage / sub-resolution cracks (diagnostic only)
      default: return 0;
    }
  }

  /**
   * Renders the terrain using `c5ae88b` / `2cc3d34`'s exact `progSkyBg`, `progTerrainRender`,
   * `progRockInstance`, `progGrassInstance`, and `progParticleRender`, plus `progCave` when
   * 3D SDF Caves / Cliff Undercuts are active in the stack!
   */
  renderPipelineFrame(viewProj, eye, sunDir, timeSec) {
    this.lastViewProj = viewProj;
    const gl = this.gl;
    const R = this.R;
    const p = this.engine.params;
    p.materialTheme = this.paletteId;
    p.fabricMode = this.fabricMode;

    const maxRelief = this._maxReliefEstimate();
    const viewCode = this._resolveEngineViewCode();
    const hasCaves = this.caveSystem.vertexCount > 0;

    if (viewCode === 7) {
      const sel = this.getSelectedLayer();
      const previewMask = sel?.mask?.enabled
        ? sel.mask
        : { enabled: true, ...(sel?.mask || { mode: 'height', min: 0.2, max: 0.85, feather: 0.15 }) };
      this._bakeMaskTexture(previewMask, maxRelief);
    }

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, this.engine.canvas.width, this.engine.canvas.height);
    gl.clear(gl.DEPTH_BUFFER_BIT);

    // 1. Studio Sky Backdrop (`progSkyBg`)
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.depthMask(true);
    gl.useProgram(this.engine.progSkyBg);
    gl.uniform1i(gl.getUniformLocation(this.engine.progSkyBg, 'uMaterialTheme'), this.engine._themeCode());
    gl.bindVertexArray(this.engine.emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    // 2. Proven Geological Terrain Renderer (`progTerrainRender` from `erosion-shaders.js`!)
    const { hardnessVec, strataVec } = this.engine._getHardnessUniforms();
    const layerVisVec = this.engine._getLayerVisUniforms();
    const climate = this.engine._getClimateUniforms();

    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.depthMask(true);
    if (this.hasActiveVecDisp || hasCaves) {
      gl.disable(gl.CULL_FACE);
    } else {
      gl.enable(gl.CULL_FACE);
      gl.cullFace(gl.BACK);
    }

    gl.useProgram(this.engine.progTerrainRender);
    gl.bindVertexArray(this.engine.terrainVao);

    this.engine._bindTex(this.engine.progTerrainRender, 'uTerrainTex', this.engine.terrainTex[this.engine.terrainIdx], 0);
    this.engine._bindTex(this.engine.progTerrainRender, 'uInitialTex', this.engine.initialTerrainTex, 1);
    this.engine._bindTex(this.engine.progTerrainRender, 'uVelocityFieldTex', this.engine.velocityFieldTex, 2);
    this.engine._bindTex(this.engine.progTerrainRender, 'uSnowTex', this.engine.snowTex[this.engine.snowIdx], 3);
    this.engine._bindTex(this.engine.progTerrainRender, 'uVecDispTex', this.vecDispTex[this.vecIdx], 4);
    this.engine._bindTex(this.engine.progTerrainRender, 'uCavePortalTex', this.cavePortalTex, 5);
    this.engine._bindTex(this.engine.progTerrainRender, 'uMaskPreviewTex', this.maskTex, 6);
    this.engine._bindTex(this.engine.progTerrainRender, 'uChemTex', this.engine.chemTex[this.engine.chemIdx], 7);
    this.engine._bindTex(this.engine.progTerrainRender, 'uMineralTex', this.engine.mineralTex[this.engine.mineralIdx], 8);
    this.engine._bindTex(this.engine.progTerrainRender, 'uFrostTex', this.engine.frostTex[this.engine.frostIdx], 9);

    gl.uniformMatrix4fv(gl.getUniformLocation(this.engine.progTerrainRender, 'uViewProj'), false, viewProj);
    gl.uniform3fv(gl.getUniformLocation(this.engine.progTerrainRender, 'uCameraPos'), eye);
    gl.uniform3fv(gl.getUniformLocation(this.engine.progTerrainRender, 'uSunDir'), sunDir);
    gl.uniform4f(
      gl.getUniformLocation(this.engine.progTerrainRender, 'uRenderDomain'),
      p.worldSpan,
      R,
      (p.showWater && p.waterLayerEnabled !== false) ? 1.0 : 0.0,
      timeSec
    );
    gl.uniform4fv(gl.getUniformLocation(this.engine.progTerrainRender, 'uLayerVis'), layerVisVec);
    gl.uniform4fv(gl.getUniformLocation(this.engine.progTerrainRender, 'uHardnessParams'), hardnessVec);
    gl.uniform4fv(gl.getUniformLocation(this.engine.progTerrainRender, 'uStrataOrient'), strataVec);
    this.engine._bindStrataColumnUniforms(this.engine.progTerrainRender);

    const sel = this.getSelectedLayer();
    const isSculptTool = this.authorTool.startsWith('sculpt-') || this.authorTool.startsWith('cave-');
    const brushRad = sel?.type === 'sculpt-layer' ? (sel.params.brushRadius ?? 34) : p.brushRadius;
    const brushCode = isSculptTool ? (this.authorTool.startsWith('cave-') ? 3 : 2) : 0;
    gl.uniform4f(
      gl.getUniformLocation(this.engine.progTerrainRender, 'uBrush'),
      this.engine.pointerWorldXZ[0],
      this.engine.pointerWorldXZ[1],
      brushRad,
      brushCode
    );
    gl.uniform1i(gl.getUniformLocation(this.engine.progTerrainRender, 'uViewMode'), viewCode);
    gl.uniform1i(gl.getUniformLocation(this.engine.progTerrainRender, 'uMaterialTheme'), this.engine._themeCode());
    gl.uniform1i(gl.getUniformLocation(this.engine.progTerrainRender, 'uFabricMode'), this.engine._fabricCode());
    gl.uniform4fv(gl.getUniformLocation(this.engine.progTerrainRender, 'uClimate'), climate.snowVec);
    gl.uniform4f(
      gl.getUniformLocation(this.engine.progTerrainRender, 'uWindRender'),
      climate.windVec[0],
      climate.windVec[1],
      p.windSpeed || 0.0,
      p.dryAeolianMode || 0.0
    );
    gl.uniform1f(gl.getUniformLocation(this.engine.progTerrainRender, 'uSeed'), p.seed);
    gl.uniform1f(gl.getUniformLocation(this.engine.progTerrainRender, 'uChemVis'), p.chemicalWeathering ?? DEFAULT_EROSION_PARAMS.chemicalWeathering);
    gl.uniform1f(gl.getUniformLocation(this.engine.progTerrainRender, 'uUseVecDisp'), this.hasActiveVecDisp ? 1.0 : 0.0);
    gl.uniform1f(gl.getUniformLocation(this.engine.progTerrainRender, 'uHasCaves'), hasCaves ? 1.0 : 0.0);

    gl.drawElements(gl.TRIANGLES, this.engine.terrainIndexCount, gl.UNSIGNED_INT, 0);

    // 3. Real 3D Volumetric SDF Cave & Cliff-Undercut Interior Mesh (when active)
    if (hasCaves) {
      gl.disable(gl.CULL_FACE);
      gl.useProgram(this.progCave);
      gl.bindVertexArray(this.caveVao);
      gl.uniformMatrix4fv(gl.getUniformLocation(this.progCave, 'uViewProj'), false, viewProj);
      gl.uniform3fv(gl.getUniformLocation(this.progCave, 'uCameraPos'), eye);
      gl.uniform3fv(gl.getUniformLocation(this.progCave, 'uSunDir'), sunDir);
      gl.uniform4f(gl.getUniformLocation(this.progCave, 'uRenderDomain'), p.worldSpan, R, 1.0, maxRelief);
      gl.uniform2i(gl.getUniformLocation(this.progCave, 'uShadeConfig'), this.engine._themeCode(), viewCode);
      gl.drawArrays(gl.TRIANGLES, 0, this.caveSystem.vertexCount);
    }

    // 4. Real 3D SDF Outcrops & 6-DOF Rigid-Body Cliff-Calved Boulders (`progRockInstance`)
    if (p.showOutcrops !== false && this.engine.rockCalving.rocks.length > 0 && viewCode !== 7) {
      gl.enable(gl.CULL_FACE);
      gl.cullFace(gl.BACK);
      gl.useProgram(this.engine.progRockInstance);
      gl.bindVertexArray(this.engine.rockVao);

      this.engine._bindTex(this.engine.progRockInstance, 'uTerrainTex', this.engine.terrainTex[this.engine.terrainIdx], 0);
      this.engine._bindTex(this.engine.progRockInstance, 'uSnowTex', this.engine.snowTex[this.engine.snowIdx], 1);
      this.engine._bindTex(this.engine.progRockInstance, 'uChemTex', this.engine.chemTex[this.engine.chemIdx], 2);
      this.engine._bindTex(this.engine.progRockInstance, 'uMineralTex', this.engine.mineralTex[this.engine.mineralIdx], 3);
      this.engine._bindTex(this.engine.progRockInstance, 'uRockSdfTex', this.engine.rockSdfTex, 4);

      gl.uniformMatrix4fv(gl.getUniformLocation(this.engine.progRockInstance, 'uViewProj'), false, viewProj);
      gl.uniform3fv(gl.getUniformLocation(this.engine.progRockInstance, 'uCameraPos'), eye);
      gl.uniform3fv(gl.getUniformLocation(this.engine.progRockInstance, 'uSunDir'), sunDir);
      gl.uniform4f(
        gl.getUniformLocation(this.engine.progRockInstance, 'uRenderDomain'),
        p.worldSpan,
        R,
        (p.showWater && p.waterLayerEnabled !== false) ? 1.0 : 0.0,
        timeSec
      );
      gl.uniform4fv(gl.getUniformLocation(this.engine.progRockInstance, 'uLayerVis'), layerVisVec);
      gl.uniform4fv(gl.getUniformLocation(this.engine.progRockInstance, 'uHardnessParams'), hardnessVec);
      gl.uniform4fv(gl.getUniformLocation(this.engine.progRockInstance, 'uStrataOrient'), strataVec);
      this.engine._bindStrataColumnUniforms(this.engine.progRockInstance);
      gl.uniform4fv(gl.getUniformLocation(this.engine.progRockInstance, 'uClimate'), climate.snowVec);
      gl.uniform1i(gl.getUniformLocation(this.engine.progRockInstance, 'uViewMode'), viewCode === 8 ? 0 : viewCode);
      gl.uniform1i(gl.getUniformLocation(this.engine.progRockInstance, 'uMaterialTheme'), this.engine._themeCode());
      gl.uniform1i(gl.getUniformLocation(this.engine.progRockInstance, 'uFabricMode'), this.engine._fabricCode());
      gl.uniform1f(gl.getUniformLocation(this.engine.progRockInstance, 'uSeed'), p.seed);
      gl.uniform1f(gl.getUniformLocation(this.engine.progRockInstance, 'uChemWeather'), p.chemicalWeathering ?? 0.7);

      gl.bindBuffer(gl.ARRAY_BUFFER, this.engine.rockInstVbo);
      const subs = this.engine.rockCalving.atlas.submeshes;
      for (let b = 0; b < subs.length; b++) {
        const count = this.engine.rockCalving.bucketCounts[b];
        if (count <= 0) continue;
        gl.uniform1i(gl.getUniformLocation(this.engine.progRockInstance, 'uRockArchetype'), b);
        gl.bufferSubData(gl.ARRAY_BUFFER, 0, this.engine.rockCalving.bucketData[b].subarray(0, count * 16));
        gl.drawElementsInstanced(
          gl.TRIANGLES,
          subs[b].indexCount,
          gl.UNSIGNED_SHORT,
          subs[b].firstIndex * 2,
          count
        );
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, null);
    }

    // 5. Real 3D GPU-Instanced Grass Vegetation Layer (`progGrassInstance`)
    if (p.grassEnabled && (p.grassDensity ?? 0.5) > 0.01 && (viewCode === 0 || viewCode === 4)) {
      const tuftCount = Math.max(512, Math.min(MAX_GRASS_TUFTS, Math.round((p.grassDensity ?? 0.55) * MAX_GRASS_TUFTS)));
      gl.disable(gl.CULL_FACE);
      gl.useProgram(this.engine.progGrassInstance);
      gl.bindVertexArray(this.engine.grassVao);

      this.engine._bindTex(this.engine.progGrassInstance, 'uTerrainTex', this.engine.terrainTex[this.engine.terrainIdx], 0);
      this.engine._bindTex(this.engine.progGrassInstance, 'uSnowTex', this.engine.snowTex[this.engine.snowIdx], 1);

      gl.uniformMatrix4fv(gl.getUniformLocation(this.engine.progGrassInstance, 'uViewProj'), false, viewProj);
      gl.uniform3fv(gl.getUniformLocation(this.engine.progGrassInstance, 'uCameraPos'), eye);
      gl.uniform3fv(gl.getUniformLocation(this.engine.progGrassInstance, 'uSunDir'), sunDir);
      gl.uniform4f(
        gl.getUniformLocation(this.engine.progGrassInstance, 'uRenderDomain'),
        p.worldSpan,
        R,
        p.showWater ? 1.0 : 0.0,
        timeSec
      );
      gl.uniform4f(
        gl.getUniformLocation(this.engine.progGrassInstance, 'uGrassParams'),
        tuftCount,
        p.grassHeight ?? 1.15,
        p.grassWindSway ?? 0.65,
        p.grassSlopeMax ?? 0.62
      );
      gl.uniform4f(
        gl.getUniformLocation(this.engine.progGrassInstance, 'uGrassStyle'),
        p.grassMode ?? 0,
        p.grassDryBlend ?? 0.15,
        p.grassElevMax ?? 0.52,
        p.grassPatchiness ?? 0.58
      );
      gl.uniform4f(
        gl.getUniformLocation(this.engine.progGrassInstance, 'uWindRender'),
        climate.windVec[0],
        climate.windVec[1],
        p.windSpeed || 0.25,
        p.dryAeolianMode || 0.0
      );
      gl.uniform4fv(gl.getUniformLocation(this.engine.progGrassInstance, 'uStrataOrient'), strataVec);
      gl.uniform1i(gl.getUniformLocation(this.engine.progGrassInstance, 'uViewMode'), viewCode);
      gl.uniform1i(gl.getUniformLocation(this.engine.progGrassInstance, 'uMaterialTheme'), this.engine._themeCode());
      gl.uniform1f(gl.getUniformLocation(this.engine.progGrassInstance, 'uSeed'), p.seed);

      gl.drawElementsInstanced(gl.TRIANGLES, this.engine.grassIndexCount, gl.UNSIGNED_SHORT, 0, tuftCount);
      gl.enable(gl.CULL_FACE);
    }

    // 6. 3D Falling Sky Raindrops, Snow, Wind Sand & Runoff Particles (`progParticleRender`)
    if (this.liveSimRunning && p.showParticles && p.particleOpacity > 0.01 && climate.anyParticleSpecies && viewCode !== 7) {
      const activeCount = Math.min(MAX_GPU_PARTICLES, Math.max(1024, Math.floor(p.particleCount)));
      gl.disable(gl.CULL_FACE);
      gl.depthMask(false);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

      gl.useProgram(this.engine.progParticleRender);
      gl.bindVertexArray(this.engine.emptyVao);

      this.engine._bindTex(this.engine.progParticleRender, 'uPosAgeTex', this.engine.partPosAge[this.engine.partIdx], 0);
      this.engine._bindTex(this.engine.progParticleRender, 'uVelWaterTex', this.engine.partVelWater[this.engine.partIdx], 1);
      this.engine._bindTex(this.engine.progParticleRender, 'uSedimentTex', this.engine.partSediment[this.engine.partIdx], 2);
      this.engine._bindTex(this.engine.progParticleRender, 'uTerrainTex', this.engine.terrainTex[this.engine.terrainIdx], 3);

      gl.uniformMatrix4fv(gl.getUniformLocation(this.engine.progParticleRender, 'uViewProj'), false, viewProj);
      gl.uniform3fv(gl.getUniformLocation(this.engine.progParticleRender, 'uCameraPos'), eye);
      gl.uniform4fv(gl.getUniformLocation(this.engine.progParticleRender, 'uSpeciesVis'), this.engine._getSpeciesVisUniform());
      gl.uniform4f(
        gl.getUniformLocation(this.engine.progParticleRender, 'uPartVis'),
        p.worldSpan,
        R,
        p.particleOpacity,
        activeCount
      );

      gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, activeCount);

      gl.disable(gl.BLEND);
      gl.depthMask(true);
    }

    gl.bindVertexArray(null);
  }
}
