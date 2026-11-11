// Light fibres: analytic streaks, ribbons and path trails ported from the Strand Editor.
// Fibres are not simulated. The GPU evaluates each fibre's curve per vertex from the loop
// phase, so a fibre system costs one uniform write (plus a path table when its path changes).
// Packing here must match struct Fib in shaders.js.
// 📝 Converted from an IIFE to an ES module; the body keeps its two-space indent so that `git blame`
//    still points at whoever wrote each line rather than at the conversion.
import { PE } from "./pe.js";

  PE.PATH_SAMPLES = 512;                                   // arc-length table size for trails
  PE.FIBRE_SHAPES = ["streak", "ribbon", "trail"];         // Bezier streak, Wave sheet, Path trail
  PE.FIBRE_PATHS = ["Ring", "Figure eight", "Rose", "Weave", "Loop", "Stadium", "Dipole field line"];
  PE.PULSE_SHAPES = ["Breathe", "Heartbeat", "Ripple", "Sweep"];

  // sRGB hex to linear RGBA, the colour space every system parameter uses.
  PE.hexLinear = function hexLinear(hex) {
    const v = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255);
    const lin = v.map((c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)));
    return [lin[0], lin[1], lin[2], 1];
  };

  // Every fibre parameter, with defaults from the Strand Editor's Organic fibres scene.
  // Lengths and spreads are local metres before `scale`; loopSeconds sets the animation period.
  PE.fibreParams = function fibreParams(o = {}) {
    return Object.assign({
      shape: "streak",
      strands: 200,
      segments: 72,
      seed: 11,
      loopSeconds: 12,
      scale: 1,
      length: 12,
      spread: 1.2,
      amplitude: 2.4,
      frequency: 1,
      sheetWidth: 6,
      ripple: 1,
      waves: 0.8,
      phaseSpread: 1.2,
      trailLength: 0.3,
      pathShape: "Ring",
      pathSize: 5,
      harmonic: 1,
      windowCycles: 1,
      window: 0.55,
      thickness: 1.5,
      taper: 0.6,
      intensity: 1.15,
      halo: 0.12,
      baseline: 0.1,
      // 📝 Colour: mode "solid" uses stops[0]; "ramp" blends stops along the fibre by position;
      //    "palette" picks one stop per strand (stops evenly weighted). Up to 8 stops.
      colourMode: "ramp",
      stops: [{ pos: 0, col: [0.094, 0.706, 1, 1] }, { pos: 1, col: [0.37, 0.85, 1, 1] }],
      colC: [0.92, 1, 1, 1],
      accentMix: 0.25,
      sparks: 0.8,
      sparkSize: 0.02,
      sparkBrightness: 3,
      pulseRate: 1,
      pulseDepth: 0.25,
      pulseShape: "Breathe",
    }, o);
  };

  // Closed parametric paths, the same set as the Strand Editor. Each is scaled so its furthest
  // point sits `size` metres from the origin, then resampled at equal arc length.
  const CURVES = {
    Ring: (a) => [Math.cos(a), Math.sin(a)],
    "Figure eight": (a) => [Math.sin(a), 0.5 * Math.sin(2 * a)],
    Rose: (a) => { const r = Math.cos(5 * a); return [r * Math.cos(a), r * Math.sin(a)]; },
    Weave: (a) => [Math.sin(3 * a), 0.6 * Math.sin(2 * a + 0.5)],
    Loop: (a) => { const r = (0.5 + Math.cos(a)) / 1.5; return [r * Math.cos(a), r * Math.sin(a)]; },
    // 📝 Dipole field line r = L·sin²θ (axis along the local y, which maps to world Z). One full line runs out and back through the dipole.
    "Dipole field line": (a) => { const th = a / 2; const r = Math.sin(th) ** 2; return [r * Math.sin(th), r * Math.cos(th)]; },
    Stadium: (a) => {
      const c = Math.cos(a), s = Math.sin(a);
      return [Math.sign(c) * Math.abs(c) ** 0.5, 0.36 * Math.sign(s) * Math.abs(s) ** 0.5];
    },
  };

  PE.samplePath = function samplePath(shape, size, count = PE.PATH_SAMPLES) {
    const curve = CURVES[shape] || CURVES.Ring;
    const N = 4096;
    const dense = [];
    for (let i = 0; i <= N; i++) dense.push(curve((i / N) * 2 * Math.PI));
    let furthest = 0;
    for (const [x, y] of dense) furthest = Math.max(furthest, Math.hypot(x, y));
    const scale = furthest > 0 ? size / furthest : 0;
    const cum = [0];
    for (let i = 1; i <= N; i++) {
      cum.push(cum[i - 1] + Math.hypot(dense[i][0] - dense[i - 1][0], dense[i][1] - dense[i - 1][1]));
    }
    const total = cum[N];
    const out = new Float32Array(count * 4);
    let cursor = 0;
    for (let s = 0; s < count; s++) {
      const target = (s / count) * total;
      while (cursor < N - 1 && cum[cursor + 1] < target) cursor++;
      const span = cum[cursor + 1] - cum[cursor];
      const r = span > 0 ? (target - cum[cursor]) / span : 0;
      const [ax, ay] = dense[cursor];
      const [bx, by] = dense[cursor + 1];
      out[s * 4] = (ax + (bx - ax) * r) * scale;
      out[s * 4 + 1] = (ay + (by - ay) * r) * scale;
    }
    return out;
  };

  PE.COLOUR_MODES = ["solid", "ramp", "palette"];
  PE.MAX_STOPS = 8;

  // Evenly spaced stops from a list of sRGB hex colours, for presets.
  PE.evenStops = function evenStops(hexes) {
    const n = hexes.length;
    return hexes.map((h, i) => ({ pos: n > 1 ? i / (n - 1) : 0, col: PE.hexLinear(h) }));
  };

  // Packs one fibre system into the 128-float Fib uniform (352 bytes used). The curve is built in local axes
  // where the fibre's path plane is XY and its lift is Z; the shader swaps Z and Y to the
  // editor's Y-up world. Directions are swapped the same way here.
  PE.fillFibre = function fillFibre(out, p, ctx) {
    const f = p.fibre;
    out.fill(0);
    const put = (slot, a, b, c, d) => out.set([a, b, c, d], slot * 4);
    const o = p.origin;
    const d = p.dir;
    const dl = Math.hypot(d[0], d[1], d[2]) || 1;
    put(0, o[0], o[1], o[2], f.scale);
    put(1, d[0] / dl, d[2] / dl, d[1] / dl, 0);
    put(2, f.length, f.spread, f.amplitude, f.frequency);
    put(3, f.sheetWidth, f.ripple, f.waves, f.phaseSpread);
    put(4, f.trailLength, ctx.lf, f.thickness, f.taper);
    put(5, f.intensity, f.halo, f.baseline, f.window);
    put(6, f.accentMix, f.pulseDepth, ctx.projScale, ctx.viewH / 1080);
    put(7, f.harmonic, f.windowCycles, f.pulseRate, Math.max(0, PE.PULSE_SHAPES.indexOf(f.pulseShape)));
    put(8, Math.max(0, PE.FIBRE_SHAPES.indexOf(f.shape)), f.strands, f.segments, f.seed);
    put(9, PE.PATH_SAMPLES, f.sparks, f.sparkSize, f.sparkBrightness);
    put(10, ctx.viewW, ctx.viewH, 0, 0);
    // Colour block: slot 11 accent, slots 12..19 stop colours, slots 20..21 stop positions.
    // p8.z = stop count, p8.w = colour mode index (0 solid, 1 ramp, 2 palette).
    const stops = (f.stops || []).slice(0, PE.MAX_STOPS).sort((a, b) => a.pos - b.pos);
    const count = Math.max(1, stops.length);
    const first = stops.length ? stops : [{ pos: 0, col: [1, 1, 1, 1] }];
    const mode = Math.max(0, PE.COLOUR_MODES.indexOf(f.colourMode));
    out.set([ctx.viewW, ctx.viewH, count, mode], 40);
    out.set([f.colC[0], f.colC[1], f.colC[2], 0], 44);
    for (let k = 0; k < count; k++) {
      const c = first[k].col;
      out.set([c[0], c[1], c[2], 1], (12 + k) * 4);
    }
    for (let k = 0; k < PE.MAX_STOPS; k++) {
      const pos = k < first.length ? first[k].pos : 1;
      out[(20 + (k >> 2)) * 4 + (k & 3)] = pos;
    }
  };
