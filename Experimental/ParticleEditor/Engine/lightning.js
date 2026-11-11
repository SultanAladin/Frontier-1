// Lightning bolts (CPU midpoint-displacement fractals, drawn as GPU quads) and
// thunder (WebAudio synthesis, delayed by distance / 343 m/s).
// 📝 Converted from an IIFE to an ES module; the body keeps its two-space indent so that `git blame`
//    still points at whoever wrote each line rather than at the conversion.
import { PE } from "./pe.js";

  function mulberry32(seed) {
    let a = seed >>> 0;
    return function () {
      a = (a + 0x6d2b79f5) >>> 0;
      let t = a;
      t = Math.imul(t ^ (t >>> 15), t | 1);
      t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  PE.mulberry32 = mulberry32;

  // Builds one bolt as a list of {a, b, w} segments. Branches are thinner and shorter.
  function makeBolt(rng, start, end, opt) {
    const segs = [];
    const MAX = PE.MAX_SEGS - 64;
    const rec = (a, b, amp, level, w, branchOk) => {
      if (level <= 0 || segs.length > MAX) {
        segs.push({ a, b, w });
        return;
      }
      const m = [
        (a[0] + b[0]) / 2 + (rng() - 0.5) * amp,
        (a[1] + b[1]) / 2 + (rng() - 0.5) * amp * 0.4,
        (a[2] + b[2]) / 2 + (rng() - 0.5) * amp,
      ];
      rec(a, m, amp * 0.5, level - 1, w, branchOk);
      rec(m, b, amp * 0.5, level - 1, w, branchOk);
      if (branchOk && level >= 3 && rng() < opt.branchChance) {
        const e = [
          m[0] + (rng() - 0.5) * amp * 2 + (b[0] - a[0]) * 0.35,
          m[1] - amp * (0.6 + rng()),
          m[2] + (rng() - 0.5) * amp * 2 + (b[2] - a[2]) * 0.35,
        ];
        rec(m, e, amp * 0.6, level - 2, w * 0.5, false);
      }
    };
    rec(start, end, opt.amp, opt.levels, opt.width, true);
    return segs;
  }

  PE.Lightning = class Lightning {
    constructor() {
      this.bolts = [];
      this.flash = 0;
      this.auto = true;
      this.intervalMin = 3;
      this.intervalMax = 9;
      this.nextAt = 1.5;
      this.distance = 1200;     // metres from the viewer, sets thunder delay
      this.amp = 2.0;
      this.levels = 6;
      this.branchChance = 0.35;
      this.strikes = 0;
      this.lastStrike = null;
      this.rng = mulberry32(20261008);
      this.thunder = new PE.Thunder();
      this.dataBuf = new Float32Array(PE.MAX_SEGS * 8);
      this.segCount = 0;
    }

    // Starts a strike. Returns the ground point so the caller can fire sparks there.
    strike(now) {
      const r = this.rng;
      const x = (r() - 0.5) * 9;
      const z = (r() - 0.5) * 9;
      const start = [x + (r() - 0.5) * 2, 15, z + (r() - 0.5) * 2];
      const end = [x, 0, z];
      const segs = makeBolt(r, start, end, {
        amp: this.amp, levels: this.levels, width: 0.11, branchChance: this.branchChance,
      });
      this.bolts.push({ segs, t0: now, life: 0.36, seed: r() * 1000 });
      this.flash = Math.min(1.4, this.flash + 1.0);
      this.strikes++;
      this.lastStrike = { at: end, time: now };
      const delay = this.distance / 343;
      this.thunder.strike(delay, this.distance);
      return end;
    }

    // Advances bolts and auto-strikes. Returns the strike points that happened this step.
    update(dt, now) {
      this.flash = Math.max(0, this.flash - dt * 3.2);
      this.bolts = this.bolts.filter((b) => now - b.t0 < b.life);
      const hits = [];
      if (this.auto && now >= this.nextAt) {
        hits.push(this.strike(now));
        this.nextAt = now + this.intervalMin + this.rng() * Math.max(0, this.intervalMax - this.intervalMin);
      }
      return hits;
    }

    // 📝 Lightning web: each node links to its k nearest neighbours with one branching bolt per link.
    // Nodes are scene objects (particle-system origins). Bolts are drawn only; they do not light particles.
    web(now, nodes, opt = {}) {
      const k = opt.k || 2;
      const life = opt.life || 0.3;
      const maxLinks = opt.maxLinks || 16;
      const pairs = new Map();
      nodes.forEach((a, i) => {
        const near = nodes
          .map((b, j) => ({ j, d: Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) }))
          .filter((o) => o.j !== i)
          .sort((x, y) => x.d - y.d)
          .slice(0, k);
        for (const o of near) pairs.set(Math.min(i, o.j) + "," + Math.max(i, o.j), [i, o.j]);
      });
      let made = 0;
      for (const [i, j] of pairs.values()) {
        if (made >= maxLinks) break;
        const a = nodes[i];
        const b = nodes[j];
        const d = Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
        const segs = makeBolt(this.rng, a, b, {
          amp: Math.min(1.5, 0.05 * d + 0.2), levels: 4, width: 0.07, branchChance: 0.5,
        });
        this.bolts.push({ segs, t0: now, life, seed: this.rng() * 1000 });
        made++;
      }
      return made;
    }

    // Packs the alive bolts into the GPU segment buffer: (a, width), (b, intensity).
    pack(now) {
      let n = 0;
      const out = this.dataBuf;
      for (const bolt of this.bolts) {
        const age = now - bolt.t0;
        const fade = Math.pow(Math.max(0, 1 - age / bolt.life), 1.5);
        // Flicker: a new brightness value every 40 ms.
        const cell = Math.floor(age / 0.04);
        const flick = 0.55 + 0.45 * ((Math.sin((cell + bolt.seed) * 91.7) * 43758.5453) % 1 + 1) % 1;
        const inten = 2.6 * fade * Math.abs(flick);
        for (const s of bolt.segs) {
          if (n >= PE.MAX_SEGS) break;
          const o = n * 8;
          out[o] = s.a[0]; out[o + 1] = s.a[1]; out[o + 2] = s.a[2]; out[o + 3] = s.w;
          out[o + 4] = s.b[0]; out[o + 5] = s.b[1]; out[o + 6] = s.b[2]; out[o + 7] = inten;
          n++;
        }
      }
      this.segCount = n;
      return n;
    }
  };

  // Thunder: brown-noise rumble with a low-pass sweep, plus a short crack for close strikes.
  PE.Thunder = class Thunder {
    constructor() {
      this.ctx = null;
      this.rumble = null;
      this.white = null;
      this.enabled = false;
    }
    enable() {
      if (!this.ctx) {
        const AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) return false;
        this.ctx = new AC();
        this.rumble = makeNoise(this.ctx, 6, true);
        this.white = makeNoise(this.ctx, 1, false);
      }
      if (this.ctx.state === "suspended") this.ctx.resume();
      this.enabled = true;
      return true;
    }
    disable() {
      this.enabled = false;
      if (this.ctx && this.ctx.state === "running") this.ctx.suspend();
    }
    strike(delaySeconds, distance) {
      if (!this.enabled || !this.ctx || this.ctx.state !== "running") return;
      const ctx = this.ctx;
      const t0 = ctx.currentTime + delaySeconds;
      const dist = Math.max(50, distance);
      const level = Math.min(0.9, 320 / dist);
      const src = ctx.createBufferSource();
      src.buffer = this.rumble;
      const lp = ctx.createBiquadFilter();
      lp.type = "lowpass";
      lp.frequency.setValueAtTime(Math.max(120, 700 - dist * 0.12), t0);
      lp.frequency.exponentialRampToValueAtTime(60, t0 + 4.5);
      const g = ctx.createGain();
      g.gain.setValueAtTime(0.0001, t0);
      g.gain.exponentialRampToValueAtTime(level, t0 + 0.09);
      g.gain.exponentialRampToValueAtTime(0.0001, t0 + 5.0);
      src.connect(lp).connect(g).connect(ctx.destination);
      src.start(t0, Math.random() * 2, 5.2);
      if (dist < 1600) {
        const crack = ctx.createBufferSource();
        crack.buffer = this.white;
        const hp = ctx.createBiquadFilter();
        hp.type = "highpass";
        hp.frequency.value = 1400;
        const cg = ctx.createGain();
        const amp = 0.7 * (1 - dist / 1600);
        cg.gain.setValueAtTime(amp, t0);
        cg.gain.exponentialRampToValueAtTime(0.0001, t0 + 0.22);
        crack.connect(hp).connect(cg).connect(ctx.destination);
        crack.start(t0, 0, 0.3);
      }
    }
  };

  function makeNoise(ctx, seconds, brown) {
    const len = Math.floor(ctx.sampleRate * seconds);
    const buf = ctx.createBuffer(1, len, ctx.sampleRate);
    const d = buf.getChannelData(0);
    let last = 0;
    let peak = 0.0001;
    for (let i = 0; i < len; i++) {
      const w = Math.random() * 2 - 1;
      if (brown) {
        last = (last + 0.02 * w) / 1.02;
        d[i] = last * 3.5;
      } else {
        d[i] = w;
      }
      peak = Math.max(peak, Math.abs(d[i]));
    }
    for (let i = 0; i < len; i++) d[i] = (d[i] / peak) * 0.9;
    return buf;
  }
