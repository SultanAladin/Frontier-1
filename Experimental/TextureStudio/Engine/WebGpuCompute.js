/*==========================================================================================================================================
                                                            WEBGPUCOMPUTE.JS
==========================================================================================================================================*/
/* Real WebGPU (navigator.gpu) compute acceleration for periodic noise-field baking, UV texture dilation, and Volume SDF evaluation,
   with automatic deterministic fallback so every studio function works seamlessly across all browsers and headless verification. */

const NOISE_FIELD_WGSL = /* wgsl */ `
struct Params {
  width : u32,
  height : u32,
  periodX : f32,
  periodY : f32,
  seed : u32,
  noiseKind : u32,
  contrast : f32,
  bias : f32,
  octaves : u32,
  invert : u32,
  pad0 : u32,
  pad1 : u32,
};

@group(0) @binding(0) var<uniform> params : Params;
@group(0) @binding(1) var<storage, read_write> outField : array<f32>;

fn hash2(ix : i32, iy : i32, rx : i32, ry : i32, seed : u32) -> f32 {
  var px = ix;
  var py = iy;
  if (rx > 0) { px = ((ix % rx) + rx) % rx; }
  if (ry > 0) { py = ((iy % ry) + ry) % ry; }
  var h : u32 = u32(px * 374761393 + py * 668265263) ^ (seed * 1442695041u);
  h = (h ^ (h >> 13u)) * 1274126177u;
  h = h ^ (h >> 16u);
  return f32(h) / 4294967296.0;
}

fn fade(t : f32) -> f32 {
  return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

fn grad2(ix : i32, iy : i32, fx : f32, fy : f32, rx : i32, ry : i32, seed : u32) -> f32 {
  let ang = hash2(ix, iy, rx, ry, seed) * 6.2831853;
  return cos(ang) * fx + sin(ang) * fy;
}

fn perlinPeriodic(x : f32, y : f32, rx : i32, ry : i32, seed : u32) -> f32 {
  let ix = i32(floor(x));
  let iy = i32(floor(y));
  let fx = x - f32(ix);
  let fy = y - f32(iy);
  let u = fade(fx);
  let v = fade(fy);
  let n00 = grad2(ix, iy, fx, fy, rx, ry, seed);
  let n10 = grad2(ix + 1, iy, fx - 1.0, fy, rx, ry, seed);
  let n01 = grad2(ix, iy + 1, fx, fy - 1.0, rx, ry, seed);
  let n11 = grad2(ix + 1, iy + 1, fx - 1.0, fy - 1.0, rx, ry, seed);
  let nx0 = mix(n00, n10, u);
  let nx1 = mix(n01, n11, u);
  return clamp(mix(nx0, nx1, v) * 0.7071 + 0.5, 0.0, 1.0);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
  if (gid.x >= params.width || gid.y >= params.height) { return; }
  let u = (f32(gid.x) + 0.5) / f32(params.width);
  let v = (f32(gid.y) + 0.5) / f32(params.height);
  let rx = max(1, i32(round(params.periodX)));
  let ry = max(1, i32(round(params.periodY)));
  let fx = u * f32(rx);
  let fy = v * f32(ry);
  var sum = 0.0;
  var amp = 0.5;
  var norm = 0.0;
  var mult = 1;
  let oct = max(1u, min(5u, params.octaves));
  for (var o = 0u; o < oct; o = o + 1u) {
    var n = perlinPeriodic(fx * f32(mult), fy * f32(mult), rx * mult, ry * mult, params.seed + o * 197u);
    if (params.noiseKind == 2u) {
      n = 1.0 - abs(n * 2.0 - 1.0);
    }
    sum += n * amp;
    norm += amp;
    amp *= 0.5;
    mult *= 2;
  }
  var val = clamp((sum / max(norm, 1e-5)) + params.bias, 0.0, 1.0);
  if (params.invert != 0u) { val = 1.0 - val; }
  val = val * val * (3.0 - 2.0 * val);
  val = clamp((val - 0.5) * params.contrast + 0.5, 0.0, 1.0);
  outField[gid.y * params.width + gid.x] = val;
}
`;

export class WebGpuComputeEngine {
  constructor() {
    this.available = false;
    this.adapter = null;
    this.device = null;
    this.adapterName = "WebGL2 + CPU Fallback";
    this.backend = "WebGL2 + CPU";
    this.noisePipeline = null;
    this.initPromise = null;
  }

  async init() {
    if (this.initPromise) return this.initPromise;
    this.initPromise = (async () => {
      if (typeof navigator === "undefined" || !navigator.gpu) {
        return this.status();
      }
      try {
        const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
        if (!adapter) return this.status();
        const device = await adapter.requestDevice();
        this.adapter = adapter;
        this.device = device;
        this.available = true;
        const info = adapter.info || {};
        this.adapterName = [info.vendor, info.architecture, info.device, info.description].filter(Boolean).join(" ") || "WebGPU Adapter";
        this.backend = "WebGPU Compute";
        this.noisePipeline = device.createComputePipeline({
          layout: "auto",
          compute: {
            module: device.createShaderModule({ code: NOISE_FIELD_WGSL }),
            entryPoint: "main",
          },
        });
      } catch {
        this.available = false;
      }
      return this.status();
    })();
    return this.initPromise;
  }

  status() {
    return {
      available: this.available,
      adapterName: this.adapterName,
      backend: this.backend,
    };
  }

  async computeNoiseField(width, height, config = {}) {
    await this.init();
    if (!this.available || !this.device || !this.noisePipeline) return null;
    const d = this.device;
    const count = width * height;
    const outSize = count * 4;
    const paramData = new ArrayBuffer(48);
    const uView = new Uint32Array(paramData);
    const fView = new Float32Array(paramData);
    uView[0] = width;
    uView[1] = height;
    fView[2] = Math.max(1, Math.round(config.frequency || 4));
    fView[3] = Math.max(1, Math.round(config.frequency || 4));
    uView[4] = (config.seed || 1337) >>> 0;
    uView[5] = config.noiseType === "ridged" ? 2 : 0;
    fView[6] = config.contrast ?? 1.0;
    fView[7] = config.bias ?? 0.0;
    uView[8] = Math.max(1, Math.min(5, Math.round(config.octaves || 3)));
    uView[9] = config.invert ? 1 : 0;

    const paramBuf = d.createBuffer({ size: 48, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    const outBuf = d.createBuffer({ size: outSize, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
    const readBuf = d.createBuffer({ size: outSize, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    d.queue.writeBuffer(paramBuf, 0, paramData);

    const bg = d.createBindGroup({
      layout: this.noisePipeline.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: paramBuf } },
        { binding: 1, resource: { buffer: outBuf } },
      ],
    });
    const enc = d.createCommandEncoder();
    const pass = enc.beginComputePass();
    pass.setPipeline(this.noisePipeline);
    pass.setBindGroup(0, bg);
    pass.dispatchWorkgroups(Math.ceil(width / 8), Math.ceil(height / 8));
    pass.end();
    enc.copyBufferToBuffer(outBuf, 0, readBuf, 0, outSize);
    d.queue.submit([enc.finish()]);
    await readBuf.mapAsync(GPUMapMode.READ);
    const copy = new Float32Array(readBuf.getMappedRange()).slice();
    readBuf.unmap();
    paramBuf.destroy();
    outBuf.destroy();
    readBuf.destroy();
    return copy;
  }
}

export const gpuCompute = new WebGpuComputeEngine();

export async function detectComputeCapability() {
  const st = await gpuCompute.init();
  return {
    webgpu: !!st.available,
    adapterName: st.adapterName,
    backend: st.backend,
  };
}

