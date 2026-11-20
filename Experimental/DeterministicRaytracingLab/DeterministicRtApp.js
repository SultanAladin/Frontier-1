// Frontier Engine — 100% Deterministic WebGPU Full Raytracer Application
// Zero Monte Carlo RNG | Zero Russian Roulette | 4-Wide SIMD BVH4 + Fibonacci DDGI + Analytic LTC + Cone Specular & Glass

import {
  DDGI_PROBE_UPDATE_WGSL,
  PRIMARY_AND_SHADOW_WGSL,
  BLOCKER_DILATE_WGSL,
  SHADOW_AO_FILTER_WGSL,
  SPECULAR_AND_GLASS_WGSL,
  COMPOSITE_WGSL,
  PRESENT_WGSL,
} from "./DeterministicRtShaders.js?v=2";

const TLAS4_RESERVED = 16;
const DDGI_PROBES = 20 * 16 * 6; // 1,920 world-space octahedral sensors
const DDGI_VEC4_PER_PROBE = 164; // 36 vec4f (6x6 irradiance) + 128 vec4f (16x16 Chebyshev radial-depth moments)
const UNIFORM_STRIDE = 256; // WebGPU minUniformBufferOffsetAlignment
const UNIFORM_SLOTS = 8;

const PAINT_PRESETS = {
  pearl: [0.92, 0.94, 0.98],
  cobalt: [0.07, 0.28, 0.86],
  crimson: [0.84, 0.05, 0.08],
};

const CAM_PRESETS = {
  hero: { target: [-0.45, 0.85, 0.62], yaw: -1.88, pitch: 0.29, dist: 6.95 },
  cockpit: { target: [-0.15, 0.55, 0.95], yaw: -1.32, pitch: 0.19, dist: 3.45 },
  mirror: { target: [-0.40, 3.20, 0.95], yaw: -1.62, pitch: 0.14, dist: 8.40 },
  bleed: { target: [-0.55, 0.65, 0.65], yaw: -0.78, pitch: 0.36, dist: 7.80 },
};

function mat4Identity() {
  const m = new Float32Array(16);
  m[0] = m[5] = m[10] = m[15] = 1.0;
  return m;
}

function mat4RigidZ(tx, ty, tz, yawRad) {
  const c = Math.cos(yawRad);
  const s = Math.sin(yawRad);
  const m = new Float32Array(16);
  m[0] = c;  m[4] = -s; m[8] = 0;  m[12] = tx;
  m[1] = s;  m[5] = c;  m[9] = 0;  m[13] = ty;
  m[2] = 0;  m[6] = 0;  m[10] = 1; m[14] = tz;
  m[3] = 0;  m[7] = 0;  m[11] = 0; m[15] = 1;
  return m;
}

function mat4InvertRigid(a) {
  const r = new Float32Array(16);
  r[0] = a[0]; r[4] = a[1]; r[8]  = a[2];
  r[1] = a[4]; r[5] = a[5]; r[9]  = a[6];
  r[2] = a[8]; r[6] = a[9]; r[10] = a[10];
  const tx = a[12], ty = a[13], tz = a[14];
  r[12] = -(r[0] * tx + r[4] * ty + r[8]  * tz);
  r[13] = -(r[1] * tx + r[5] * ty + r[9]  * tz);
  r[14] = -(r[2] * tx + r[6] * ty + r[10] * tz);
  r[15] = 1.0;
  return r;
}

function xformPt(m, x, y, z) {
  return [
    m[0] * x + m[4] * y + m[8]  * z + m[12],
    m[1] * x + m[5] * y + m[9]  * z + m[13],
    m[2] * x + m[6] * y + m[10] * z + m[14],
  ];
}

function norm3(v) {
  const l = Math.hypot(v[0], v[1], v[2]) || 1.0;
  return [v[0] / l, v[1] / l, v[2] / l];
}

function cross3(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

function showFatalOverlay(title, message) {
  const ov = document.getElementById("statusOverlay");
  document.getElementById("statusTitle").textContent = title;
  document.getElementById("statusMessage").textContent = message;
  ov.classList.remove("hidden");
}

async function loadFrt1Asset(url) {
  const resp = await fetch(url);
  if (!resp.ok) {
    throw new Error(`Failed to fetch ${url} (HTTP ${resp.status})`);
  }
  const buf = await resp.arrayBuffer();
  const dv = new DataView(buf);
  const magic = String.fromCharCode(dv.getUint8(0), dv.getUint8(1), dv.getUint8(2), dv.getUint8(3));
  if (magic !== "FRT1") {
    throw new Error(`Unexpected asset header '${magic}' (expected 'FRT1')`);
  }
  const meshCount = dv.getUint32(4, true);
  const totalBvh4Nodes = dv.getUint32(8, true);
  const totalTris = dv.getUint32(12, true);

  let offset = 16;
  const meshes = [];
  for (let i = 0; i < meshCount; i++) {
    meshes.push({
      bvh4Offset: dv.getUint32(offset + 0, true),
      bvh4Count: dv.getUint32(offset + 4, true),
      triOffset: dv.getUint32(offset + 8, true),
      triCount: dv.getUint32(offset + 12, true),
      minBounds: [
        dv.getFloat32(offset + 16, true),
        dv.getFloat32(offset + 20, true),
        dv.getFloat32(offset + 24, true),
      ],
      maxBounds: [
        dv.getFloat32(offset + 28, true),
        dv.getFloat32(offset + 32, true),
        dv.getFloat32(offset + 36, true),
      ],
    });
    offset += 40;
  }

  const bvh4Bytes = totalBvh4Nodes * 128;
  const blas4Floats = new Float32Array(buf, offset, totalBvh4Nodes * 32);
  offset += bvh4Bytes;

  const trisFloats = new Float32Array(buf, offset, totalTris * 16);

  const fullBvh4Floats = new Float32Array((TLAS4_RESERVED + totalBvh4Nodes) * 32);
  fullBvh4Floats.set(blas4Floats, TLAS4_RESERVED * 32);

  return { meshCount, totalBvh4Nodes, totalTris, meshes, fullBvh4Floats, trisFloats };
}

class DeterministicWebGpuRaytracer {
  constructor(canvas) {
    this.canvas = canvas;
    this.state = {
      viewMode: 0,
      splitRatio: 0.56,
      ddgiBounces: 99, // 1, 2, 3, or 99 (live infinite feedback)
      ddgiDirtySolve: true,
      probeSliceMode: -1, // -1 = full 1920 probes/frame, 1 = 4x rotational slice
      probeRays: 64,
      ddgiGain: 0.95,
      specBounces: 3,
      paint: [...PAINT_PRESETS.pearl],
      shadowRays: 8,
      aoRays: 6,
      sunRadius: 0.65,
      sunAzimuthDeg: 72,
      sunElevationDeg: 41.5,
      ltcGain: 0.78,
      resScale: 1.00,
      enableDdgi: true,
      enableLtc: true,
      enableSoftShadow: true,
      enableContactAo: true,
      enableGlassIor: true,
      show3dProbes: false,
      carX: -0.55,
      carY: 0.65,
      carYaw: 0.42,
      steerDeg: 13,
      camTarget: [...CAM_PRESETS.hero.target],
      camYaw: CAM_PRESETS.hero.yaw,
      camPitch: CAM_PRESETS.hero.pitch,
      camDist: CAM_PRESETS.hero.dist,
    };

    this.frameIndex = 0;
    this.ddgiPing = 0; // 0 => read A / write B; 1 => read B / write A
    this.renderW = 0;
    this.renderH = 0;
    this.hasTimestamps = false;
    this.queryPending = false;
    this.smoothFps = 60.0;
    this.smoothMs = 16.6;
    this.lastFrameTime = performance.now();
  }

  async init() {
    if (!navigator.gpu) {
      showFatalOverlay(
        "WebGPU Not Enabled in This Browser",
        "This 100% Deterministic Raytracer runs native WGSL @compute 4-Wide BVH4 shaders on WebGPU. Use Chrome/Edge 113+ or Firefox Nightly with WebGPU enabled. Showing verified C++20 FRT1 deterministic render below:"
      );
      return;
    }

    let adapter = null;
    for (const opts of [{ powerPreference: "high-performance" }, {}, { forceFallbackAdapter: true }]) {
      try {
        adapter = await navigator.gpu.requestAdapter(opts);
        if (adapter) break;
      } catch (_e) {
        // Continue to next WebGPU adapter option
      }
    }
    if (!adapter) {
      showFatalOverlay(
        "No WebGPU Hardware Adapter Found",
        "navigator.gpu is present, but Chrome's GPU process could not open an adapter (click 'Relaunch to update' in Chrome or verify Hardware Acceleration in chrome://settings/system). Showing verified C++20 FRT1 deterministic render below:"
      );
      return;
    }

    const reqFeatures = [];
    if (adapter.features.has("timestamp-query")) {
      reqFeatures.push("timestamp-query");
      this.hasTimestamps = true;
    }

    this.device = await adapter.requestDevice({ requiredFeatures: reqFeatures });
    this.context = this.canvas.getContext("webgpu");
    this.canvasFormat = navigator.gpu.getPreferredCanvasFormat();
    this.context.configure({
      device: this.device,
      format: this.canvasFormat,
      alphaMode: "opaque",
    });

    const info = adapter.info || {};
    const adapterDesc = info.description || info.device || info.vendor || "WebGPU Hardware Adapter";
    document.getElementById("gpuAdapterLabel").textContent =
      `${adapterDesc} · WGSL @compute 4-Wide BVH4${this.hasTimestamps ? " · HW Timestamp Queries" : ""}`;

    this.assets = await loadFrt1Asset("./Assets/DeterministicRtAssets.bin");
    document.getElementById("hudBvhNodes").textContent =
      `${this.assets.totalBvh4Nodes.toLocaleString()} BVH4 nodes`;

    await this.createGpuBuffersAndPipelines();
    this.updateTlasAndInstances();
    this.resizeIfNeeded(true);
    this.bindUiControls();
    this.bindViewportInput();

    requestAnimationFrame((now) => this.frame(now));
  }

  async compileModule(label, code) {
    const mod = this.device.createShaderModule({ label, code });
    if (mod.getCompilationInfo) {
      const info = await mod.getCompilationInfo();
      for (const msg of info.messages) {
        if (msg.type === "error") {
          throw new Error(`[${label}] Line ${msg.lineNum}:${msg.linePos} — ${msg.message}`);
        }
      }
    }
    return mod;
  }

  async createGpuBuffersAndPipelines() {
    const d = this.device;

    // 1. Uniform Buffer with 8 x 256-byte Dynamic Offset Slots
    this.uniformStaging = new ArrayBuffer(UNIFORM_STRIDE * UNIFORM_SLOTS);
    this.uniformBuffer = d.createBuffer({
      label: "FrameUniformsDynamic",
      size: UNIFORM_STRIDE * UNIFORM_SLOTS,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    // 2. 4-Wide BVH4 + Triangle + Instance Storage Buffers
    this.bvh4Buffer = d.createBuffer({
      label: "Bvh4NodesBuffer",
      size: this.assets.fullBvh4Floats.byteLength,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    d.queue.writeBuffer(this.bvh4Buffer, 0, this.assets.fullBvh4Floats);

    this.triBuffer = d.createBuffer({
      label: "PackedTrisBuffer",
      size: this.assets.trisFloats.byteLength,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    d.queue.writeBuffer(this.triBuffer, 0, this.assets.trisFloats);

    this.instanceBuffer = d.createBuffer({
      label: "InstancesBuffer",
      size: 10 * 144,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });

    // 3. Ping-Pong DDGI 6x6 Octahedral Irradiance + 16x16 Chebyshev Radial-Depth Moments Buffers
    // 1,920 probes * 164 vec4f * 16 bytes = 5,038,080 bytes (~4.80 MB)
    const ddgiFloatsCount = DDGI_PROBES * DDGI_VEC4_PER_PROBE * 4;
    const initDdgi = new Float32Array(ddgiFloatsCount);
    for (let p = 0; p < DDGI_PROBES; p++) {
      const base = p * DDGI_VEC4_PER_PROBE * 4;
      initDdgi[base + 3] = 1.0; // probeActive = 1.0 until classified
      for (let d = 0; d < 128; d++) {
        const off = base + (36 + d) * 4;
        initDdgi[off + 0] = 12.0;
        initDdgi[off + 1] = 144.0;
        initDdgi[off + 2] = 12.0;
        initDdgi[off + 3] = 144.0;
      }
    }
    this.ddgiBufferA = d.createBuffer({
      label: "DdgiAtlasA",
      size: initDdgi.byteLength,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    this.ddgiBufferB = d.createBuffer({
      label: "DdgiAtlasB",
      size: initDdgi.byteLength,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    d.queue.writeBuffer(this.ddgiBufferA, 0, initDdgi);
    d.queue.writeBuffer(this.ddgiBufferB, 0, initDdgi);

    // 4. Optional GPU Timestamp QuerySet (10 timestamps = 5 compute stages)
    if (this.hasTimestamps) {
      this.querySet = d.createQuerySet({ type: "timestamp", count: 10 });
      this.queryResolveBuf = d.createBuffer({
        size: 10 * 8,
        usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC,
      });
      this.queryReadBuf = d.createBuffer({
        size: 10 * 8,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
      });
    }

    // 5. Explicit BindGroupLayouts (ensures rgba32float unfilterable-float works everywhere)
    const C = GPUShaderStage.COMPUTE;
    this.bglScene = d.createBindGroupLayout({
      label: "SceneAndDdgiReadBGL",
      entries: [
        { binding: 0, visibility: C, buffer: { type: "uniform", hasDynamicOffset: true, minBindingSize: 192 } },
        { binding: 1, visibility: C, buffer: { type: "read-only-storage" } },
        { binding: 2, visibility: C, buffer: { type: "read-only-storage" } },
        { binding: 3, visibility: C, buffer: { type: "read-only-storage" } },
        { binding: 4, visibility: C, buffer: { type: "read-only-storage" } },
      ],
    });

    this.bglDdgiWrite = d.createBindGroupLayout({
      label: "DdgiWriteBGL",
      entries: [{ binding: 0, visibility: C, buffer: { type: "storage" } }],
    });

    this.bglPrimaryOut = d.createBindGroupLayout({
      label: "PrimaryOutBGL",
      entries: [
        { binding: 0, visibility: C, storageTexture: { access: "write-only", format: "rgba32float" } },
        { binding: 1, visibility: C, storageTexture: { access: "write-only", format: "rgba16float" } },
        { binding: 2, visibility: C, storageTexture: { access: "write-only", format: "rgba16float" } },
      ],
    });

    this.bglFilter = d.createBindGroupLayout({
      label: "DilateAndFilterBGL",
      entries: [
        { binding: 0, visibility: C, texture: { sampleType: "unfilterable-float" } },
        { binding: 1, visibility: C, texture: { sampleType: "unfilterable-float" } },
        { binding: 2, visibility: C, texture: { sampleType: "unfilterable-float" } },
        { binding: 3, visibility: C, storageTexture: { access: "write-only", format: "rgba16float" } },
      ],
    });

    this.bglSpecOut = d.createBindGroupLayout({
      label: "SpecGlassOutBGL",
      entries: [
        { binding: 0, visibility: C, texture: { sampleType: "unfilterable-float" } },
        { binding: 1, visibility: C, texture: { sampleType: "unfilterable-float" } },
        { binding: 2, visibility: C, storageTexture: { access: "write-only", format: "rgba16float" } },
      ],
    });

    this.bglComposite = d.createBindGroupLayout({
      label: "CompositeBGL",
      entries: [
        { binding: 0, visibility: C, texture: { sampleType: "unfilterable-float" } },
        { binding: 1, visibility: C, texture: { sampleType: "unfilterable-float" } },
        { binding: 2, visibility: C, texture: { sampleType: "unfilterable-float" } },
        { binding: 3, visibility: C, texture: { sampleType: "unfilterable-float" } },
        { binding: 4, visibility: C, storageTexture: { access: "write-only", format: "rgba16float" } },
      ],
    });

    this.bglPresent = d.createBindGroupLayout({
      label: "PresentBGL",
      entries: [
        { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: "unfilterable-float" } },
      ],
    });

    // 6. Scene BindGroups (Ping-Pong A and B)
    const makeSceneBg = (ddgiReadBuf) =>
      d.createBindGroup({
        layout: this.bglScene,
        entries: [
          { binding: 0, resource: { buffer: this.uniformBuffer, offset: 0, size: 192 } },
          { binding: 1, resource: { buffer: this.bvh4Buffer } },
          { binding: 2, resource: { buffer: this.triBuffer } },
          { binding: 3, resource: { buffer: this.instanceBuffer } },
          { binding: 4, resource: { buffer: ddgiReadBuf } },
        ],
      });
    this.bgSceneReadA = makeSceneBg(this.ddgiBufferA);
    this.bgSceneReadB = makeSceneBg(this.ddgiBufferB);

    this.bgDdgiWriteA = d.createBindGroup({
      layout: this.bglDdgiWrite,
      entries: [{ binding: 0, resource: { buffer: this.ddgiBufferA } }],
    });
    this.bgDdgiWriteB = d.createBindGroup({
      layout: this.bglDdgiWrite,
      entries: [{ binding: 0, resource: { buffer: this.ddgiBufferB } }],
    });

    // 7. Compile WGSL Modules & Create Compute + Present Pipelines
    const [mDdgi, mPrim, mDilate, mFilter, mSpec, mComp, mPresent] = await Promise.all([
      this.compileModule("DdgiProbeUpdate", DDGI_PROBE_UPDATE_WGSL),
      this.compileModule("PrimaryAndShadow", PRIMARY_AND_SHADOW_WGSL),
      this.compileModule("BlockerDilate", BLOCKER_DILATE_WGSL),
      this.compileModule("ShadowAoFilter", SHADOW_AO_FILTER_WGSL),
      this.compileModule("SpecularAndGlass", SPECULAR_AND_GLASS_WGSL),
      this.compileModule("Composite", COMPOSITE_WGSL),
      this.compileModule("Present", PRESENT_WGSL),
    ]);

    this.pipeDdgi = d.createComputePipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.bglScene, this.bglDdgiWrite] }),
      compute: { module: mDdgi, entryPoint: "main" },
    });
    this.pipePrimary = d.createComputePipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.bglScene, this.bglPrimaryOut] }),
      compute: { module: mPrim, entryPoint: "main" },
    });
    this.pipeDilate = d.createComputePipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.bglScene, this.bglFilter] }),
      compute: { module: mDilate, entryPoint: "main" },
    });
    this.pipeFilter = d.createComputePipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.bglScene, this.bglFilter] }),
      compute: { module: mFilter, entryPoint: "main" },
    });
    this.pipeSpec = d.createComputePipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.bglScene, this.bglSpecOut] }),
      compute: { module: mSpec, entryPoint: "main" },
    });
    this.pipeComp = d.createComputePipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.bglScene, this.bglComposite] }),
      compute: { module: mComp, entryPoint: "main" },
    });
    this.pipePresent = d.createRenderPipeline({
      layout: d.createPipelineLayout({ bindGroupLayouts: [this.bglPresent] }),
      vertex: { module: mPresent, entryPoint: "vsMain" },
      fragment: {
        module: mPresent,
        entryPoint: "fsMain",
        targets: [{ format: this.canvasFormat }],
      },
      primitive: { topology: "triangle-list" },
    });
  }

  updateTlasAndInstances() {
    const { carX, carY, carYaw, steerDeg } = this.state;
    const steerRad = (steerDeg * Math.PI) / 180.0;
    const tyreR = 0.50855;

    const bodyM = mat4RigidZ(carX, carY, tyreR, carYaw);
    const mounts = [
      { x:  1.665, y:  1.045, front: true,  right: false },
      { x:  1.665, y: -1.045, front: true,  right: true  },
      { x: -1.705, y:  1.045, front: false, right: false },
      { x: -1.705, y: -1.045, front: false, right: true  },
    ];
    const cy = Math.cos(carYaw);
    const sy = Math.sin(carYaw);
    const wheelMats = mounts.map((m) => {
      const wx = carX + m.x * cy - m.y * sy;
      const wy = carY + m.x * sy + m.y * cy;
      const wyaw = carYaw + (m.front ? steerRad : 0.0) + (m.right ? Math.PI : 0.0);
      return mat4RigidZ(wx, wy, tyreR, wyaw);
    });

    const instList = [
      { l2w: bodyM, matOverride: -1, meshIdx: 0 },
      ...wheelMats.map((m) => ({ l2w: m, matOverride: -1, meshIdx: 1 })),
      ...wheelMats.map((m) => ({ l2w: m, matOverride: 11, meshIdx: 2 })),
      { l2w: mat4Identity(), matOverride: -1, meshIdx: 3 },
    ];

    // Pack 10 instances x 144 bytes (36 x 4-byte words)
    const instBuf = new ArrayBuffer(10 * 144);
    const f32 = new Float32Array(instBuf);
    const i32 = new Int32Array(instBuf);
    instList.forEach((inst, idx) => {
      const w2l = inst.meshIdx === 3 ? mat4Identity() : mat4InvertRigid(inst.l2w);
      const base = idx * 36;
      f32.set(inst.l2w, base + 0);
      f32.set(w2l, base + 16);
      i32[base + 32] = inst.matOverride;
      i32[base + 33] = inst.meshIdx;
      i32[base + 34] = 0;
      i32[base + 35] = 0;
    });
    this.device.queue.writeBuffer(this.instanceBuffer, 0, instBuf);

    // Build 4-Wide TLAS4 in nodes 0..3
    const tlasFloats = new Float32Array(4 * 32);
    for (let n = 0; n < 4; n++) {
      const b = n * 32;
      for (let k = 0; k < 4; k++) {
        tlasFloats[b + 0 + k]  = 1e30;  tlasFloats[b + 4 + k]  = -1e30;
        tlasFloats[b + 8 + k]  = 1e30;  tlasFloats[b + 12 + k] = -1e30;
        tlasFloats[b + 16 + k] = 1e30;  tlasFloats[b + 20 + k] = -1e30;
      }
    }

    const groups = [
      [0, 9, 1, 2],
      [3, 4, 5, 6],
      [7, 8, -1, -1],
    ];

    for (let g = 0; g < 3; g++) {
      const gmn = [1e30, 1e30, 1e30];
      const gmx = [-1e30, -1e30, -1e30];
      const childNodeBase = (g + 1) * 32;

      for (let s = 0; s < 4; s++) {
        const ii = groups[g][s];
        if (ii < 0) continue;
        const inst = instList[ii];
        const mb = this.assets.meshes[inst.meshIdx];
        const imn = [1e30, 1e30, 1e30];
        const imx = [-1e30, -1e30, -1e30];
        for (let c = 0; c < 8; c++) {
          const lx = (c & 1) ? mb.maxBounds[0] : mb.minBounds[0];
          const ly = (c & 2) ? mb.maxBounds[1] : mb.minBounds[1];
          const lz = (c & 4) ? mb.maxBounds[2] : mb.minBounds[2];
          const wp = xformPt(inst.l2w, lx, ly, lz);
          for (let a = 0; a < 3; a++) {
            imn[a] = Math.min(imn[a], wp[a]);
            imx[a] = Math.max(imx[a], wp[a]);
          }
        }
        tlasFloats[childNodeBase + 0 + s]  = imn[0];
        tlasFloats[childNodeBase + 4 + s]  = imx[0];
        tlasFloats[childNodeBase + 8 + s]  = imn[1];
        tlasFloats[childNodeBase + 12 + s] = imx[1];
        tlasFloats[childNodeBase + 16 + s] = imn[2];
        tlasFloats[childNodeBase + 20 + s] = imx[2];
        tlasFloats[childNodeBase + 24 + s] = TLAS4_RESERVED + mb.bvh4Offset;
        tlasFloats[childNodeBase + 28 + s] = -(ii + 1);

        for (let a = 0; a < 3; a++) {
          gmn[a] = Math.min(gmn[a], imn[a]);
          gmx[a] = Math.max(gmx[a], imx[a]);
        }
      }

      tlasFloats[0 + g]  = gmn[0];
      tlasFloats[4 + g]  = gmx[0];
      tlasFloats[8 + g]  = gmn[1];
      tlasFloats[12 + g] = gmx[1];
      tlasFloats[16 + g] = gmn[2];
      tlasFloats[20 + g] = gmx[2];
      tlasFloats[24 + g] = g + 1;
      tlasFloats[28 + g] = 0;
    }

    this.device.queue.writeBuffer(this.bvh4Buffer, 0, tlasFloats);
    this.state.ddgiDirtySolve = true;
  }

  resizeIfNeeded(force = false) {
    const dpr = Math.min(window.devicePixelRatio || 1, 2.0);
    const dispW = Math.max(320, Math.floor(window.innerWidth * dpr));
    const dispH = Math.max(240, Math.floor(window.innerHeight * dpr));
    if (this.canvas.width !== dispW || this.canvas.height !== dispH) {
      this.canvas.width = dispW;
      this.canvas.height = dispH;
      force = true;
    }

    const rw = Math.max(320, Math.floor(dispW * this.state.resScale));
    const rh = Math.max(240, Math.floor(dispH * this.state.resScale));
    if (!force && rw === this.renderW && rh === this.renderH) {
      return;
    }

    this.renderW = rw;
    this.renderH = rh;

    const kill = (t) => t && t.destroy();
    kill(this.texPosT);
    kill(this.texNrmMat);
    kill(this.texShadowRaw);
    kill(this.texShadowPing);
    kill(this.texShadowPong);
    kill(this.texSpecGlass);
    kill(this.texDisplay);

    const d = this.device;
    const usageRW = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
    this.texPosT       = d.createTexture({ size: [rw, rh], format: "rgba32float", usage: usageRW });
    this.texNrmMat     = d.createTexture({ size: [rw, rh], format: "rgba16float", usage: usageRW });
    this.texShadowRaw  = d.createTexture({ size: [rw, rh], format: "rgba16float", usage: usageRW });
    this.texShadowPing = d.createTexture({ size: [rw, rh], format: "rgba16float", usage: usageRW });
    this.texShadowPong = d.createTexture({ size: [rw, rh], format: "rgba16float", usage: usageRW });
    this.texSpecGlass  = d.createTexture({ size: [rw, rh], format: "rgba16float", usage: usageRW });
    this.texDisplay    = d.createTexture({ size: [rw, rh], format: "rgba16float", usage: usageRW });

    const vPos    = this.texPosT.createView();
    const vNrm    = this.texNrmMat.createView();
    const vShRaw  = this.texShadowRaw.createView();
    const vShPing = this.texShadowPing.createView();
    const vShPong = this.texShadowPong.createView();
    const vSpec   = this.texSpecGlass.createView();
    const vDisp   = this.texDisplay.createView();

    this.bgPrimaryOut = d.createBindGroup({
      layout: this.bglPrimaryOut,
      entries: [
        { binding: 0, resource: vPos },
        { binding: 1, resource: vNrm },
        { binding: 2, resource: vShRaw },
      ],
    });
    this.bgDilate = d.createBindGroup({
      layout: this.bglFilter,
      entries: [
        { binding: 0, resource: vPos },
        { binding: 1, resource: vNrm },
        { binding: 2, resource: vShRaw },
        { binding: 3, resource: vShPing },
      ],
    });
    this.bgFilterPingToPong = d.createBindGroup({
      layout: this.bglFilter,
      entries: [
        { binding: 0, resource: vPos },
        { binding: 1, resource: vNrm },
        { binding: 2, resource: vShPing },
        { binding: 3, resource: vShPong },
      ],
    });
    this.bgFilterPongToPing = d.createBindGroup({
      layout: this.bglFilter,
      entries: [
        { binding: 0, resource: vPos },
        { binding: 1, resource: vNrm },
        { binding: 2, resource: vShPong },
        { binding: 3, resource: vShPing },
      ],
    });
    this.bgSpecOut = d.createBindGroup({
      layout: this.bglSpecOut,
      entries: [
        { binding: 0, resource: vPos },
        { binding: 1, resource: vNrm },
        { binding: 2, resource: vSpec },
      ],
    });
    this.bgComposite = d.createBindGroup({
      layout: this.bglComposite,
      entries: [
        { binding: 0, resource: vPos },
        { binding: 1, resource: vNrm },
        { binding: 2, resource: vShPong },
        { binding: 3, resource: vSpec },
        { binding: 4, resource: vDisp },
      ],
    });
    this.bgPresent = d.createBindGroup({
      layout: this.bglPresent,
      entries: [{ binding: 0, resource: vDisp }],
    });
  }

  writeUniformSlot(slotIdx, overrides = {}) {
    const s = this.state;
    const cp = Math.cos(s.camPitch);
    const camPos = [
      s.camTarget[0] + cp * Math.cos(s.camYaw) * s.camDist,
      s.camTarget[1] + cp * Math.sin(s.camYaw) * s.camDist,
      s.camTarget[2] + Math.sin(s.camPitch) * s.camDist,
    ];
    const camFwd = norm3([
      s.camTarget[0] - camPos[0],
      s.camTarget[1] - camPos[1],
      s.camTarget[2] - camPos[2],
    ]);
    const camRight = norm3(cross3(camFwd, [0, 0, 1]));
    const camUp = cross3(camRight, camFwd);

    const az = (s.sunAzimuthDeg * Math.PI) / 180.0;
    const el = (s.sunElevationDeg * Math.PI) / 180.0;
    const sunDir = norm3([Math.cos(el) * Math.cos(az), Math.cos(el) * Math.sin(az), Math.sin(el)]);

    const f = new Float32Array(this.uniformStaging, slotIdx * UNIFORM_STRIDE, 48);
    const tanHalfFov = Math.tan((26.0 * Math.PI) / 180.0);
    const aspect = this.renderW / this.renderH;

    // 0: camPosFov
    f[0] = camPos[0]; f[1] = camPos[1]; f[2] = camPos[2]; f[3] = tanHalfFov;
    // 1: camFwdAspect
    f[4] = camFwd[0]; f[5] = camFwd[1]; f[6] = camFwd[2]; f[7] = aspect;
    // 2: camRightSplit
    f[8] = camRight[0]; f[9] = camRight[1]; f[10] = camRight[2]; f[11] = s.splitRatio;
    // 3: camUpViewMode
    f[12] = camUp[0]; f[13] = camUp[1]; f[14] = camUp[2]; f[15] = s.viewMode;
    // 4: sunDirRadius
    f[16] = sunDir[0]; f[17] = sunDir[1]; f[18] = sunDir[2]; f[19] = s.sunRadius;
    // 5: paintBounces
    f[20] = s.paint[0]; f[21] = s.paint[1]; f[22] = s.paint[2]; f[23] = s.specBounces;
    // 6: resFilter
    f[24] = this.renderW;
    f[25] = this.renderH;
    f[26] = overrides.filterStep ?? 1;
    f[27] = this.frameIndex;
    // 7: ddgiOriginRays (strictly inside full interior air volume [-19.5..+19.45] x [-13.2..+13.20] x [0.25..5.85])
    f[28] = -19.5; f[29] = -13.2; f[30] = 0.25; f[31] = s.probeRays;
    // 8: ddgiSpaceHyst
    f[32] = 2.05; f[33] = 1.76; f[34] = 1.12; f[35] = overrides.hysteresis ?? 0.0;
    // 9: lightKnobs
    f[36] = s.ltcGain; f[37] = s.ddgiGain; f[38] = s.shadowRays; f[39] = s.aoRays;
    // 10: flagsA
    f[40] = s.enableDdgi ? 1 : 0;
    f[41] = s.enableLtc ? 1 : 0;
    f[42] = s.enableSoftShadow ? 1 : 0;
    f[43] = s.enableContactAo ? 1 : 0;
    // 11: flagsB
    f[44] = s.enableGlassIor ? 1 : 0;
    f[45] = s.show3dProbes ? 1 : 0;
    f[46] = overrides.ddgiReadBounce ?? 1;
    f[47] = overrides.probeSliceIdx ?? -1;
  }

  frame(now) {
    const dtMs = Math.max(0.5, now - this.lastFrameTime);
    this.lastFrameTime = now;
    this.smoothMs = this.smoothMs * 0.9 + dtMs * 0.1;
    this.smoothFps = 1000.0 / Math.max(this.smoothMs, 0.1);
    if ((this.frameIndex & 7) === 0) {
      document.getElementById("hudFps").textContent = this.smoothFps.toFixed(1);
      document.getElementById("hudFrameMs").textContent = `${this.smoothMs.toFixed(2)} ms`;
      if (!this.hasTimestamps) {
        for (const id of ["tmDdgi", "tmPrimary", "tmFilter", "tmSpec", "tmComp"]) {
          document.getElementById(id).textContent = "Active";
        }
      }
    }

    this.resizeIfNeeded(false);

    // Prepare Uniform Slots:
    // Slots 0..2: DDGI Bounce Passes (0 = 1st bounce, 1 = 2nd bounce, 2 = 3rd / live feedback)
    // Slot 3: Main Screen Passes (Primary, Dilate, Specular, Composite)
    // Slots 4..6: Shadow/AO Filter Steps (1, 2, 4)
    const isInfiniteLive = this.state.ddgiBounces >= 99;
    if (isInfiniteLive) {
      const sliceIdx = this.state.probeSliceMode > 0 ? (this.frameIndex & 3) : -1;
      const hyst = this.state.ddgiDirtySolve ? 0.0 : 0.86;
      this.writeUniformSlot(0, { ddgiReadBounce: 1, hysteresis: hyst, probeSliceIdx: sliceIdx });
      this.state.ddgiDirtySolve = false;
    } else {
      for (let b = 0; b < this.state.ddgiBounces; b++) {
        this.writeUniformSlot(b, { ddgiReadBounce: b > 0 ? 1 : 0, hysteresis: 0.0, probeSliceIdx: -1 });
      }
    }
    this.writeUniformSlot(3, { filterStep: 1 });
    this.writeUniformSlot(4, { filterStep: 1 });
    this.writeUniformSlot(5, { filterStep: 2 });
    this.writeUniformSlot(6, { filterStep: 4 });

    this.device.queue.writeBuffer(this.uniformBuffer, 0, this.uniformStaging);

    const enc = this.device.createCommandEncoder({ label: "DeterministicRtFrameEncoder" });
    const ts = (beginIdx, endIdx) =>
      this.hasTimestamps
        ? { timestampWrites: { querySet: this.querySet, beginningOfPassWriteIndex: beginIdx, endOfPassWriteIndex: endIdx } }
        : {};

    // Stage 1: Deterministic Spherical-Fibonacci DDGI Probe Update
    if (this.state.enableDdgi) {
      const passes = isInfiniteLive ? 1 : (this.state.ddgiDirtySolve ? this.state.ddgiBounces : 0);
      if (passes > 0) {
        const pDdgi = enc.beginComputePass({ label: "DdgiPass", ...ts(0, 1) });
        pDdgi.setPipeline(this.pipeDdgi);
        for (let b = 0; b < passes; b++) {
          const sceneBg = this.ddgiPing === 0 ? this.bgSceneReadA : this.bgSceneReadB;
          const writeBg = this.ddgiPing === 0 ? this.bgDdgiWriteB : this.bgDdgiWriteA;
          pDdgi.setBindGroup(0, sceneBg, [b * UNIFORM_STRIDE]);
          pDdgi.setBindGroup(1, writeBg);
          pDdgi.dispatchWorkgroups(DDGI_PROBES, 1, 1);
          this.ddgiPing ^= 1;
        }
        pDdgi.end();
        this.state.ddgiDirtySolve = false;
      } else if (this.hasTimestamps) {
        const pNoop = enc.beginComputePass({ ...ts(0, 1) });
        pNoop.end();
      }
    } else if (this.hasTimestamps) {
      const pNoop = enc.beginComputePass({ ...ts(0, 1) });
      pNoop.end();
    }

    const activeSceneBg = this.ddgiPing === 0 ? this.bgSceneReadA : this.bgSceneReadB;
    const wgX = Math.ceil(this.renderW / 8);
    const wgY = Math.ceil(this.renderH / 8);

    // Stage 2: Full-Res Primary Camera BVH4 + Solar Frustum + 4-Ray BVH4 Contact AO
    {
      const pPrim = enc.beginComputePass({ label: "PrimaryAndShadowPass", ...ts(2, 3) });
      pPrim.setPipeline(this.pipePrimary);
      pPrim.setBindGroup(0, activeSceneBg, [3 * UNIFORM_STRIDE]);
      pPrim.setBindGroup(1, this.bgPrimaryOut);
      pPrim.dispatchWorkgroups(wgX, wgY, 1);
      pPrim.end();
    }

    // Stage 3: Coplanar Blocker Dilation + 3-Pass Plane-Aware Continuous-Sigma Penumbra & AO Filter
    {
      const pFlt = enc.beginComputePass({ label: "PenumbraAndAoFilterPass", ...ts(4, 5) });
      pFlt.setPipeline(this.pipeDilate);
      pFlt.setBindGroup(0, activeSceneBg, [3 * UNIFORM_STRIDE]);
      pFlt.setBindGroup(1, this.bgDilate);
      pFlt.dispatchWorkgroups(wgX, wgY, 1);

      pFlt.setPipeline(this.pipeFilter);
      // Pass 0 (step 1): Ping -> Pong
      pFlt.setBindGroup(0, activeSceneBg, [4 * UNIFORM_STRIDE]);
      pFlt.setBindGroup(1, this.bgFilterPingToPong);
      pFlt.dispatchWorkgroups(wgX, wgY, 1);
      // Pass 1 (step 2): Pong -> Ping
      pFlt.setBindGroup(0, activeSceneBg, [5 * UNIFORM_STRIDE]);
      pFlt.setBindGroup(1, this.bgFilterPongToPing);
      pFlt.dispatchWorkgroups(wgX, wgY, 1);
      // Pass 2 (step 4): Ping -> Pong
      pFlt.setBindGroup(0, activeSceneBg, [6 * UNIFORM_STRIDE]);
      pFlt.setBindGroup(1, this.bgFilterPingToPong);
      pFlt.dispatchWorkgroups(wgX, wgY, 1);
      pFlt.end();
    }

    // Stage 4: Multi-Bounce Cone-Footprint Specular + Snell's-Law Cabin Glass Refraction
    {
      const pSpec = enc.beginComputePass({ label: "SpecularAndGlassPass", ...ts(6, 7) });
      pSpec.setPipeline(this.pipeSpec);
      pSpec.setBindGroup(0, activeSceneBg, [3 * UNIFORM_STRIDE]);
      pSpec.setBindGroup(1, this.bgSpecOut);
      pSpec.dispatchWorkgroups(wgX, wgY, 1);
      pSpec.end();
    }

    // Stage 5: Final Deterministic Composite + 3D DDGI Probe Spheres + ACES
    {
      const pComp = enc.beginComputePass({ label: "CompositePass", ...ts(8, 9) });
      pComp.setPipeline(this.pipeComp);
      pComp.setBindGroup(0, activeSceneBg, [3 * UNIFORM_STRIDE]);
      pComp.setBindGroup(1, this.bgComposite);
      pComp.dispatchWorkgroups(wgX, wgY, 1);
      pComp.end();
    }

    // Present to Swapchain Canvas
    {
      const swapView = this.context.getCurrentTexture().createView();
      const rp = enc.beginRenderPass({
        colorAttachments: [
          {
            view: swapView,
            loadOp: "clear",
            storeOp: "store",
            clearValue: { r: 0.03, g: 0.04, b: 0.07, a: 1.0 },
          },
        ],
      });
      rp.setPipeline(this.pipePresent);
      rp.setBindGroup(0, this.bgPresent);
      rp.draw(3, 1, 0, 0);
      rp.end();
    }

    if (this.hasTimestamps && !this.queryPending) {
      enc.resolveQuerySet(this.querySet, 0, 10, this.queryResolveBuf, 0);
      enc.copyBufferToBuffer(this.queryResolveBuf, 0, this.queryReadBuf, 0, 80);
    }

    this.device.queue.submit([enc.finish()]);

    if (this.hasTimestamps && !this.queryPending && (this.frameIndex & 7) === 0) {
      this.queryPending = true;
      this.queryReadBuf.mapAsync(GPUMapMode.READ).then(() => {
        const ns = new BigUint64Array(this.queryReadBuf.getMappedRange());
        const ms = (i0, i1) => Number(ns[i1] - ns[i0]) / 1e6;
        document.getElementById("tmDdgi").textContent = `${ms(0, 1).toFixed(2)} ms`;
        document.getElementById("tmPrimary").textContent = `${ms(2, 3).toFixed(2)} ms`;
        document.getElementById("tmFilter").textContent = `${ms(4, 5).toFixed(2)} ms`;
        document.getElementById("tmSpec").textContent = `${ms(6, 7).toFixed(2)} ms`;
        document.getElementById("tmComp").textContent = `${ms(8, 9).toFixed(2)} ms`;
        this.queryReadBuf.unmap();
        this.queryPending = false;
      }).catch(() => {
        this.queryPending = false;
      });
    }

    this.frameIndex++;
    requestAnimationFrame((t) => this.frame(t));
  }

  bindUiControls() {
    const modeNames = [
      "Split Compare",
      "Full Det. RT",
      "Clay GI + Penumbra",
      "3D DDGI Probes",
      "Spec + Glass Only",
      "BVH4 Heatmap",
    ];

    document.querySelectorAll("[data-view]").forEach((btn) => {
      btn.addEventListener("click", () => {
        document.querySelectorAll("[data-view]").forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
        this.state.viewMode = Number(btn.dataset.view);
        document.getElementById("activeModeTag").textContent = modeNames[this.state.viewMode];
        document.getElementById("splitBanner").style.display = this.state.viewMode === 0 ? "flex" : "none";
      });
    });

    document.querySelectorAll("[data-cam]").forEach((btn) => {
      btn.addEventListener("click", () => {
        const p = CAM_PRESETS[btn.dataset.cam];
        if (!p) return;
        this.state.camTarget = [...p.target];
        this.state.camYaw = p.yaw;
        this.state.camPitch = p.pitch;
        this.state.camDist = p.dist;
      });
    });

    document.querySelectorAll("[data-ddgi-bounces]").forEach((btn) => {
      btn.addEventListener("click", () => {
        document.querySelectorAll("[data-ddgi-bounces]").forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
        this.state.ddgiBounces = Number(btn.dataset.ddgiBounces);
        this.state.ddgiDirtySolve = true;
        document.getElementById("ddgiBounceLabel").textContent =
          this.state.ddgiBounces >= 99 ? "∞ Feedback" : `${this.state.ddgiBounces}-Bounce Exact`;
      });
    });

    document.querySelectorAll("[data-probe-slice]").forEach((btn) => {
      btn.addEventListener("click", () => {
        document.querySelectorAll("[data-probe-slice]").forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
        this.state.probeSliceMode = Number(btn.dataset.probeSlice);
      });
    });

    document.querySelectorAll("[data-spec-bounces]").forEach((btn) => {
      btn.addEventListener("click", () => {
        document.querySelectorAll("[data-spec-bounces]").forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
        this.state.specBounces = Number(btn.dataset.specBounces);
      });
    });

    document.querySelectorAll("[data-paint]").forEach((btn) => {
      btn.addEventListener("click", () => {
        document.querySelectorAll("[data-paint]").forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
        this.state.paint = [...PAINT_PRESETS[btn.dataset.paint]];
        this.state.ddgiDirtySolve = true;
      });
    });

    const bindSlider = (id, labelId, fmt, cb) => {
      const el = document.getElementById(id);
      const lb = document.getElementById(labelId);
      el.addEventListener("input", () => {
        const v = Number(el.value);
        lb.textContent = fmt(v);
        cb(v);
      });
    };

    bindSlider("slProbeRays", "valProbeRays", (v) => `${v}`, (v) => {
      this.state.probeRays = v;
      this.state.ddgiDirtySolve = true;
    });
    bindSlider("slDdgiGain", "valDdgiGain", (v) => v.toFixed(2), (v) => {
      this.state.ddgiGain = v;
    });
    bindSlider("slShadowRays", "valShadowRays", (v) => `${v}`, (v) => {
      this.state.shadowRays = v;
    });
    bindSlider("slSunRadius", "valSunRadius", (v) => v.toFixed(2), (v) => {
      this.state.sunRadius = v;
    });
    bindSlider("slSunAz", "valSunAz", (v) => `${v}°`, (v) => {
      this.state.sunAzimuthDeg = v;
      this.state.ddgiDirtySolve = true;
    });
    bindSlider("slLtcGain", "valLtcGain", (v) => v.toFixed(2), (v) => {
      this.state.ltcGain = v;
      this.state.ddgiDirtySolve = true;
    });
    bindSlider("slResScale", "valResScale", (v) => `${v.toFixed(2)}×`, (v) => {
      this.state.resScale = v;
    });
    bindSlider("slCarX", "valCarX", (v) => `${v.toFixed(2)} m`, (v) => {
      this.state.carX = v;
      this.updateTlasAndInstances();
    });
    bindSlider("slSteer", "valSteer", (v) => `${v}°`, (v) => {
      this.state.steerDeg = v;
      this.updateTlasAndInstances();
    });

    const bindChk = (id, key, markDdgi = false) => {
      const el = document.getElementById(id);
      el.addEventListener("change", () => {
        this.state[key] = el.checked;
        if (markDdgi) this.state.ddgiDirtySolve = true;
      });
    };
    bindChk("chkDdgi", "enableDdgi", true);
    bindChk("chkLtc", "enableLtc", true);
    bindChk("chkSoftShadow", "enableSoftShadow", false);
    bindChk("chkContactAo", "enableContactAo", false);
    bindChk("chkGlassIor", "enableGlassIor", false);
    bindChk("chkShowProbes", "show3dProbes", false);
  }

  bindViewportInput() {
    let dragging = false;
    let dragMode = "orbit";
    let lastX = 0;
    let lastY = 0;

    this.canvas.addEventListener("contextmenu", (e) => e.preventDefault());
    this.canvas.addEventListener("pointerdown", (e) => {
      dragging = true;
      lastX = e.clientX;
      lastY = e.clientY;
      this.canvas.setPointerCapture(e.pointerId);

      const splitScreenX = window.innerWidth * this.state.splitRatio;
      if (this.state.viewMode === 0 && (e.altKey || Math.abs(e.clientX - splitScreenX) < 14)) {
        dragMode = "split";
      } else if (e.button === 2 || e.shiftKey) {
        dragMode = "pan";
      } else {
        dragMode = "orbit";
      }
    });

    this.canvas.addEventListener("pointermove", (e) => {
      if (!dragging) return;
      const dx = e.clientX - lastX;
      const dy = e.clientY - lastY;
      lastX = e.clientX;
      lastY = e.clientY;

      if (dragMode === "split") {
        this.state.splitRatio = Math.min(0.95, Math.max(0.05, e.clientX / window.innerWidth));
      } else if (dragMode === "orbit") {
        this.state.camYaw -= dx * 0.0065;
        this.state.camPitch = Math.min(1.42, Math.max(0.03, this.state.camPitch + dy * 0.0055));
      } else if (dragMode === "pan") {
        const panScale = this.state.camDist * 0.0014;
        const rx = -Math.sin(this.state.camYaw);
        const ry = Math.cos(this.state.camYaw);
        this.state.camTarget[0] -= rx * dx * panScale;
        this.state.camTarget[1] -= ry * dx * panScale;
        this.state.camTarget[2] += dy * panScale;
      }
    });

    const endDrag = () => {
      dragging = false;
    };
    this.canvas.addEventListener("pointerup", endDrag);
    this.canvas.addEventListener("pointercancel", endDrag);

    this.canvas.addEventListener(
      "wheel",
      (e) => {
        e.preventDefault();
        this.state.camDist = Math.min(28.0, Math.max(2.2, this.state.camDist * (1.0 + e.deltaY * 0.0009)));
      },
      { passive: false }
    );
  }
}

window.addEventListener("DOMContentLoaded", () => {
  const app = new DeterministicWebGpuRaytracer(document.getElementById("viewportCanvas"));
  app.init().catch((err) => {
    console.error(err);
    showFatalOverlay("Deterministic WebGPU Pipeline Error", String(err.message || err));
  });
});
