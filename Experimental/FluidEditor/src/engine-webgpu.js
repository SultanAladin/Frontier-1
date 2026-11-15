/**
 * WebGPU 3D Eulerian Voxel Grid Pyro Solver
 * Uses WGSL @compute @workgroup_size(4, 4, 4) shaders over 3D storage buffers.
 * Gas-only extraction; the upstream compute and gas rendering passes are retained.
 */

import { WGSL_COMPUTE_SHADER, WGSL_RAYMARCH_SHADER } from './shaders-wgsl.js';

export class WebGPUPyroEngine {
  static async isAvailable() {
    if (typeof navigator === 'undefined' || !navigator.gpu) return false;
    try {
      const adapter = await navigator.gpu.requestAdapter();
      return !!adapter;
    } catch {
      return false;
    }
  }

  static async create(canvas, params) {

    if (!navigator.gpu) {
      throw new Error('WebGPU is not supported in this browser.');
    }
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) {
      throw new Error('No WebGPU adapter found.');
    }
    const device = await adapter.requestDevice();
    const context = canvas.getContext('webgpu');
    if (!context) {
      throw new Error('Could not acquire WebGPU canvas context.');
    }
    const format = navigator.gpu.getPreferredCanvasFormat();
    context.configure({
      device,
      format,
      alphaMode: 'opaque',
    });

    device.pushErrorScope('validation');
    let engine;
    try {
      engine = new WebGPUPyroEngine(canvas, params, device, context, format);
      const error = await device.popErrorScope();
      if (error) throw new Error(error.message);
      return engine;
    } catch (error) {
      device.destroy();
      throw error;
    }
  }

  constructor(canvas, params, device, context, format) {
    this.canvas = canvas;
    this.params = params;
    this.device = device;
    this.context = context;
    this.format = format;
    this.backendName = 'WebGPU (WGSL Compute)';

    this.time = 0.0;
    this.stepCount = 0;
    this.pendingBlasts = [];
    this.simDurationMs = 0.0;
    this.dynamicBoundsSurge = 0.0;
    this.dynamicBoundsTarget = 0.0;

    this.initPipelines();
    this.initGridBuffers(params.gridResolution);
  }

  getBoundsBox() {
    const p = this.params;
    const maxMult = p.dynamicBoundsMax || 1.55;
    const surgeMult = p.dynamicBounds ? 1.0 + this.dynamicBoundsSurge * (maxMult - 1.0) : 1.0;

    const effWidth = (p.boundsWidth || 1.85) * surgeMult;
    const effHeight = (p.boundsHeight || 2.10) * (1.0 + (surgeMult - 1.0) * 1.15);

    const floorY = -0.6;
    const boxMin = [-0.5 * effWidth, floorY, -0.5 * effWidth];
    const boxMax = [0.5 * effWidth, floorY + effHeight, 0.5 * effWidth];
    const domainScale = [effWidth, effHeight / 1.2, effWidth];

    return { boxMin, boxMax, domainScale, effWidth, effHeight, surgeMult };
  }

  initPipelines() {
    const device = this.device;

    const computeModule = device.createShaderModule({ code: WGSL_COMPUTE_SHADER });
    this.computeBGL = device.createBindGroupLayout({
      entries: [
        { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
        { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
        { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
        { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
        { binding: 4, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
        { binding: 5, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
      ],
    });
    const computeLayout = device.createPipelineLayout({ bindGroupLayouts: [this.computeBGL] });

    const makeCompute = (entryPoint) =>
      device.createComputePipeline({
        layout: computeLayout,
        compute: { module: computeModule, entryPoint },
      });

    this.pipelines = {
      splat: makeCompute('csSplat'),
      advect: makeCompute('csAdvect'),
      curl: makeCompute('csCurl'),
      forces: makeCompute('csForces'),
      divergence: makeCompute('csDivergence'),
      pressure: makeCompute('csPressure'),
      gradient: makeCompute('csGradient'),
    };

    const renderModule = device.createShaderModule({ code: WGSL_RAYMARCH_SHADER });
    this.renderBGL = device.createBindGroupLayout({
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT | GPUShaderStage.VERTEX, buffer: { type: 'uniform' } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
        { binding: 3, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
        { binding: 4, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
      ],
    });

    this.renderPipeline = device.createRenderPipeline({
      layout: device.createPipelineLayout({ bindGroupLayouts: [this.renderBGL] }),
      vertex: { module: renderModule, entryPoint: 'vsMain' },
      fragment: {
        module: renderModule,
        entryPoint: 'fsMain',
        targets: [{ format: this.format }],
      },
      primitive: { topology: 'triangle-list' },
    });

    this.simUniformBuffer = device.createBuffer({
      size: 176,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    this.renderUniformBuffer = device.createBuffer({
      size: 208,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
  }

  initGridBuffers(resolution) {
    const device = this.device;
    this.gridRes = Number(resolution) || 64;
    const numVoxels = this.gridRes * this.gridRes * this.gridRes;
    const byteSize = numVoxels * 16; // vec4<f32>

    if (this.buffers) {
      Object.values(this.buffers).forEach((b) => b && b.destroy());
    }

    const createStorage = () =>
      device.createBuffer({
        size: byteSize,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
      });

    this.buffers = {
      vel0: createStorage(),
      vel1: createStorage(),
      thermo0: createStorage(),
      thermo1: createStorage(),
      curl: createStorage(),
      div: createStorage(),
      pres0: createStorage(),
      pres1: createStorage(),
      dummy: createStorage(),
    };

    const makeBG = (b1, b2, b3, b4, b5) =>
      device.createBindGroup({
        layout: this.computeBGL,
        entries: [
          { binding: 0, resource: { buffer: this.simUniformBuffer } },
          { binding: 1, resource: { buffer: b1 } },
          { binding: 2, resource: { buffer: b2 } },
          { binding: 3, resource: { buffer: b3 } },
          { binding: 4, resource: { buffer: b4 } },
          { binding: 5, resource: { buffer: b5 } },
        ],
      });

    const B = this.buffers;
    this.bindGroups = {
      splat: makeBG(B.vel0, B.thermo0, B.dummy, B.vel1, B.thermo1),
      advect: makeBG(B.vel1, B.thermo1, B.dummy, B.vel0, B.thermo0),
      curl: makeBG(B.vel0, B.dummy, B.dummy, B.curl, B.dummy),
      forces: makeBG(B.vel0, B.thermo0, B.curl, B.vel1, B.dummy),
      divergence: makeBG(B.vel1, B.thermo0, B.dummy, B.div, B.dummy),
      pres0to1: makeBG(B.vel1, B.pres0, B.div, B.pres1, B.dummy),
      pres1to0: makeBG(B.vel1, B.pres1, B.div, B.pres0, B.dummy),
      gradient: makeBG(B.vel1, B.pres0, B.dummy, B.vel0, B.dummy),
      render0: device.createBindGroup({
        layout: this.renderBGL,
        entries: [
          { binding: 0, resource: { buffer: this.renderUniformBuffer } },
          { binding: 1, resource: { buffer: B.thermo0 } },
          { binding: 2, resource: { buffer: B.vel0 } },
          { binding: 3, resource: { buffer: B.curl } },
          { binding: 4, resource: { buffer: B.pres0 } },
        ],
      }),
      render1: device.createBindGroup({
        layout: this.renderBGL,
        entries: [
          { binding: 0, resource: { buffer: this.renderUniformBuffer } },
          { binding: 1, resource: { buffer: B.thermo1 } },
          { binding: 2, resource: { buffer: B.vel1 } },
          { binding: 3, resource: { buffer: B.curl } },
          { binding: 4, resource: { buffer: B.pres0 } },
        ],
      }),
    };
  }

  setGridResolution(resolution) {
    if (Number(resolution) !== this.gridRes) {
      this.initGridBuffers(Number(resolution));
    }
  }

  clearGrid() {
    const encoder = this.device.createCommandEncoder();
    Object.values(this.buffers).forEach((b) => {
      encoder.clearBuffer(b);
    });
    this.device.queue.submit([encoder.finish()]);
  }

  setBrush(pos, vel, active = true) {
    if (active && pos) {
      this.triggerExplosion({
        center: pos,
        radius: 0.095,
        strength: 4.5,
        temp: 4.6,
        fuel: 3.4,
        smoke: 1.6,
        lobes: 4.0,
      });
    }
  }

  spawnShrapnelBurst(center = [0.5, 0.22, 0.5], count = 6) {
    for (let i = 0; i < Math.min(4, count); i++) {
      const angle = (i / count) * Math.PI * 2.0;
      setTimeout(() => {
        this.triggerExplosion({
          center: [
            Math.max(0.15, Math.min(0.85, center[0] + Math.cos(angle) * 0.22)),
            Math.min(0.75, center[1] + 0.18),
            Math.max(0.15, Math.min(0.85, center[2] + Math.sin(angle) * 0.22)),
          ],
          radius: 0.11,
          strength: 5.5,
          temp: 5.0,
          fuel: 3.6,
          smoke: 1.8,
          lobes: 5.0,
        });
      }, i * 60);
    }
  }

  triggerExplosion(options = {}) {
    const p = this.params;

    this.pendingBlasts.push({
      center: options.center || [0.5, 0.18, 0.5],
      radius: options.radius ?? p.blastRadius,
      strength: options.strength ?? p.blastStrength,
      temp: options.temp ?? p.blastTemperature,
      fuel: options.fuel ?? p.blastFuel,
      smoke: options.smoke ?? p.blastSmoke,
      lobes: options.lobes ?? p.blastLobes,
      seed: Math.random() * 100.0,
    });
    this.dynamicBoundsTarget = Math.min(1.0, this.dynamicBoundsTarget + 0.65);
  }

  stepSimulation(rawDt) {
    const t0 = performance.now();
    const p = this.params;
    const dt = Math.min(rawDt, 0.033) * p.timeScale;
    this.time += dt;
    this.stepCount++;

    if (p.dynamicBounds) {
      const riseSpeed = this.dynamicBoundsTarget > this.dynamicBoundsSurge ? 2.8 : 0.45;
      this.dynamicBoundsSurge += (this.dynamicBoundsTarget - this.dynamicBoundsSurge) * Math.min(1.0, dt * riseSpeed);
      this.dynamicBoundsTarget = Math.max(0.0, this.dynamicBoundsTarget - dt * 0.22);
    } else {
      this.dynamicBoundsSurge = 0.0;
      this.dynamicBoundsTarget = 0.0;
    }

    const blast = this.pendingBlasts.shift();
    const buf = new ArrayBuffer(176);
    const u32 = new Uint32Array(buf);
    const f32 = new Float32Array(buf);

    u32[0] = this.gridRes;
    u32[1] = p.macCormackAdvection ? 1 : 0;
    u32[2] = p.enclosedBox ? 1 : 0;
    u32[3] = p.emitterEnabled ? 1 : 0;

    f32[4] = dt;
    f32[5] = this.time;
    f32[6] = p.emitterRate;
    f32[7] = p.emitterRadius;

    f32[8] = p.emitterHeight;
    f32[9] = p.emitterUpwardVelocity;
    f32[10] = p.emitterSwirl;
    f32[11] = p.emitterTemperature;

    f32[12] = p.emitterFuel;
    f32[13] = p.emitterSmoke;
    u32[14] = blast ? 1 : 0;
    u32[15] = p.obstacleType;

    f32[16] = blast ? blast.center[0] : 0.5;
    f32[17] = blast ? blast.center[1] : 0.22;
    f32[18] = blast ? blast.center[2] : 0.5;
    f32[19] = blast ? blast.radius : p.blastRadius;

    f32[20] = blast ? blast.strength : p.blastStrength;
    f32[21] = blast ? blast.temp : p.blastTemperature;
    f32[22] = blast ? blast.fuel : p.blastFuel;
    f32[23] = blast ? blast.smoke : p.blastSmoke;

    f32[24] = blast ? blast.lobes : p.blastLobes;
    f32[25] = blast ? blast.seed : 0.0;
    f32[26] = p.burnRate;
    f32[27] = p.burnHeat;

    f32[28] = p.sootGeneration;
    f32[29] = p.coolingRate;
    f32[30] = p.smokeDissipation;
    f32[31] = p.velocityDamping;

    f32[32] = p.vorticityConfinement;
    f32[33] = p.buoyancy;
    f32[34] = p.smokeWeight;
    f32[35] = p.combustionExpansion;

    f32[36] = p.turbulenceStrength;
    f32[37] = p.turbulenceScale;
    f32[38] = p.windX;
    f32[39] = p.windZ;

    f32[40] = p.obstacleX;
    f32[41] = p.obstacleY;
    f32[42] = p.obstacleZ;
    f32[43] = p.obstacleRadius;

    this.device.queue.writeBuffer(this.simUniformBuffer, 0, buf);

    const wg = Math.ceil(this.gridRes / 4);
    const encoder = this.device.createCommandEncoder();
    const pass = encoder.beginComputePass();

    const dispatch = (pipeline, bg) => {
      pass.setPipeline(pipeline);
      pass.setBindGroup(0, bg);
      pass.dispatchWorkgroups(wg, wg, wg);
    };



    dispatch(this.pipelines.splat, this.bindGroups.splat);
    dispatch(this.pipelines.advect, this.bindGroups.advect);
    dispatch(this.pipelines.curl, this.bindGroups.curl);
    dispatch(this.pipelines.forces, this.bindGroups.forces);
    dispatch(this.pipelines.divergence, this.bindGroups.divergence);

    // Ensure even number of Jacobi iterations so pres0 holds final result
    const pairs = Math.max(2, Math.round(p.pressureIterations / 2));
    for (let i = 0; i < pairs; i++) {
      dispatch(this.pipelines.pressure, this.bindGroups.pres0to1);
      dispatch(this.pipelines.pressure, this.bindGroups.pres1to0);
    }

    dispatch(this.pipelines.gradient, this.bindGroups.gradient);
    pass.end();

    this.device.queue.submit([encoder.finish()]);
    this.simDurationMs = performance.now() - t0;
  }

  render(camera) {
    const p = this.params;
    const az = (p.sunAzimuth * Math.PI) / 180;
    const el = (p.sunElevation * Math.PI) / 180;
    const sunX = Math.cos(el) * Math.sin(az);
    const sunY = Math.sin(el);
    const sunZ = Math.cos(el) * Math.cos(az);
    const tanHalfFov = Math.tan(camera.fovY * 0.5);

    const buf = new ArrayBuffer(208);
    const f32 = new Float32Array(buf);
    const u32 = new Uint32Array(buf);
    const { boxMin, boxMax } = this.getBoundsBox();

    f32[0] = camera.position[0];
    f32[1] = camera.position[1];
    f32[2] = camera.position[2];
    f32[3] = tanHalfFov;

    f32[4] = camera.forward[0];
    f32[5] = camera.forward[1];
    f32[6] = camera.forward[2];
    f32[7] = camera.aspect;

    f32[8] = camera.right[0];
    f32[9] = camera.right[1];
    f32[10] = camera.right[2];
    f32[11] = this.time;

    f32[12] = camera.up[0];
    f32[13] = camera.up[1];
    f32[14] = camera.up[2];
    u32[15] = this.gridRes;

    f32[16] = sunX;
    f32[17] = sunY;
    f32[18] = sunZ;
    f32[19] = p.voxelQuantization;

    u32[20] = p.renderChannel;
    u32[21] = p.colorPalette;
    u32[22] = p.raymarchSteps;
    u32[23] = p.shadowSteps;

    f32[24] = p.densityExtinction;
    f32[25] = p.smokeAlbedo;
    f32[26] = p.shadowDensity;
    f32[27] = p.fireIntensity;

    f32[28] = p.temperatureScale;
    f32[29] = p.internalScattering;
    f32[30] = p.phaseAnisotropy;
    f32[31] = p.ambientIntensity;

    f32[32] = p.sunIntensity;
    f32[33] = p.exposure;
    u32[34] = p.sliceAxis;
    f32[35] = p.slicePosition;

    u32[36] = p.obstacleType;
    f32[37] = p.obstacleX;
    f32[38] = p.obstacleY;
    f32[39] = p.obstacleZ;

    f32[40] = p.obstacleRadius;
    u32[41] = p.showBoundingBox ? 1 : 0;
    u32[42] = p.showVoxelGridLines ? 1 : 0;
    u32[43] = p.showFloorGrid ? 1 : 0;

    f32[44] = boxMin[0];
    f32[45] = boxMin[1];
    f32[46] = boxMin[2];
    f32[47] = 0.0;

    f32[48] = boxMax[0];
    f32[49] = boxMax[1];
    f32[50] = boxMax[2];
    f32[51] = 0.0;
    this.device.queue.writeBuffer(this.renderUniformBuffer, 0, buf);

    const encoder = this.device.createCommandEncoder();
    const view = this.context.getCurrentTexture().createView();
    const pass = encoder.beginRenderPass({
      colorAttachments: [
        {
          view,
          clearValue: { r: 0.015, g: 0.018, b: 0.024, a: 1.0 },
          loadOp: 'clear',
          storeOp: 'store',
        },
      ],
    });
    pass.setPipeline(this.renderPipeline);
    pass.setBindGroup(0, this.bindGroups.render0);
    pass.draw(3);
    pass.end();
    this.device.queue.submit([encoder.finish()]);
  }

  destroy() {
    if (this.buffers) {
      Object.values(this.buffers).forEach((b) => b && b.destroy());
    }
    if (this.simUniformBuffer) this.simUniformBuffer.destroy();
    if (this.renderUniformBuffer) this.renderUniformBuffer.destroy();
  }
}
