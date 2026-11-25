import {
  DEFAULT_EROSION_PARAMS,
  EMITTER_MODES,
  EROSION_LANDSCAPES,
  getVegetationDefaultsForLandscape,
} from './erosion-presets.js';
import {
  FULLSCREEN_VERT,
  CHEM_WEATHER_FRAG,
  FROST_DAMAGE_FRAG,
  MINERAL_ACCUM_FRAG,
  GRASS_INSTANCE_FRAG,
  GRASS_INSTANCE_VERT,
  INIT_PARTICLES_FRAG,
  INIT_SNOW_FRAG,
  INIT_TERRAIN_FRAG,
  PARTICLE_RENDER_FRAG,
  PARTICLE_RENDER_VERT,
  PARTICLE_SCATTER_FRAG,
  PARTICLE_SCATTER_VERT,
  PARTICLE_STEP_FRAG,
  PROCEDURAL_MODIFIER_FRAG,
  ROCK_IMPRINT_FRAG,
  ROCK_OBSTACLE_FRAG,
  ROCK_TERRAIN_COUPLE_FRAG,
  ROCK_INSTANCE_FRAG,
  ROCK_INSTANCE_VERT,
  SKY_BG_FRAG,
  SNOW_CRYO_FRAG,
  TERRAIN_INTEGRATE_FRAG,
  TERRAIN_RENDER_FRAG,
  TERRAIN_RENDER_VERT,
  THERMAL_TALUS_FRAG,
  WATER_FLUX_FRAG,
} from './erosion-shaders.js';
import { MAX_GRASS_TUFTS, buildGrassTuftMesh } from './grass-system.js';
import { RockCalvingSystem } from './rock-calving.js';
import { TerrainStackPipeline } from './terrain-stack-pipeline.js';
import { effectiveWindAzimuthAtStep, windShiftOffsetDeg, windCyclesAtStep } from './wind-regime.js';
import {
  createSnowEffectPreset,
  deriveSnowEffectWeights,
  normalizeFrostParams,
  normalizeSnowEffects,
  snowmeltCycleAtStep,
} from './frost-weathering.js';
import {
  createStrataColumnPreset,
  normalizeStrataColumn,
  packStrataColumnUniforms,
} from './stratigraphic-column.js';
import {
  climateDefaultsForLandscape,
  seasonalClimateAt,
} from './seasonal-climate.js';

const PART_TEX_W = 256;
const PART_TEX_H = 128; // 256 * 128 = 32,768 GPU particle slots
export const MAX_GPU_PARTICLES = PART_TEX_W * PART_TEX_H;

const TELEMETRY_RES = 64;

export const TERRAIN_RESOLUTION_OPTIONS = Object.freeze([256, 512, 768, 1024]);
export const TERRAIN_RESOLUTION_STORAGE_KEY = 'frontier.terrainResolution.v1';

const TERRAIN_RESOLUTION_PROFILES = Object.freeze({
  256: Object.freeze({ label: 'Performance', detail: 'fast iteration' }),
  512: Object.freeze({ label: 'Balanced', detail: 'default' }),
  768: Object.freeze({ label: 'High', detail: 'finer channels & detail' }),
  1024: Object.freeze({ label: 'Ultra', detail: 'high VRAM' }),
});

export function normalizeTerrainResolution(value, fallback = DEFAULT_EROSION_PARAMS.gridSize) {
  const numeric = Number(value);
  if (TERRAIN_RESOLUTION_OPTIONS.includes(numeric)) return numeric;
  const safeFallback = Number(fallback);
  return TERRAIN_RESOLUTION_OPTIONS.includes(safeFallback)
    ? safeFallback
    : DEFAULT_EROSION_PARAMS.gridSize;
}

export function terrainResolutionProfile(value, worldSpan = DEFAULT_EROSION_PARAMS.worldSpan) {
  const resolution = normalizeTerrainResolution(value);
  const span = Number.isFinite(Number(worldSpan)) && Number(worldSpan) > 0
    ? Number(worldSpan)
    : DEFAULT_EROSION_PARAMS.worldSpan;
  const profile = TERRAIN_RESOLUTION_PROFILES[resolution];
  return {
    value: resolution,
    label: profile.label,
    detail: profile.detail,
    cellSizeMeters: span / resolution,
    relativeCellCount: (resolution / DEFAULT_EROSION_PARAMS.gridSize) ** 2,
  };
}

export function resolveTerrainResolution(preferred) {
  if (preferred !== undefined && preferred !== null && preferred !== '') {
    return normalizeTerrainResolution(preferred);
  }

  try {
    const search = globalThis.location?.search || '';
    const fromUrl = new URLSearchParams(search).get('terrainRes');
    if (fromUrl) return normalizeTerrainResolution(fromUrl);
  } catch (_) {
    // A malformed/host-restricted location should never prevent the studio booting.
  }

  try {
    const stored = globalThis.localStorage?.getItem(TERRAIN_RESOLUTION_STORAGE_KEY);
    if (stored) return normalizeTerrainResolution(stored);
  } catch (_) {
    // Storage may be unavailable in private/embedded browser contexts.
  }

  return DEFAULT_EROSION_PARAMS.gridSize;
}

const TELEMETRY_FRAG = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
in vec2 vUv;
layout(location = 0) out vec4 outStats;
uniform sampler2D uTerrainTex;
uniform float uGridRes;
void main() {
  ivec2 tc = clamp(ivec2(vUv * uGridRes), ivec2(0), ivec2(int(uGridRes) - 1));
  outStats = texelFetch(uTerrainTex, tc, 0);
}
`;

function compileShader(gl, type, source, label) {
  const sh = gl.createShader(type);
  gl.shaderSource(sh, source);
  gl.compileShader(sh);
  if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
    const log = gl.getShaderInfoLog(sh);
    gl.deleteShader(sh);
    throw new Error(`[GPU Erosion ${label}] Shader compile error:\n${log}`);
  }
  return sh;
}

function createProgram(gl, vsSrc, fsSrc, label) {
  const vs = compileShader(gl, gl.VERTEX_SHADER, vsSrc, `${label}:VS`);
  const fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSrc, `${label}:FS`);
  const prog = gl.createProgram();
  gl.attachShader(prog, vs);
  gl.attachShader(prog, fs);
  gl.linkProgram(prog);
  gl.deleteShader(vs);
  gl.deleteShader(fs);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
    const log = gl.getProgramInfoLog(prog);
    gl.deleteProgram(prog);
    throw new Error(`[GPU Erosion ${label}] Program link error:\n${log}`);
  }
  return prog;
}

function createDataTexture(gl, w, h, internalFormat, format, type, data = null) {
  const tex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, data);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return tex;
}

function createFbo(gl, textures) {
  const fbo = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
  const drawBuffers = [];
  for (let i = 0; i < textures.length; i++) {
    const att = gl.COLOR_ATTACHMENT0 + i;
    gl.framebufferTexture2D(gl.FRAMEBUFFER, att, gl.TEXTURE_2D, textures[i], 0);
    drawBuffers.push(att);
  }
  gl.drawBuffers(drawBuffers);
  const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  if (status !== gl.FRAMEBUFFER_COMPLETE) {
    throw new Error(`Incomplete WebGL2 FBO: 0x${status.toString(16)}`);
  }
  return fbo;
}

export class GpuErosionEngine {
  constructor(canvas, onTelemetry = null, options = {}) {
    this.canvas = canvas;
    this.onTelemetry = onTelemetry;

    const gl = canvas.getContext('webgl2', {
      antialias: true,
      alpha: false,
      depth: true,
      powerPreference: 'high-performance',
      preserveDrawingBuffer: true,
    });
    if (!gl) {
      throw new Error('WebGL2 is required for GPU Particle Erosion.');
    }
    this.gl = gl;

    const extColorFloat = gl.getExtension('EXT_color_buffer_float');
    if (!extColorFloat) {
      throw new Error('EXT_color_buffer_float is required for float32 GPGPU erosion.');
    }
    this.hasFloatBlend = Boolean(gl.getExtension('EXT_float_blend'));
    gl.getExtension('OES_texture_float_linear');

    // Terrain resolution is deliberately a boot-time choice: every height,
    // flux, chemistry, obstacle, sculpt and render-mesh buffer is allocated
    // from this one value. The studio persists the user's choice and reloads
    // so a resolution change can never leave mismatched GPU textures behind.
    const engineOptions = options && typeof options === 'object' ? options : {};
    const requestedGridSize = resolveTerrainResolution(
      engineOptions.gridSize ?? engineOptions.terrainResolution
    );
    this.params = { ...DEFAULT_EROSION_PARAMS, gridSize: requestedGridSize };
    this.params.snowEffects = createSnowEffectPreset(DEFAULT_EROSION_PARAMS.snowEffectPreset).effects;
    this.params.strataBeds = createStrataColumnPreset(DEFAULT_EROSION_PARAMS.strataColumnPreset);
    this._strataColumnCacheKey = '';
    this._strataColumnCache = null;
    this.gridRes = requestedGridSize;

    this.active = false;   // Whether this viewport tab is visible
    this.playing = true;   // Whether simulation steps advance each frame
    this.stepCount = 0;
    this.cryoStepCount = 0;
    this.climateElapsedYears = 0;
    this.climateRunStartYears = 0;
    this.climateRunActive = false;
    this.climateRunCompleted = false;
    this._seasonalClimateSuppressed = false;
    this.startTimeMs = performance.now();
    this.fps = 60;
    this._fpsFrames = 0;
    this._fpsLastMs = performance.now();

    this.telemetry = {
      steps: 0,
      gridRes: this.gridRes,
      particles: this.params.particleCount,
      erodedVolumeM3: 0,
      coarseSedM3: 0,
      fineSedM3: 0,
      waterVolumeM3: 0,
      boulders: 0,
      fps: 60,
      climateYear: 1,
      climateSeason: 'Seasonal climate off',
      climateDate: '',
      climateElapsedYears: 0,
      climateTargetYears: 0,
      climateRunProgress: 0,
    };

    // Camera state (Unreal WASD + Q/E Free-Fly + RMB Free-Look + LMB Orbit)
    this.camera = {
      yaw: 0.42,
      pitch: 0.46,
      distance: 740.0,
      target: [0, 54, 0],
      fovDeg: 44,
      flySpeed: 175.0,
    };
    this._keysDown = new Set();
    this._rmbDown = false;
    this._lastFrameMs = performance.now();

    // Storm & Interactive Brush state in world XZ coordinates
    this.stormTarget = [0, 0];
    this.pointerWorldXZ = [0, 0];
    this.brushDown = false;

    this.rockCalving = new RockCalvingSystem();

    this._initPrograms();
    this._initBuffersAndTextures();
    this._initMeshVao();
    this._initRockInstanceVao();
    this._initGrassInstanceVao();
    this._bindEvents();

    Object.assign(this.params, getVegetationDefaultsForLandscape(this.params.landscape));

    // Non-Destructive Bottom-to-Top Terrain Stack Pipeline (Landforms + SDF Caves + Simulation + Placements)
    this.pipelineMode = true;
    this.playing = true;
    this.pipeline = new TerrainStackPipeline(this);
  }

  _initPrograms() {
    const gl = this.gl;
    this.progInitTerrain = createProgram(gl, FULLSCREEN_VERT, INIT_TERRAIN_FRAG, 'InitTerrain');
    this.progProceduralMod = createProgram(gl, FULLSCREEN_VERT, PROCEDURAL_MODIFIER_FRAG, 'ProceduralModifier');
    this.progInitSnow = createProgram(gl, FULLSCREEN_VERT, INIT_SNOW_FRAG, 'InitSnow');
    this.progInitParticles = createProgram(gl, FULLSCREEN_VERT, INIT_PARTICLES_FRAG, 'InitParticles');
    this.progParticleStep = createProgram(gl, FULLSCREEN_VERT, PARTICLE_STEP_FRAG, 'ParticleStep');
    this.progParticleScatter = createProgram(gl, PARTICLE_SCATTER_VERT, PARTICLE_SCATTER_FRAG, 'ParticleScatter');
    this.progWaterFlux = createProgram(gl, FULLSCREEN_VERT, WATER_FLUX_FRAG, 'WaterFlux');
    this.progTerrainIntegrate = createProgram(gl, FULLSCREEN_VERT, TERRAIN_INTEGRATE_FRAG, 'TerrainIntegrate');
    this.progSnowCryo = createProgram(gl, FULLSCREEN_VERT, SNOW_CRYO_FRAG, 'SnowCryo');
    this.progFrostDamage = createProgram(gl, FULLSCREEN_VERT, FROST_DAMAGE_FRAG, 'FrostDamage');
    this.progRockImprint = createProgram(gl, FULLSCREEN_VERT, ROCK_IMPRINT_FRAG, 'RockImprint');
    this.progRockObstacle = createProgram(gl, FULLSCREEN_VERT, ROCK_OBSTACLE_FRAG, 'RockObstacle');
    this.progRockCouple = createProgram(gl, FULLSCREEN_VERT, ROCK_TERRAIN_COUPLE_FRAG, 'RockTerrainCouple');
    this.progChemWeather = createProgram(gl, FULLSCREEN_VERT, CHEM_WEATHER_FRAG, 'ChemWeather');
    this.progMineralAccum = createProgram(gl, FULLSCREEN_VERT, MINERAL_ACCUM_FRAG, 'MineralAccum');
    this.progThermalTalus = createProgram(gl, FULLSCREEN_VERT, THERMAL_TALUS_FRAG, 'ThermalTalus');
    this.progTerrainRender = createProgram(gl, TERRAIN_RENDER_VERT, TERRAIN_RENDER_FRAG, 'TerrainRender');
    this.progRockInstance = createProgram(gl, ROCK_INSTANCE_VERT, ROCK_INSTANCE_FRAG, 'RockInstance');
    this.progGrassInstance = createProgram(gl, GRASS_INSTANCE_VERT, GRASS_INSTANCE_FRAG, 'GrassInstance');
    this.progParticleRender = createProgram(gl, PARTICLE_RENDER_VERT, PARTICLE_RENDER_FRAG, 'ParticleRender');
    this.progSkyBg = createProgram(gl, FULLSCREEN_VERT, SKY_BG_FRAG, 'SkyBg');
    this.progTelemetry = createProgram(gl, FULLSCREEN_VERT, TELEMETRY_FRAG, 'Telemetry');
  }

  _initBuffersAndTextures() {
    const gl = this.gl;
    const R = this.gridRes;

    this.emptyVao = gl.createVertexArray();

    // Terrain ping-pong (RGBA32F: bedrock, coarseSed, fineSed, water)
    this.terrainTex = [
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.terrainIdx = 0;

    // Initial un-eroded snapshot for cut/fill heatmap & cubic-meter telemetry
    this.initialTerrainTex = createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT);
    this.fboInitialTerrain = createFbo(gl, [this.initialTerrainTex]);

    // Pipe flux ping-pong (RGBA32F)
    this.fluxTex = [
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.fluxIdx = 0;
    this.fboFlux = [
      createFbo(gl, [this.fluxTex[0]]),
      createFbo(gl, [this.fluxTex[1]]),
    ];

    // Eulerian velocity & discharge field (RGBA32F)
    this.velocityFieldTex = createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT);

    // Cryosphere ping-pong (RGBA32F: snowpack depth, glacier ice thickness, prev snow, ice melt)
    this.snowTex = [
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.snowIdx = 0;
    this.fboSnowClear = [
      createFbo(gl, [this.snowTex[0]]),
      createFbo(gl, [this.snowTex[1]]),
    ];

    // Chemical weathering state ping-pong (RGBA32F: rust, salt,
    // desert varnish (+) / copper patina (-), hydrolysis alteration)
    this.chemTex = [
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.chemIdx = 0;
    this.fboChem = [
      createFbo(gl, [this.chemTex[0]]),
      createFbo(gl, [this.chemTex[1]]),
    ];
    for (let i = 0; i < 2; i++) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboChem[i]);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    // Frost-damage state ping-pong (RGBA32F: damage, crack aperture mm,
    // pore saturation, detached metres queued for this step's integrator).
    this.frostTex = [
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.frostIdx = 0;
    this.fboFrost = [
      createFbo(gl, [this.frostTex[0]]),
      createFbo(gl, [this.frostTex[1]]),
    ];
    for (let i = 0; i < 2; i++) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboFrost[i]);
      gl.clearColor(0, 0.05, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    // Mineral source field ping-pong (RGBA32F: iron, copper, carbonate, salt)
    // baked from sedimentary / stratigraphic stack layers during evaluation.
    this.mineralTex = [
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.mineralIdx = 0;
    this.fboMineral = [
      createFbo(gl, [this.mineralTex[0]]),
      createFbo(gl, [this.mineralTex[1]]),
    ];
    for (let i = 0; i < 2; i++) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboMineral[i]);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    // Boulder occupancy field for sediment/water transport (rockTop, coverage)
    this.obstacleTex = [
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, R, R, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.obstacleIdx = 0;
    this.fboRockObstacle = [
      createFbo(gl, [this.obstacleTex[0]]),
      createFbo(gl, [this.obstacleTex[1]]),
    ];
    for (let i = 0; i < 2; i++) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboRockObstacle[i]);
      gl.clearColor(-1000, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this._obstA = new Float32Array(24 * 4); // (x, z, Rh, centreY)
    this._obstB = new Float32Array(24 * 4); // (Rv, -, -, -)
    this._obstBuiltStep = -999;

    // Terrain single & MRT FBOs
    this.fboTerrainSingle = [
      createFbo(gl, [this.terrainTex[0]]),
      createFbo(gl, [this.terrainTex[1]]),
    ];
    this.fboTerrainIntegrate = [
      createFbo(gl, [this.terrainTex[0], this.velocityFieldTex]),
      createFbo(gl, [this.terrainTex[1], this.velocityFieldTex]),
    ];

    // Additive particle scatter target: RGBA32F when EXT_float_blend is supported, RGBA16F fallback otherwise!
    const scatterInternal = this.hasFloatBlend ? gl.RGBA32F : gl.RGBA16F;
    const scatterType = this.hasFloatBlend ? gl.FLOAT : gl.HALF_FLOAT;
    this.deltaTex = createDataTexture(gl, R, R, scatterInternal, gl.RGBA, scatterType);
    this.snowDeltaTex = createDataTexture(gl, R, R, scatterInternal, gl.RGBA, scatterType);
    this.fboDelta = createFbo(gl, [this.deltaTex, this.snowDeltaTex]);

    // 32,768 GPU Particle Ping-Pong Textures (256 x 128 RGBA32F)
    this.partPosAge = [
      createDataTexture(gl, PART_TEX_W, PART_TEX_H, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, PART_TEX_W, PART_TEX_H, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.partVelWater = [
      createDataTexture(gl, PART_TEX_W, PART_TEX_H, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, PART_TEX_W, PART_TEX_H, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.partSediment = [
      createDataTexture(gl, PART_TEX_W, PART_TEX_H, gl.RGBA32F, gl.RGBA, gl.FLOAT),
      createDataTexture(gl, PART_TEX_W, PART_TEX_H, gl.RGBA32F, gl.RGBA, gl.FLOAT),
    ];
    this.partExchange = createDataTexture(gl, PART_TEX_W, PART_TEX_H, gl.RGBA32F, gl.RGBA, gl.FLOAT);
    this.partExchange2 = createDataTexture(gl, PART_TEX_W, PART_TEX_H, gl.RGBA32F, gl.RGBA, gl.FLOAT);
    this.partIdx = 0;

    this.fboInitParticles = [
      createFbo(gl, [this.partPosAge[0], this.partVelWater[0], this.partSediment[0]]),
      createFbo(gl, [this.partPosAge[1], this.partVelWater[1], this.partSediment[1]]),
    ];
    this.fboStepParticles = [
      createFbo(gl, [this.partPosAge[0], this.partVelWater[0], this.partSediment[0], this.partExchange, this.partExchange2]),
      createFbo(gl, [this.partPosAge[1], this.partVelWater[1], this.partSediment[1], this.partExchange, this.partExchange2]),
    ];

    // Tiny 64x64 telemetry downsample FBO (sampled only once every 36 steps -> zero frame stall!)
    this.telemetryTex = createDataTexture(gl, TELEMETRY_RES, TELEMETRY_RES, gl.RGBA32F, gl.RGBA, gl.FLOAT);
    this.fboTelemetry = createFbo(gl, [this.telemetryTex]);
    this.telemetryBuf = new Float32Array(TELEMETRY_RES * TELEMETRY_RES * 4);
    this.initialSnap64 = new Float32Array(TELEMETRY_RES * TELEMETRY_RES * 4);
    this.snowSnap64 = new Float32Array(TELEMETRY_RES * TELEMETRY_RES * 4); // cryo twin for rock embedding
    this.frostSnap64 = new Float32Array(TELEMETRY_RES * TELEMETRY_RES * 4); // frost/crack twin for cliff calving & boulder spalling
    this._imprintA = new Float32Array(24 * 4); // ROCK_IMPRINT stamp scratch (x, z, patchR, rimH)
    this._imprintB = new Float32Array(24 * 4); // (rimW, compactSnow, -, -)
    this._coupleA = new Float32Array(24 * 4);  // ROCK_TERRAIN_COUPLE scratch (x, z, radius, cutDepth)
    this._coupleB = new Float32Array(24 * 4);  // (coarseGain, fineGain, rimRatio, kind)
  }

  _initMeshVao() {
    const gl = this.gl;
    // Build a (R + 2) x (R + 2) grid where outer 1-vertex border has UV slightly outside [0, 1]
    // so TERRAIN_RENDER_VERT drops the outer ring cleanly to y = 0 as a solid geological block plinth!
    const N = this.gridRes;
    const M = N + 2;
    const uvs = new Float32Array(M * M * 2);
    let ptr = 0;
    for (let z = 0; z < M; z++) {
      const v = z === 0 ? -0.002 : z === M - 1 ? 1.002 : (z - 1) / (N - 1);
      for (let x = 0; x < M; x++) {
        const u = x === 0 ? -0.002 : x === M - 1 ? 1.002 : (x - 1) / (N - 1);
        uvs[ptr++] = u;
        uvs[ptr++] = v;
      }
    }

    const quadCount = (M - 1) * (M - 1);
    const indices = new Uint32Array(quadCount * 6);
    let ip = 0;
    for (let z = 0; z < M - 1; z++) {
      for (let x = 0; x < M - 1; x++) {
        const i00 = z * M + x;
        const i10 = i00 + 1;
        const i01 = i00 + M;
        const i11 = i01 + 1;
        indices[ip++] = i00;
        indices[ip++] = i01;
        indices[ip++] = i10;
        indices[ip++] = i10;
        indices[ip++] = i01;
        indices[ip++] = i11;
      }
    }

    this.terrainIndexCount = indices.length;
    this.terrainVao = gl.createVertexArray();
    gl.bindVertexArray(this.terrainVao);

    const vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, uvs, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);

    const ibo = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);

    gl.bindVertexArray(null);
  }

  _initRockInstanceVao() {
    const gl = this.gl;
    const atlas = this.rockCalving.atlas;
    this.rockVao = gl.createVertexArray();
    gl.bindVertexArray(this.rockVao);

    const posBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);
    gl.bufferData(gl.ARRAY_BUFFER, atlas.positions, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);

    const nrmBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, nrmBuf);
    gl.bufferData(gl.ARRAY_BUFFER, atlas.normals, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);

    const mskBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, mskBuf);
    gl.bufferData(gl.ARRAY_BUFFER, atlas.masks, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 4, gl.FLOAT, false, 0, 0);

    this.rockInstVbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.rockInstVbo);
    gl.bufferData(gl.ARRAY_BUFFER, 320 * 16 * 4, gl.DYNAMIC_DRAW);
    const stride = 16 * 4;
    for (let k = 0; k < 4; k++) {
      const loc = 3 + k;
      gl.enableVertexAttribArray(loc);
      gl.vertexAttribPointer(loc, 4, gl.FLOAT, false, stride, k * 16);
      gl.vertexAttribDivisor(loc, 1);
    }

    const idxBuf = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, idxBuf);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, atlas.indices, gl.STATIC_DRAW);

    gl.bindVertexArray(null);

    this.rockSdfTex = createDataTexture(
      gl,
      atlas.sdfTexWidth,
      atlas.sdfTexHeight,
      gl.RGBA32F,
      gl.RGBA,
      gl.FLOAT,
      atlas.sdfTexData
    );
  }

  _initGrassInstanceVao() {
    const gl = this.gl;
    const tuft = buildGrassTuftMesh();
    this.grassIndexCount = tuft.indexCount;
    this.grassVao = gl.createVertexArray();
    gl.bindVertexArray(this.grassVao);

    const posBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);
    gl.bufferData(gl.ARRAY_BUFFER, tuft.positions, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);

    const nrmBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, nrmBuf);
    gl.bufferData(gl.ARRAY_BUFFER, tuft.normals, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);

    const metaBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, metaBuf);
    gl.bufferData(gl.ARRAY_BUFFER, tuft.bladeMeta, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 2, gl.FLOAT, false, 0, 0);

    const idxBuf = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, idxBuf);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, tuft.indices, gl.STATIC_DRAW);

    gl.bindVertexArray(null);
  }

  triggerBoulderCalving(count = 24) {
    const spawned = this.rockCalving.calveCliffBoulders(count);
    this.rockCalving._rebuildInstanceBuckets();
    this.telemetry.boulders = this.rockCalving.rocks.length;
    if (this.onTelemetry) this.onTelemetry(this.telemetry);
    return spawned;
  }

  _bindEvents() {
    const el = this.canvas;
    let dragging = false;
    let button = 0;
    let lastX = 0;
    let lastY = 0;

    el.addEventListener('contextmenu', (e) => e.preventDefault());

    el.addEventListener('pointerdown', (e) => {
      if (!this.active) return;
      dragging = true;
      button = e.button;
      if (button === 2) this._rmbDown = true;
      lastX = e.clientX;
      lastY = e.clientY;
      el.setPointerCapture(e.pointerId);

      const hit = this._raycastPlaneXZ(e.clientX, e.clientY);
      if (hit) this.pointerWorldXZ = hit;

      const pipeTool = this.pipelineMode && this.pipeline ? this.pipeline.authorTool : 'navigate';
      const isPipeSculpt =
        button === 0 &&
        !e.altKey &&
        (pipeTool.startsWith('sculpt-') || pipeTool.startsWith('cave-'));

      if (isPipeSculpt && hit) {
        this.brushDown = true;
        this.pipeline.applySculptStroke(hit[0], hit[1], true);
        if (this.onStackMutated) this.onStackMutated();
        return;
      }

      const isToolStroke =
        button === 0 &&
        (e.shiftKey || (this.params.brushMode && this.params.brushMode !== 'orbit'));
      if (isToolStroke && hit) {
        this.brushDown = true;
        if (this.params.brushMode === 'cloudburst' || e.shiftKey) {
          this.stormTarget = [hit[0], hit[1]];
        }
      }
    });

    el.addEventListener('pointermove', (e) => {
      if (!this.active) return;
      const hit = this._raycastPlaneXZ(e.clientX, e.clientY);
      if (hit) {
        this.pointerWorldXZ = hit;
        const pipeTool = this.pipelineMode && this.pipeline ? this.pipeline.authorTool : 'navigate';
        if (this.brushDown && (pipeTool.startsWith('sculpt-') || pipeTool.startsWith('cave-'))) {
          this.pipeline.applySculptStroke(hit[0], hit[1], false);
          return;
        }
        if (this.brushDown && (this.params.brushMode === 'cloudburst' || e.shiftKey)) {
          this.stormTarget = [hit[0], hit[1]];
        }
      }

      if (!dragging) return;
      const dx = e.clientX - lastX;
      const dy = e.clientY - lastY;
      lastX = e.clientX;
      lastY = e.clientY;

      if (this.brushDown) return;

      if (button === 2) {
        // Unreal Engine RMB Free-Look Mouselook: rotate yaw & pitch around current eye!
        const { eye } = this._getCameraBasis();
        this.camera.yaw -= dx * 0.0046;
        this.camera.pitch = Math.max(-1.42, Math.min(1.48, this.camera.pitch + dy * 0.0044));
        const cp = Math.cos(this.camera.pitch);
        const sp = Math.sin(this.camera.pitch);
        const cy = Math.cos(this.camera.yaw);
        const sy = Math.sin(this.camera.yaw);
        const fx = -cp * sy;
        const fy = -sp;
        const fz = -cp * cy;
        this.camera.target[0] = eye[0] + fx * this.camera.distance;
        this.camera.target[1] = eye[1] + fy * this.camera.distance;
        this.camera.target[2] = eye[2] + fz * this.camera.distance;
      } else if (button === 1 || (button === 0 && e.altKey)) {
        const panScale = Math.max(40, this.camera.distance) * 0.0014;
        const { right, up } = this._getCameraBasis();
        this.camera.target[0] -= (right[0] * dx - up[0] * dy) * panScale;
        this.camera.target[1] -= (right[1] * dx - up[1] * dy) * panScale;
        this.camera.target[2] -= (right[2] * dx - up[2] * dy) * panScale;
      } else if (button === 0) {
        this.camera.yaw -= dx * 0.0065;
        this.camera.pitch = Math.max(-0.35, Math.min(1.48, this.camera.pitch + dy * 0.006));
      }
    });

    const endDrag = (e) => {
      const wasBrushDown = this.brushDown;
      dragging = false;
      if (!e || e.button === 2) this._rmbDown = false;
      this.brushDown = false;
      if (wasBrushDown && this.pipelineMode && this.pipeline && typeof this.pipeline.finishSculptStroke === 'function') {
        this.pipeline.finishSculptStroke();
      }
    };
    el.addEventListener('pointerup', endDrag);
    el.addEventListener('pointercancel', endDrag);

    el.addEventListener(
      'wheel',
      (e) => {
        if (!this.active) return;
        e.preventDefault();
        if (this._rmbDown) {
          // Unreal Engine RMB + Scroll adjusts WASD fly speed!
          const speedMul = Math.exp(-e.deltaY * 0.0012);
          this.camera.flySpeed = Math.max(30, Math.min(900, this.camera.flySpeed * speedMul));
          return;
        }
        const factor = Math.exp(e.deltaY * 0.0011);
        this.camera.distance = Math.max(14, Math.min(1450, this.camera.distance * factor));
      },
      { passive: false }
    );

    // Unreal Engine WASD + Q/E + Shift Free-Fly Keyboard Navigation
    const flyCodes = new Set([
      'KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE',
      'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
      'ShiftLeft', 'ShiftRight',
    ]);
    window.addEventListener('keydown', (e) => {
      if (!this.active) return;
      if (e.metaKey || e.ctrlKey) return;
      const activeTag = document.activeElement ? document.activeElement.tagName : '';
      const activeType = document.activeElement ? (document.activeElement.type || '') : '';
      if (activeTag === 'TEXTAREA' || (activeTag === 'INPUT' && activeType !== 'range' && activeType !== 'checkbox')) {
        return;
      }
      if (activeTag === 'SELECT' && e.code.startsWith('Arrow')) return;
      if (flyCodes.has(e.code)) {
        if (activeTag === 'INPUT' && activeType === 'range' && e.code.startsWith('Key')) {
          document.activeElement.blur();
        }
        this._keysDown.add(e.code);
        if (e.code.startsWith('Arrow')) e.preventDefault();
      }
    });
    window.addEventListener('keyup', (e) => {
      this._keysDown.delete(e.code);
    });
    window.addEventListener('blur', () => {
      this._keysDown.clear();
      this._rmbDown = false;
    });
  }

  _stepWasdCamera(dtSec) {
    if (this._keysDown.size === 0) return;
    const k = this._keysDown;
    let moveFwd = (k.has('KeyW') || k.has('ArrowUp') ? 1 : 0) - (k.has('KeyS') || k.has('ArrowDown') ? 1 : 0);
    let moveRight = (k.has('KeyD') || k.has('ArrowRight') ? 1 : 0) - (k.has('KeyA') || k.has('ArrowLeft') ? 1 : 0);
    let moveUp = (k.has('KeyE') ? 1 : 0) - (k.has('KeyQ') ? 1 : 0);
    if (moveFwd === 0 && moveRight === 0 && moveUp === 0) return;

    const turbo = (k.has('ShiftLeft') || k.has('ShiftRight')) ? 3.2 : 1.0;
    const speed = (this.camera.flySpeed || 175.0) * turbo * Math.min(0.08, Math.max(0.001, dtSec));
    const { forward, right } = this._getCameraBasis();

    let dx = forward[0] * moveFwd + right[0] * moveRight;
    let dy = forward[1] * moveFwd + right[1] * moveRight + moveUp;
    let dz = forward[2] * moveFwd + right[2] * moveRight;
    const mag = Math.hypot(dx, dy, dz);
    if (mag > 1.0) {
      dx /= mag; dy /= mag; dz /= mag;
    }

    this.camera.target[0] += dx * speed;
    this.camera.target[1] += dy * speed;
    this.camera.target[2] += dz * speed;

    // Prevent eye from dipping below y = 4m plinth base while allowing low-altitude canyon flight!
    const { eye } = this._getCameraBasis();
    if (eye[1] < 4.0) {
      this.camera.target[1] += (4.0 - eye[1]);
    }
  }

  _raycastPlaneXZ(clientX, clientY) {
    const rect = this.canvas.getBoundingClientRect();
    if (rect.width < 2 || rect.height < 2) return null;
    const ndcX = ((clientX - rect.left) / rect.width) * 2 - 1;
    const ndcY = 1 - ((clientY - rect.top) / rect.height) * 2;

    const { eye, forward, right, up } = this._getCameraBasis();
    const aspect = rect.width / rect.height;
    const tanHalf = Math.tan((this.camera.fovDeg * Math.PI) / 360);

    let dirX = forward[0] + right[0] * ndcX * aspect * tanHalf + up[0] * ndcY * tanHalf;
    let dirY = forward[1] + right[1] * ndcX * aspect * tanHalf + up[1] * ndcY * tanHalf;
    let dirZ = forward[2] + right[2] * ndcX * aspect * tanHalf + up[2] * ndcY * tanHalf;
    const dl = Math.hypot(dirX, dirY, dirZ) || 1;
    dirX /= dl; dirY /= dl; dirZ /= dl;

    const half = this.params.worldSpan * 0.5;
    // Fast 24-step 3D terrain heightfield raymarch + binary refinement so sculpt/cave/spline hits exact cliff & ridge surfaces!
    if (this.rockCalving && this.rockCalving.snapGrid) {
      let prevT = 10.0;
      let prevDiff = 1.0;
      for (let s = 1; s <= 28; s++) {
        const t = 10.0 + (s / 28.0) * 1650.0;
        const rx = eye[0] + dirX * t;
        const ry = eye[1] + dirY * t;
        const rz = eye[2] + dirZ * t;
        if (Math.abs(rx) <= half * 1.15 && Math.abs(rz) <= half * 1.15) {
          const st = this.rockCalving.sampleTerrain(rx, rz);
          const diff = ry - st.surfaceH;
          if (diff <= 0.0 && prevDiff > 0.0) {
            const hitT = prevT + (t - prevT) * (prevDiff / Math.max(1e-4, prevDiff - diff));
            const wx = Math.max(-half, Math.min(half, eye[0] + dirX * hitT));
            const wz = Math.max(-half, Math.min(half, eye[2] + dirZ * hitT));
            return [wx, wz];
          }
          prevDiff = diff;
          prevT = t;
        }
      }
    }

    const refY = this.params.relief * 0.35;
    if (Math.abs(dirY) < 1e-4) return null;
    const t = (refY - eye[1]) / dirY;
    if (t < 0) return null;

    const wx = Math.max(-half, Math.min(half, eye[0] + dirX * t));
    const wz = Math.max(-half, Math.min(half, eye[2] + dirZ * t));
    return [wx, wz];
  }

  _getCameraBasis() {
    const { yaw, pitch, distance, target } = this.camera;
    const cp = Math.cos(pitch);
    const sp = Math.sin(pitch);
    const cy = Math.cos(yaw);
    const sy = Math.sin(yaw);

    const eye = [
      target[0] + distance * cp * sy,
      target[1] + distance * sp,
      target[2] + distance * cp * cy,
    ];
    let fx = target[0] - eye[0];
    let fy = target[1] - eye[1];
    let fz = target[2] - eye[2];
    const fl = Math.hypot(fx, fy, fz) || 1;
    fx /= fl; fy /= fl; fz /= fl;

    // right = normalize(cross(forward, worldUp))
    let rx = -fz;
    let ry = 0;
    let rz = fx;
    const rl = Math.hypot(rx, ry, rz) || 1;
    rx /= rl; rz /= rl;

    // up = cross(right, forward)
    const ux = ry * fz - rz * fy;
    const uy = rz * fx - rx * fz;
    const uz = rx * fy - ry * fx;

    return {
      eye,
      forward: [fx, fy, fz],
      right: [rx, ry, rz],
      up: [ux, uy, uz],
    };
  }

  _buildViewProjMatrix(aspect) {
    const { eye, forward, right, up } = this._getCameraBasis();
    const f = 1.0 / Math.tan((this.camera.fovDeg * Math.PI) / 360);
    const near = 2.0;
    const far = 3200.0;
    const nf = 1.0 / (near - far);

    // View matrix (column-major)
    const v0 = right[0], v4 = right[1], v8 = right[2],  v12 = -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]);
    const v1 = up[0],    v5 = up[1],    v9 = up[2],     v13 = -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]);
    const v2 = -forward[0], v6 = -forward[1], v10 = -forward[2], v14 = (forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2]);

    const p0 = f / Math.max(aspect, 0.1);
    const p5 = f;
    const p10 = (far + near) * nf;
    const p14 = 2 * far * near * nf;

    const out = new Float32Array(16);
    out[0] = p0 * v0;  out[4] = p0 * v4;  out[8]  = p0 * v8;  out[12] = p0 * v12;
    out[1] = p5 * v1;  out[5] = p5 * v5;  out[9]  = p5 * v9;  out[13] = p5 * v13;
    out[2] = p10 * v2; out[6] = p10 * v6; out[10] = p10 * v10; out[14] = p10 * v14 + p14;
    out[3] = -v2;      out[7] = -v6;      out[11] = -v10;      out[15] = -v14;
    return { viewProj: out, eye };
  }

  _themeCode() {
    const theme = Number(this.params.materialTheme);
    return Number.isFinite(theme) ? Math.max(0, Math.min(9, Math.round(theme))) : 0;
  }

  _fabricCode() {
    const fab = Number(this.params.fabricMode);
    return Number.isFinite(fab) ? Math.max(0, Math.min(4, Math.round(fab))) : 0;
  }

  _getHardnessUniforms() {
    const p = this.params;
    const strikeRad = (p.strataStrike * Math.PI) / 180;
    const dipRad = (p.strataDip * Math.PI) / 180;
    const strataStr = p.strataLayerEnabled === false ? 0.0 : p.strataStrength;
    return {
      hardnessVec: [p.rockHardness, strataStr, p.strataFreq, p.faultWeakness],
      strataVec: [Math.sin(strikeRad), Math.cos(strikeRad), Math.tan(dipRad), Math.max(40, p.relief)],
    };
  }

  /** Cached authored-bed uniform arrays shared by every geology shader. */
  _getStrataColumnUniforms() {
    const p = this.params;
    const beds = normalizeStrataColumn(p.strataBeds, p.strataColumnPreset || DEFAULT_EROSION_PARAMS.strataColumnPreset);
    const enabled = p.strataLayerEnabled !== false && p.strataColumnEnabled === true;
    const key = JSON.stringify({
      enabled,
      preset: p.strataColumnPreset,
      base: p.strataColumnBaseM,
      color: p.strataColumnColorStrength,
      beds,
    });
    if (key !== this._strataColumnCacheKey || !this._strataColumnCache) {
      this._strataColumnCacheKey = key;
      this._strataColumnCache = packStrataColumnUniforms(beds, {
        enabled,
        presetId: p.strataColumnPreset,
        baseM: p.strataColumnBaseM ?? DEFAULT_EROSION_PARAMS.strataColumnBaseM,
        colorStrength: p.strataColumnColorStrength ?? DEFAULT_EROSION_PARAMS.strataColumnColorStrength,
      });
    }
    return this._strataColumnCache;
  }

  _bindStrataColumnUniforms(program) {
    if (!program) return;
    const gl = this.gl;
    const packed = this._getStrataColumnUniforms();
    gl.uniform4fv(gl.getUniformLocation(program, 'uStrataColumnMeta'), packed.meta);
    gl.uniform4fv(gl.getUniformLocation(program, 'uStrataBedsA[0]'), packed.bedsA);
    gl.uniform4fv(gl.getUniformLocation(program, 'uStrataBedsB[0]'), packed.bedsB);
    gl.uniform4fv(gl.getUniformLocation(program, 'uStrataBedsC[0]'), packed.bedsC);
  }

  /**
   * Current location climate. During terrain-stack bakes the pipeline sets
   * `_seasonalClimateSuppressed`, returning annual-neutral forcing so a bake
   * cannot consume calendar years or depend on the live playback date.
   */
  _getSeasonalClimate(elapsedYears = this.climateElapsedYears || 0) {
    const state = seasonalClimateAt(this.params, elapsedYears, this.params.seed);
    if (!state.enabled || !this._seasonalClimateSuppressed) return state;
    return {
      ...state,
      enabled: false,
      suppressedForBake: true,
      seasonName: 'Stack bake · annual neutral',
      warmth: 0,
      astronomicalWarmth: 0,
      temperatureAnomalyC: 0,
      temperatureAmplitudeScale: 1,
      precipitationMultiplier: 1,
      rainMultiplier: 1,
      snowMultiplier: 1,
      meltMultiplier: 1,
      frostMultiplier: 1,
      moistureMultiplier: 1,
      evaporationMultiplier: 1,
      windMultiplier: 1,
      lowElevationRetention: 0.34,
      stormPulse: false,
    };
  }

  getSeasonalClimateRunState(state = this._getSeasonalClimate()) {
    const targetYears = Math.max(0, Number(state.params.climateYearsToSimulate) || 0);
    const runStartYears = Math.max(0, Number(this.climateRunStartYears) || 0);
    const runElapsedYears = Math.max(0, state.elapsedYears - runStartYears);
    const complete = state.enabled && targetYears > 0 && (this.climateRunCompleted || runElapsedYears >= targetYears - 1e-9);
    return {
      enabled: state.enabled,
      elapsedYears: state.elapsedYears,
      runStartYears,
      runElapsedYears,
      targetYears,
      targetElapsedYears: targetYears > 0 ? runStartYears + targetYears : Infinity,
      remainingYears: targetYears > 0 ? Math.max(0, targetYears - runElapsedYears) : Infinity,
      progress: targetYears > 0 ? Math.max(0, Math.min(1, runElapsedYears / targetYears)) : 0,
      active: Boolean(state.enabled && this.climateRunActive && !complete),
      complete,
    };
  }

  getSeasonalClimateState() {
    const climate = this._getSeasonalClimate();
    return { climate, run: this.getSeasonalClimateRunState(climate) };
  }

  resetSeasonalClimateClock() {
    this.climateElapsedYears = 0;
    this.climateRunStartYears = 0;
    this.climateRunActive = false;
    this.climateRunCompleted = false;
    this._syncSeasonalClimateTelemetry();
    return this.getSeasonalClimateState();
  }

  beginSeasonalClimateRun() {
    const state = this._getSeasonalClimate();
    if (!state.enabled) {
      this.climateRunActive = false;
      return this.getSeasonalClimateState();
    }
    const run = this.getSeasonalClimateRunState(state);
    if (!this.climateRunActive || run.complete) {
      this.climateRunStartYears = state.elapsedYears;
      this.climateRunCompleted = false;
    }
    this.climateRunActive = true;
    this._syncSeasonalClimateTelemetry(state);
    return this.getSeasonalClimateState();
  }

  _advanceSeasonalClimateClock() {
    const before = this._getSeasonalClimate();
    if (!before.enabled) return { climate: before, completedNow: false };
    if (!this.climateRunActive) {
      const run = this.getSeasonalClimateRunState(before);
      if (run.complete) return { climate: before, completedNow: false };
      this.climateRunActive = true;
    }
    const stepsPerYear = Math.max(1, before.params.climateStepsPerYear || 240);
    this.climateElapsedYears = Math.max(0, (this.climateElapsedYears || 0) + 1 / stepsPerYear);
    const climate = this._getSeasonalClimate();
    const run = this.getSeasonalClimateRunState(climate);
    const completedNow = Boolean(run.complete && !this.climateRunCompleted);
    if (run.complete) {
      this.climateRunCompleted = true;
      this.climateRunActive = false;
    }
    return { climate, run, completedNow };
  }

  _syncSeasonalClimateTelemetry(state = this._getSeasonalClimate()) {
    if (!this.telemetry) return state;
    const run = this.getSeasonalClimateRunState(state);
    this.telemetry.climateYear = state.year;
    this.telemetry.climateSeason = state.seasonName;
    this.telemetry.climateDate = `${state.monthName} ${state.dayOfMonth}`;
    this.telemetry.climateElapsedYears = state.elapsedYears;
    this.telemetry.climateRunElapsedYears = run.runElapsedYears;
    this.telemetry.climateTargetYears = run.targetYears;
    this.telemetry.climateRunProgress = run.progress;
    this.telemetry.climateRunComplete = run.complete;
    this.telemetry.climateRegime = state.regime.label;
    this.telemetry.climateLatitudeDeg = state.params.climateLatitudeDeg;
    this.telemetry.climateTemperatureC = state.temperatureC;
    return state;
  }

  /** Normalised Snow effect-stack weights + physical frost controls. */
  _getFrostUniforms() {
    const p = this.params;
    const frost = normalizeFrostParams(p);
    const derived = deriveSnowEffectWeights(normalizeSnowEffects(p.snowEffects, p.snowEffectPreset));
    const w = derived.weights;
    const ep = derived.params;
    const cryoPlaying = p.simCryoPlaying !== false;
    const seasonalClimate = this._getSeasonalClimate();
    const meltCycle = snowmeltCycleAtStep(derived, this.cryoStepCount || 0, seasonalClimate);
    const seasonalFrostYears = seasonalClimate.enabled
      ? Math.max(0, Math.min(12, frost.frostYearsPerStep * (0.30 + 0.70 * seasonalClimate.frostMultiplier)))
      : frost.frostYearsPerStep;
    const seasonalFrostMoisture = seasonalClimate.enabled
      ? Math.max(0, Math.min(1, frost.frostMoisture * seasonalClimate.moistureMultiplier))
      : frost.frostMoisture;
    const seasonalFrostMean = seasonalClimate.enabled
      ? frost.frostMeanTempC + seasonalClimate.temperatureAnomalyC
      : frost.frostMeanTempC;
    const seasonalFrostAmplitude = seasonalClimate.enabled
      ? frost.frostSeasonalAmplitudeC * seasonalClimate.temperatureAmplitudeScale
      : frost.frostSeasonalAmplitudeC;
    const active = cryoPlaying && derived.anyDamage;
    const sunAz = ((Number(p.sunAzimuth) || 0) * Math.PI) / 180;
    const sunEl = ((Number(p.sunElevation) || 28) * Math.PI) / 180;
    const sunHoriz = Math.max(0.12, Math.cos(sunEl));
    return {
      frost,
      derived,
      seasonalClimate,
      meltCycle,
      meltCycleVec: meltCycle.vec,
      active,
      anyWeathering: cryoPlaying && derived.anyWeathering,
      anyTransport: cryoPlaying && derived.anyTransport,
      domain: [this.gridRes, p.worldSpan / this.gridRes, p.worldSpan, this.stepCount || 0],
      climateA: [seasonalFrostMean, seasonalFrostAmplitude, seasonalFrostYears, frost.frostCrackDepthM],
      climateB: [frost.frostThermalDiffusivity, seasonalFrostMoisture, frost.frostSaturationPeak, frost.frostSnowInsulationM],
      effectsA: [w.segregation, w.volumetric, w.nivation, w.legacy],
      effectsB: [w.frostCreep, w.avalanche, w.glacierPlucking, active ? 1 : 0],
      release: [frost.frostDamageThreshold, frost.frostVisibleCrackMm, frost.frostPitDepthM, frost.frostPitScaleCells],
      effectParamsA: [ep.windowLowC, ep.windowHighC, ep.gradientSensitivity, ep.cycleSensitivity],
      effectParamsB: [ep.saturationFloor, ep.diurnalAmplitudeC, ep.optimalSnowDepthM, ep.snowDepthWidthM],
      effectParamsC: [frost.frostInitialDamage, 0, frost.frostDebrisRetention, ep.depthScale],
      sun: [Math.cos(sunAz) * sunHoriz, Math.sin(sunAz) * sunHoriz, Math.max(0, Math.sin(sunEl)), 0],
      integrateA: [cryoPlaying ? w.legacy : 0, cryoPlaying ? w.avalanche : 0, cryoPlaying ? w.glacierPlucking : 0, active ? 1 : 0],
      integrateB: [frost.frostDebrisRetention, ep.avalancheSlope, ep.avalancheScour, ep.legacyRetention],
      integrateC: [ep.glacierPlucking, ep.glacierAbrasion, ep.jointSensitivity, ep.legacyMarginRate],
      integrateD: [cryoPlaying ? w.snowmelt : 0, ep.runoffErosion, ep.sedimentCapacity, ep.infiltration],
      talusA: [p.simRockfallPlaying === false ? 0 : 1, cryoPlaying ? w.frostCreep : 0, cryoPlaying ? w.avalanche : 0, cryoPlaying ? w.glacierPlucking : 0],
      talusB: [ep.creepRate, ep.creepMinSlope, ep.coarseMobility, ep.avalancheEntrainment],
      talusC: [seasonalFrostYears, ep.avalancheSlope, ep.meltwashExport, cryoPlaying && derived.anyTransport ? 1 : 0],
      talusD: [cryoPlaying ? w.snowmelt : 0, ep.sedimentCapacity, ep.runoffErosion, meltCycle.meltMultiplier],
    };
  }

  _clearFrostWeathering() {
    const gl = this.gl;
    gl.viewport(0, 0, this.gridRes, this.gridRes);
    for (let i = 0; i < 2; i++) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboFrost[i]);
      gl.clearColor(0, 0.05, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this.frostIdx = 0;
  }

  _runFrostPass(initialise = false) {
    const frostU = this._getFrostUniforms();
    if (!initialise && !frostU.active) return false;
    const gl = this.gl;
    const R = this.gridRes;
    const p = this.params;
    const { hardnessVec, strataVec } = this._getHardnessUniforms();
    const climate = this._getClimateUniforms();
    const next = 1 - this.frostIdx;
    const prev = this.frostIdx;

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboFrost[next]);
    gl.viewport(0, 0, R, R);
    gl.useProgram(this.progFrostDamage);
    gl.bindVertexArray(this.emptyVao);
    this._bindTex(this.progFrostDamage, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
    this._bindTex(this.progFrostDamage, 'uSnowTex', this.snowTex[this.snowIdx], 1);
    this._bindTex(this.progFrostDamage, 'uFrostTex', this.frostTex[prev], 2);
    this._bindTex(this.progFrostDamage, 'uChemTex', this.chemTex[this.chemIdx], 3);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostDomain'), frostU.domain);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostClimateA'), frostU.climateA);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostClimateB'), frostU.climateB);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostEffectsA'), frostU.effectsA);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostEffectsB'), frostU.effectsB);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostRelease'), frostU.release);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostEffectParamsA'), frostU.effectParamsA);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostEffectParamsB'), frostU.effectParamsB);
    const paramsC = [...frostU.effectParamsC];
    paramsC[1] = initialise ? 1 : 0;
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostEffectParamsC'), paramsC);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uFrostSun'), frostU.sun);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uClimate'), climate.snowVec);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uHardnessParams'), hardnessVec);
    gl.uniform4fv(gl.getUniformLocation(this.progFrostDamage, 'uStrataOrient'), strataVec);
    this._bindStrataColumnUniforms(this.progFrostDamage);
    gl.uniform1f(gl.getUniformLocation(this.progFrostDamage, 'uSeed'), p.seed);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    this.frostIdx = next;
    return true;
  }

  /** Clears and spatially pre-ages sub-resolution cracks for a fresh stack. */
  seedFrostWeathering() {
    this._clearFrostWeathering();
    const frostU = this._getFrostUniforms();
    if (!frostU.derived.anyDamage || frostU.frost.frostInitialDamage <= 0.0005) return;
    this._runFrostPass(true);
  }

  _getLayerVisUniforms() {
    const p = this.params;
    return [
      p.lithologyLayerEnabled === false ? 0.0 : (p.lithologyOpacity ?? 1.0),
      p.fabricLayerEnabled === false ? 0.0 : (p.fabricStrength ?? 1.0),
      (p.snowLayerEnabled === false || p.showSnow === false) ? 0.0 : (p.snowVisOpacity ?? 1.0),
      (p.waterLayerEnabled === false || !p.showWater) ? 0.0 : (p.waterVisOpacity ?? 1.0),
    ];
  }

  /** Per-species particle visibility for PARTICLE_RENDER_VERT (rain, snow, wind sand, rockfall). */
  _getSpeciesVisUniform() {
    const p = this.params;
    const seasonalClimate = this._getSeasonalClimate();
    const snowDerived = deriveSnowEffectWeights(normalizeSnowEffects(p.snowEffects, p.snowEffectPreset));
    const meltCycle = snowmeltCycleAtStep(snowDerived, this.cryoStepCount || 0, seasonalClimate);
    const rainWeather = !seasonalClimate.enabled
      || (Number(p.rainIntensity) || 0) * seasonalClimate.rainMultiplier >= 0.12;
    const snowRate = Math.max(0, Math.min(1, Number(p.snowfallRate) || 0));
    const snowWeather = seasonalClimate.enabled
      ? snowRate * meltCycle.snowfallMultiplier >= 0.05
      : snowRate * meltCycle.snowfallMultiplier > 0.001;
    // Visibility toggles and season gates are separate controls, but a
    // frozen season's particles should not remain visible as if its weather
    // were still falling. Snow-only therefore hides rain/wind/rockfall
    // particles even before the step shader recycles their slots. The same
    // cutoff prevents a trace of arid-climate precipitation from rendering
    // as a full particle storm after species normalisation.
    return [
      p.showRain === false || p.simHydraulicPlaying === false || !rainWeather ? 0.0 : 1.0,
      p.showSnow === false || p.simCryoPlaying === false || !snowWeather ? 0.0 : 1.0,
      p.simWindPlaying === false ? 0.0 : 1.0,
      p.simRockfallPlaying === false ? 0.0 : 1.0,
    ];
  }

  /**
   * Derives the multi-agent particle species mix (rain / snow / wind / rockfall) plus the
   * wind vector from the active emitter mode and climate sliders.
   */
  _getClimateUniforms() {
    const p = this.params;
    const clamp01 = (v) => Math.max(0, Math.min(1, Number(v) || 0));
    const snowPossible = clamp01(p.snowfallRate) * (1.0 - 0.65 * clamp01((p.snowline - 0.25) / 0.85));

    let rain = 1, snow = 0, wind = 0, rock = 0;
    if (p.emitterMode === 'glacial-blizzard') {
      rain = 0.20 + 0.25 * (1.0 - snowPossible);
      snow = 0.95 + 0.45 * snowPossible;
      wind = 0.22 + 0.30 * clamp01(p.windSpeed);
      rock = 0.05 + 0.14 * clamp01(p.rockfallRate);
    } else if (p.emitterMode === 'aeolian-wind') {
      rain = (p.dryAeolianMode || 0) > 0.35 ? 0.0 : 0.05;
      snow = 0.05 * snowPossible;
      wind = 1.05 + 0.45 * clamp01(p.windSpeed);
      rock = (p.rockfallRate || 0) > 0.28 ? (0.06 + 0.20 * clamp01(p.rockfallRate)) : 0.0;
    } else if (p.emitterMode === 'rockfall-avalanche') {
      rain = (p.dryAeolianMode || 0) > 0.5 ? 0.0 : 0.05;
      snow = 0.03 * snowPossible;
      wind = p.windSpeed > 0.02 ? 0.10 : 0.0;
      rock = 1.00 + 0.65 * clamp01(p.rockfallRate);
    } else if (p.emitterMode === 'storm-cell') {
      rain = 0.90;
      snow = 0.12 * snowPossible;
      wind = 0.14;
      rock = 0.03;
    } else if (p.emitterMode === 'sky-rain') {
      rain = 1.0;
      snow = 0.0;
      wind = 0.0;
      rock = 0.0;
    } else {
      // 'all-weather' multi-agent synchronized mix
      rain = 0.58;
      snow = 0.60 * snowPossible;
      wind = 0.22 + 0.34 * clamp01(p.windSpeed);
      rock = 0.14 + 0.30 * clamp01(p.rockfallRate);
    }

    // A dedicated Wind Erosion layer must emit saltating grains even when a
    // legacy emitter preset (e.g. sky-rain) originally supplied none.
    if (p.simWindPlaying !== false && (p.windSpeed ?? 0) > 0.03) {
      wind = Math.max(wind, 0.18 + 0.55 * clamp01(p.windSpeed) * (0.45 + 0.55 * clamp01(p.windSandSupply ?? DEFAULT_EROSION_PARAMS.windSandSupply)));
    }

    // Active Snowpack & Frost must emit visible falling snow whenever
    // snowfallRate > 0, both when soloed AND when running alongside Hydraulic
    // or Wind layers (even if the preset emitterMode was 'sky-rain'/'storm-cell').
    if (p.simCryoPlaying !== false && clamp01(p.snowfallRate) > 0.001) {
      const snowFloor = p.simHydraulicPlaying === false
        ? 0.85
        : 0.48 + 0.44 * clamp01(p.snowfallRate);
      snow = Math.max(snow, snowFloor);
    }

    // Independent Simulation Layer Play/Pause Gating: allows running all active simulations
    // simultaneously OR soloing/pausing Hydraulic, Cryo, Wind, or Rockfall one at a time!
    if (p.simHydraulicPlaying === false) rain = 0.0;
    if (p.simCryoPlaying === false) snow = 0.0;
    if (p.simWindPlaying === false) wind = 0.0;
    if (p.simRockfallPlaying === false) rock = 0.0;
    // A Cryo season may be running only for frost damage or for melting an
    // existing glacier. Zero snowfall means zero new snow particles; the
    // glacial-blizzard emitter label alone must not create precipitation.
    if (clamp01(p.snowfallRate) <= 0.001) snow = 0.0;

    // One shared location/season phase drives visible precipitation,
    // Eulerian snowfall, low-elevation retention and finite melt. Applying
    // it here prevents particle snow at full rate during summer and prevents
    // winter precipitation in an alpine climate from appearing as rain.
    const seasonalClimate = this._getSeasonalClimate();
    const snowDerived = deriveSnowEffectWeights(normalizeSnowEffects(p.snowEffects, p.snowEffectPreset));
    const meltCycle = snowmeltCycleAtStep(snowDerived, this.cryoStepCount || 0, seasonalClimate);
    if (p.simCryoPlaying !== false) snow *= meltCycle.snowfallMultiplier;
    if (seasonalClimate.enabled) {
      if (p.simHydraulicPlaying !== false) rain *= seasonalClimate.rainMultiplier;
      if (p.simWindPlaying !== false) wind *= 0.35 + 0.65 * seasonalClimate.windMultiplier;
      // Species weights are normalised below. Without an absolute cutoff a
      // trace precipitation multiplier in a desert dry spell could become a
      // 100%-visible rain/snow storm simply because it is the only process.
      if ((Number(p.rainIntensity) || 0) * seasonalClimate.rainMultiplier < 0.12) rain = 0.0;
      if (clamp01(p.snowfallRate) * meltCycle.snowfallMultiplier < 0.05) snow = 0.0;
    }

    const rawSum = rain + snow + wind + rock;
    const sum = Math.max(1e-4, rawSum);
    // Slow weather regime: the large-scale wind persists through a sector
    // spell, then veers/backs smoothly through intervening bearings into
    // the next spell (variable wind rose / fronts / equal full rose). Local
    // moving gusts and terrain wrap in the shaders act around this vector.
    const windCycles = windCyclesAtStep(p, this.stepCount || 0);
    const effectiveWindAzimuth = effectiveWindAzimuthAtStep(p, this.stepCount || 0);
    const azRad = (effectiveWindAzimuth * Math.PI) / 180;
    const activeWind = p.simWindPlaying === false
      ? 0.0
      : clamp01(clamp01(p.windSpeed) * (seasonalClimate.enabled ? 0.45 + 0.55 * seasonalClimate.windMultiplier : 1));
    const windMag = 0.35 + 0.65 * activeWind;
    let activeSnow = p.simCryoPlaying === false ? 0.0 : clamp01(clamp01(p.snowfallRate) * meltCycle.snowfallMultiplier);
    // Keep a much lower Eulerian cutoff than the visible-flake cutoff: a
    // warm valley reference temperature can still mean a trace of wet-season
    // snow on the highest terrain, where the Cryo shader's altitude
    // retention/melt lapse confines it. Desert snow propensity remains far
    // below this floor unless a Snow layer deliberately supplies snowfall.
    if (seasonalClimate.enabled && activeSnow < 0.004) activeSnow = 0.0;
    const effectiveRainIntensity = p.simHydraulicPlaying === false
      ? 0.0
      : Math.max(0, Math.min(4, (Number(p.rainIntensity) || 0) * (seasonalClimate.enabled ? seasonalClimate.rainMultiplier : 1)));
    const effectiveEvaporation = (p.simHydraulicPlaying === false && p.simCryoPlaying === false)
      ? 0.0
      : Math.max(0, Math.min(1, (Number(p.evaporation) || 0) * (seasonalClimate.enabled ? seasonalClimate.evaporationMultiplier : 1)));
    return {
      anySimActive: rawSum > 1e-4 || p.simHydraulicPlaying !== false || p.simCryoPlaying !== false || p.simWindPlaying !== false || p.simRockfallPlaying !== false || p.simChemicalPlaying !== false,
      anyParticleSpecies: rawSum > 1e-4,
      speciesVec: rawSum > 1e-4 ? [rain / sum, snow / sum, wind / sum, rock / sum] : [0, 0, 0, 0],
      snowVec: [p.snowline, activeSnow, p.simCryoPlaying === false ? 0.0 : clamp01(p.glacierStrength), 0.35 + 0.9 * activeSnow],
      seasonalClimate,
      effectiveRainIntensity,
      effectiveEvaporation,
      effectiveWindSpeed: activeWind,
      meltCycle,
      meltCycleVec: meltCycle.vec,
      windVec: [Math.cos(azRad) * windMag, Math.sin(azRad) * windMag, p.simWindPlaying === false ? 0.0 : (0.25 + 0.95 * activeWind), 0.35 + 0.65 * activeWind],
      windAzimuthDeg: effectiveWindAzimuth,
      windShiftOffsetDeg: windShiftOffsetDeg(p, windCycles),
      windCycles,
      materialTheme: Number.isFinite(p.materialTheme) ? p.materialTheme : 0,
    };
  }

  /**
   * Dedicated Wind Erosion uniforms shared by particle saltation and the
   * terrain integrator:
   *   A = (abrasion rate incl. gust phase, lee deposition, turbulence, curtain)
   *   B = (sand supply, vertical fluting, streamlining/shelter, deflation)
   */
  _getWindErosionUniforms() {
    const p = this.params;
    const clamp = (v, lo, hi, fb) => {
      const n = Number(v ?? fb);
      return Number.isFinite(n) ? Math.max(lo, Math.min(hi, n)) : fb;
    };
    const active = p.simWindPlaying === false ? 0.0 : 1.0;
    const gustiness = clamp(p.windGustiness, 0, 1, DEFAULT_EROSION_PARAMS.windGustiness);
    const gustPhase = 1.0 + gustiness * 0.38 * Math.sin((this.stepCount || 0) * 0.17 + (p.seed || 0) * 0.001);
    const seasonalClimate = this._getSeasonalClimate();
    const seasonalWind = seasonalClimate.enabled
      ? Math.max(0.12, Math.min(1.65, 0.45 + 0.55 * seasonalClimate.windMultiplier))
      : 1;
    const abrasion = active
      * clamp(p.windErosionRate, 0, 1.5, DEFAULT_EROSION_PARAMS.windErosionRate)
      * clamp(p.windAbrasion, 0, 1.5, DEFAULT_EROSION_PARAMS.windAbrasion)
      * gustPhase
      * seasonalWind;
    return {
      a: [
        Math.max(0, Math.min(2.4, abrasion)),
        clamp(p.windDeposition, 0, 1.5, DEFAULT_EROSION_PARAMS.windDeposition),
        clamp(p.windTurbulence, 0, 1, DEFAULT_EROSION_PARAMS.windTurbulence),
        clamp(p.windCurtainHeight, 0.03, 1, DEFAULT_EROSION_PARAMS.windCurtainHeight),
      ],
      b: [
        clamp(p.windSandSupply, 0, 1.2, DEFAULT_EROSION_PARAMS.windSandSupply),
        clamp(p.windFluting, 0, 1, DEFAULT_EROSION_PARAMS.windFluting),
        clamp(p.windStreamlining, 0, 1, DEFAULT_EROSION_PARAMS.windStreamlining),
        clamp(p.windDeflation, 0, 1.5, DEFAULT_EROSION_PARAMS.windDeflation),
      ],
    };
  }

  applyLandscapePreset(landscapeId) {
    const prevWasLunar = this.params.landscape === 'lunar-mare-craters';
    const preset = EROSION_LANDSCAPES[landscapeId] || EROSION_LANDSCAPES['layered-plateau'];
    const keep = {
      seed: this.params.seed,
      particleCount: this.params.particleCount,
      substepsPerFrame: this.params.substepsPerFrame,
      showParticles: this.params.showParticles,
      showWater: prevWasLunar ? true : this.params.showWater,
      showOutcrops: this.params.showOutcrops,
      viewMode: this.params.viewMode,
      brushMode: this.params.brushMode,
      brushRadius: this.params.brushRadius,
      simHydraulicPlaying: this.params.simHydraulicPlaying,
      simCryoPlaying: this.params.simCryoPlaying,
      simWindPlaying: this.params.simWindPlaying,
      simRockfallPlaying: this.params.simRockfallPlaying,
      simChemicalPlaying: this.params.simChemicalPlaying,
    };
    const vegDefaults = getVegetationDefaultsForLandscape(preset.id);
    Object.assign(this.params, DEFAULT_EROSION_PARAMS, vegDefaults, preset.defaults, keep);
    Object.assign(this.params, climateDefaultsForLandscape(preset.id));
    this.params.proceduralModifiers = [];
    this.params.snowEffects = createSnowEffectPreset(DEFAULT_EROSION_PARAMS.snowEffectPreset).effects;
    this.params.strataBeds = createStrataColumnPreset(DEFAULT_EROSION_PARAMS.strataColumnPreset);
    this.params.materialTheme = preset.defaults.materialTheme ?? preset.materialTheme ?? 0;
    this.params.fabricMode = preset.defaults.fabricMode ?? preset.fabricMode ?? 0;
    if (preset.defaults.showWater !== undefined) {
      this.params.showWater = preset.defaults.showWater;
    }
    this.params.landscape = preset.id;
    this.resetSimulation();
  }

  resetCamera() {
    this.camera.yaw = 0.42;
    this.camera.pitch = 0.46;
    this.camera.distance = 740.0;
    this.camera.target = [0, this.params.relief * 0.30, 0];
  }

  resetSimulation() {
    const gl = this.gl;
    const R = this.gridRes;
    const p = this.params;
    this.resetSeasonalClimateClock();
    const preset = EROSION_LANDSCAPES[p.landscape] || EROSION_LANDSCAPES['layered-plateau'];

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.bindVertexArray(this.emptyVao);

    // 1. Generate Initial Bedrock Terrain into terrainTex[0] AND initialTerrainTex
    gl.useProgram(this.progInitTerrain);
    gl.uniform4f(
      gl.getUniformLocation(this.progInitTerrain, 'uInitParams'),
      p.relief,
      p.roughness,
      preset.terrainType,
      p.seed
    );
    gl.uniform1f(gl.getUniformLocation(this.progInitTerrain, 'uWorldSpan'), p.worldSpan);

    gl.viewport(0, 0, R, R);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTerrainSingle[0]);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    this.terrainIdx = 0;

    // 1B. Non-destructively evaluate any user-added Procedural Shape Modifier Layers!
    // When proceduralModifiers is empty (default for all 19 presets), zero passes run -> 100% bit-exact output!
    const mods = Array.isArray(p.proceduralModifiers) ? p.proceduralModifiers : [];
    for (let m = 0; m < mods.length; m++) {
      const mod = mods[m];
      if (!mod || mod.enabled === false || (mod.strength ?? 1.0) <= 0.001) continue;
      const nextT = 1 - this.terrainIdx;
      gl.useProgram(this.progProceduralMod);
      this._bindTex(this.progProceduralMod, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
      gl.uniform4f(
        gl.getUniformLocation(this.progProceduralMod, 'uModShape'),
        mod.amplitude ?? 48.0,
        mod.scale ?? 2.2,
        mod.detail ?? 0.55,
        mod.angle ?? 25.0
      );
      gl.uniform4f(
        gl.getUniformLocation(this.progProceduralMod, 'uModBlend'),
        mod.strength ?? 0.85,
        mod.blendMode ?? 0,
        mod.maskType ?? 0,
        mod.seed ?? 4029
      );
      gl.uniform4f(
        gl.getUniformLocation(this.progProceduralMod, 'uModMask'),
        mod.maskLow ?? 0.0,
        mod.maskHigh ?? 1.0,
        mod.maskInvert ? 1.0 : 0.0,
        mod.kind ?? 0
      );
      gl.uniform1f(gl.getUniformLocation(this.progProceduralMod, 'uWorldSpan'), p.worldSpan);
      gl.uniform1f(gl.getUniformLocation(this.progProceduralMod, 'uGridRes'), R);
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTerrainSingle[nextT]);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      this.terrainIdx = nextT;
    }

    // Copy final initial terrain into initialTerrainTex for Cut/Fill telemetry & view mode
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.fboTerrainSingle[this.terrainIdx]);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, this.fboInitialTerrain);
    gl.blitFramebuffer(0, 0, R, R, 0, 0, R, R, gl.COLOR_BUFFER_BIT, gl.NEAREST);
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);

    // 2. Clear shallow-water flux & seed initial mountain snowpack + valley glaciers (`progInitSnow`)
    for (let i = 0; i < 2; i++) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboFlux[i]);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    this.fluxIdx = 0;

    const { strataVec } = this._getHardnessUniforms();
    const climate = this._getClimateUniforms();
    const cellSize = p.worldSpan / R;
    gl.viewport(0, 0, R, R);
    gl.useProgram(this.progInitSnow);
    this._bindTex(this.progInitSnow, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
    gl.uniform4f(gl.getUniformLocation(this.progInitSnow, 'uCryoParams'), R, cellSize, 0.085, p.worldSpan);
    gl.uniform4fv(gl.getUniformLocation(this.progInitSnow, 'uClimate'), [p.snowline, Math.max(0, Math.min(1, p.snowfallRate || 0)), Math.max(0, Math.min(1, p.glacierStrength || 0)), 0.8]);
    gl.uniform4fv(gl.getUniformLocation(this.progInitSnow, 'uStrataOrient'), strataVec);
    gl.uniform1f(gl.getUniformLocation(this.progInitSnow, 'uSeed'), p.seed);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboSnowClear[0]);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboSnowClear[1]);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    this.snowIdx = 0;
    this.seedDefaultMineralField();
    this.seedChemicalWeathering();
    this.seedFrostWeathering();

    // 3. Spawn staggered sky raindrops across all 32,768 GPU particle slots
    const emitter = EMITTER_MODES[p.emitterMode] || EMITTER_MODES['sky-rain'];
    gl.viewport(0, 0, PART_TEX_W, PART_TEX_H);
    gl.useProgram(this.progInitParticles);
    gl.uniform4f(
      gl.getUniformLocation(this.progInitParticles, 'uSpawnBox'),
      p.worldSpan,
      p.cloudHeight,
      p.seed,
      emitter.code
    );
    gl.uniform4fv(gl.getUniformLocation(this.progInitParticles, 'uSpeciesMix'), this._getClimateUniforms().speciesVec);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboInitParticles[0]);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    this.partIdx = 0;

    // 4. Capture 64x64 initial terrain snapshot & seed 3D cliff outcrops + fallen boulders!
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTelemetry);
    gl.viewport(0, 0, TELEMETRY_RES, TELEMETRY_RES);
    gl.useProgram(this.progTelemetry);
    this._bindTex(this.progTelemetry, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
    gl.uniform1f(gl.getUniformLocation(this.progTelemetry, 'uGridRes'), this.gridRes);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.readPixels(0, 0, TELEMETRY_RES, TELEMETRY_RES, gl.RGBA, gl.FLOAT, this.initialSnap64);
    this._readSnowSnapshot64();

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.bindVertexArray(null);

    this.rockCalving.updateTerrainSnapshot(this.initialSnap64, TELEMETRY_RES, p.worldSpan, this.snowSnap64);
    this.rockCalving.seedForLandscape(p);

    let initWaterSum = 0;
    for (let i = 0; i < this.initialSnap64.length; i += 4) {
      initWaterSum += this.initialSnap64[i + 3];
    }
    const cellAreaSample = (p.worldSpan * p.worldSpan) / (TELEMETRY_RES * TELEMETRY_RES);

    this.stepCount = 0;
    this.cryoStepCount = 0;
    this.resetSeasonalClimateClock();
    this._obstBuiltStep = -999; // force obstacle rebuild for the new landscape
    this.telemetry.steps = 0;
    this.telemetry.erodedVolumeM3 = 0;
    this.telemetry.coarseSedM3 = 0;
    this.telemetry.fineSedM3 = 0;
    this.telemetry.waterVolumeM3 = Math.round(initWaterSum * cellAreaSample);
    this.telemetry.boulders = this.rockCalving.rocks.length;
    this.telemetry.particles = p.particleCount;
    if (this.onTelemetry) this.onTelemetry(this.telemetry);
  }

  /**
   * Seeds initial mountain snowpack & valley glaciers (`progInitSnow`) onto `snowTex[snowIdx]`.
   */
  seedInitialSnow() {
    const gl = this.gl;
    const R = this.gridRes;
    const p = this.params;
    const { strataVec } = this._getHardnessUniforms();
    const cellSize = p.worldSpan / R;

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.viewport(0, 0, R, R);
    gl.useProgram(this.progInitSnow);
    gl.bindVertexArray(this.emptyVao);
    this._bindTex(this.progInitSnow, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
    gl.uniform4f(gl.getUniformLocation(this.progInitSnow, 'uCryoParams'), R, cellSize, 0.085, p.worldSpan);
    gl.uniform4fv(
      gl.getUniformLocation(this.progInitSnow, 'uClimate'),
      [p.snowline, Math.max(0, Math.min(1, p.snowfallRate || 0)), Math.max(0, Math.min(1, p.glacierStrength || 0)), 0.8]
    );
    gl.uniform4fv(gl.getUniformLocation(this.progInitSnow, 'uStrataOrient'), strataVec);
    gl.uniform1f(gl.getUniformLocation(this.progInitSnow, 'uSeed'), p.seed);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboSnowClear[this.snowIdx]);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  /** Clears the sedimentary mineral source field (no layer-baked Fe/Cu/carbonate/salt). */
  _clearMineralField() {
    const gl = this.gl;
    gl.viewport(0, 0, this.gridRes, this.gridRes);
    for (let i = 0; i < 2; i++) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboMineral[i]);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this.mineralIdx = 0;
    this.params.mineralFieldActive = 0.0;
  }

  /**
   * Saturatingly accumulates one sedimentary / stratigraphic layer's mineral
   * composition into the source field, gated by that layer's dipping beds
   * and target horizon. Called by the stack pipeline for every enabled
   * stratigraphic-beds layer (specific layers -> specific mineral horizons).
   */
  _accumulateMineralStrata(comp = {}, band = {}, hardnessVec = null, strataVec = null, seed = null) {
    const gl = this.gl;
    const R = this.gridRes;
    const p = this.params;
    const hu = this._getHardnessUniforms();
    const hv = hardnessVec || hu.hardnessVec;
    const sv = strataVec || hu.strataVec;
    const clamp01 = (v, fb) => {
      const n = Number(v ?? fb);
      return Number.isFinite(n) ? Math.max(0, Math.min(1, n)) : 0;
    };
    const next = 1 - this.mineralIdx;
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboMineral[next]);
    gl.viewport(0, 0, R, R);
    gl.useProgram(this.progMineralAccum);
    gl.bindVertexArray(this.emptyVao);
    this._bindTex(this.progMineralAccum, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
    this._bindTex(this.progMineralAccum, 'uMineralTex', this.mineralTex[this.mineralIdx], 1);
    gl.uniform4f(
      gl.getUniformLocation(this.progMineralAccum, 'uMineralComp'),
      clamp01(comp.iron, p.mineralIron ?? DEFAULT_EROSION_PARAMS.mineralIron),
      clamp01(comp.copper, p.mineralCopper ?? DEFAULT_EROSION_PARAMS.mineralCopper),
      clamp01(comp.carbonate, p.mineralCarbonate ?? DEFAULT_EROSION_PARAMS.mineralCarbonate),
      clamp01(comp.salt, p.mineralSalt ?? DEFAULT_EROSION_PARAMS.mineralSalt)
    );
    gl.uniform4f(
      gl.getUniformLocation(this.progMineralAccum, 'uMineralBand'),
      clamp01(band.horizon, p.mineralHorizon ?? DEFAULT_EROSION_PARAMS.mineralHorizon),
      Math.max(0.03, Number(band.horizonWidth ?? p.mineralHorizonWidth ?? DEFAULT_EROSION_PARAMS.mineralHorizonWidth) || 0.55),
      clamp01(band.bandStrength, p.mineralBandStrength ?? DEFAULT_EROSION_PARAMS.mineralBandStrength),
      clamp01(band.opacity, 1.0)
    );
    gl.uniform4fv(gl.getUniformLocation(this.progMineralAccum, 'uHardnessParams'), hv);
    gl.uniform4fv(gl.getUniformLocation(this.progMineralAccum, 'uStrataOrient'), sv);
    this._bindStrataColumnUniforms(this.progMineralAccum);
    gl.uniform4f(gl.getUniformLocation(this.progMineralAccum, 'uMineralDomain'), R, p.worldSpan, seed ?? p.seed, 0);
    gl.uniform1f(gl.getUniformLocation(this.progMineralAccum, 'uSeed'), seed ?? p.seed);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this.mineralIdx = next;
    this.params.mineralFieldActive = 1.0;
  }

  /** Standalone-mode default mineral field from the global mineral sliders. */
  seedDefaultMineralField() {
    this._clearMineralField();
    if (this._themeCode() === 7) return;
    this._accumulateMineralStrata({}, {}, null, null, this.params.seed);
  }

  /** Master + per-process chemical reaction rates (zero on the airless lunar theme). */
  _chemRates() {
    const p = this.params;
    const rate = (value, fallback) => {
      const n = Number(value ?? fallback);
      return Number.isFinite(n) ? Math.max(0, n) : 0;
    };
    const master = this._themeCode() === 7 ? 0 : rate(p.chemRate, DEFAULT_EROSION_PARAMS.chemRate);
    const seasonalClimate = this._getSeasonalClimate();
    const wetScale = seasonalClimate.enabled
      ? Math.max(0.12, Math.min(1.8, 0.35 + 0.65 * seasonalClimate.moistureMultiplier))
      : 1;
    const dryScale = seasonalClimate.enabled
      ? Math.max(0.18, Math.min(2.2, 0.45 + 0.55 * seasonalClimate.evaporationMultiplier))
      : 1;
    return {
      master,
      rust: master * rate(p.chemRust, DEFAULT_EROSION_PARAMS.chemRust) * wetScale,
      hydrolysis: master * rate(p.chemHydrolysis, DEFAULT_EROSION_PARAMS.chemHydrolysis) * wetScale,
      dissolution: master * rate(p.chemDissolution, DEFAULT_EROSION_PARAMS.chemDissolution) * wetScale,
      salt: master * rate(p.chemSalt, DEFAULT_EROSION_PARAMS.chemSalt) * dryScale,
      varnish: master * rate(p.chemVarnish, DEFAULT_EROSION_PARAMS.chemVarnish) * dryScale,
      copper: master * rate(p.chemCopper, DEFAULT_EROSION_PARAMS.chemCopper) * wetScale,
    };
  }

  /** Clears both chemical-state buffers (pristine, unweathered terrain). */
  _clearChemicalWeathering() {
    const gl = this.gl;
    gl.viewport(0, 0, this.gridRes, this.gridRes);
    for (let i = 0; i < 2; i++) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboChem[i]);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    this.chemIdx = 0;
  }

  /**
   * One CHEM_WEATHER_FRAG reaction pass. `flowScale` is 0 while pre-ageing a
   * freshly generated landscape so a stale discharge field cannot stain it.
   */
  _runChemPass(dtChem, flowScale = 1.0) {
    const rates = this._chemRates();
    if (rates.master <= 0.0001) return;
    const gl = this.gl;
    const R = this.gridRes;
    const p = this.params;
    const cellSize = p.worldSpan / R;
    const { hardnessVec, strataVec } = this._getHardnessUniforms();
    const climate = this._getClimateUniforms();
    const rainWet = p.simHydraulicPlaying === false
      ? 0.0
      : Math.max(0, Math.min(1.5, p.rainIntensity ?? DEFAULT_EROSION_PARAMS.rainIntensity));
    const nextChem = 1 - this.chemIdx;

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboChem[nextChem]);
    gl.viewport(0, 0, R, R);
    gl.useProgram(this.progChemWeather);
    gl.bindVertexArray(this.emptyVao);

    this._bindTex(this.progChemWeather, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
    this._bindTex(this.progChemWeather, 'uChemTex', this.chemTex[this.chemIdx], 1);
    this._bindTex(this.progChemWeather, 'uSnowTex', this.snowTex[this.snowIdx], 2);
    this._bindTex(this.progChemWeather, 'uVelocityFieldTex', this.velocityFieldTex, 3);
    this._bindTex(this.progChemWeather, 'uMineralTex', this.mineralTex[this.mineralIdx], 4);

    gl.uniform4f(gl.getUniformLocation(this.progChemWeather, 'uChemDomain'), R, cellSize, dtChem, p.worldSpan);
    gl.uniform4f(
      gl.getUniformLocation(this.progChemWeather, 'uChemRates'),
      rates.rust,
      rates.hydrolysis,
      rates.salt,
      rates.varnish
    );
    gl.uniform4f(
      gl.getUniformLocation(this.progChemWeather, 'uChemEnv'),
      rates.copper,
      rainWet,
      p.evaporation ?? DEFAULT_EROSION_PARAMS.evaporation,
      p.dryAeolianMode ?? 0.0
    );
    gl.uniform4fv(gl.getUniformLocation(this.progChemWeather, 'uHardnessParams'), hardnessVec);
    gl.uniform4fv(gl.getUniformLocation(this.progChemWeather, 'uStrataOrient'), strataVec);
    this._bindStrataColumnUniforms(this.progChemWeather);
    gl.uniform4fv(gl.getUniformLocation(this.progChemWeather, 'uClimate'), climate.snowVec);
    gl.uniform1f(gl.getUniformLocation(this.progChemWeather, 'uSeed'), p.seed);
    gl.uniform1f(gl.getUniformLocation(this.progChemWeather, 'uChemFlowScale'), flowScale);
    gl.uniform1f(gl.getUniformLocation(this.progChemWeather, 'uMineralFieldActive'), p.mineralFieldActive ?? 0.0);

    gl.drawArrays(gl.TRIANGLES, 0, 3);
    this.chemIdx = nextChem;
  }

  /**
   * Clears and pre-ages the chemical field so a new landscape opens with
   * mature rust/patina/salt/varnish/alteration rather than pristine rock.
   * The reaction ODEs are saturating exponentials, so three long-dt passes
   * are unconditionally stable.
   */
  seedChemicalWeathering() {
    this._clearChemicalWeathering();
    if (this.params.simChemicalPlaying === false) return;
    for (let i = 0; i < 3; i++) {
      this._runChemPass(100.0, 0.0);
    }
  }

  /**
   * Executes `passes` iterations of conservative `THERMAL_TALUS_FRAG` (`progThermalTalus`).
   */
  stepThermalTalusOnly(passes = 1) {
    const gl = this.gl;
    const R = this.gridRes;
    const p = this.params;
    const cellSize = p.worldSpan / R;
    const { hardnessVec, strataVec } = this._getHardnessUniforms();
    this._ensureRockObstacles();

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.viewport(0, 0, R, R);
    gl.useProgram(this.progThermalTalus);
    gl.bindVertexArray(this.emptyVao);
    gl.uniform4f(gl.getUniformLocation(this.progThermalTalus, 'uTalusParams'), R, cellSize, p.talusAngle, p.worldSpan);
    gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uHardnessParams'), hardnessVec);
    gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uStrataOrient'), strataVec);
    this._bindStrataColumnUniforms(this.progThermalTalus);
    const frostU = this._getFrostUniforms();
    const talusA = [...frostU.talusA];
    talusA[0] = 1;
    gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uFrostTalusA'), talusA);
    gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uFrostTalusB'), frostU.talusB);
    gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uFrostTalusC'), frostU.talusC);
    gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uFrostTalusD'), frostU.talusD);
    gl.uniform1f(gl.getUniformLocation(this.progThermalTalus, 'uDryAeolian'), p.dryAeolianMode || 0.0);
    gl.uniform1f(gl.getUniformLocation(this.progThermalTalus, 'uSeed'), p.seed);

    for (let i = 0; i < passes; i++) {
      const nextTalus = 1 - this.terrainIdx;
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTerrainSingle[nextTalus]);
      this._bindTex(this.progThermalTalus, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
      this._bindTex(this.progThermalTalus, 'uRockObstacleTex', this.obstacleTex[this.obstacleIdx], 1);
      this._bindTex(this.progThermalTalus, 'uSnowTex', this.snowTex[this.snowIdx], 2);
      this._bindTex(this.progThermalTalus, 'uFrostTex', this.frostTex[this.frostIdx], 3);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      this.terrainIdx = nextTalus;
    }
  }

  /**
   * Executes `count` full GPU erosion + shallow-water + sedimentation + thermal talus substeps!
   */
  stepSimulation(count = 1, opts = {}) {
    const gl = this.gl;
    const R = this.gridRes;
    const p = this.params;
    const dt = 0.085;
    const cellSize = p.worldSpan / R;
    const activeCount = Math.min(MAX_GPU_PARTICLES, Math.max(1024, Math.floor(p.particleCount)));
    const emitter = EMITTER_MODES[p.emitterMode] || EMITTER_MODES['sky-rain'];
    const suppressSeasonalClimate = opts.skipTelemetry === true || opts.suppressSeasonalClimate === true;
    const previousSeasonalSuppressed = this._seasonalClimateSuppressed;
    this._seasonalClimateSuppressed = suppressSeasonalClimate;
    let seasonalRunCompleted = false;
    const { hardnessVec, strataVec } = this._getHardnessUniforms();
    const chemRates = this._chemRates();

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);

    for (let s = 0; s < count; s++) {
      this.stepCount++;
      if (p.simCryoPlaying !== false) this.cryoStepCount = (this.cryoStepCount || 0) + 1;
      // Recompute per substep, not once per batch: a 96-step stack bake must
      // travel through the shifting wind regime (and gust phase), rather
      // than eroding every substep from the direction at batch start.
      const climate = this._getClimateUniforms();
      const windErosion = this._getWindErosionUniforms();
      const frostU = this._getFrostUniforms();
      this._ensureRockObstacles();
      const prevP = this.partIdx;
      const nextP = 1 - prevP;

      // ---------------------------------------------------------------------
      // PASS A & B: GPU Lagrangian Particle Kinematics & Scatter (only when >=1 particle sim layer is playing)
      // ---------------------------------------------------------------------
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboDelta);
      gl.viewport(0, 0, R, R);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);

      if (climate.anyParticleSpecies) {
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboStepParticles[nextP]);
        gl.viewport(0, 0, PART_TEX_W, PART_TEX_H);
        gl.disable(gl.BLEND);
        gl.useProgram(this.progParticleStep);
        gl.bindVertexArray(this.emptyVao);

        this._bindTex(this.progParticleStep, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
        this._bindTex(this.progParticleStep, 'uPosAgeTex', this.partPosAge[prevP], 1);
        this._bindTex(this.progParticleStep, 'uVelWaterTex', this.partVelWater[prevP], 2);
        this._bindTex(this.progParticleStep, 'uSedimentTex', this.partSediment[prevP], 3);
        this._bindTex(this.progParticleStep, 'uVelocityFieldTex', this.velocityFieldTex, 4);
        this._bindTex(this.progParticleStep, 'uSnowTex', this.snowTex[this.snowIdx], 5);
        this._bindTex(this.progParticleStep, 'uRockObstacleTex', this.obstacleTex[this.obstacleIdx], 6);

        gl.uniform4f(gl.getUniformLocation(this.progParticleStep, 'uDomain'), p.worldSpan, R, dt, this.stepCount);
        gl.uniform4f(
          gl.getUniformLocation(this.progParticleStep, 'uRainEmitter'),
          p.cloudHeight,
          climate.effectiveRainIntensity,
          emitter.code,
          activeCount
        );
        gl.uniform4f(
          gl.getUniformLocation(this.progParticleStep, 'uStormTarget'),
          this.stormTarget[0],
          this.stormTarget[1],
          Math.max(24.0, p.brushRadius * 1.8),
          0.35
        );
        gl.uniform4f(
          gl.getUniformLocation(this.progParticleStep, 'uHydraulics'),
          p.simHydraulicPlaying === false ? 0.0 : p.erosionStrength,
          p.sedimentCapacity,
          p.depositionRate,
          p.particleInertia
        );
        gl.uniform4f(
          gl.getUniformLocation(this.progParticleStep, 'uLifeEvap'),
          climate.effectiveEvaporation,
          p.gravity,
          38.0,
          p.simHydraulicPlaying === false ? 0.0 : p.pipeFlowBlend
        );
        gl.uniform4fv(gl.getUniformLocation(this.progParticleStep, 'uHardnessParams'), hardnessVec);
        gl.uniform4fv(gl.getUniformLocation(this.progParticleStep, 'uStrataOrient'), strataVec);
        this._bindStrataColumnUniforms(this.progParticleStep);
        gl.uniform4fv(gl.getUniformLocation(this.progParticleStep, 'uSpeciesMix'), climate.speciesVec);
        gl.uniform4fv(gl.getUniformLocation(this.progParticleStep, 'uSnowParams'), climate.snowVec);
        gl.uniform4fv(gl.getUniformLocation(this.progParticleStep, 'uWindParams'), climate.windVec);
        gl.uniform4fv(gl.getUniformLocation(this.progParticleStep, 'uWindErodeA'), windErosion.a);
        gl.uniform4fv(gl.getUniformLocation(this.progParticleStep, 'uWindErodeB'), windErosion.b);
        gl.uniform1f(gl.getUniformLocation(this.progParticleStep, 'uDryAeolian'), p.dryAeolianMode || 0.0);
        gl.uniform1f(gl.getUniformLocation(this.progParticleStep, 'uSeed'), p.seed);

        gl.drawArrays(gl.TRIANGLES, 0, 3);
        this.partIdx = nextP;

        gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboDelta);
        gl.viewport(0, 0, R, R);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.ONE, gl.ONE);
        gl.useProgram(this.progParticleScatter);

        this._bindTex(this.progParticleScatter, 'uPrevPosAgeTex', this.partPosAge[prevP], 0);
        this._bindTex(this.progParticleScatter, 'uNextPosAgeTex', this.partPosAge[nextP], 1);
        this._bindTex(this.progParticleScatter, 'uExchangeTex', this.partExchange, 2);
        this._bindTex(this.progParticleScatter, 'uExchange2Tex', this.partExchange2, 3);
        gl.uniform2f(gl.getUniformLocation(this.progParticleScatter, 'uDomainAndRes'), p.worldSpan, R);

        gl.drawArrays(gl.POINTS, 0, activeCount);
        gl.disable(gl.BLEND);
      }

      // ---------------------------------------------------------------------
      // PASS C: GPU Shallow-Water Pipe Flux (`uFluxTex`). Rain creates and
      // erodes with this water, but the routing field is also refreshed for
      // snowmelt and chemical seasons so existing/melt water can carry
      // dissolved Fe/Cu without secretly turning rainfall back on.
      // ---------------------------------------------------------------------
      if (p.simHydraulicPlaying !== false || p.simCryoPlaying !== false || p.simChemicalPlaying !== false) {
        const nextFlux = 1 - this.fluxIdx;
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboFlux[nextFlux]);
        gl.viewport(0, 0, R, R);
        gl.useProgram(this.progWaterFlux);
        gl.bindVertexArray(this.emptyVao);

        this._bindTex(this.progWaterFlux, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
        this._bindTex(this.progWaterFlux, 'uFluxTex', this.fluxTex[this.fluxIdx], 1);
        this._bindTex(this.progWaterFlux, 'uRockObstacleTex', this.obstacleTex[this.obstacleIdx], 2);
        gl.uniform4f(gl.getUniformLocation(this.progWaterFlux, 'uPipeParams'), R, cellSize, dt, p.gravity);

        gl.drawArrays(gl.TRIANGLES, 0, 3);
        this.fluxIdx = nextFlux;
      }

      // PASS C2: Frost-damage state. Runs before integration so only this
      // step's resolution-visible detached equivalent (frost A) is consumed;
      // sub-threshold cracks stay stored in R/G and change no geometry.
      if (frostU.active) this._runFrostPass(false);

      // ---------------------------------------------------------------------
      // PASS D: Integrate Terrain + Water + Scattered Exchanges + Brush
      // ---------------------------------------------------------------------
      const nextTerr = 1 - this.terrainIdx;
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTerrainIntegrate[nextTerr]);
      gl.viewport(0, 0, R, R);
      gl.useProgram(this.progTerrainIntegrate);
      gl.bindVertexArray(this.emptyVao);

      this._bindTex(this.progTerrainIntegrate, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
      this._bindTex(this.progTerrainIntegrate, 'uDeltaTex', this.deltaTex, 1);
      this._bindTex(this.progTerrainIntegrate, 'uFluxTex', this.fluxTex[this.fluxIdx], 2);
      this._bindTex(this.progTerrainIntegrate, 'uSnowTex', this.snowTex[this.snowIdx], 3);
      this._bindTex(this.progTerrainIntegrate, 'uRockObstacleTex', this.obstacleTex[this.obstacleIdx], 4);
      this._bindTex(this.progTerrainIntegrate, 'uChemTex', this.chemTex[this.chemIdx], 5);
      this._bindTex(this.progTerrainIntegrate, 'uFrostTex', this.frostTex[this.frostIdx], 6);

      gl.uniform4f(gl.getUniformLocation(this.progTerrainIntegrate, 'uIntParams'), R, cellSize, dt, climate.effectiveEvaporation);
      gl.uniform4f(
        gl.getUniformLocation(this.progTerrainIntegrate, 'uPipeErode'),
        p.simHydraulicPlaying === false ? 0.0 : p.pipeFlowBlend,
        p.erosionStrength,
        p.rockHardness,
        p.worldSpan
      );
      gl.uniform4fv(gl.getUniformLocation(this.progTerrainIntegrate, 'uHardnessParams'), hardnessVec);
      gl.uniform4fv(gl.getUniformLocation(this.progTerrainIntegrate, 'uStrataOrient'), strataVec);
      this._bindStrataColumnUniforms(this.progTerrainIntegrate);
      gl.uniform4fv(gl.getUniformLocation(this.progTerrainIntegrate, 'uClimate'), climate.snowVec);
      gl.uniform4fv(gl.getUniformLocation(this.progTerrainIntegrate, 'uFrostIntA'), frostU.integrateA);
      gl.uniform4fv(gl.getUniformLocation(this.progTerrainIntegrate, 'uFrostIntB'), frostU.integrateB);
      gl.uniform4fv(gl.getUniformLocation(this.progTerrainIntegrate, 'uFrostIntC'), frostU.integrateC);
      gl.uniform4fv(gl.getUniformLocation(this.progTerrainIntegrate, 'uFrostIntD'), frostU.integrateD);
      const riverSpring = p.simHydraulicPlaying === false
        ? 0.0
        : (p.oasisSpring ? -Math.max(0.05, p.headwaterFlow ?? 0.65) : (p.headwaterFlow ?? 0.0));
      gl.uniform4f(
        gl.getUniformLocation(this.progTerrainIntegrate, 'uHydrology'),
        p.simCryoPlaying === false ? 0.0 : (p.iceMeltRate ?? 0.22),
        riverSpring,
        p.lakeRetention ?? 0.40,
        p.dryAeolianMode ?? 0.0
      );
      gl.uniform4f(
        gl.getUniformLocation(this.progTerrainIntegrate, 'uWindField'),
        climate.windVec[0],
        climate.windVec[1],
        climate.effectiveWindSpeed,
        p.erosionStrength ?? 0.70
      );
      gl.uniform4fv(gl.getUniformLocation(this.progTerrainIntegrate, 'uWindErodeA'), windErosion.a);
      gl.uniform4fv(gl.getUniformLocation(this.progTerrainIntegrate, 'uWindErodeB'), windErosion.b);
      gl.uniform2f(
        gl.getUniformLocation(this.progTerrainIntegrate, 'uWindPhase'),
        this.stepCount || 0,
        climate.windCycles || 0
      );

      const chemOn = p.simChemicalPlaying === false ? 0.0 : 1.0;
      gl.uniform4f(
        gl.getUniformLocation(this.progTerrainIntegrate, 'uChemInt'),
        chemOn * chemRates.dissolution,
        chemOn * chemRates.salt,
        chemOn * chemRates.hydrolysis,
        chemOn * chemRates.master
      );

      let brushCode = 0;
      if (this.brushDown) {
        if (p.brushMode === 'cloudburst') brushCode = 1;
        else if (p.brushMode === 'raise') brushCode = 2;
        else if (p.brushMode === 'carve') brushCode = 3;
      }
      gl.uniform4f(
        gl.getUniformLocation(this.progTerrainIntegrate, 'uBrush'),
        this.pointerWorldXZ[0],
        this.pointerWorldXZ[1],
        p.brushRadius,
        brushCode
      );
      gl.uniform1f(gl.getUniformLocation(this.progTerrainIntegrate, 'uBrushStrength'), p.brushStrength);
      gl.uniform1f(gl.getUniformLocation(this.progTerrainIntegrate, 'uSeed'), p.seed);

      gl.drawArrays(gl.TRIANGLES, 0, 3);
      this.terrainIdx = nextTerr;

      // ---------------------------------------------------------------------
      // PASS E: GPU Differential Cliff Spalling & Talus Angle-of-Repose Pass — gated by Rockfall/Talus Sim Layer!
      // ---------------------------------------------------------------------
      if (p.simRockfallPlaying !== false || frostU.anyTransport) {
        const nextTalus = 1 - this.terrainIdx;
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTerrainSingle[nextTalus]);
        gl.viewport(0, 0, R, R);
        gl.useProgram(this.progThermalTalus);

        this._bindTex(this.progThermalTalus, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
        this._bindTex(this.progThermalTalus, 'uRockObstacleTex', this.obstacleTex[this.obstacleIdx], 1);
        this._bindTex(this.progThermalTalus, 'uSnowTex', this.snowTex[this.snowIdx], 2);
        this._bindTex(this.progThermalTalus, 'uFrostTex', this.frostTex[this.frostIdx], 3);
        gl.uniform4f(
          gl.getUniformLocation(this.progThermalTalus, 'uTalusParams'),
          R,
          cellSize,
          p.talusAngle,
          p.worldSpan
        );
        gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uHardnessParams'), hardnessVec);
        gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uStrataOrient'), strataVec);
        this._bindStrataColumnUniforms(this.progThermalTalus);
        gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uFrostTalusA'), frostU.talusA);
        gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uFrostTalusB'), frostU.talusB);
        gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uFrostTalusC'), frostU.talusC);
        gl.uniform4fv(gl.getUniformLocation(this.progThermalTalus, 'uFrostTalusD'), frostU.talusD);
        gl.uniform1f(gl.getUniformLocation(this.progThermalTalus, 'uDryAeolian'), p.dryAeolianMode || 0.0);
        gl.uniform1f(gl.getUniformLocation(this.progThermalTalus, 'uSeed'), p.seed);

        gl.drawArrays(gl.TRIANGLES, 0, 3);
        this.terrainIdx = nextTalus;
      }

      // PASS E2: GPU cliff detachment scars, boulder impact craters & boulder weathering aprons
      if (p.simRockfallPlaying !== false) {
        this._applyRockTerrainCoupling(R, cellSize);
      }

      // ---------------------------------------------------------------------
      // PASS F: GPU Cryosphere — Snowpack, Avalanche, Firn & Glacier Creep — gated by Cryo Sim Layer!
      // ---------------------------------------------------------------------
      if (p.simCryoPlaying !== false) {
        const nextSnow = 1 - this.snowIdx;
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboSnowClear[nextSnow]);
        gl.viewport(0, 0, R, R);
        gl.useProgram(this.progSnowCryo);

        const sunAzRad = ((p.sunAzimuth ?? 132) * Math.PI) / 180;
        const sunElRad = ((p.sunElevation ?? 38) * Math.PI) / 180;

        this._bindTex(this.progSnowCryo, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
        this._bindTex(this.progSnowCryo, 'uSnowTex', this.snowTex[this.snowIdx], 1);
        this._bindTex(this.progSnowCryo, 'uSnowDeltaTex', this.snowDeltaTex, 2);
        this._bindTex(this.progSnowCryo, 'uRockObstacleTex', this.obstacleTex[this.obstacleIdx], 3);
        gl.uniform4f(gl.getUniformLocation(this.progSnowCryo, 'uCryoParams'), R, cellSize, dt, p.worldSpan);
        gl.uniform4fv(gl.getUniformLocation(this.progSnowCryo, 'uClimate'), climate.snowVec);
        gl.uniform4fv(gl.getUniformLocation(this.progSnowCryo, 'uMeltCycle'), climate.meltCycleVec);
        gl.uniform4fv(gl.getUniformLocation(this.progSnowCryo, 'uStrataOrient'), strataVec);
        gl.uniform4f(
          gl.getUniformLocation(this.progSnowCryo, 'uWindCryo'),
          climate.windVec[0],
          climate.windVec[1],
          climate.effectiveWindSpeed,
          this.stepCount * dt
        );
        gl.uniform3fv(
          gl.getUniformLocation(this.progSnowCryo, 'uSunDir'),
          [
            Math.cos(sunElRad) * Math.sin(sunAzRad),
            Math.sin(sunElRad),
            Math.cos(sunElRad) * Math.cos(sunAzRad),
          ]
        );
        gl.uniform1f(gl.getUniformLocation(this.progSnowCryo, 'uIceMeltRate'), p.iceMeltRate ?? 0.22);
        gl.uniform1f(gl.getUniformLocation(this.progSnowCryo, 'uSeed'), p.seed);

        gl.drawArrays(gl.TRIANGLES, 0, 3);
        this.snowIdx = nextSnow;
      }

      // PASS F2: embedded-boulder snow imprints queued by rock physics
      this._applyRockImprints(R, cellSize);

      // ---------------------------------------------------------------------
      // PASS G: Chemical weathering & dissolution reactions — gated by the
      // Chemical Weathering simulation layer. Runs last so it sees this
      // step's fresh water depth, discharge, sediment and snowmelt fields.
      // ---------------------------------------------------------------------
      if (p.simChemicalPlaying !== false) {
        this._runChemPass(dt, 1.0);
      }

      if (!suppressSeasonalClimate) {
        const advanced = this._advanceSeasonalClimateClock();
        if (advanced.completedNow) {
          seasonalRunCompleted = true;
          break;
        }
      }
    }

    this._seasonalClimateSuppressed = previousSeasonalSuppressed;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.bindVertexArray(null);

    if (opts.skipTelemetry) return;

    this._syncSeasonalClimateTelemetry();
    if (seasonalRunCompleted) {
      const { climate, run } = this.getSeasonalClimateState();
      const yearsLabel = Number.isInteger(run.targetYears) ? String(run.targetYears) : run.targetYears.toFixed(1);
      if (this.pipeline && typeof this.pipeline.pauseSimulation === 'function') {
        this.pipeline.pauseSimulation(`Seasonal climate run complete — ${yearsLabel} years simulated · Year ${climate.year} ${climate.monthName} ${climate.dayOfMonth}`);
      } else {
        this.playing = false;
      }
    }

    if (this.stepCount % 36 === 0) {
      this._sampleTelemetry();
    } else {
      this.telemetry.steps = this.stepCount;
      this.telemetry.particles = activeCount;
      this.telemetry.fps = this.fps;
      if (this.pipeline) {
        this.telemetry.layerCount = this.pipeline.layers.length;
        this.telemetry.evalMs = Number(this.pipeline.lastEvalMs.toFixed(1));
        this.telemetry.caveTris = Math.round(this.pipeline.caveSystem.vertexCount / 3);
      }
      if (this.onTelemetry && (seasonalRunCompleted || this.stepCount % 8 === 0)) this.onTelemetry(this.telemetry);
    }
  }

  /** Downsamples the cryosphere texture to 64x64 so rock physics can embed into snow/ice. */
  _readSnowSnapshot64() {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTelemetry);
    gl.viewport(0, 0, TELEMETRY_RES, TELEMETRY_RES);
    gl.useProgram(this.progTelemetry);
    gl.bindVertexArray(this.emptyVao);
    this._bindTex(this.progTelemetry, 'uTerrainTex', this.snowTex[this.snowIdx], 0);
    gl.uniform1f(gl.getUniformLocation(this.progTelemetry, 'uGridRes'), this.gridRes);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.readPixels(0, 0, TELEMETRY_RES, TELEMETRY_RES, gl.RGBA, gl.FLOAT, this.snowSnap64);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  /** Downsamples the frost/thermal crack texture to 64x64 so rockfall couples to rock damage. */
  _readFrostSnapshot64() {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTelemetry);
    gl.viewport(0, 0, TELEMETRY_RES, TELEMETRY_RES);
    gl.useProgram(this.progTelemetry);
    gl.bindVertexArray(this.emptyVao);
    this._bindTex(this.progTelemetry, 'uTerrainTex', this.frostTex[this.frostIdx], 0);
    gl.uniform1f(gl.getUniformLocation(this.progTelemetry, 'uGridRes'), this.gridRes);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.readPixels(0, 0, TELEMETRY_RES, TELEMETRY_RES, gl.RGBA, gl.FLOAT, this.frostSnap64);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  /**
   * Stamps cliff detachment notches, high-energy rockfall impact craters + scree rims,
   * and boulder weathering sediment halos into `terrainTex`.
   */
  _applyRockTerrainCoupling(R, cellSize) {
    const gl = this.gl;
    for (let pass = 0; pass < 3; pass++) {
      const events = this.rockCalving.consumeTerrainEvents(24);
      if (!events || events.length === 0) return;
      const A = this._coupleA;
      const B = this._coupleB;
      A.fill(0);
      B.fill(0);
      for (let i = 0; i < events.length; i++) {
        const ev = events[i];
        A[i * 4] = ev.x;
        A[i * 4 + 1] = ev.z;
        A[i * 4 + 2] = ev.radius;
        A[i * 4 + 3] = ev.cutDepth;
        B[i * 4] = ev.coarseGain;
        B[i * 4 + 1] = ev.fineGain;
        B[i * 4 + 2] = ev.rimRatio ?? 1.5;
        B[i * 4 + 3] = ev.kind ?? 0;
      }
      const nextTerr = 1 - this.terrainIdx;
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTerrainSingle[nextTerr]);
      gl.viewport(0, 0, R, R);
      gl.useProgram(this.progRockCouple);
      gl.bindVertexArray(this.emptyVao);
      this._bindTex(this.progRockCouple, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
      gl.uniform4f(gl.getUniformLocation(this.progRockCouple, 'uRockCoupleDomain'), this.params.worldSpan, cellSize, events.length, 0);
      gl.uniform4fv(gl.getUniformLocation(this.progRockCouple, 'uRockCoupleA[0]'), A);
      gl.uniform4fv(gl.getUniformLocation(this.progRockCouple, 'uRockCoupleB[0]'), B);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      this.terrainIdx = nextTerr;
    }
  }

  /**
   * Rebuilds the boulder-occupancy field consumed by water flux, sediment
   * integration, droplets, talus and snow creep. Throttled while rocks are
   * tumbling; static once they sleep. MAX-accumulated in batches of 24.
   */
  _ensureRockObstacles() {
    if (!this.rockCalving.obstaclesDirty) return;
    if (this.stepCount - this._obstBuiltStep < 6) return;
    const gl = this.gl;
    const R = this.gridRes;
    const cellSize = this.params.worldSpan / R;
    const stamps = this.rockCalving.getObstacleStamps();

    gl.disable(gl.BLEND);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboRockObstacle[0]);
    gl.viewport(0, 0, R, R);
    gl.clearColor(-1000, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    let idx = 0;

    if (stamps.length > 0) {
      gl.useProgram(this.progRockObstacle);
      gl.bindVertexArray(this.emptyVao);
      const A = this._obstA;
      const B = this._obstB;
      for (let b = 0; b < stamps.length; b += 24) {
        const n = Math.min(24, stamps.length - b);
        A.fill(0);
        B.fill(0);
        for (let i = 0; i < n; i++) {
          const s = stamps[b + i];
          A[i * 4] = s.x; A[i * 4 + 1] = s.z; A[i * 4 + 2] = s.Rh; A[i * 4 + 3] = s.cy;
          B[i * 4] = s.Rv;
        }
        const next = 1 - idx;
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboRockObstacle[next]);
        gl.viewport(0, 0, R, R);
        this._bindTex(this.progRockObstacle, 'uObstacleTex', this.obstacleTex[idx], 0);
        gl.uniform4f(gl.getUniformLocation(this.progRockObstacle, 'uObstacleDomain'), this.params.worldSpan, cellSize, n, 0);
        gl.uniform4fv(gl.getUniformLocation(this.progRockObstacle, 'uObstacleA[0]'), A);
        gl.uniform4fv(gl.getUniformLocation(this.progRockObstacle, 'uObstacleB[0]'), B);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        idx = next;
      }
    }
    this.obstacleIdx = idx;
    this.rockCalving.obstaclesDirty = false;
    this._obstBuiltStep = this.stepCount;
  }

  /**
   * Stamps embedded-boulder imprints (compacted footprint + displaced rim)
   * into the snowpack. Runs right after the cryo pass; no-op unless rock
   * physics queued stamps. Batches of 24, at most 3 passes per sim step.
   */
  _applyRockImprints(R, cellSize) {
    const gl = this.gl;
    for (let pass = 0; pass < 3; pass++) {
      const stamps = this.rockCalving.consumeImprints(24);
      if (!stamps || stamps.length === 0) return;
      const A = this._imprintA;
      const B = this._imprintB;
      A.fill(0);
      B.fill(0);
      for (let i = 0; i < stamps.length; i++) {
        const s = stamps[i];
        A[i * 4] = s.x; A[i * 4 + 1] = s.z; A[i * 4 + 2] = s.a; A[i * 4 + 3] = s.rimH;
        B[i * 4] = s.rimW; B[i * 4 + 1] = s.compactSnow;
      }
      const nextSnow = 1 - this.snowIdx;
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboSnowClear[nextSnow]);
      gl.viewport(0, 0, R, R);
      gl.useProgram(this.progRockImprint);
      this._bindTex(this.progRockImprint, 'uSnowTex', this.snowTex[this.snowIdx], 0);
      gl.uniform4f(gl.getUniformLocation(this.progRockImprint, 'uImprintDomain'), this.params.worldSpan, cellSize, stamps.length, 0);
      gl.uniform4fv(gl.getUniformLocation(this.progRockImprint, 'uImprintA[0]'), A);
      gl.uniform4fv(gl.getUniformLocation(this.progRockImprint, 'uImprintB[0]'), B);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      this.snowIdx = nextSnow;
    }
  }

  _sampleTelemetry() {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTelemetry);
    gl.viewport(0, 0, TELEMETRY_RES, TELEMETRY_RES);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.useProgram(this.progTelemetry);
    gl.bindVertexArray(this.emptyVao);

    this._bindTex(this.progTelemetry, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
    gl.uniform1f(gl.getUniformLocation(this.progTelemetry, 'uGridRes'), this.gridRes);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    gl.readPixels(0, 0, TELEMETRY_RES, TELEMETRY_RES, gl.RGBA, gl.FLOAT, this.telemetryBuf);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    let sumEroded = 0;
    let sumCoarse = 0;
    let sumFine = 0;
    let sumWater = 0;
    const buf = this.telemetryBuf;
    const ini = this.initialSnap64;
    for (let i = 0; i < buf.length; i += 4) {
      sumEroded += Math.max(0, ini[i] - buf[i]);
      sumCoarse += Math.max(0, buf[i + 1] - ini[i + 1]);
      sumFine += Math.max(0, buf[i + 2] - ini[i + 2]);
      sumWater += buf[i + 3];
    }

    // Feed live 64x64 terrain, cryosphere & frost/crack fields to 3D rock calving,
    // undercut-boulder wake-up, and progressive boulder weathering!
    this._readSnowSnapshot64();
    this._readFrostSnapshot64();
    this.rockCalving.updateTerrainSnapshot(buf, TELEMETRY_RES, this.params.worldSpan, this.snowSnap64, this.frostSnap64);
    if (this.playing && this.params.showOutcrops !== false) {
      if (this.params.simRockfallPlaying !== false && (this.params.rockfallRate || 0) > 0.14 && (this.params.boulderDensity ?? 0.65) > 0.04) {
        if (this.stepCount % 72 === 0) {
          this.rockCalving.calveCliffBoulders(Math.max(1, Math.round((this.params.rockfallRate || 0.35) * 4)));
        }
      }
      this.rockCalving.weatherBoulders(0.18, {
        rain: this.params.simHydraulicPlaying === false ? 0 : (this.params.rainIntensity ?? 0.5),
        frost: this.params.simCryoPlaying === false ? 0 : 0.65,
        wind: this.params.simWindPlaying === false ? 0 : (this.params.windSpeed ?? 0) * (this.params.windErosionRate ?? 0.5),
        chem: this.params.simChemicalPlaying === false ? 0 : (this.params.chemRate ?? 0.35),
      });
    }

    const cellAreaSample = (this.params.worldSpan * this.params.worldSpan) / (TELEMETRY_RES * TELEMETRY_RES);
    this.telemetry.steps = this.stepCount;
    this.telemetry.particles = this.params.particleCount;
    this.telemetry.erodedVolumeM3 = Math.round(sumEroded * cellAreaSample);
    this.telemetry.coarseSedM3 = Math.round(sumCoarse * cellAreaSample);
    this.telemetry.fineSedM3 = Math.round(sumFine * cellAreaSample);
    this.telemetry.waterVolumeM3 = Math.round(sumWater * cellAreaSample);
    this.telemetry.boulders = this.rockCalving.rocks.length;
    this.telemetry.fps = this.fps;
    this._syncSeasonalClimateTelemetry();
    if (this.pipeline) {
      this.telemetry.layerCount = this.pipeline.layers.length;
      this.telemetry.evalMs = Number(this.pipeline.lastEvalMs.toFixed(1));
      this.telemetry.caveTris = Math.round(this.pipeline.caveSystem.vertexCount / 3);
    }
    if (this.onTelemetry) this.onTelemetry(this.telemetry);
  }

  _bindTex(prog, name, tex, unit) {
    const gl = this.gl;
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.uniform1i(gl.getUniformLocation(prog, name), unit);
  }

  resizeIfNeeded() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(2, Math.floor(this.canvas.clientWidth * dpr));
    const h = Math.max(2, Math.floor(this.canvas.clientHeight * dpr));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
    }
  }

  renderFrame() {
    const gl = this.gl;
    this.resizeIfNeeded();
    const W = this.canvas.width;
    const H = this.canvas.height;
    const p = this.params;
    const { viewProj, eye } = this._buildViewProjMatrix(W / H);
    const az = (p.sunAzimuth * Math.PI) / 180;
    const el = (p.sunElevation * Math.PI) / 180;
    const sunDir = [Math.cos(el) * Math.sin(az), Math.sin(el), Math.cos(el) * Math.cos(az)];
    const timeSec = (performance.now() - this.startTimeMs) * 0.001;

    if (this.pipelineMode && this.pipeline) {
      this.pipeline.renderPipelineFrame(viewProj, eye, sunDir, timeSec);
      if (this.onFrameRendered) this.onFrameRendered();
      return;
    }

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, W, H);
    gl.clear(gl.DEPTH_BUFFER_BIT);

    // 1. Studio Sky Backdrop
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.useProgram(this.progSkyBg);
    gl.uniform1i(gl.getUniformLocation(this.progSkyBg, 'uMaterialTheme'), this._themeCode());
    gl.bindVertexArray(this.emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    // 2. 3D Displaced Terrain + Stratigraphy + Sedimentation + Raymarched Sun Shadows
    const { hardnessVec, strataVec } = this._getHardnessUniforms();
    const layerVisVec = this._getLayerVisUniforms();
    const climate = this._getClimateUniforms();

    const viewCode =
      p.viewMode === 'sediment' ? 1 :
      p.viewMode === 'hardness' ? 2 :
      p.viewMode === 'flow'     ? 3 :
      p.viewMode === 'clay'     ? 4 :
      p.viewMode === 'cryo'     ? 5 :
      p.viewMode === 'hydro'    ? 6 :
      p.viewMode === 'mineral'  ? 9 :
      p.viewMode === 'frost'    ? 10 : 0;

    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.depthMask(true);
    gl.enable(gl.CULL_FACE);
    gl.cullFace(gl.BACK);

    gl.useProgram(this.progTerrainRender);
    gl.bindVertexArray(this.terrainVao);

    this._bindTex(this.progTerrainRender, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
    this._bindTex(this.progTerrainRender, 'uInitialTex', this.initialTerrainTex, 1);
    this._bindTex(this.progTerrainRender, 'uVelocityFieldTex', this.velocityFieldTex, 2);
    this._bindTex(this.progTerrainRender, 'uSnowTex', this.snowTex[this.snowIdx], 3);
    this._bindTex(this.progTerrainRender, 'uChemTex', this.chemTex[this.chemIdx], 7);
    this._bindTex(this.progTerrainRender, 'uMineralTex', this.mineralTex[this.mineralIdx], 8);
    this._bindTex(this.progTerrainRender, 'uFrostTex', this.frostTex[this.frostIdx], 9);

    gl.uniformMatrix4fv(gl.getUniformLocation(this.progTerrainRender, 'uViewProj'), false, viewProj);
    gl.uniform3fv(gl.getUniformLocation(this.progTerrainRender, 'uCameraPos'), eye);
    gl.uniform3fv(gl.getUniformLocation(this.progTerrainRender, 'uSunDir'), sunDir);
    gl.uniform4f(
      gl.getUniformLocation(this.progTerrainRender, 'uRenderDomain'),
      p.worldSpan,
      this.gridRes,
      (p.showWater && p.waterLayerEnabled !== false) ? 1.0 : 0.0,
      timeSec
    );
    gl.uniform4fv(gl.getUniformLocation(this.progTerrainRender, 'uLayerVis'), layerVisVec);
    gl.uniform4fv(gl.getUniformLocation(this.progTerrainRender, 'uHardnessParams'), hardnessVec);
    gl.uniform4fv(gl.getUniformLocation(this.progTerrainRender, 'uStrataOrient'), strataVec);
    this._bindStrataColumnUniforms(this.progTerrainRender);

    const showCursorMode =
      p.brushMode === 'cloudburst' ? 1 :
      p.brushMode === 'raise'      ? 2 :
      p.brushMode === 'carve'      ? 3 :
      p.emitterMode === 'storm-cell' ? 1 : 0;
    const cursorXZ = p.emitterMode === 'storm-cell' && p.brushMode === 'orbit' ? this.stormTarget : this.pointerWorldXZ;
    gl.uniform4f(
      gl.getUniformLocation(this.progTerrainRender, 'uBrush'),
      cursorXZ[0],
      cursorXZ[1],
      p.emitterMode === 'storm-cell' && p.brushMode === 'orbit' ? Math.max(24, p.brushRadius * 1.8) : p.brushRadius,
      showCursorMode
    );
    gl.uniform1i(gl.getUniformLocation(this.progTerrainRender, 'uViewMode'), viewCode);
    gl.uniform1i(gl.getUniformLocation(this.progTerrainRender, 'uMaterialTheme'), this._themeCode());
    gl.uniform1i(gl.getUniformLocation(this.progTerrainRender, 'uFabricMode'), this._fabricCode());
    gl.uniform4fv(gl.getUniformLocation(this.progTerrainRender, 'uClimate'), climate.snowVec);
    gl.uniform4f(
      gl.getUniformLocation(this.progTerrainRender, 'uWindRender'),
      climate.windVec[0],
      climate.windVec[1],
      p.windSpeed || 0.0,
      p.dryAeolianMode || 0.0
    );
    gl.uniform1f(gl.getUniformLocation(this.progTerrainRender, 'uSeed'), p.seed);
    gl.uniform1f(gl.getUniformLocation(this.progTerrainRender, 'uChemVis'), p.chemicalWeathering ?? DEFAULT_EROSION_PARAMS.chemicalWeathering);

    gl.drawElements(gl.TRIANGLES, this.terrainIndexCount, gl.UNSIGNED_INT, 0);

    // 2B. 3D Bedrock Outcrops & Gravity-Settled Cliff-Calved Boulders!
    if (p.showOutcrops !== false && this.rockCalving.rocks.length > 0) {
      gl.useProgram(this.progRockInstance);
      gl.bindVertexArray(this.rockVao);

      this._bindTex(this.progRockInstance, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
      this._bindTex(this.progRockInstance, 'uSnowTex', this.snowTex[this.snowIdx], 1);
      this._bindTex(this.progRockInstance, 'uChemTex', this.chemTex[this.chemIdx], 2);
      this._bindTex(this.progRockInstance, 'uMineralTex', this.mineralTex[this.mineralIdx], 3);
      this._bindTex(this.progRockInstance, 'uRockSdfTex', this.rockSdfTex, 4);

      gl.uniformMatrix4fv(gl.getUniformLocation(this.progRockInstance, 'uViewProj'), false, viewProj);
      gl.uniform3fv(gl.getUniformLocation(this.progRockInstance, 'uCameraPos'), eye);
      gl.uniform3fv(gl.getUniformLocation(this.progRockInstance, 'uSunDir'), sunDir);
      gl.uniform4f(
        gl.getUniformLocation(this.progRockInstance, 'uRenderDomain'),
        p.worldSpan,
        this.gridRes,
        (p.showWater && p.waterLayerEnabled !== false) ? 1.0 : 0.0,
        timeSec
      );
      gl.uniform4fv(gl.getUniformLocation(this.progRockInstance, 'uLayerVis'), layerVisVec);
      gl.uniform4fv(gl.getUniformLocation(this.progRockInstance, 'uHardnessParams'), hardnessVec);
      gl.uniform4fv(gl.getUniformLocation(this.progRockInstance, 'uStrataOrient'), strataVec);
      this._bindStrataColumnUniforms(this.progRockInstance);
      gl.uniform4fv(gl.getUniformLocation(this.progRockInstance, 'uClimate'), climate.snowVec);
      gl.uniform1i(gl.getUniformLocation(this.progRockInstance, 'uViewMode'), viewCode);
      gl.uniform1i(gl.getUniformLocation(this.progRockInstance, 'uMaterialTheme'), this._themeCode());
      gl.uniform1i(gl.getUniformLocation(this.progRockInstance, 'uFabricMode'), this._fabricCode());
      gl.uniform1f(gl.getUniformLocation(this.progRockInstance, 'uSeed'), p.seed);
      gl.uniform1f(gl.getUniformLocation(this.progRockInstance, 'uChemWeather'), p.chemicalWeathering ?? 0.7);

      gl.bindBuffer(gl.ARRAY_BUFFER, this.rockInstVbo);
      const subs = this.rockCalving.atlas.submeshes;
      for (let b = 0; b < subs.length; b++) {
        const count = this.rockCalving.bucketCounts[b];
        if (count <= 0) continue;
        gl.uniform1i(gl.getUniformLocation(this.progRockInstance, 'uRockArchetype'), b);
        gl.bufferSubData(gl.ARRAY_BUFFER, 0, this.rockCalving.bucketData[b].subarray(0, count * 16));
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

    // 2C. Real 3D GPU-Instanced Grass Vegetation Layer (Green Grass / Dry Grass / Biome Auto-Blend)!
    if (p.grassEnabled && (p.grassDensity ?? 0.5) > 0.01 && (viewCode === 0 || viewCode === 4)) {
      const tuftCount = Math.max(512, Math.min(MAX_GRASS_TUFTS, Math.round((p.grassDensity ?? 0.55) * MAX_GRASS_TUFTS)));
      gl.disable(gl.CULL_FACE);
      gl.useProgram(this.progGrassInstance);
      gl.bindVertexArray(this.grassVao);

      this._bindTex(this.progGrassInstance, 'uTerrainTex', this.terrainTex[this.terrainIdx], 0);
      this._bindTex(this.progGrassInstance, 'uSnowTex', this.snowTex[this.snowIdx], 1);

      gl.uniformMatrix4fv(gl.getUniformLocation(this.progGrassInstance, 'uViewProj'), false, viewProj);
      gl.uniform3fv(gl.getUniformLocation(this.progGrassInstance, 'uCameraPos'), eye);
      gl.uniform3fv(gl.getUniformLocation(this.progGrassInstance, 'uSunDir'), sunDir);
      gl.uniform4f(
        gl.getUniformLocation(this.progGrassInstance, 'uRenderDomain'),
        p.worldSpan,
        this.gridRes,
        p.showWater ? 1.0 : 0.0,
        timeSec
      );
      gl.uniform4f(
        gl.getUniformLocation(this.progGrassInstance, 'uGrassParams'),
        tuftCount,
        p.grassHeight ?? 1.15,
        p.grassWindSway ?? 0.65,
        p.grassSlopeMax ?? 0.62
      );
      gl.uniform4f(
        gl.getUniformLocation(this.progGrassInstance, 'uGrassStyle'),
        p.grassMode ?? 0,
        p.grassDryBlend ?? 0.15,
        p.grassElevMax ?? 0.52,
        p.grassPatchiness ?? 0.58
      );
      gl.uniform4f(
        gl.getUniformLocation(this.progGrassInstance, 'uWindRender'),
        climate.windVec[0],
        climate.windVec[1],
        p.windSpeed || 0.25,
        p.dryAeolianMode || 0.0
      );
      gl.uniform4fv(gl.getUniformLocation(this.progGrassInstance, 'uStrataOrient'), strataVec);
      gl.uniform1i(gl.getUniformLocation(this.progGrassInstance, 'uViewMode'), viewCode);
      gl.uniform1i(gl.getUniformLocation(this.progGrassInstance, 'uMaterialTheme'), this._themeCode());
      gl.uniform1f(gl.getUniformLocation(this.progGrassInstance, 'uSeed'), p.seed);

      gl.drawElementsInstanced(gl.TRIANGLES, this.grassIndexCount, gl.UNSIGNED_SHORT, 0, tuftCount);
      gl.enable(gl.CULL_FACE);
    }

    // 3. 3D Falling Sky Raindrops & Sediment-Colored Surface Runoff Particles!
    if (p.showParticles && p.particleOpacity > 0.01 && climate.anyParticleSpecies) {
      const activeCount = Math.min(MAX_GPU_PARTICLES, Math.max(1024, Math.floor(p.particleCount)));
      gl.disable(gl.CULL_FACE);
      gl.depthMask(false);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

      gl.useProgram(this.progParticleRender);
      gl.bindVertexArray(this.emptyVao);

      this._bindTex(this.progParticleRender, 'uPosAgeTex', this.partPosAge[this.partIdx], 0);
      this._bindTex(this.progParticleRender, 'uVelWaterTex', this.partVelWater[this.partIdx], 1);
      this._bindTex(this.progParticleRender, 'uSedimentTex', this.partSediment[this.partIdx], 2);
      this._bindTex(this.progParticleRender, 'uTerrainTex', this.terrainTex[this.terrainIdx], 3);

      gl.uniformMatrix4fv(gl.getUniformLocation(this.progParticleRender, 'uViewProj'), false, viewProj);
      gl.uniform3fv(gl.getUniformLocation(this.progParticleRender, 'uCameraPos'), eye);
      gl.uniform4fv(gl.getUniformLocation(this.progParticleRender, 'uSpeciesVis'), this._getSpeciesVisUniform());
      gl.uniform4f(
        gl.getUniformLocation(this.progParticleRender, 'uPartVis'),
        p.worldSpan,
        this.gridRes,
        p.particleOpacity,
        activeCount
      );

      gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, activeCount);

      gl.disable(gl.BLEND);
      gl.depthMask(true);
    }

    gl.bindVertexArray(null);
  }

  start() {
    if (this.active) return;
    this.active = true;
    this._fpsLastMs = performance.now();
    this._fpsFrames = 0;

    const tick = () => {
      if (!this.active) return;
      this._rafId = requestAnimationFrame(tick);

      const now = performance.now();
      const frameDt = Math.min(0.08, Math.max(0.001, (now - this._lastFrameMs) * 0.001));
      this._lastFrameMs = now;
      this._stepWasdCamera(frameDt);

      this._fpsFrames++;
      if (now - this._fpsLastMs >= 500) {
        this.fps = Math.round((this._fpsFrames * 1000) / (now - this._fpsLastMs));
        this._fpsFrames = 0;
        this._fpsLastMs = now;
      }

      if (this.pipelineMode && this.pipeline) {
        this.playing = Boolean(this.pipeline.liveSimRunning);
        const anySimLayerOn =
          this.params.simHydraulicPlaying !== false ||
          this.params.simCryoPlaying !== false ||
          this.params.simWindPlaying !== false ||
          this.params.simRockfallPlaying !== false ||
          this.params.simChemicalPlaying !== false;
        if (this.playing && anySimLayerOn) {
          this.stepSimulation(this.params.substepsPerFrame);
        }
        // Pausing the live sim freezes rigid-body boulders as well as the GPU
        // erosion passes; otherwise rocks keep settling/sinking on a frozen
        // terrain snapshot after the user pressed Pause Sim.
        if (this.playing && this.params.simRockfallPlaying !== false && this.params.showOutcrops !== false) {
          this.rockCalving.stepPhysics(frameDt, false);
        }
        this.telemetry.fps = this.fps;
        this.telemetry.boulders = this.rockCalving.rocks.length;
        this.telemetry.layerCount = this.pipeline.layers.length;
        this.renderFrame();
        return;
      }

      const anySimLayerOn =
        this.params.simHydraulicPlaying !== false ||
        this.params.simCryoPlaying !== false ||
        this.params.simWindPlaying !== false ||
        this.params.simRockfallPlaying !== false ||
        this.params.simChemicalPlaying !== false;
      if (this.playing && anySimLayerOn) {
        this.stepSimulation(this.params.substepsPerFrame);
        if (this.params.simRockfallPlaying !== false) {
          this.rockCalving.stepPhysics(frameDt);
        }
      } else {
        if (this.playing && this.params.simRockfallPlaying !== false) {
          this.rockCalving.stepPhysics(frameDt);
        }
        if (this.brushDown && this.params.brushMode !== 'orbit') {
          this.stepSimulation(1);
        }
      }

      this.renderFrame();
    };
    this._rafId = requestAnimationFrame(tick);
  }

  stop() {
    this.active = false;
    if (this._rafId) {
      cancelAnimationFrame(this._rafId);
      this._rafId = null;
    }
  }

  /**
   * Reads back the live GPU-eroded terrain heightfield on demand and builds a game-ready
   * baked polygon mesh (positions, normals, tangents, UVs, indices, and vertex colors)
   * compatible with our `.GLB` and `.OBJ` exporters. Any active SDF cave/tunnel
   * mesh is baked into the same static mesh, and heightfield triangles inside
   * an open cave portal are omitted so tunnels remain actual holes in-game.
   */
  extractMeshForExport(exportStep = 2) {
    const gl = this.gl;
    const R = this.gridRes;
    const raw = new Float32Array(R * R * 4);

    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fboTerrainSingle[this.terrainIdx]);
    gl.readPixels(0, 0, R, R, gl.RGBA, gl.FLOAT, raw);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    let rawVecDisp = null;
    if (this.pipelineMode && this.pipeline && this.pipeline.fboVecOnly) {
      rawVecDisp = new Float32Array(R * R * 4);
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.pipeline.fboVecOnly[this.pipeline.vecIdx]);
      gl.readPixels(0, 0, R, R, gl.RGBA, gl.FLOAT, rawVecDisp);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    }

    const stride = Math.max(1, exportStep);
    const M = Math.floor((R - 1) / stride) + 1;
    const vCount = M * M;
    const caveSys = this.pipelineMode && this.pipeline ? this.pipeline.caveSystem : null;
    const caveVertexCount = caveSys && caveSys.positions
      ? Math.min(Math.floor(caveSys.vertexCount || 0), Math.floor(caveSys.positions.length / 3))
      : 0;
    const totalVertexCount = vCount + caveVertexCount;
    const positions = new Float32Array(totalVertexCount * 3);
    const normals = new Float32Array(totalVertexCount * 3);
    const tangents = new Float32Array(totalVertexCount * 4);
    const uvs = new Float32Array(totalVertexCount * 2);
    const colors = new Float32Array(totalVertexCount * 4);

    const span = this.params.worldSpan;
    const cellWorld = span / (R - 1);

    const heightAt = (ix, iz) => {
      const cx = Math.max(0, Math.min(R - 1, ix));
      const cz = Math.max(0, Math.min(R - 1, iz));
      const idx = (cz * R + cx) * 4;
      return raw[idx] + raw[idx + 1] + raw[idx + 2];
    };

    for (let gz = 0; gz < M; gz++) {
      const iz = Math.min(R - 1, gz * stride);
      const v = iz / (R - 1);
      for (let gx = 0; gx < M; gx++) {
        const ix = Math.min(R - 1, gx * stride);
        const u = ix / (R - 1);
        const vi = gz * M + gx;
        const rawIdx = (iz * R + ix) * 4;

        const bed = raw[rawIdx];
        const coarse = raw[rawIdx + 1];
        const fine = raw[rawIdx + 2];
        const dx = rawVecDisp ? rawVecDisp[rawIdx] : 0;
        const dy = rawVecDisp ? rawVecDisp[rawIdx + 1] : 0;
        const dz = rawVecDisp ? rawVecDisp[rawIdx + 2] : 0;
        const h = bed + coarse + fine + dy;

        positions[vi * 3]     = (u - 0.5) * span + dx;
        positions[vi * 3 + 1] = h;
        positions[vi * 3 + 2] = (v - 0.5) * span + dz;

        uvs[vi * 2]     = u;
        uvs[vi * 2 + 1] = v;

        const hL = heightAt(ix - stride, iz);
        const hR = heightAt(ix + stride, iz);
        const hD = heightAt(ix, iz - stride);
        const hU = heightAt(ix, iz + stride);
        let nx = hL - hR;
        let ny = 2.0 * cellWorld * stride;
        let nz = hD - hU;
        const nl = Math.hypot(nx, ny, nz) || 1;
        nx /= nl; ny /= nl; nz /= nl;

        normals[vi * 3]     = nx;
        normals[vi * 3 + 1] = ny;
        normals[vi * 3 + 2] = nz;

        tangents[vi * 4]     = 1;
        tangents[vi * 4 + 1] = 0;
        tangents[vi * 4 + 2] = 0;
        tangents[vi * 4 + 3] = 1;

        // Bake rock vs coarse gravel vs fine ochre alluvial fan color into vertex colors!
        const fineW = Math.min(1, fine / 0.22);
        const coarseW = Math.min(1, coarse / 0.28) * (1 - fineW);
        const rockW = Math.max(0, 1 - fineW - coarseW);
        colors[vi * 4]     = rockW * 0.48 + coarseW * 0.52 + fineW * 0.72;
        colors[vi * 4 + 1] = rockW * 0.42 + coarseW * 0.47 + fineW * 0.58;
        colors[vi * 4 + 2] = rockW * 0.37 + coarseW * 0.40 + fineW * 0.36;
        colors[vi * 4 + 3] = 1.0;
      }
    }

    // Bake the editor-time SDF cave/tunnel surface into ordinary static
    // triangles. No runtime SDF/volume evaluation is needed in a game.
    for (let i = 0; i < caveVertexCount; i++) {
      const vi = vCount + i;
      const px = caveSys.positions[i * 3] || 0;
      const py = caveSys.positions[i * 3 + 1] || 0;
      const pz = caveSys.positions[i * 3 + 2] || 0;
      positions[vi * 3] = px;
      positions[vi * 3 + 1] = py;
      positions[vi * 3 + 2] = pz;
      normals[vi * 3] = caveSys.normals?.[i * 3] ?? 0;
      normals[vi * 3 + 1] = caveSys.normals?.[i * 3 + 1] ?? 1;
      normals[vi * 3 + 2] = caveSys.normals?.[i * 3 + 2] ?? 0;
      tangents[vi * 4] = 1;
      tangents[vi * 4 + 1] = 0;
      tangents[vi * 4 + 2] = 0;
      tangents[vi * 4 + 3] = 1;
      uvs[vi * 2] = Math.max(0, Math.min(1, px / span + 0.5));
      uvs[vi * 2 + 1] = Math.max(0, Math.min(1, pz / span + 0.5));
      const ao = Math.max(0, Math.min(1, caveSys.attribs?.[i * 4] ?? 0.55));
      const shade = 0.30 + ao * 0.58;
      colors[vi * 4] = 0.48 * shade;
      colors[vi * 4 + 1] = 0.43 * shade;
      colors[vi * 4 + 2] = 0.38 * shade;
      colors[vi * 4 + 3] = 1.0;
    }

    const portalAt = (u, v) => {
      if (!caveSys || !caveSys.portalTexData || caveVertexCount <= 0) return 0;
      const P = caveSys.portalMaskSize || 128;
      const ix = Math.max(0, Math.min(P - 1, Math.floor(u * P)));
      const iz = Math.max(0, Math.min(P - 1, Math.floor(v * P)));
      return caveSys.portalTexData[(iz * P + ix) * 4] || 0;
    };

    const qCount = (M - 1) * (M - 1);
    const indices = new Uint32Array(qCount * 6 + caveVertexCount);
    let ip = 0;
    for (let gz = 0; gz < M - 1; gz++) {
      for (let gx = 0; gx < M - 1; gx++) {
        const i00 = gz * M + gx;
        const i10 = i00 + 1;
        const i01 = i00 + M;
        const i11 = i01 + 1;
        // Cut a real polygon hole where an SDF cave/tunnel breaches the
        // heightfield, instead of relying on the editor's portal shader.
        if (portalAt((gx + 0.5) / Math.max(1, M - 1), (gz + 0.5) / Math.max(1, M - 1)) > 0.38) continue;
        indices[ip++] = i00;
        indices[ip++] = i01;
        indices[ip++] = i10;
        indices[ip++] = i10;
        indices[ip++] = i01;
        indices[ip++] = i11;
      }
    }

    for (let i = 0; i < caveVertexCount; i++) {
      indices[ip++] = vCount + i;
    }

    return { positions, normals, tangents, uvs, colors, indices: indices.slice(0, ip) };
  }
}
