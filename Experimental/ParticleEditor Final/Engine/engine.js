// WebGPU engine for the Particle Editor: buffers, pipelines, per-frame encoding and
// asynchronous CPU readbacks. Scene logic (emission, UI, camera) lives in app.js.
// 📝 Converted from an IIFE to an ES module; the body keeps its two-space indent so that `git blame`
//    still points at whoever wrote each line rather than at the conversion.
import { PE } from "./pe.js";

  // Offsets into the System uniform, in vec4 slots (16 floats each). Mirrored in shaders.js.
  PE.SYS_SLOT = {
    origin: 0, dir: 1, speed: 2, life: 3, colA: 4, colB: 5, colC: 6, phys: 7,
    phys2: 8, phys3: 9, phys4: 10, mol: 11, mol2: 12, mol3: 13, emit: 14, misc: 15,
    colorCfg: 16, colorCfg2: 17, extra0: 18, extra1: 19, extra2: 20, extra3: 21,
  };
  // Offsets (floats) into the Glob uniform (240 bytes).
  PE.GLOB_OFF = {
    viewProj: 0, camRight: 16, camUp: 20, camPos: 24, windMin: 28, windSize: 32,
    windDim: 36, timing: 40, viz: 44, swirl: 48, ballPos: 52, ballVel: 56,
  };
  PE.WIND = { min: [-6, 0, -6], size: [12, 8, 12], dim: [24, 12, 24], maxComps: 16 };
  const PART_BYTES = 80;
  const TRAIL_SEGS = 20;
  const LINE_BYTES = 28;
  const MAX_LINE_VERTS = 32768;
  const MESH_BYTES = 40;
  const MAX_MESH_VERTS = 65536;
  const ARC_BYTES = 44;
  const MAX_ARC_VERTS = 24576;

  const ADD = {
    color: { srcFactor: "one", dstFactor: "one", operation: "add" },
    alpha: { srcFactor: "one", dstFactor: "one", operation: "add" },
  };
  const OVER = {
    color: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
    alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
  };

  // Per-system GPU resources.
  class SystemGPU {
    constructor(engine, params) {
      const d = engine.device;
      const U = GPUBufferUsage;
      this.engine = engine;
      this.cap = Math.max(64, Math.floor(params.capacity));
      this.isRibbon = !params.transition && ((params.curlMode || 0) === 3 || (params.trailMode || 0) > 0 || (params.webMode || 0) > 0 || (params.dynMode || 0) > 0 || (params.mudSplashMode || 0) > 0 || (params.beadsMode || 0) > 0 || (params.seedHeadMode || 0) > 0 || (params.jellyMode || 0) > 0 || (params.flareMode || 0) > 0 || (params.fireworkMode || 0) > 0);
      this.parts = d.createBuffer({ size: this.cap * PART_BYTES, usage: U.STORAGE | U.COPY_SRC | U.COPY_DST });
      this.snap = d.createBuffer({ size: this.cap * PART_BYTES, usage: U.STORAGE | U.COPY_DST });
      this.trail = this.isRibbon ? d.createBuffer({ size: this.cap * TRAIL_SEGS * 16, usage: U.STORAGE | U.COPY_DST }) : null;
      this.stats = d.createBuffer({ size: 32, usage: U.STORAGE | U.COPY_SRC });
      this.statStage = d.createBuffer({ size: 32, usage: U.MAP_READ | U.COPY_DST });
      this.uniform = d.createBuffer({ size: 352, usage: U.UNIFORM | U.COPY_DST });
      this.cellCount = engine.vacantCellCount;
      this.cellSlots = engine.vacantCellSlots;
      this.latticeBytes = 0;
      this.trailRes = this.trail || this.engine.dummySegs;
      this.rampCap = 0;
      this.ramp = null;
      this.simBG = null;
      this.renderBG = null;
      this.ensureRamp(16);
      this.cpu = new Float32Array(88);
      this.statPending = false;
      this.gone = false;
      this.stats_ = null;       // latest decoded readback
      this.readbackMs = 0;
      this.readbackCount = 0;
      this.lastReadAt = 0;
    }
    ensureRamp(stopCount) {
      const need = Math.max(16, Math.ceil(stopCount || 1));
      if (this.ramp && need <= this.rampCap) return;
      let next = Math.max(16, this.rampCap || 16);
      while (next < need) next *= 2;
      const d = this.engine.device;
      const old = this.ramp;
      this.ramp = d.createBuffer({ size: next * 32, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
      this.rampCap = next;
      if (old) old.destroy();
      this.simBG = d.createBindGroup({
        layout: this.engine.gl1Sim,
        entries: [
          { binding: 0, resource: { buffer: this.uniform } },
          { binding: 1, resource: { buffer: this.parts } },
          { binding: 2, resource: { buffer: this.stats } },
          { binding: 3, resource: { buffer: this.cellCount } },
          { binding: 4, resource: { buffer: this.cellSlots } },
          { binding: 5, resource: { buffer: this.snap } },
          { binding: 6, resource: { buffer: this.engine.fieldBuf } },
          { binding: 7, resource: { buffer: this.trailRes } },
          { binding: 8, resource: { buffer: this.ramp } },
        ],
      });
      this.renderBG = d.createBindGroup({
        layout: this.engine.gl1Render,
        entries: [
          { binding: 0, resource: { buffer: this.uniform } },
          { binding: 1, resource: { buffer: this.parts } },
          { binding: 2, resource: { buffer: this.trailRes } },
          { binding: 3, resource: { buffer: this.ramp } },
        ],
      });
    }
    destroy() {
      this.gone = true;
      for (const b of [this.parts, this.snap, this.trail, this.stats, this.statStage, this.uniform, this.ramp]) {
        if (b) b.destroy();
      }
    }
  }
  PE.SystemGPU = SystemGPU;

  PE.Engine = class Engine {
    constructor(canvas) {
      this.canvas = canvas;
      this.probePending = false;
      this.probeResult = null;
      this.windCells = PE.WIND.dim[0] * PE.WIND.dim[1] * PE.WIND.dim[2];
    }

    async init() {
      if (!navigator.gpu) {
        throw new Error("WebGPU is not available in this browser. Use a current Chrome, Edge or Safari build with WebGPU enabled.");
      }
      let adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
      if (!adapter) adapter = await navigator.gpu.requestAdapter();
      if (!adapter) adapter = await navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
      if (!adapter) throw new Error("WebGPU adapter unavailable.");
      this.adapter = adapter;
      this.device = await adapter.requestDevice();
      const d = this.device;
      this.deviceLost = false;
      d.lost.then((info) => {
        this.deviceLost = true;
        if (this.onLost) this.onLost(info);
      });
      const ai = adapter.info || {};
      this.adapterName = [ai.vendor, ai.architecture, ai.device, ai.description].filter(Boolean).join(" ") || "WebGPU adapter";
      this.format = navigator.gpu.getPreferredCanvasFormat();
      this.context = this.canvas.getContext("webgpu");
      this.context.configure({ device: d, format: this.format, alphaMode: "opaque", usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });

      const U = GPUBufferUsage;
      const VF = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
      const C = GPUShaderStage.COMPUTE;

      // Shared buffers.
      this.glob = d.createBuffer({ size: 240, usage: U.UNIFORM | U.COPY_DST });
      this.comps = d.createBuffer({ size: PE.WIND.maxComps * 32, usage: U.STORAGE | U.COPY_DST });
      this.dummySegs = d.createBuffer({ size: 256, usage: U.STORAGE });
      this.lines = d.createBuffer({ size: MAX_LINE_VERTS * LINE_BYTES, usage: U.VERTEX | U.COPY_DST });
      this.meshes = d.createBuffer({ size: MAX_MESH_VERTS * MESH_BYTES, usage: U.VERTEX | U.COPY_DST });
      this.arcs = d.createBuffer({ size: MAX_ARC_VERTS * ARC_BYTES, usage: U.VERTEX | U.COPY_DST });
      this.arcScratch = new Float32Array(MAX_ARC_VERTS * 11);
      this.dummySys = d.createBuffer({ size: 352, usage: U.UNIFORM | U.COPY_DST });
      this.dummyRamp = d.createBuffer({ size: 64, usage: U.STORAGE | U.COPY_DST });
      this.dummyParts = d.createBuffer({ size: PART_BYTES, usage: U.STORAGE });
      this.probeBuf = d.createBuffer({ size: 256, usage: U.COPY_DST | U.MAP_READ });

      // Wind texture: rgb velocity (m/s), a magnitude.
      const T = GPUTextureUsage;
      const [gx, gy, gz] = PE.WIND.dim;
      this.windTex = d.createTexture({
        size: [gx, gy, gz],
        dimension: "3d",
        format: "rgba16float",
        usage: T.STORAGE_BINDING | T.TEXTURE_BINDING | T.COPY_SRC | T.COPY_DST,
      });
      // Separate views so each bind group sees exactly the usage its layout declares.
      this.windView = this.windTex.createView({ usage: T.TEXTURE_BINDING });
      this.windStoreView = this.windTex.createView({ usage: T.STORAGE_BINDING });
      this.windSampler = d.createSampler({
        magFilter: "linear", minFilter: "linear", addressModeU: "clamp-to-edge",
        addressModeV: "clamp-to-edge", addressModeW: "clamp-to-edge",
      });

      // Bind group layouts.
      this.gl0 = d.createBindGroupLayout({
        entries: [
          { binding: 0, visibility: C | VF, buffer: { type: "uniform" } },
          { binding: 1, visibility: C | VF, texture: { sampleType: "float", viewDimension: "3d" } },
          { binding: 2, visibility: C | VF, sampler: { type: "filtering" } },
        ],
      });
      this.gl1Sim = d.createBindGroupLayout({
        entries: [
          { binding: 0, visibility: C, buffer: { type: "uniform" } },
          { binding: 1, visibility: C, buffer: { type: "storage" } },
          { binding: 2, visibility: C, buffer: { type: "storage" } },
          { binding: 3, visibility: C, buffer: { type: "storage" } },
          { binding: 4, visibility: C, buffer: { type: "storage" } },
          { binding: 5, visibility: C, buffer: { type: "read-only-storage" } },
          { binding: 6, visibility: C, buffer: { type: "read-only-storage" } },
          { binding: 7, visibility: C, buffer: { type: "storage" } },
          { binding: 8, visibility: C, buffer: { type: "read-only-storage" } },
        ],
      });
      // 📝 Force fields, written each frame (1040 bytes: count/time vec4 + 16 fields of 4 vec4).
      this.fieldBuf = d.createBuffer({ size: 1040, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
      // The neighbour lattice is only read by the molecular kinds, but the bind group layout demands
      //    bindings 3 and 4 from every system. One shared stub stands in for the systems that never index
      //    it, instead of 1.4 MB of untouched VRAM each. See SystemGPU below.
      this.vacantCellCount = d.createBuffer({ size: 16, usage: GPUBufferUsage.STORAGE });
      this.vacantCellSlots = d.createBuffer({ size: 16, usage: GPUBufferUsage.STORAGE });
      this.gl1Render = d.createBindGroupLayout({
        entries: [
          { binding: 0, visibility: VF, buffer: { type: "uniform" } },
          { binding: 1, visibility: GPUShaderStage.VERTEX, buffer: { type: "read-only-storage" } },
          { binding: 2, visibility: GPUShaderStage.VERTEX, buffer: { type: "read-only-storage" } },
          { binding: 3, visibility: VF, buffer: { type: "read-only-storage" } },
        ],
      });
      this.glWind = d.createBindGroupLayout({
        entries: [
          { binding: 0, visibility: C, buffer: { type: "uniform" } },
          { binding: 1, visibility: C, buffer: { type: "read-only-storage" } },
          { binding: 2, visibility: C, storageTexture: { access: "write-only", format: "rgba16float", viewDimension: "3d" } },
        ],
      });

      this.bg0 = d.createBindGroup({
        layout: this.gl0,
        entries: [
          { binding: 0, resource: { buffer: this.glob } },
          { binding: 1, resource: this.windView },
          { binding: 2, resource: this.windSampler },
        ],
      });
      this.bgWind = d.createBindGroup({
        layout: this.glWind,
        entries: [
          { binding: 0, resource: { buffer: this.glob } },
          { binding: 1, resource: { buffer: this.comps } },
          { binding: 2, resource: this.windStoreView },
        ],
      });
      this.bgDummy = d.createBindGroup({
        layout: this.gl1Render,
        entries: [
          { binding: 0, resource: { buffer: this.dummySys } },
          { binding: 1, resource: { buffer: this.dummyParts } },
          { binding: 2, resource: { buffer: this.dummySegs } },
          { binding: 3, resource: { buffer: this.dummyRamp } },
        ],
      });

      // Shaders, compile-checked so errors show up in the UI rather than as silent black frames.
      const modules = {};
      for (const key of ["sim", "wind", "render", "lens"]) {
        const m = d.createShaderModule({ code: PE.Shaders[key], label: "ParticleEditor " + key });
        const info = await m.getCompilationInfo();
        const errors = info.messages.filter((x) => x.type === "error");
        if (errors.length) {
          throw new Error(key + " shader: " + errors.map((e) => "line " + e.lineNum + ": " + e.message).join("; "));
        }
        modules[key] = m;
      }

      const simLayout = d.createPipelineLayout({ bindGroupLayouts: [this.gl0, this.gl1Sim] });
      const mkC = (entry, layout = simLayout) =>
        d.createComputePipeline({ layout, compute: { module: modules.sim, entryPoint: entry } });
      this.pipe = {
        wind: d.createComputePipeline({
          layout: d.createPipelineLayout({ bindGroupLayouts: [this.glWind] }),
          compute: { module: modules.wind, entryPoint: "buildWind" },
        }),
        emit: mkC("emitParticles"),
        update: mkC("updateParticles"),
        swarm: mkC("swarmStep"),
        reset: mkC("resetStats"),
        reduce: mkC("reduceStats"),
      };

      const renderLayout = d.createPipelineLayout({ bindGroupLayouts: [this.gl0, this.gl1Render] });
      const depth = { format: "depth24plus", depthWriteEnabled: false, depthCompare: "less" };
      const mkR = (vs, fs, blend, opts = {}) =>
        d.createRenderPipeline({
          layout: renderLayout,
          vertex: { module: modules.render, entryPoint: vs, buffers: opts.buffers || [] },
          fragment: { module: modules.render, entryPoint: fs, targets: [{ format: this.format, blend }] },
          primitive: { topology: opts.topology || "triangle-list" },
          depthStencil: Object.assign({}, depth, opts.depth || {}),
        });
      this.pipe.partAdd = mkR("vsPart", "fsPart", ADD);
      this.pipe.partAlpha = mkR("vsPart", "fsPart", OVER);
      this.pipe.cube = mkR("vsCube", "fsCube", OVER, { depth: { depthWriteEnabled: true } });
      this.pipe.shatter = mkR("vsShatter", "fsCube", OVER, { depth: { depthWriteEnabled: true } });
      this.pipe.fly = mkR("vsFly", "fsFly", OVER);
      this.lensPipe = d.createRenderPipeline({
        layout: "auto",
        vertex: { module: modules.lens, entryPoint: "vsLens" },
        fragment: { module: modules.lens, entryPoint: "fsLens", targets: [{ format: this.format }] },
        primitive: { topology: "triangle-list" },
      });
      this.lensUniform = d.createBuffer({ size: 128, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      this.lensSampler = d.createSampler({ magFilter: "linear", minFilter: "linear", addressModeU: "clamp-to-edge", addressModeV: "clamp-to-edge" });
      this.pipe.ribbonAdd = mkR("vsRibbon", "fsRibbon", ADD);
      this.pipe.ribbonAlpha = mkR("vsRibbon", "fsRibbon", OVER);
      this.pipe.arrow = mkR("vsArrow", "fsArrow", OVER, { depth: { depthWriteEnabled: true } });
      this.pipe.floor = mkR("vsFloor", "fsFloor", undefined, { depth: { depthWriteEnabled: true } });
      this.pipe.line = mkR("vsLine", "fsLine", OVER, {
        topology: "line-list",
        buffers: [{
          arrayStride: LINE_BYTES,
          attributes: [
            { shaderLocation: 0, offset: 0, format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "float32x4" },
          ],
        }],
      });
      this.pipe.mesh = mkR("vsMesh", "fsMesh", OVER, {
        topology: "triangle-list",
        depth: { depthWriteEnabled: true },
        buffers: [{
          arrayStride: MESH_BYTES,
          attributes: [
            { shaderLocation: 0, offset: 0, format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "float32x3" },
            { shaderLocation: 2, offset: 24, format: "float32x4" },
          ],
        }],
      });
      this.pipe.arc = mkR("vsArc", "fsArc", ADD, {
        topology: "triangle-list",
        depth: { depthWriteEnabled: false, depthCompare: "less" },
        buffers: [{
          arrayStride: ARC_BYTES,
          attributes: [
            { shaderLocation: 0, offset: 0, format: "float32x3" },
            { shaderLocation: 1, offset: 12, format: "float32x2" },
            { shaderLocation: 2, offset: 20, format: "float32x4" },
            { shaderLocation: 3, offset: 36, format: "float32x2" },
          ],
        }],
      });
      this.info = {
        adapter: this.adapterName,
        maxStorage: d.limits.maxStorageBufferBindingSize,
        maxInvocations: d.limits.maxComputeInvocationsPerWorkgroup,
      };
      this.resize(this.canvas.width || 800, this.canvas.height || 600);
      return this.info;
    }

    resize(w, h) {
      if (!this.device) return;
      this.depthTex?.destroy();
      this.depthTex = this.device.createTexture({
        size: [Math.max(1, w), Math.max(1, h)], format: "depth24plus", usage: GPUTextureUsage.RENDER_ATTACHMENT,
      });
      this.depthView = this.depthTex.createView();
    }

    createSystem(params) {
      return new SystemGPU(this, params);
    }

    // Pushes the frame's uniforms, runs every simulation job, then renders.
    render(frame) {
      if (!this.device || this.deviceLost) return;
      const d = this.device;
      const q = d.queue;
      q.writeBuffer(this.glob, 0, frame.glob);
      q.writeBuffer(this.comps, 0, frame.comps);
      if (frame.windTexData) {
        q.writeTexture(
          { texture: this.windTex },
          frame.windTexData,
          { bytesPerRow: PE.WIND.dim[0] * 8, rowsPerImage: PE.WIND.dim[1] },
          PE.WIND.dim,
        );
      }
      if (frame.fields) q.writeBuffer(this.fieldBuf, 0, frame.fields);
      const safeLineVerts = Math.min(frame.lineCount || 0, MAX_LINE_VERTS);
      const safeMeshVerts = Math.min(frame.meshCount || 0, MAX_MESH_VERTS);
      if (safeLineVerts) q.writeBuffer(this.lines, 0, frame.lines, 0, safeLineVerts * 7);
      if (safeMeshVerts) q.writeBuffer(this.meshes, 0, frame.meshes, 0, safeMeshVerts * 10);
      let arcVerts = 0;
      const segs = frame.arcSegs;
      const flashes = frame.arcFlashes;
      if ((segs && segs.length) || (flashes && flashes.length)) {
        const scratch = this.arcScratch;
        const camX = frame.glob[24], camY = frame.glob[25], camZ = frame.glob[26];
        const rx = frame.glob[16], ry = frame.glob[17], rz = frame.glob[18];
        const ux = frame.glob[20], uy = frame.glob[21], uz = frame.glob[22];
        const pushVert = (px, py, pz, u, v, r, g, b, a, inten, ratio) => {
          const o = arcVerts * 11;
          scratch[o] = px; scratch[o + 1] = py; scratch[o + 2] = pz;
          scratch[o + 3] = u; scratch[o + 4] = v;
          scratch[o + 5] = r; scratch[o + 6] = g; scratch[o + 7] = b; scratch[o + 8] = a;
          scratch[o + 9] = inten; scratch[o + 10] = ratio;
          arcVerts++;
        };
        if (segs) {
          for (let s = 0; s < segs.length && arcVerts + 6 <= MAX_ARC_VERTS; s += 12) {
            const ax = segs[s], ay = segs[s + 1], az = segs[s + 2], wa = Math.max(0.002, segs[s + 3]);
            const bx = segs[s + 4], by = segs[s + 5], bz = segs[s + 6], wb = Math.max(0.002, segs[s + 7]);
            const cr = segs[s + 8], cg = segs[s + 9], cb = segs[s + 10], inten = segs[s + 11];
            if (inten <= 0.002) continue;
            const dx = bx - ax, dy = by - ay, dz = bz - az;
            const segLen = Math.hypot(dx, dy, dz);
            if (segLen < 1e-5) continue;
            const tx = dx / segLen, ty = dy / segLen, tz = dz / segLen;
            const mx = (ax + bx) * 0.5, my = (ay + by) * 0.5, mz = (az + bz) * 0.5;
            const cx = camX - mx, cy = camY - my, cz = camZ - mz;
            let sx = ty * cz - tz * cy, sy = tz * cx - tx * cz, sz = tx * cy - ty * cx;
            const sl = Math.hypot(sx, sy, sz) || 1;
            sx /= sl; sy /= sl; sz /= sl;
            const wAvg = Math.max(0.002, 0.5 * (wa + wb));
            const ratio = segLen / wAvg;
            const p0x = ax - tx * wa, p0y = ay - ty * wa, p0z = az - tz * wa;
            const p1x = bx + tx * wb, p1y = by + ty * wb, p1z = bz + tz * wb;
            const vEnd = ratio + 1.0;
            pushVert(p0x - sx * wa, p0y - sy * wa, p0z - sz * wa, -1, -1, cr, cg, cb, 1, inten, ratio);
            pushVert(p0x + sx * wa, p0y + sy * wa, p0z + sz * wa,  1, -1, cr, cg, cb, 1, inten, ratio);
            pushVert(p1x - sx * wb, p1y - sy * wb, p1z - sz * wb, -1, vEnd, cr, cg, cb, 1, inten, ratio);
            pushVert(p0x + sx * wa, p0y + sy * wa, p0z + sz * wa,  1, -1, cr, cg, cb, 1, inten, ratio);
            pushVert(p1x + sx * wb, p1y + sy * wb, p1z + sz * wb,  1, vEnd, cr, cg, cb, 1, inten, ratio);
            pushVert(p1x - sx * wb, p1y - sy * wb, p1z - sz * wb, -1, vEnd, cr, cg, cb, 1, inten, ratio);
          }
        }
        if (flashes) {
          for (let f = 0; f < flashes.length && arcVerts + 6 <= MAX_ARC_VERTS; f += 8) {
            const fx = flashes[f], fy = flashes[f + 1], fz = flashes[f + 2], rad = flashes[f + 3];
            const cr = flashes[f + 4], cg = flashes[f + 5], cb = flashes[f + 6], inten = flashes[f + 7];
            if (inten <= 0.002 || rad <= 0.001) continue;
            pushVert(fx - rx * rad - ux * rad, fy - ry * rad - uy * rad, fz - rz * rad - uz * rad, -1, -1, cr, cg, cb, 1, inten, -1);
            pushVert(fx + rx * rad - ux * rad, fy + ry * rad - uy * rad, fz + rz * rad - uz * rad,  1, -1, cr, cg, cb, 1, inten, -1);
            pushVert(fx - rx * rad + ux * rad, fy - ry * rad + uy * rad, fz - rz * rad + uz * rad, -1,  1, cr, cg, cb, 1, inten, -1);
            pushVert(fx + rx * rad - ux * rad, fy + ry * rad - uy * rad, fz + rz * rad - uz * rad,  1, -1, cr, cg, cb, 1, inten, -1);
            pushVert(fx + rx * rad + ux * rad, fy + ry * rad + uy * rad, fz + rz * rad + uz * rad,  1,  1, cr, cg, cb, 1, inten, -1);
            pushVert(fx - rx * rad + ux * rad, fy - ry * rad + uy * rad, fz - rz * rad + uz * rad, -1,  1, cr, cg, cb, 1, inten, -1);
          }
        }
        if (arcVerts) q.writeBuffer(this.arcs, 0, scratch, 0, arcVerts * 11);
      }
      for (const job of frame.jobs) {
        if (job.draw) {
          q.writeBuffer(job.gpu.uniform, 0, job.cpu);
          if (job.rampData && job.rampCount > 0) {
            job.gpu.ensureRamp(job.rampCount);
            q.writeBuffer(job.gpu.ramp, 0, job.rampData);
          }
          if (job.anchorSnapData && job.anchorSnapKey && job.gpu._lastAnchorKey !== job.anchorSnapKey) {
            if (job.anchorSnapData.byteLength <= job.gpu.cap * PART_BYTES) {
              q.writeBuffer(job.gpu.snap, 0, job.anchorSnapData);
            }
            job.gpu._lastAnchorKey = job.anchorSnapKey;
          }
        }
      }

      const enc = d.createCommandEncoder({ label: "ParticleEditor frame" });

      // 1. Wind grid: skip legacy shader build when the exact ForceFieldCatalogue 3D volume was uploaded via writeTexture.
      let pass;
      if (!frame.windTexData) {
        pass = enc.beginComputePass({ label: "wind" });
        pass.setPipeline(this.pipe.wind);
        pass.setBindGroup(0, this.bgWind);
        pass.dispatchWorkgroups(PE.WIND.dim[0] / 4, PE.WIND.dim[1] / 4, PE.WIND.dim[2] / 4);
        pass.end();
      }

      // 2. Particle systems.
      for (const job of frame.jobs) {
        if (!job.simulate) continue;
        const g = job.gpu;
        const cap = g.cap;
        if (job.kind === 6) {
          if (job.emitN > 0) {
            pass = enc.beginComputePass({ label: "emit-swarm" });
            pass.setBindGroup(0, this.bg0);
            pass.setBindGroup(1, g.simBG);
            pass.setPipeline(this.pipe.emit);
            pass.dispatchWorkgroups(Math.ceil(job.emitN / 64));
            pass.end();
          }
          enc.copyBufferToBuffer(g.parts, 0, g.snap, 0, cap * PART_BYTES);
          pass = enc.beginComputePass({ label: "swarm" });
          pass.setBindGroup(0, this.bg0);
          pass.setBindGroup(1, g.simBG);
          pass.setPipeline(this.pipe.swarm);
          pass.dispatchWorkgroups(Math.ceil(cap / 64));
          pass.end();
        } else {
          pass = enc.beginComputePass({ label: "particles" });
          pass.setBindGroup(0, this.bg0);
          pass.setBindGroup(1, g.simBG);
          if (job.emitN > 0) {
            pass.setPipeline(this.pipe.emit);
            pass.dispatchWorkgroups(Math.ceil(job.emitN / 64));
          }
          pass.setPipeline(this.pipe.update);
          pass.dispatchWorkgroups(Math.ceil(cap / 64));
          pass.end();
        }

        // Statistics reduction: alive count and kinetic energy.
        pass = enc.beginComputePass({ label: "stats" });
        pass.setBindGroup(0, this.bg0);
        pass.setBindGroup(1, g.simBG);
        pass.setPipeline(this.pipe.reset);
        pass.dispatchWorkgroups(1);
        pass.setPipeline(this.pipe.reduce);
        pass.dispatchWorkgroups(Math.ceil(cap / 64));
        pass.end();
        job.copyStats = !!job.readStats && !g.statPending;
        if (job.copyStats) enc.copyBufferToBuffer(g.stats, 0, g.statStage, 0, 32);
      }

      // 3. Optional probe readback: one texel (8 bytes) of the wind grid.
      const doProbe = !!frame.probe && !this.probePending;
      if (doProbe) {
        const p = frame.probe;
        enc.copyTextureToBuffer(
          { texture: this.windTex, origin: [p.ix, p.iy, p.iz] },
          { buffer: this.probeBuf, bytesPerRow: 256 },
          [1, 1, 1],
        );
      }

      // 4. Render.
      const view = this.context.getCurrentTexture().createView();
      const rp = enc.beginRenderPass({
        colorAttachments: [{
          view,
          clearValue: { r: frame.clear[0], g: frame.clear[1], b: frame.clear[2], a: 1 },
          loadOp: "clear",
          storeOp: "store",
        }],
        depthStencilAttachment: {
          view: this.depthView, depthClearValue: 1.0, depthLoadOp: "clear", depthStoreOp: "store",
        },
      });
      rp.setBindGroup(0, this.bg0);
      if (frame.floor) {
        rp.setPipeline(this.pipe.floor);
        rp.setBindGroup(1, this.bgDummy);
        rp.draw(6);
      }
      if (safeLineVerts) {
        rp.setPipeline(this.pipe.line);
        rp.setBindGroup(1, this.bgDummy);
        rp.setVertexBuffer(0, this.lines);
        rp.draw(safeLineVerts);
      }
      if (safeMeshVerts) {
        rp.setPipeline(this.pipe.mesh);
        rp.setBindGroup(1, this.bgDummy);
        rp.setVertexBuffer(0, this.meshes);
        rp.draw(safeMeshVerts);
      }
      if (frame.arrows) {
        rp.setPipeline(this.pipe.arrow);
        rp.setBindGroup(1, this.bgDummy);
        rp.draw(54, this.windCells);
      }
      for (const job of frame.jobs) {
        if (!job.draw) continue;
        if (job.gpu.isRibbon) {
          rp.setPipeline(job.alpha ? this.pipe.ribbonAlpha : this.pipe.ribbonAdd);
          rp.setBindGroup(1, job.gpu.renderBG);
          const drawSegs = Math.max(TRAIL_SEGS - 1, Math.min(32, Math.round(job.curveSegs || (TRAIL_SEGS - 1))));
          rp.draw(drawSegs * 6, job.gpu.cap);
        }
        if (job.cube) {
          rp.setPipeline(this.pipe.cube);
          rp.setBindGroup(1, job.gpu.renderBG);
          rp.draw(36, job.gpu.cap);
        }
        if (!job.cube || job.dualPrim) {
          rp.setPipeline(job.alpha ? this.pipe.partAlpha : this.pipe.partAdd);
          rp.setBindGroup(1, job.gpu.renderBG);
          rp.draw(6, job.gpu.cap);
        }
        if (job.shatter) {
          rp.setPipeline(this.pipe.shatter);
          rp.setBindGroup(1, job.gpu.renderBG);
          rp.draw(288, job.gpu.cap);
        }
        if (job.fly) {
          rp.setPipeline(this.pipe.fly);
          rp.setBindGroup(1, job.gpu.renderBG);
          rp.draw(6, job.gpu.cap);
        }
      }
      if (arcVerts) {
        rp.setPipeline(this.pipe.arc);
        rp.setBindGroup(1, this.bgDummy);
        rp.setVertexBuffer(0, this.arcs);
        rp.draw(arcVerts);
      }
      rp.end();

      // Gravitational lensing & heat shimmer post-pass (copies rendered frame then samples warped UVs)
      if (frame.lensCount && frame.lens && this.lensPipe) {
        const cur = this.context.getCurrentTexture();
        if (!this.capTex || this.capTex.width !== cur.width || this.capTex.height !== cur.height) {
          if (this.capTex) this.capTex.destroy();
          this.capTex = this.device.createTexture({
            size: [cur.width, cur.height], format: this.format,
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
          });
          this.lensBG = this.device.createBindGroup({
            layout: this.lensPipe.getBindGroupLayout(0),
            entries: [
              { binding: 0, resource: { buffer: this.lensUniform } },
              { binding: 1, resource: this.capTex.createView() },
              { binding: 2, resource: this.lensSampler },
            ],
          });
        }
        q.writeBuffer(this.lensUniform, 0, frame.lens);
        enc.copyTextureToTexture({ texture: cur }, { texture: this.capTex }, [cur.width, cur.height]);
        const lp = enc.beginRenderPass({ colorAttachments: [{ view, loadOp: "load", storeOp: "store" }] });
        lp.setPipeline(this.lensPipe);
        lp.setBindGroup(0, this.lensBG);
        lp.draw(3);
        lp.end();
      }

      q.submit([enc.finish()]);

      // Asynchronous readbacks. Each staging buffer has at most one map in flight.
      for (const job of frame.jobs) {
        if (!job.copyStats) continue;
        const g = job.gpu;
        g.statPending = true;
        const t0 = performance.now();
        g.statStage.mapAsync(GPUMapMode.READ).then(() => {
          if (g.gone) return;                      // the system was removed while the copy was in flight
          const u = new Uint32Array(g.statStage.getMappedRange().slice(0));
          g.statStage.unmap();
          g.stats_ = { alive: u[0], A: u[1], B: u[2], C: u[3], energy: u[4] / 1000, bytes: 32 };
          g.readbackMs = performance.now() - t0;
          g.readbackCount++;
          g.lastReadAt = performance.now();
          g.statPending = false;
        }, () => { g.statPending = false; });
      }
      if (doProbe) {
        this.probePending = true;
        const t0 = performance.now();
        const p = frame.probe;
        this.probeBuf.mapAsync(GPUMapMode.READ).then(() => {
          const h = new Uint16Array(this.probeBuf.getMappedRange().slice(0, 8));
          this.probeBuf.unmap();
          const v = Array.from(h, halfToFloat);
          this.probeResult = { at: [p.ix, p.iy, p.iz], value: v.slice(0, 3), mag: v[3], ms: performance.now() - t0, bytes: 8 };
          this.probePending = false;
        }, () => { this.probePending = false; });
      }
    }

    // Measures what a full particle-buffer readback costs (for the Readback card).
    async benchmarkFullReadback(gpu) {
      const d = this.device;
      const size = gpu.cap * PART_BYTES;
      const stage = d.createBuffer({ size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
      const t0 = performance.now();
      const enc = d.createCommandEncoder();
      enc.copyBufferToBuffer(gpu.parts, 0, stage, 0, size);
      d.queue.submit([enc.finish()]);
      await stage.mapAsync(GPUMapMode.READ);
      const t1 = performance.now();
      const data = new Float32Array(stage.getMappedRange().slice(0));
      stage.unmap();
      stage.destroy();
      let alive = 0;
      const stride = PART_BYTES / 4; // 20 floats (80 bytes) per Part
      for (let i = 0; i < gpu.cap; i++) if (data[i * stride + 3] < data[i * stride + 7]) alive++;
      return { bytes: size, ms: t1 - t0, alive, mbPerSecond: size / 1048576 / ((t1 - t0) / 1000) };
    }

    destroySystem(g) {
      g.destroy();
    }
  };

  function halfToFloat(h) {
    const s = h & 0x8000 ? -1 : 1;
    const e = (h >> 10) & 0x1f;
    const f = h & 0x3ff;
    if (e === 0) return s * 2 ** -14 * (f / 1024);
    if (e === 31) return f ? NaN : s * Infinity;
    return s * 2 ** (e - 15) * (1 + f / 1024);
  }
  PE.halfToFloat = halfToFloat;

  // Fills one system's uniform block from its parameters and per-frame values.
  PE.fillSys = function fillSys(f, p, o) {
    const S = PE.SYS_SLOT;
    const set = (name, a, b = 0, c = 0, d = 0) => {
      const s = S[name] * 4;
      f[s] = a; f[s + 1] = b; f[s + 2] = c; f[s + 3] = d;
    };
    const dl = p.dir;
    const dlen = Math.hypot(dl[0], dl[1], dl[2]) || 1;
    set("origin", o.origin[0], o.origin[1], o.origin[2], p.radius);
    set("dir", dl[0] / dlen, dl[1] / dlen, dl[2] / dlen, p.spread);
    set("speed", p.speedMin, p.speedMax, p.drag, p.gravity);
    set("life", p.lifeMin, p.lifeMax, p.sizeStart, p.sizeEnd);
    const ramp = PE.packColorRamp(p);
    const stops = ramp.stops;
    const cA = stops[0].color;
    const cB = stops[stops.length - 1].color;
    const cC = stops.length >= 3 ? stops[Math.floor((stops.length - 1) / 2)].color : (p.colC || cB);
    set("colA", ...cA);
    set("colB", ...cB);
    set("colC", ...cC);
    set("phys", o.dt, o.time, o.frame, p.kind);
    set("phys2", p.windCoupling, p.bounce, p.buoyancy, p.flutter);
    set("phys3", o.cap, p.emitShape, 0.5, o.emitN);
    const bh = Array.isArray(p.boxHalf) ? p.boxHalf : [Number(p.boxHalf) || 0.6, Number(p.boxHalf) || 0.6, Number(p.boxHalf) || 0.6];
    set("phys4", bh[0] ?? 0.6, bh[1] ?? 0.6, bh[2] ?? 0.6, o.head);
    const hasArc = p.arcMode === 0 || p.arcMode === 1 || p.arcMode === 2 || p.arcMode === 3;
    const hasBeam = (p.beamMode || 0) > 0;
    const isBoid = p.kind === 6;
    const isTrans = p.kind === 5 && !!p.transition;
    const fireworkMode = !isTrans && !hasArc && !hasBeam && !isBoid ? Math.max(0, Math.min(2, p.fireworkMode | 0)) : 0;
    const flareMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode ? Math.max(0, Math.min(2, p.flareMode | 0)) : 0;
    const crackMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode && !flareMode ? Math.max(0, Math.min(2, p.crackMode | 0)) : 0;
    const ferroMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode && !flareMode && !crackMode ? Math.max(0, Math.min(2, p.ferroMode | 0)) : 0;
    const mudMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode && !flareMode && !crackMode && !ferroMode ? Math.max(0, Math.min(3, p.mudSplashMode | 0)) : 0;
    const beadsMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode && !flareMode && !crackMode && !ferroMode && !mudMode ? Math.max(0, Math.min(3, p.beadsMode | 0)) : 0;
    const seedHeadMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode && !flareMode && !crackMode && !ferroMode && !mudMode && !beadsMode ? Math.max(0, Math.min(2, p.seedHeadMode | 0)) : 0;
    const jellyMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode && !flareMode && !crackMode && !ferroMode && !mudMode && !beadsMode && !seedHeadMode ? Math.max(0, Math.min(2, p.jellyMode | 0)) : 0;
    const webMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode && !flareMode && !crackMode && !ferroMode && !mudMode && !beadsMode && !seedHeadMode && !jellyMode ? Math.max(0, Math.min(2, p.webMode | 0)) : 0;
    const dynMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode && !flareMode && !crackMode && !ferroMode && !mudMode && !beadsMode && !seedHeadMode && !jellyMode && !webMode ? Math.max(0, Math.min(9, p.dynMode | 0)) : 0;
    const gridMode = !isTrans && !hasArc && !hasBeam && !isBoid && !fireworkMode && !flareMode && !crackMode && !ferroMode && !mudMode && !beadsMode && !seedHeadMode && !jellyMode && !webMode && !dynMode ? Math.max(0, Math.min(2, p.gridMode | 0)) : 0;
    const isRibbon = !fireworkMode && !flareMode && !crackMode && !ferroMode && !mudMode && !beadsMode && !seedHeadMode && !jellyMode && !webMode && !dynMode && !gridMode && (p.curlMode || 0) === 3;
    const tp = p.targetPos || [o.origin[0] + 4, o.origin[1], o.origin[2]];
    const packedDyn = dynMode > 0 ? (dynMode <= 7 ? 5 + dynMode : 6 + dynMode) : 0;
    const packedCurlMode = fireworkMode > 0 ? 23 : (flareMode > 0 ? 22 : (jellyMode > 0 ? 21 : (seedHeadMode > 0 ? 20 : (beadsMode > 0 ? 19 : (crackMode > 0 ? 18 : (ferroMode > 0 ? 17 : (mudMode > 0 ? 16 : (webMode > 0 ? (3 + webMode) : (dynMode > 0 ? packedDyn : (gridMode > 0 ? 13 : (p.curlMode || 0)))))))))));
    const packedRate = fireworkMode > 0 ? (p.burstEvery ?? 3.1) : (flareMode > 0 ? (p.burstEvery ?? 2.2) : ((jellyMode > 0 || seedHeadMode > 0 || beadsMode > 0) ? (p.dynStiff ?? 1.25) : (crackMode > 0 ? (p.curlFreq ?? 1.15) : (ferroMode > 0 ? (p.ferroMagnetSpeed ?? 1.35) : (mudMode > 0 ? (p.carSpeed ?? 4.2) : (webMode > 0 ? (p.webSpinRate ?? 1.0) : (dynMode > 0 ? (p.dynStiff ?? 1.0) : (gridMode > 0 ? (p.gridContrast ?? 1.85) : (p.curlRate ?? 0.24)))))))));
    const packedFreq = fireworkMode > 0 ? (p.fireworkScale ?? 2.6) : (flareMode > 0 ? (p.gravity ?? 0.68) : (crackMode > 0 ? (p.crackSpeed ?? 2.6) : (p.curlFreq ?? 0.85)));
    const packedAmp = fireworkMode > 0 ? (p.gravity ?? 0.38) : (flareMode > 0 ? (p.drag ?? 1.55) : (crackMode > 0 ? (p.crackDepth ?? 0.44) : (p.curlAmp ?? 2.2)));
    set("mol", packedCurlMode, packedFreq, packedAmp, packedRate);
    if (isTrans) {
      const tr = p.transition;
      const waveId = tr.wave === "melt" ? 1 : tr.wave === "glitch" ? 2 : 0;
      set("mol2", waveId, tr.after === "butterfly" ? 1 : 0, tr.assemble | 0, tr.fragment ? 1 : 0);
      set("mol3", tr.holdBase ?? 0.5, tr.holdSpread ?? 1.5, tr.burstShare ?? 0.5, tr.childLife ?? 1.2);
    } else if (fireworkMode > 0) {
      set("mol2", fireworkMode, p.fireworkShape ?? 0, o.fireworkShellIdx ?? 0, p.drag ?? 1.55);
      set("mol3", p.windCoupling ?? 0.42, p.showObject !== false ? 1 : 0, o.fireworkBurstX ?? o.origin[0], o.fireworkBurstZ ?? o.origin[2]);
    } else if (flareMode > 0) {
      set("mol2", flareMode, p.burstCount ?? 16, p.buoyancy ?? 0.35, p.bounce ?? 0.25);
      set("mol3", p.windCoupling ?? 0.85, p.showObject !== false ? 1 : 0, 0, o.preseedCurtain ? 1 : 0);
    } else if (jellyMode > 0) {
      set("mol2", jellyMode, p.radius ?? 1.1, p.dynSag ?? 0.3, p.dynWaveSpeed ?? 4.5);
      set("mol3", p.dynTearThresh ?? 5.2, p.bounce ?? 0.65, p.windCoupling ?? 0.9, o.preseedCurtain ? 1 : 0);
    } else if (seedHeadMode > 0) {
      set("mol2", seedHeadMode, p.dynRows ?? 8, p.dynHeight ?? 2.0, p.radius ?? 0.5);
      set("mol3", p.dynTearThresh ?? 3.0, p.dynWaveSpeed ?? 1.7, p.windCoupling ?? 2.2, o.preseedCurtain ? 1 : 0);
    } else if (beadsMode > 0) {
      set("mol2", beadsMode, p.dynRows ?? 24, p.dynHeight ?? 3.0, p.sizeStart ?? 0.08);
      set("mol3", p.dynSag ?? 0.04, p.dynWaveSpeed ?? 1.8, p.dynTearThresh ?? 14.0, o.preseedCurtain ? 1 : 0);
    } else if (crackMode > 0) {
      const gridSide = Math.max(8, Math.round(Math.sqrt(Math.max(64, o.cap * 0.72))));
      set("mol2", crackMode, gridSide, o.crackHit ? o.crackHit.x : o.origin[0], o.crackHit ? o.crackHit.z : o.origin[2]);
      set("mol3", o.crackHit ? o.crackHit.t : -1.0, p.showObject !== false ? 1 : 0, 0, o.preseedCurtain ? 1 : 0);
    } else if (ferroMode > 0) {
      set("mol2", ferroMode, p.ferroSpikes ?? 520, 1, p.radius ?? 2.0);
      set("mol3", (Array.isArray(p.boxHalf) ? p.boxHalf[1] : Number(p.boxHalf)) || 1.05, p.streakThin ?? 1.0, 0, o.preseedCurtain ? 1 : 0);
    } else if (mudMode > 0) {
      set("mol2", mudMode, p.carTrackRadius ?? 3.15, p.showObject !== false ? 1 : 0, p.sizeScale ?? 1.0);
      set("mol3", p.bounce ?? 0.04, p.radius ?? 1.8, o.crackHit ? o.crackHit.x : o.origin[0], o.preseedCurtain ? 1 : 0);
    } else if (webMode > 0) {
      set("mol2", p.webSpokes ?? 18, p.webRings ?? 14, p.webSag ?? 0.16, p.webDew ?? 0.85);
      set("mol3", p.dynTearThresh ?? 8.2, 0, 0, o.preseedCurtain ? 1 : 0);
    } else if (dynMode > 0) {
      set("mol2", p.dynRows ?? 28, p.dynHeight ?? 2.4, p.dynSag ?? 0.35, p.dynSub ?? 0);
      set("mol3", p.dynTearThresh ?? 0, p.dynWaveSpeed ?? 2.2, p.leafMode || 0, o.preseedCurtain ? 1 : 0);
    } else if (gridMode > 0) {
      set("mol2", p.gridSurface ?? 0, p.gridShape ?? 2, p.gridCols ?? 24, p.gridRows ?? 24);
      set("mol3", p.gridEffector ?? 0, p.gridMinScale ?? 0.10, p.gridMaxHeight ?? 0.95, o.preseedCurtain ? 1 : 0);
    } else if (isRibbon) {
      set("mol2", p.shellRadius ?? 1.75, p.streakThin ?? 1.0, p.objectShape ?? 0, p.pulseMode ?? 0);
      set("mol3", p.pulseAxis ?? 0, p.pulseSpeed ?? 0.34, p.pulseWidth ?? 0.22, o.preseedCurtain ? 1 : 0);
    } else if (hasArc || hasBeam) {
      set("mol2", p.shellRadius ?? 1.75, p.trailMode || 0, hasBeam ? (p.beamWidth ?? 0.048) : 0, p.targetRadius ?? 0.45);
      set("mol3", tp[0], tp[1], tp[2], o.preseedCurtain ? 1 : 0);
    } else if (isBoid) {
      set("mol2", p.boidsSeparate ?? 2.85, p.trailMode || 0, p.boidsCohere ?? 1.15, p.boidsAlign ?? 1.65);
      set("mol3", p.swarmRadius ?? 1.4, p.boidsSpeed ?? 3.4, p.pulseHz ?? 0, p.pulseDepth ?? 0);
    } else {
      set("mol2", p.shellRadius ?? 1.75, p.trailMode || 0, p.shieldForm ?? 0, 0);
      set("mol3", p.shieldMask ?? 1, p.shieldSpin ?? 0.20, p.glitchRate ?? 14, o.preseedCurtain ? 1 : 0);
    }
    set("emit", o.head, o.seed, p.shape, p.leafMode);
    set("misc", p.sizeScale, gridMode > 0 ? (p.gridBillboard ? 1.0 : 0.0) : (p.streakThin ?? 1.0), hasArc ? (p.arcMode + 1) : 0, isTrans ? (p.transition.shell ?? 3.0) : (hasBeam ? (p.beamMode || 1) : 0));
    set("colorCfg", ramp.count, ramp.byCode, ramp.min, ramp.max);
    set("colorCfg2", ramp.repeat, ramp.wrapCode, ramp.phase, ramp.interpCode);
    set("extra0", Math.max(2, Math.min(32, Math.round(p.dynSegs ?? 16))), Math.max(0.1, p.headScale ?? 1), dynMode > 0 ? (p.dynCurveDebug ? 1.0 : 0.0) : Math.max(0, Math.min(3, Math.round(p.headStyle ?? 0))), p.blend === "add" ? 1 : 0);
    const effA = Array.isArray(p.effPos) ? p.effPos : [0, 1.5, 0];
    const effB = Array.isArray(p.effPos2) ? p.effPos2 : [2.5, 1.5, 0];
    if (ferroMode > 0) {
      set("extra1", effA[0], effA[1], effA[2], p.ferroMagnetFree ? 1.0 : 0.0);
    } else {
      set("extra1", effA[0], effA[1], effA[2], Math.max(0, p.effSize ?? 0));
    }
    set("extra2", effB[0], effB[1], effB[2], Math.max(0, p.effSize2 ?? 0));
    set("extra3", Math.max(0, Math.min(4, Math.round(p.effShape ?? 0))), Math.max(0, Math.min(4, Math.round(p.effShape2 ?? 0))), Math.max(0.1, p.dynAnemThick ?? 1.0), p.effBlend ? 1.0 : 0.0);
    o._ramp = ramp;
  };
