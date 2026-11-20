/*============================================================================================================================================
                                                      FIELD VIEWPORT
============================================================================================================================================*/
// 🧩 WebGL2 viewport for the accumulated field. Streamlines are RK4-advected on the CPU through the same evaluator the engine uses, then rasterised
// as camera-facing ribbons in one instanced pass; vectors are instanced 3-D shafts with cone heads; motes are advected probes drawn as gl points;
// influence envelopes, motion paths, grid and the probe are instanced line work. If WebGL2 is unavailable the viewport degrades to the software
// rasteriser instead of failing, so the studio always shows the field.

import * as M from '../Engine/FieldMath.js';
import { EvaluateAt, FlowAt, QuantityAt, SuggestMaximum, DEFAULT_OVERLAY } from '../Engine/FieldEvaluator.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                  MATRICES (column-major Float32Array)
//------------------------------------------------------------------------------------------------------------------------

export const M4 = {
  Identity: () => Float32Array.from([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]),
  Multiply(a, b) {
    const out = new Float32Array(16);
    for (let r = 0; r < 4; r++) {
      for (let c = 0; c < 4; c++) {
        out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
      }
    }
    return out;
  },
  Perspective(fovy, aspect, near, far) {
    const f = 1 / Math.tan(fovy / 2);
    const out = new Float32Array(16);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (far + near) / (near - far);
    out[11] = -1;
    out[14] = (2 * far * near) / (near - far);
    return out;
  },
  LookAt(eye, target, up = [0, 0, 1]) {
    const f = M.Normalize(M.Sub(target, eye), [0, 0, -1]);
    const s = M.Normalize(M.Cross(f, up), [1, 0, 0]);
    const u = M.Cross(s, f);
    const out = new Float32Array(16);
    out[0] = s[0]; out[1] = u[0]; out[2] = -f[0]; out[3] = 0;
    out[4] = s[1]; out[5] = u[1]; out[6] = -f[1]; out[7] = 0;
    out[8] = s[2]; out[9] = u[2]; out[10] = -f[2]; out[11] = 0;
    out[12] = -M.Dot(s, eye); out[13] = -M.Dot(u, eye); out[14] = M.Dot(f, eye); out[15] = 1;
    return out;
  },
  Translation(t) {
    const out = M4.Identity();
    out[12] = t[0]; out[13] = t[1]; out[14] = t[2];
    return out;
  },
  Scale(s) {
    const out = M4.Identity();
    out[0] = s[0]; out[5] = s[1]; out[10] = s[2];
    return out;
  },
};

// Rodrigues rotation vector → 3×3 matrix rows packed as vec3 [right, up, forward] for instance orientation.
export function BasisFromDirection(dir) {
  const f = M.Normalize(dir, [0, 0, 1]);
  const reference = Math.abs(f[2]) > 0.92 ? [1, 0, 0] : [0, 0, 1];
  const r = M.Normalize(M.Cross(reference, f), [1, 0, 0]);
  const u = M.Cross(f, r);
  return [r, u, f];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHADER SOURCES
//------------------------------------------------------------------------------------------------------------------------

const RIBBON_VERT = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPrev;
layout(location=1) in vec3 aCurr;
layout(location=2) in vec3 aNext;
layout(location=3) in vec4 aColor;
layout(location=4) in float aSide;
uniform mat4 uViewProj;
uniform vec3 uCameraTarget;
uniform float uWidth;
uniform float uPulse;
uniform vec3 uEye;
uniform vec2 uFog;
out vec4 vColor;
out float vFog;
void main() {
  vec3 tangent = normalize(aNext - aPrev + vec3(1e-5));
  vec3 toCamera = normalize(uCameraTarget - aCurr + vec3(1e-4));
  vec3 side = normalize(cross(tangent, toCamera));
  float w = uWidth * (1.2 + 0.9 * aColor.a) * uPulse;
  vec3 world = aCurr + side * aSide * w * 0.022;
  gl_Position = uViewProj * vec4(world, 1.0);
  vColor = vec4(aColor.rgb, 1.0);
  vFog = clamp((distance(world, uEye) - uFog.x) / max(1e-4, uFog.y - uFog.x), 0.0, 1.0);
}
`;

const RIBBON_FRAG = `#version 300 es
precision highp float;
in vec4 vColor;
in float vFog;
uniform vec3 uHaze;
out vec4 fragColor;
void main() {
  fragColor = vec4(mix(vColor.rgb, uHaze, vFog * 0.78), 1.0);
}
`;

const SHAFT_VERT = `#version 300 es
precision highp float;
layout(location=0) in vec3 aCorner;
layout(location=1) in vec3 aOrigin;
layout(location=2) in vec3 aRight;
layout(location=3) in vec3 aUp;
layout(location=4) in vec3 aForward;
layout(location=5) in vec4 aTint;
uniform mat4 uViewProj;
uniform float uThickness;
uniform float uScale;
uniform vec3 uEye;
uniform vec2 uFog;
out vec3 vTint;
out float vFog;
void main() {
  // aCorner.x runs 0 → 1 along the shaft and 1 → 2 out to the head tip; .yz is the cross-section offset.
  float x = aCorner.x;
  // Shaft runs 0 → 1 at full radius; the head runs 1 → 2, tapering to a point at the tip.
  float radius = x > 1.0 ? uThickness * mix(1.0, 0.04, x - 1.0) * 0.62 : uThickness;
  vec3 radial = (aRight * aCorner.y + aUp * aCorner.z) * radius;
  vec3 world = aOrigin + aForward * x * aTint.a * uScale + radial;
  gl_Position = uViewProj * vec4(world, 1.0);
  vTint = aTint.rgb;
  vFog = clamp((distance(world, uEye) - uFog.x) / max(1e-4, uFog.y - uFog.x), 0.0, 1.0);
}
`;

const SHAFT_FRAG = `#version 300 es
precision highp float;
in vec3 vTint;
in float vFog;
uniform vec3 uHaze;
out vec4 fragColor;
void main() {
  fragColor = vec4(mix(vTint, uHaze, vFog * 0.7), 1.0);
}
`;

const MOTE_VERT = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
layout(location=1) in vec4 aTint;
uniform mat4 uViewProj;
uniform float uPointSize;
uniform vec3 uEye;
uniform vec2 uFog;
out vec4 vTint;
out float vFog;
void main() {
  gl_Position = uViewProj * vec4(aPosition, 1.0);
  float fog = clamp((distance(aPosition, uEye) - uFog.x) / max(1e-4, uFog.y - uFog.x), 0.0, 1.0);
  // Distant motes shrink and dim with the fog so the volume keeps its depth ordering.
  gl_PointSize = uPointSize * (0.6 + aTint.a) * (1.25 - 0.55 * fog);
  vTint = aTint;
  vFog = fog;
}
`;

const MOTE_FRAG = `#version 300 es
precision highp float;
in vec4 vTint;
in float vFog;
uniform vec3 uHaze;
out vec4 fragColor;
void main() {
  vec2 d = gl_PointCoord - vec2(0.5);
  float r = length(d);
  if (r > 0.5) discard;
  float core = smoothstep(0.5, 0.12, r);
  vec3 tint = mix(vTint.rgb, uHaze, vFog * 0.8);
  fragColor = vec4(tint, (0.35 + 0.65 * core) * (1.0 - 0.45 * vFog));
}
`;

const LINE_VERT = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aColor;
uniform mat4 uViewProj;
uniform vec3 uEye;
uniform vec2 uFog;
out vec3 vColor;
out float vFog;
void main() {
  gl_Position = uViewProj * vec4(aPosition, 1.0);
  vColor = aColor;
  vFog = clamp((distance(aPosition, uEye) - uFog.x) / max(1e-4, uFog.y - uFog.x), 0.0, 1.0);
}
`;

const LINE_FRAG = `#version 300 es
precision highp float;
in vec3 vColor;
in float vFog;
uniform vec3 uHaze;
out vec4 fragColor;
void main() {
  // Hazed geometry also fades out, so the far side of the box recedes instead of stacking up solid lines.
  fragColor = vec4(mix(vColor, uHaze, vFog * 0.85), 1.0 - 0.55 * vFog);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                  PROGRAM HELPERS
//------------------------------------------------------------------------------------------------------------------------

function compile(gl, type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const info = gl.getShaderInfoLog(shader);
    gl.deleteShader(shader);
    throw new Error(`Shader compile failed: ${info}`);
  }
  return shader;
}

function program(gl, vertex, fragment) {
  const p = gl.createProgram();
  gl.attachShader(p, compile(gl, gl.VERTEX_SHADER, vertex));
  gl.attachShader(p, compile(gl, gl.FRAGMENT_SHADER, fragment));
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(`Program link failed: ${gl.getProgramInfoLog(p)}`);
  return p;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FIELD VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

export class FieldViewport {
  constructor(canvas, options = {}) {
    this.Canvas = canvas;
    this.Gl = null;
    this.Fallback = null;
    this.Error = null;
    this.Buffer = { Key: '', Geometry: null };
    this.Motes = null;
    this.MoteVelocity = new Float32Array(0);
    this.MoteTint = new Float32Array(0);
    this.MoteSeed = 0;
    this.Width = 1;
    this.Height = 1;
    this.DomainScale = 1;
    this.ArrowThickness = 0.03;
    try {
      const gl = canvas.getContext('webgl2', { antialias: true, alpha: false, preserveDrawingBuffer: true });
      if (!gl) throw new Error('WebGL2 context unavailable');
      this.Gl = gl;
      this.Programs = {
        Ribbon: program(gl, RIBBON_VERT, RIBBON_FRAG),
        Shaft: program(gl, SHAFT_VERT, SHAFT_FRAG),
        Mote: program(gl, MOTE_VERT, MOTE_FRAG),
        Line: program(gl, LINE_VERT, LINE_FRAG),
      };
      this.Buffers = {
        RibbonVao: gl.createVertexArray(),
        ShaftVao: gl.createVertexArray(),
        MoteVao: gl.createVertexArray(),
        LineVao: gl.createVertexArray(),
        ShaftCorner: gl.createBuffer(),
        Ribbon: gl.createBuffer(),
        ShaftInstance: gl.createBuffer(),
        MotePosition: gl.createBuffer(),
        MoteTint: gl.createBuffer(),
        Line: gl.createBuffer(),
      };
      this.BuildShaftGeometry();
      gl.enable(gl.DEPTH_TEST);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    } catch (error) {
      this.Error = error;
      this.Gl = null;
      this.Fallback = options.Fallback || null;
    }
  }

  get UsesWebGL() {
    return !!this.Gl;
  }

  BuildShaftGeometry() {
    const gl = this.Gl;
    // A unit shaft along +X with a four-sided pyramid head; the shader expands it by the instance thickness,
    // so an arrow is a real 3-D solid of revolution from any camera angle.
    const corners = [];
    const ring = [[0, -1, -1], [0, 1, -1], [0, 1, 1], [0, -1, 1]];
    const tailRing = ring.map((r) => [0, r[1], r[2]]);
    const neckRing = ring.map((r) => [1, r[1] * 0.45, r[2] * 0.45]);
    const tip = [2, 0, 0];
    const quad = (a, b, c, d) => corners.push(...a, ...b, ...c, ...a, ...c, ...d);
    for (let i = 0; i < 4; i++) {
      const n = (i + 1) % 4;
      quad(tailRing[i], tailRing[n], neckRing[n], neckRing[i]);
      corners.push(...neckRing[i], ...neckRing[n], ...tip); // four head triangles converging on the tip
    }
    this.ShaftVertexCount = corners.length / 3;
    this.ShaftStride = 16;
    gl.bindVertexArray(this.Buffers.ShaftVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.Buffers.ShaftCorner);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(corners), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.Buffers.ShaftInstance);
    const strideBytes = this.ShaftStride * 4;
    for (let i = 0; i < 4; i++) {
      gl.enableVertexAttribArray(1 + i);
      gl.vertexAttribPointer(1 + i, 3, gl.FLOAT, false, strideBytes, i * 12);
      gl.vertexAttribDivisor(1 + i, 1);
    }
    gl.enableVertexAttribArray(5);
    gl.vertexAttribPointer(5, 4, gl.FLOAT, false, strideBytes, 48);
    gl.vertexAttribDivisor(5, 1);
    gl.bindVertexArray(null);
  }

  Resize() {
    const canvas = this.Canvas;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const width = Math.max(1, Math.round(canvas.clientWidth * dpr));
    const height = Math.max(1, Math.round(canvas.clientHeight * dpr));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    this.Width = width;
    this.Height = height;
  }

  // ---------------------------------------------------------------- camera
  ViewProjection(camera) {
    const eye = this.EyeOf(camera);
    const aspect = Math.max(0.001, this.Width / this.Height);
    const projection = M4.Perspective((camera.Fov || 46) * (Math.PI / 180), aspect, 0.1, 600);
    const view = M4.LookAt(eye, camera.Target, [0, 0, 1]);
    return { Matrix: M4.Multiply(projection, view), Eye: eye };
  }

  EyeOf(camera) {
    const d = camera.Distance;
    const cp = Math.cos(camera.Pitch);
    return [
      camera.Target[0] + Math.cos(camera.Yaw) * cp * d,
      camera.Target[1] + Math.sin(camera.Yaw) * cp * d,
      camera.Target[2] + Math.sin(camera.Pitch) * d,
    ];
  }

  // World → screen in CSS pixels, used by the HUD overlay and for picking.
  Project(camera, point) {
    const { Matrix } = this.ViewProjection(camera);
    const x = point[0], y = point[1], z = point[2];
    const cx = Matrix[0] * x + Matrix[4] * y + Matrix[8] * z + Matrix[12];
    const cy = Matrix[1] * x + Matrix[5] * y + Matrix[9] * z + Matrix[13];
    const cz = Matrix[2] * x + Matrix[6] * y + Matrix[10] * z + Matrix[14];
    const cw = Matrix[3] * x + Matrix[7] * y + Matrix[11] * z + Matrix[15];
    if (cw <= 0.0001) return null;
    return [((cx / cw) * 0.5 + 0.5) * this.Canvas.clientWidth, (0.5 - (cy / cw) * 0.5) * this.Canvas.clientHeight, cz / cw];
  }

  // ---------------------------------------------------------------- geometry build (CPU advection, cached)
  // `keyExtra` lets the caller pin geometry to a coarse signature (document revision + whole seconds of field time) so an
  // expensive streamline pass is not re-run on every animation frame.
  BuildGeometry(document, runtime, time, overlay, keyExtra = null) {
    const key = [
      keyExtra === null ? time.toFixed(3) : `pinned:${keyExtra}`,
      overlay.SeedRevision || 0,
      overlay.StreamSeedCount,
      overlay.StreamSteps,
      overlay.StreamStepSize,
      overlay.StreamDirection,
      overlay.StreamColorMode,
      overlay.ArrowDensity,
      overlay.ShowVectors,
      overlay.ShowStreamlines,
      document.Sources.map((s) => `${s.Id}:${s.Enabled}:${s.Muted}:${s.Law}`).join('|'),
    ].join('#');
    if (this.Buffer.Key === key) return this.Buffer.Geometry;
    const domain = document.Domain;
    const maximum = overlay.AutoRange ? SuggestMaximum(runtime, domain, time, 'Speed') : overlay.MaxValue;
    const geometry = {
      Maximum: maximum,
      Streamlines: [],
      Arrows: [],
      Lines: [],
      Envelopes: [],
    };

    // Streamlines follow the drawn flow — medium velocity plus the drift induced by force laws — so acceleration-only
    // generators are visible in the flow picture instead of reading as empty space.
    const velocityAt = (p) => FlowAt(runtime, p, time, { Drift: 0.4 });

    if (overlay.ShowStreamlines) {
      const count = Math.max(8, Math.round(overlay.StreamSeedCount));
      const golden = 2.39996323;
      const centre = [(domain.Min[0] + domain.Max[0]) / 2, (domain.Min[1] + domain.Max[1]) / 2, (domain.Min[2] + domain.Max[2]) / 2];
      const extent = [domain.Max[0] - domain.Min[0], domain.Max[1] - domain.Min[1], domain.Max[2] - domain.Min[2]];
      for (let i = 0; i < count; i++) {
        const r = Math.sqrt((i + 0.5) / count);
        const a = i * golden;
        const seed = [centre[0] + Math.cos(a) * r * extent[0] * 0.44, centre[1] + Math.sin(a) * r * extent[1] * 0.44, domain.Min[2] + ((i * 37) % 100) / 100 * extent[2] * 0.9];
        const forward = M.IntegrateFlowLine(velocityAt, seed, {
          Steps: Math.round(overlay.StreamSteps * 0.85),
          StepSize: overlay.StreamStepSize * 1.25,
          Direction: 1,
          Bounds: { Min: domain.Min, Max: domain.Max },
        });
        const backward = overlay.StreamDirection === 'Both'
          ? M.IntegrateFlowLine(velocityAt, seed, { Steps: Math.round(overlay.StreamSteps * 0.5), StepSize: overlay.StreamStepSize * 1.25, Direction: -1, Bounds: { Min: domain.Min, Max: domain.Max } })
          : [];
        const points = backward.slice(1).reverse().concat(forward);
        if (points.length < 3) continue;
        const velocity = points.map((p) => M.Length(velocityAt(p)));
        const colour = (v) => {
          const t = Math.min(1, v / Math.max(1e-6, maximum));
          if (overlay.StreamColorMode === 'CyanSingle') return [104, 220, 255];
          if (overlay.StreamColorMode === 'Ribbon') return M.SampleColormap('Turbo', Math.sqrt(t));
          return M.SampleColormap(overlay.Colormap, Math.pow(t, 0.7));
        };
        geometry.Streamlines.push({
          Points: points,
          Colours: velocity.map(colour),
          Average: velocity.reduce((a, b) => a + b, 0) / velocity.length,
        });
      }
    }

    if (overlay.ShowVectors) {
      const diagonal = M.Length([domain.Max[0] - domain.Min[0], domain.Max[1] - domain.Min[1], domain.Max[2] - domain.Min[2]]);
      this.DomainScale = Math.max(0.05, diagonal) / 12;
      this.ArrowThickness = 0.075 * (this.ArrowLength || 2.2);
      // Density 4 aims at a 2.4 m sample pitch; larger domains get more samples so the field never looks sparse.
      const pitch = (2.4 * 5) / Math.max(1, Math.min(10, Math.round(overlay.ArrowDensity)));
      const axisSteps = (lo, hi, count) => Math.max(1, Math.min(24, Math.round((hi - lo) / pitch) + 1));
      const steps = [axisSteps(domain.Min[0], domain.Max[0]), axisSteps(domain.Min[1], domain.Max[1]), axisSteps(domain.Min[2], domain.Max[2])];
      this.ArrowLength = pitch * 0.92 * Math.max(0.25, overlay.ArrowScale);
      for (let ix = 0; ix < steps[0]; ix++) {
        for (let iy = 0; iy < steps[1]; iy++) {
          for (let iz = 0; iz < steps[2]; iz++) {
            const p = [
              domain.Min[0] + ((ix + 0.5) / steps[0]) * (domain.Max[0] - domain.Min[0]),
              domain.Min[1] + ((iy + 0.5) / steps[1]) * (domain.Max[1] - domain.Min[1]),
              domain.Min[2] + ((iz + 0.5) / steps[2]) * (domain.Max[2] - domain.Min[2]),
            ];
            const sample = EvaluateAt(runtime, p, time);
            const source = overlay.Quantity === 'Acceleration' ? sample.Acceleration : sample.Velocity;
            const magnitude = M.Length(source);
            if (magnitude < 1e-4) continue;
            const t = Math.min(1, magnitude / Math.max(1e-6, maximum));
            const length = (0.35 + 1.5 * Math.pow(t, 0.6)) / 1.85;
            geometry.Arrows.push({ Origin: p, Direction: M.Normalize(source, [0, 0, 1]), Length: length * (this.ArrowLength || 2.2), Colour: M.SampleColormap(overlay.Colormap, Math.pow(t, 0.75)), Magnitude: magnitude });
          }
        }
      }
    }

    if (overlay.ShowEnvelopes) {
      for (const source of document.Sources) {
        if (!source.Enabled || source.Muted) continue;
        const anchor = M.MotionAnchorAt(source, time);
        const colour = [128, 148, 176];
        if (source.Envelope === 'Sphere') {
          geometry.Envelopes.push({ Kind: 'Sphere', Centre: anchor, Radius: source.Radius, Colour: colour, Source: source });
        } else if (source.Envelope === 'Column') {
          geometry.Envelopes.push({ Kind: 'Column', Centre: anchor, Radius: source.Radius, Height: source.Height, Colour: colour, Source: source });
        } else if (source.Envelope === 'Box') {
          geometry.Envelopes.push({ Kind: 'Box', Centre: anchor, HalfExtents: source.HalfExtents, Frame: M.DirectionFrame(source.Direction), Colour: colour, Source: source });
        }
        if (source.Duration === 'Impulse') {
          geometry.Envelopes.push({ Kind: 'Sphere', Centre: anchor, Radius: M.ImpulseFrontRadius(source, time), Colour: [255, 128, 96], Source: source });
        }
        if (overlay.ShowMotionPaths && source.Motion !== 'Static') {
          const path = M.MotionPathSamples(source, 40, 10);
          for (let i = 1; i < path.length; i++) geometry.Lines.push({ A: path[i - 1], B: path[i], Colour: [150, 130, 90] });
        }
      }
    }
    this.Buffer = { Key: key, Geometry: geometry };
    return geometry;
  }

  // ---------------------------------------------------------------- motes
  EnsureMotes(count, domain) {
    if (this.Motes && this.Motes.length === count) return;
    const rng = (() => {
      let s = 1337 + this.MoteSeed;
      return () => {
        s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
        return s / 4294967296;
      };
    })();
    this.Motes = Array.from({ length: count }, () => ({
      P: [
        domain.Min[0] + rng() * (domain.Max[0] - domain.Min[0]),
        domain.Min[1] + rng() * (domain.Max[1] - domain.Min[1]),
        domain.Min[2] + rng() * (domain.Max[2] - domain.Min[2]),
      ],
      V: [0, 0, 0],
      Age: rng() * 4,
    }));
    this.MotePosition = new Float32Array(count * 3);
    this.MoteTint = new Float32Array(count * 4);
  }

  UpdateMotes(document, runtime, dt, time, overlay) {
    const domain = document.Domain;
    this.EnsureMotes(Math.max(30, Math.round(overlay.MoteCount)), domain);
    const maximum = this.Buffer.Geometry ? this.Buffer.Geometry.Maximum : 10;
    for (let i = 0; i < this.Motes.length; i++) {
      const mote = this.Motes[i];
      if (dt > 0) {
        const steps = Math.max(1, Math.min(4, Math.ceil(dt / (1 / 45))));
        const h = dt / steps;
        for (let s = 0; s < steps; s++) {
          const sample = EvaluateAt(runtime, mote.P, time);
          const k = 1 - Math.exp(-Math.max(0, overlay.MoteCoupling) * h);
          const vx = mote.V[0] + (sample.Velocity[0] - mote.V[0]) * k + sample.Acceleration[0] * h;
          const vy = mote.V[1] + (sample.Velocity[1] - mote.V[1]) * k + sample.Acceleration[1] * h;
          const vz = mote.V[2] + (sample.Velocity[2] - mote.V[2]) * k + sample.Acceleration[2] * h;
          const damp = Math.exp(-Math.max(0, sample.Damping) * h);
          mote.V = [vx * damp, vy * damp, vz * damp];
          mote.P = [mote.P[0] + mote.V[0] * h, mote.P[1] + mote.V[1] * h, mote.P[2] + mote.V[2] * h];
        }
        mote.Age += dt;
        let outside = mote.P[0] < domain.Min[0] || mote.P[0] > domain.Max[0] || mote.P[1] < domain.Min[1] || mote.P[1] > domain.Max[1] || mote.P[2] < domain.Min[2] || mote.P[2] > domain.Max[2];
        if (!Number.isFinite(mote.P[0]) || !Number.isFinite(mote.P[1]) || !Number.isFinite(mote.P[2])) outside = true;
        if (outside || mote.Age > 16) {
          mote.Age = 0;
          mote.V = [0, 0, 0];
          mote.P = [
            domain.Min[0] + ((i * 0.6180339887) % 1) * (domain.Max[0] - domain.Min[0]),
            domain.Min[1] + ((i * 0.7548776662) % 1) * (domain.Max[1] - domain.Min[1]),
            domain.Min[2] + ((i * 0.5698402910) % 1) * (domain.Max[2] - domain.Min[2]),
          ];
        }
      }
      this.MotePosition[i * 3] = mote.P[0];
      this.MotePosition[i * 3 + 1] = mote.P[1];
      this.MotePosition[i * 3 + 2] = mote.P[2];
      const speed = M.Length(mote.V);
      const t = Math.min(1, speed / Math.max(1e-6, maximum));
      const colour = M.SampleColormap(overlay.Colormap, Math.pow(t, 0.8));
      this.MoteTint[i * 4] = colour[0] / 255;
      this.MoteTint[i * 4 + 1] = colour[1] / 255;
      this.MoteTint[i * 4 + 2] = colour[2] / 255;
      this.MoteTint[i * 4 + 3] = t;
    }
  }

  // ---------------------------------------------------------------- draw
  Render(document, runtime, time, camera, overlay) {
    if (!this.Gl) return false;
    const gl = this.Gl;
    this.Resize();
    const geometry = this.BuildGeometry(document, runtime, time, overlay);
    const { Matrix, Eye } = this.ViewProjection(camera);

    // Distance haze. Near/far follow the eye distance to the domain, so zooming keeps the same visual depth falloff.
    const diagonal = M.Length([document.Domain.Max[0] - document.Domain.Min[0], document.Domain.Max[1] - document.Domain.Min[1], document.Domain.Max[2] - document.Domain.Min[2]]);
    const eyeDistance = M.Length(M.Sub(Eye, camera.Target));
    const fog = [eyeDistance - diagonal * 0.42, eyeDistance + diagonal * 0.85];
    for (const name of ['Ribbon', 'Shaft', 'Mote', 'Line']) {
      const program = this.Programs[name];
      gl.useProgram(program);
      gl.uniform3fv(gl.getUniformLocation(program, 'uEye'), new Float32Array(Eye));
      gl.uniform2fv(gl.getUniformLocation(program, 'uFog'), new Float32Array(fog));
      gl.uniform3fv(gl.getUniformLocation(program, 'uHaze'), new Float32Array([0.043, 0.047, 0.058]));
    }

    gl.viewport(0, 0, this.Width, this.Height);
    gl.clearColor(0.043, 0.047, 0.058, 1);
    gl.depthMask(true);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    const lines = [];
    this.PendingLines = lines;
    if (overlay.ShowGrid) this.GroundGridLines(document.Domain);
    if (overlay.ShowEnvelopes) {
      for (const envelope of geometry.Envelopes) this.EnvelopeLines(envelope);
      for (const segment of geometry.Lines) {
        lines.push(segment.A[0], segment.A[1], segment.A[2], segment.Colour[0] / 255, segment.Colour[1] / 255, segment.Colour[2] / 255, segment.B[0], segment.B[1], segment.B[2], segment.Colour[0] / 255, segment.Colour[1] / 255, segment.Colour[2] / 255);
      }
    }
    this.AxisLines();
    if (overlay.ShowProbe && this.ProbePoint) this.ProbeLines(this.ProbePoint);
    this.PendingLines = null;

    if (overlay.ShowStreamlines) this.DrawStreamlines(geometry, Matrix, camera.Target, time);
    if (overlay.ShowVectors) this.DrawArrows(geometry, Matrix);
    if (overlay.ShowMotes) this.DrawMotes(Matrix);
    this.DrawLineArray(lines, Matrix, gl.LINES);
    return true;
  }

  GroundGridLines(domain) {
    const lines = this.PendingLines;
    const step = 2;
    const major = 6;
    const z = domain.Min[2];
    for (let x = Math.ceil(domain.Min[0] / step) * step; x <= domain.Max[0]; x += step) {
      const v = Math.abs(x % major) < 1e-6 ? 0.2 : 0.1;
      lines.push(x, domain.Min[1], z, v, v, v * 1.15, x, domain.Max[1], z, v, v, v * 1.15);
    }
    for (let y = Math.ceil(domain.Min[1] / step) * step; y <= domain.Max[1]; y += step) {
      const v = Math.abs(y % major) < 1e-6 ? 0.2 : 0.1;
      lines.push(domain.Min[0], y, z, v, v, v * 1.15, domain.Max[0], y, z, v, v, v * 1.15);
    }
  }

  AxisLines() {
    const lines = this.PendingLines;
    lines.push(0, 0, 0, 0.62, 0.35, 0.35, 2, 0, 0, 0.62, 0.35, 0.35);
    lines.push(0, 0, 0, 0.4, 0.68, 0.34, 0, 2, 0, 0.4, 0.68, 0.34);
    lines.push(0, 0, 0, 0.36, 0.55, 0.86, 0, 0, 2, 0.36, 0.55, 0.86);
  }

  ProbeLines(p) {
    const lines = this.PendingLines;
    const s = 0.9;
    for (const axis of [[1, 0, 0], [0, 1, 0], [0, 0, 1]]) {
      lines.push(p[0] - axis[0] * s, p[1] - axis[1] * s, p[2] - axis[2] * s, 1, 0.7, 0.22, p[0] + axis[0] * s, p[1] + axis[1] * s, p[2] + axis[2] * s, 1, 0.7, 0.22);
    }
  }

  DrawEnvelopes(geometry, viewProj) {
    const gl = this.Gl;
    const lines = [];
    this.PendingLines = lines;
    for (const envelope of geometry.Envelopes) this.EnvelopeLines(envelope);
    this.PendingLines = null;
    this.DrawLineArray(lines, viewProj, gl.LINES);
  }

  EnvelopeLines(envelope) {
    this.PendingLines = this.PendingLines || [];
    const push = (a, b, c) => this.PendingLines.push(a[0], a[1], a[2], c[0], c[1], c[2], b[0], b[1], b[2], c[0], c[1], c[2]);
    const colour = envelope.Colour.map((v) => v / 255);
    if (envelope.Kind === 'Sphere') {
      const r = envelope.Radius;
      for (let i = 0; i < 3; i++) {
        const segments = 64;
        for (let s = 0; s < segments; s++) {
          const a0 = (s / segments) * Math.PI * 2;
          const a1 = ((s + 1) / segments) * Math.PI * 2;
          const dir = (a) => {
            const v = [Math.cos(a) * r, Math.sin(a) * r, 0];
            if (i === 1) return [v[0], 0, v[1]];
            if (i === 2) return [0, v[0], v[1]];
            return v;
          };
          push(M.Add(envelope.Centre, dir(a0)), M.Add(envelope.Centre, dir(a1)), colour);
        }
      }
    } else if (envelope.Kind === 'Column') {
      const { Radius: r, Height: h } = envelope;
      for (const z of [-h / 2, h / 2]) {
        const segments = 64;
        for (let s = 0; s < segments; s++) {
          const a0 = (s / segments) * Math.PI * 2;
          const a1 = ((s + 1) / segments) * Math.PI * 2;
          push(M.Add(envelope.Centre, [Math.cos(a0) * r, Math.sin(a0) * r, z]), M.Add(envelope.Centre, [Math.cos(a1) * r, Math.sin(a1) * r, z]), colour);
        }
      }
      for (let k = 0; k < 8; k++) {
        const a = (k / 8) * Math.PI * 2;
        push(M.Add(envelope.Centre, [Math.cos(a) * r, Math.sin(a) * r, -h / 2]), M.Add(envelope.Centre, [Math.cos(a) * r, Math.sin(a) * r, h / 2]), colour);
      }
    } else if (envelope.Kind === 'Box') {
      const e = envelope.HalfExtents;
      const corners = [];
      for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1]) corners.push(M.Add(envelope.Centre, envelope.Frame.ToWorld([sx * e[0], sy * e[1], sz * e[2]])));
      const edges = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
      for (const [a, b] of edges) push(corners[a], corners[b], colour);
    }
  }

  DrawStreamlines(geometry, viewProj, target, time) {
    const gl = this.Gl;
    // Each segment becomes a camera-facing ribbon quad: two triangles, each vertex carrying prev/curr/next so the
    // vertex shader can build the cross-section. Alpha in aColor rides the travelling pulse for direction cues.
    const data = [];
    for (const streamline of geometry.Streamlines) {
      const { Points: points, Colours: colours } = streamline;
      const count = points.length;
      for (let i = 1; i < count - 1; i++) {
        const prev = points[i - 1];
        const curr = points[i];
        const next = points[i + 1];
        const nprev = points[Math.max(0, i - 2)];
        const nnext = points[Math.min(count - 1, i + 2)];
        const colour = colours[i];
        const pulse = 0.45 + 0.55 * Math.max(0, Math.sin(i * 0.28 - time * 3.2 + streamline.Average));
        const push = (behind, ahead, tail, side) => data.push(
          tail[0], tail[1], tail[2], behind[0], behind[1], behind[2], ahead[0], ahead[1], ahead[2],
          colour[0] / 255, colour[1] / 255, colour[2] / 255, pulse, side,
        );
        for (const side of [-1, 1]) {
          push(nprev, curr, prev, side);
          push(prev, next, curr, side);
          push(curr, nnext, next, side);
        }
      }
    }
    if (!data.length) return;
    gl.useProgram(this.Programs.Ribbon);
    gl.uniformMatrix4fv(gl.getUniformLocation(this.Programs.Ribbon, 'uViewProj'), false, viewProj);
    gl.uniform3fv(gl.getUniformLocation(this.Programs.Ribbon, 'uCameraTarget'), target);
    gl.uniform1f(gl.getUniformLocation(this.Programs.Ribbon, 'uWidth'), 1);
    gl.uniform1f(gl.getUniformLocation(this.Programs.Ribbon, 'uPulse'), 1);
    gl.bindVertexArray(this.Buffers.RibbonVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.Buffers.Ribbon);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(data), gl.DYNAMIC_DRAW);
    const stride = 14 * 4;
    const sizes = [3, 3, 3, 4, 1];
    let offset = 0;
    for (let i = 0; i < sizes.length; i++) {
      gl.enableVertexAttribArray(i);
      gl.vertexAttribPointer(i, sizes[i], gl.FLOAT, false, stride, offset);
      offset += sizes[i] * 4;
    }
    gl.drawArrays(gl.TRIANGLES, 0, data.length / 14);
    gl.bindVertexArray(null);
  }

  DrawArrows(geometry, viewProj) {
    const gl = this.Gl;
    if (!geometry.Arrows.length) return;
    // Arrow geometry is modelled in domain units, so lengths and radii are scaled by the domain half-diagonal and stay
    // legible whether the field spans eight metres or eighty.
    gl.uniform1f(gl.getUniformLocation(this.Programs.Shaft, 'uScale'), this.DomainScale || 1);
    // Instance layout: origin(3), right(3), up(3), forward(3), tint(4) with tint.a carrying the arrow length.
    const stride = 16;
    const packed = new Float32Array(geometry.Arrows.length * stride);
    geometry.Arrows.forEach((arrow, i) => {
      const [r, u] = BasisFromDirection(arrow.Direction);
      packed.set([
        arrow.Origin[0], arrow.Origin[1], arrow.Origin[2],
        r[0], r[1], r[2],
        u[0], u[1], u[2],
        arrow.Direction[0], arrow.Direction[1], arrow.Direction[2],
        arrow.Colour[0] / 255, arrow.Colour[1] / 255, arrow.Colour[2] / 255, arrow.Length,
      ], i * stride);
    });
    gl.useProgram(this.Programs.Shaft);
    gl.uniformMatrix4fv(gl.getUniformLocation(this.Programs.Shaft, 'uViewProj'), false, viewProj);
    gl.uniform1f(gl.getUniformLocation(this.Programs.Shaft, 'uThickness'), this.ArrowThickness || 0.03);
    gl.bindVertexArray(this.Buffers.ShaftVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.Buffers.ShaftInstance);
    gl.bufferData(gl.ARRAY_BUFFER, packed, gl.DYNAMIC_DRAW);
    const strideBytes = stride * 4;
    for (let i = 0; i < 4; i++) {
      gl.enableVertexAttribArray(1 + i);
      gl.vertexAttribPointer(1 + i, 3, gl.FLOAT, false, strideBytes, i * 12);
      gl.vertexAttribDivisor(1 + i, 1);
    }
    gl.enableVertexAttribArray(5);
    gl.vertexAttribPointer(5, 4, gl.FLOAT, false, strideBytes, 48);
    gl.vertexAttribDivisor(5, 1);
    gl.drawArraysInstanced(gl.TRIANGLES, 0, this.ShaftVertexCount, geometry.Arrows.length);
    gl.bindVertexArray(null);
  }

  DrawMotes(viewProj) {
    const gl = this.Gl;
    if (!this.Motes || !this.Motes.length) return;
    gl.useProgram(this.Programs.Mote);
    gl.uniformMatrix4fv(gl.getUniformLocation(this.Programs.Mote, 'uViewProj'), false, viewProj);
    gl.uniform1f(gl.getUniformLocation(this.Programs.Mote, 'uPointSize'), 3.4 * (window.devicePixelRatio || 1));
    gl.bindVertexArray(this.Buffers.MoteVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.Buffers.MotePosition);
    gl.bufferData(gl.ARRAY_BUFFER, this.MotePosition, gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.Buffers.MoteTint);
    gl.bufferData(gl.ARRAY_BUFFER, this.MoteTint, gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 0, 0);
    gl.drawArrays(gl.POINTS, 0, this.Motes.length);
    gl.bindVertexArray(null);
  }

  DrawLineArray(data, viewProj, mode) {
    if (!data || !data.length) return false;
    const gl = this.Gl;
    gl.useProgram(this.Programs.Line);
    gl.uniformMatrix4fv(gl.getUniformLocation(this.Programs.Line, 'uViewProj'), false, viewProj);
    gl.bindVertexArray(this.Buffers.LineVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.Buffers.Line);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(data), gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 24, 12);
    gl.drawArrays(mode, 0, data.length / 6);
    gl.bindVertexArray(null);
    return true;
  }
}
