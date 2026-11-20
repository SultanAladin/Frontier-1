/*============================================================================================================================================
                                                    SOFTWARE VIEWPORT
============================================================================================================================================*/
// 🧩 GPU-free fallback for the 3-D viewport: projects, depth-sorts and composites the same field geometry the WebGL path renders (streamlines,
// vectors, envelopes, motion paths, motes, probe), so the studio still shows the field on machines without WebGL2 — just without lighting.

import * as M from '../Engine/FieldMath.js';
import { EvaluateAt, FlowAt, SuggestMaximum } from '../Engine/FieldEvaluator.js';

export class SoftwareViewport {
  constructor(canvas) {
    this.Canvas = canvas;
    this.Context = canvas.getContext('2d');
    this.Motes = [];
    this.Seed = 0;
  }

  // One streamline pass, cached by StreamKey. Seeds are spread on a golden-angle disc so coverage is even and stable.
  BuildStreamlines(document, runtime, time, overlay, maximum) {
    const domain = document.Domain;
    const velocityAt = (p) => FlowAt(runtime, p, time, { Drift: 0.4 });
    const count = Math.max(8, Math.round(overlay.StreamSeedCount * 0.5));
    const centre = [(domain.Min[0] + domain.Max[0]) / 2, (domain.Min[1] + domain.Max[1]) / 2];
    const extent = [domain.Max[0] - domain.Min[0], domain.Max[1] - domain.Min[1], domain.Max[2] - domain.Min[2]];
    const streams = [];
    for (let i = 0; i < count; i++) {
      const r = Math.sqrt((i + 0.5) / count);
      const a = i * 2.39996323;
      const seed = [centre[0] + Math.cos(a) * r * extent[0] * 0.44, centre[1] + Math.sin(a) * r * extent[1] * 0.44, domain.Min[2] + ((i * 37) % 100) / 100 * extent[2] * 0.9];
      const points = M.IntegrateFlowLine(velocityAt, seed, { Steps: Math.round(overlay.StreamSteps * 0.6), StepSize: overlay.StreamStepSize * 1.25, Bounds: { Min: domain.Min, Max: domain.Max } });
      if (points.length < 3) continue;
      const colours = points.map((p) => {
        const c = M.SampleColormap(overlay.Colormap, Math.pow(Math.min(1, M.Length(velocityAt(p)) / Math.max(1e-6, maximum)), 0.7));
        // Lift toward white: thin lines need contrast more than hue fidelity on a near-black plate.
        return [c[0] * 0.82 + 46, c[1] * 0.82 + 46, c[2] * 0.82 + 46];
      });
      streams.push({ Points: points, Colours: colours });
    }
    return streams;
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
    this.Dpr = dpr;
    this.Context.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.CssWidth = width / dpr;
    this.CssHeight = height / dpr;
  }

  Render(document, runtime, time, camera, overlay, dt) {
    const g = this.Context;
    this.Resize();
    const width = this.CssWidth;
    const height = this.CssHeight;
    const aspect = width / height;
    const f = 1 / Math.tan((camera.Fov || 46) * (Math.PI / 360));
    const cp = Math.cos(camera.Pitch);
    const eye = [camera.Target[0] + Math.cos(camera.Yaw) * cp * camera.Distance, camera.Target[1] + Math.sin(camera.Yaw) * cp * camera.Distance, camera.Target[2] + Math.sin(camera.Pitch) * camera.Distance];
    const forward = M.Normalize(M.Sub(camera.Target, eye), [0, 0, -1]);
    const right = M.Normalize(M.Cross(forward, [0, 0, 1]), [1, 0, 0]);
    const up = M.Cross(right, forward);
    const near = 0.1;

    const projectPoint = (p) => {
      const d = M.Sub(p, eye);
      const z = M.Dot(d, forward);
      if (z <= near) return null;
      return [width / 2 + (M.Dot(d, right) / z) * f * (width / 2), height / 2 - (M.Dot(d, up) / z) * f * (height / 2), z];
    };

    g.clearRect(0, 0, width, height);
    g.fillStyle = '#0b0c10';
    g.fillRect(0, 0, width, height);
    const domain = document.Domain;
    this.DomainScale = Math.max(0.05, M.Length([domain.Max[0] - domain.Min[0], domain.Max[1] - domain.Min[1], domain.Max[2] - domain.Min[2]])) / 12;
    const maximum = overlay.AutoRange ? SuggestMaximum(runtime, domain, time, 'Speed') : overlay.MaxValue;

    // Distance haze matched to the WebGL pass: far geometry washes toward the background and thins out.
    const diagonal = M.Length([domain.Max[0] - domain.Min[0], domain.Max[1] - domain.Min[1], domain.Max[2] - domain.Min[2]]);
    const eyeDistance = M.Length(M.Sub(eye, camera.Target));
    const fogNear = eyeDistance - diagonal * 0.42;
    const fogFar = eyeDistance + diagonal * 0.85;
    const haze = [11, 12, 16];
    const fogOf = (z) => Math.max(0, Math.min(1, (z - fogNear) / Math.max(1e-4, fogFar - fogNear)));
    const hazeColour = (colour, fog, strength) => [
      colour[0] + (haze[0] - colour[0]) * fog * strength,
      colour[1] + (haze[1] - colour[1]) * fog * strength,
      colour[2] + (haze[2] - colour[2]) * fog * strength,
    ];

    const segments = [];
    const push3 = (a, b, colour, alpha = 1, widthPx = 1) => {
      const pa = projectPoint(a);
      const pb = projectPoint(b);
      if (!pa || !pb) return;
      // Geometry behind the eye can still project; keep its depth average honest.
      const fog = fogOf((pa[2] + pb[2]) / 2);
      segments.push({ A: pa, B: pb, Colour: hazeColour(colour, fog, 0.8), Alpha: alpha * (1 - 0.5 * fog), Width: widthPx, Depth: (pa[2] + pb[2]) / 2 });
    };

    if (overlay.ShowGrid) {
      const step = 2;
      const z = domain.Min[2];
      for (let x = Math.ceil(domain.Min[0] / step) * step; x <= domain.Max[0]; x += step) push3([x, domain.Min[1], z], [x, domain.Max[1], z], [38, 41, 48], 0.9, 1);
      for (let y = Math.ceil(domain.Min[1] / step) * step; y <= domain.Max[1]; y += step) push3([domain.Min[0], y, z], [domain.Max[0], y, z], [38, 41, 48], 0.9, 1);
    }
    push3([0, 0, 0], [2, 0, 0], [158, 89, 89], 0.9, 1.2);
    push3([0, 0, 0], [0, 2, 0], [102, 173, 87], 0.9, 1.2);
    push3([0, 0, 0], [0, 0, 2], [92, 140, 219], 0.9, 1.2);

    // Influence envelopes, motion paths and impulse fronts.
    if (overlay.ShowEnvelopes) {
      for (const source of document.Sources) {
        if (!source.Enabled || source.Muted) continue;
        const anchor = M.MotionAnchorAt(source, time);
        const colour = [74, 82, 96];
        if (source.Envelope === 'Sphere') {
          for (let i = 0; i < 3; i++) {
            for (let s = 0; s < 48; s++) {
              const a0 = (s / 48) * Math.PI * 2;
              const a1 = ((s + 1) / 48) * Math.PI * 2;
              const ring = (a) => (i === 0 ? [Math.cos(a) * source.Radius, Math.sin(a) * source.Radius, 0] : i === 1 ? [Math.cos(a) * source.Radius, 0, Math.sin(a) * source.Radius] : [0, Math.cos(a) * source.Radius, Math.sin(a) * source.Radius]);
              push3(M.Add(anchor, ring(a0)), M.Add(anchor, ring(a1)), colour, 0.6, 1);
            }
          }
        } else if (source.Envelope === 'Column') {
          for (const z of [-source.Height / 2, source.Height / 2]) {
            for (let s = 0; s < 48; s++) {
              const a0 = (s / 48) * Math.PI * 2;
              const a1 = ((s + 1) / 48) * Math.PI * 2;
              push3(M.Add(anchor, [Math.cos(a0) * source.Radius, Math.sin(a0) * source.Radius, z]), M.Add(anchor, [Math.cos(a1) * source.Radius, Math.sin(a1) * source.Radius, z]), colour, 0.6, 1);
            }
          }
          for (let k = 0; k < 6; k++) {
            const a = (k / 6) * Math.PI * 2;
            push3(M.Add(anchor, [Math.cos(a) * source.Radius, Math.sin(a) * source.Radius, -source.Height / 2]), M.Add(anchor, [Math.cos(a) * source.Radius, Math.sin(a) * source.Radius, source.Height / 2]), colour, 0.45, 1);
          }
        } else if (source.Envelope === 'Box') {
          const frame = M.DirectionFrame(source.Direction);
          const e = source.HalfExtents;
          const corners = [];
          for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1]) corners.push(M.Add(anchor, frame.ToWorld([sx * e[0], sy * e[1], sz * e[2]])));
          for (const [a, b] of [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]]) push3(corners[a], corners[b], colour, 0.6, 1);
        }
        if (source.Duration === 'Impulse') {
          const front = M.ImpulseFrontRadius(source, time);
          for (let s = 0; s < 48; s++) {
            const a0 = (s / 48) * Math.PI * 2;
            const a1 = ((s + 1) / 48) * Math.PI * 2;
            push3(M.Add(anchor, [Math.cos(a0) * front, Math.sin(a0) * front, 0]), M.Add(anchor, [Math.cos(a1) * front, Math.sin(a1) * front, 0]), [255, 128, 96], 0.7, 1.2);
          }
        }
        if (source.Motion !== 'Static') {
          const path = M.MotionPathSamples(source, 40, 10);
          for (let i = 1; i < path.length; i++) push3(path[i - 1], path[i], [138, 118, 82], 0.8, 1);
        }
      }
    }

    // Vectors.
    if (overlay.ShowVectors) {
      const pitch = (2.4 * 5) / Math.max(1, Math.min(10, Math.round(overlay.ArrowDensity)));
      const axisSteps = (lo, hi) => Math.max(1, Math.min(24, Math.round((hi - lo) / pitch) + 1));
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
            const vec = overlay.Quantity === 'Acceleration' ? sample.Acceleration : sample.Velocity;
            const magnitude = M.Length(vec);
            if (magnitude < 1e-4) continue;
            const t = Math.min(1, magnitude / Math.max(1e-6, maximum));
            // Draw a real arrow: shaft plus two head barbs, matching the solid head of the WebGL path.
            const unit = M.Normalize(vec, [0, 0, 1]);
            const length = ((0.35 + 1.4 * Math.pow(t, 0.6)) / 1.75) * (this.ArrowLength || 2.2);
            const tip = M.Add(p, M.Scale(unit, length));
            const colour = M.SampleColormap(overlay.Colormap, Math.pow(t, 0.75));
            push3(p, tip, colour, 0.95, 1.5 + t * 0.9);
            const back = M.Add(p, M.Scale(unit, length * 0.62));
            const side = M.Normalize(M.Cross(unit, [0, 0, 1]), [1, 0, 0]);
            const normal = M.Normalize(M.Cross(unit, side), [0, 0, 1]);
            const spread = length * 0.26;
            push3(tip, M.Add(back, M.Add(M.Scale(side, spread), M.Scale(normal, spread * 0.4))), colour, 0.9, 1.3 + t * 0.7);
            push3(tip, M.Add(back, M.Add(M.Scale(side, -spread), M.Scale(normal, -spread * 0.4))), colour, 0.9, 1.3 + t * 0.7);
          }
        }
      }
    }

    // Streamlines. Advection is cached against a coarse signature so the fallback path also keeps its frame rate.
    if (overlay.ShowStreamlines) {
      const key = `${overlay.StreamSeedCount}:${overlay.StreamSteps}:${overlay.StreamStepSize}:${overlay.Colormap}:${Math.floor(time)}:${overlay.SeedRevision || 0}`;
      if (this.StreamKey !== key) {
        this.StreamKey = key;
        this.Streams = this.BuildStreamlines(document, runtime, time, overlay, maximum);
      }
      for (const stream of this.Streams) {
        // Streamlines are the primary flow read in the fallback: bright enough to trace, fading only at their tails.
        for (let k = 1; k < stream.Points.length; k++) {
          const t = k / stream.Points.length;
          push3(stream.Points[k - 1], stream.Points[k], stream.Colours[k], 0.5 + 0.45 * t, 1.9);
        }
      }
    }

    segments.sort((a, b) => b.Depth - a.Depth);
    for (const segment of segments) {
      g.strokeStyle = `rgba(${segment.Colour[0] | 0},${segment.Colour[1] | 0},${segment.Colour[2] | 0},${segment.Alpha})`;
      g.lineWidth = segment.Width;
      g.beginPath();
      g.moveTo(segment.A[0], segment.A[1]);
      g.lineTo(segment.B[0], segment.B[1]);
      g.stroke();
    }

    // Motes.
    if (overlay.ShowMotes) {
      const extent = [domain.Max[0] - domain.Min[0], domain.Max[1] - domain.Min[1], domain.Max[2] - domain.Min[2]];
      if (this.Motes.length !== Math.round(overlay.MoteCount)) {
        let s = 991;
        const rng = () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
        this.Motes = Array.from({ length: Math.round(overlay.MoteCount) }, () => ({ P: [domain.Min[0] + rng() * extent[0], domain.Min[1] + rng() * extent[1], domain.Min[2] + rng() * extent[2]], V: [0, 0, 0], Age: rng() * 6 }));
      }
      for (let i = 0; i < this.Motes.length; i++) {
        const mote = this.Motes[i];
        if (dt > 0) {
          const sample = EvaluateAt(runtime, mote.P, time);
          const k = 1 - Math.exp(-Math.max(0, overlay.MoteCoupling) * dt);
          mote.V = [
            mote.V[0] + (sample.Velocity[0] - mote.V[0]) * k + sample.Acceleration[0] * dt,
            mote.V[1] + (sample.Velocity[1] - mote.V[1]) * k + sample.Acceleration[1] * dt,
            mote.V[2] + (sample.Velocity[2] - mote.V[2]) * k + sample.Acceleration[2] * dt,
          ];
          const damp = Math.exp(-Math.max(0, sample.Damping) * dt);
          mote.V = M.Scale(mote.V, damp);
          mote.P = M.Add(mote.P, M.Scale(mote.V, dt));
          mote.Age += dt;
          const broke = !(Number.isFinite(mote.P[0]) && Number.isFinite(mote.P[1]) && Number.isFinite(mote.P[2])) || mote.P[0] < domain.Min[0] || mote.P[0] > domain.Max[0] || mote.P[1] < domain.Min[1] || mote.P[1] > domain.Max[1] || mote.P[2] < domain.Min[2] || mote.P[2] > domain.Max[2] || mote.Age > 16;
          if (broke) {
            mote.Age = 0;
            mote.V = [0, 0, 0];
            mote.P = [domain.Min[0] + ((i * 0.618) % 1) * extent[0], domain.Min[1] + ((i * 0.755) % 1) * extent[1], domain.Min[2] + ((i * 0.570) % 1) * extent[2]];
          }
        }
        const p = projectPoint(mote.P);
        if (!p) continue;
        const speed = M.Length(mote.V);
        const colour = M.SampleColormap(overlay.Colormap, Math.pow(Math.min(1, speed / maximum), 0.8));
        const radius = 1.1 + Math.min(1.4, speed / Math.max(1e-6, maximum) * 1.6);
        g.fillStyle = `rgba(${colour[0] | 0},${colour[1] | 0},${colour[2] | 0},0.85)`;
        g.beginPath();
        g.arc(p[0], p[1], radius, 0, Math.PI * 2);
        g.fill();
      }
    }

    // Probe crosshair.
    if (overlay.ShowProbe && this.ProbePoint) {
      const p = projectPoint(this.ProbePoint);
      if (p) {
        g.strokeStyle = 'rgba(255,178,56,0.95)';
        g.lineWidth = 1.3;
        g.beginPath();
        g.arc(p[0], p[1], 7, 0, Math.PI * 2);
        g.stroke();
        g.beginPath();
        g.moveTo(p[0] - 13, p[1]);
        g.lineTo(p[0] + 13, p[1]);
        g.moveTo(p[0], p[1] - 13);
        g.lineTo(p[0], p[1] + 13);
        g.stroke();
      }
    }
    return true;
  }
}
