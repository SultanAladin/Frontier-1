// Particle system presets and default scene content.
// All lengths are metres, speeds m/s, times seconds. Colours are linear RGBA 0..1.
// 📝 Converted from an IIFE to an ES module; the body keeps its two-space indent so that `git blame`
//    still points at whoever wrote each line rather than at the conversion.
import { PE } from "./pe.js";

  // Every parameter a system can carry. Presets override a subset.
  PE.baseParams = function baseParams() {
    return {
      kind: 0,              // 0 spark, 1 leaf/debris, 3 atoms (LJ), 4 chemicals, 5 VFX
      shape: 0,             // 0 streak, 1 sphere, 2 leaf / paper, 3 soft glow
      leafMode: 0,          // 0 leaf, 1 paper
      blend: "add",         // "add" | "alpha"
      capacity: 2048,
      rate: 0,              // particles per second
      emitShape: 0,         // 0 point, 1 sphere, 2 disc, 3 box
      origin: [0, 0.5, 0],
      radius: 0.1,
      boxHalf: [0, 0, 0],
      dir: [0, 1, 0],
      spread: 0.5,          // radians
      speedMin: 1,
      speedMax: 3,
      drag: 0.2,
      gravity: 1,
      lifeMin: 1,
      lifeMax: 2,
      sizeStart: 0.05,
      sizeEnd: 0.05,
      sizeScale: 1,
      colA: [1, 1, 1, 1],
      colB: [1, 1, 1, 0],
      colC: [1, 1, 0.3, 1],
      windCoupling: 0,      // 1/s, how quickly the particle follows the wind
      bounce: 0,
      buoyancy: 0,          // m/s^2 upward
      flutter: 0,           // lateral sway amplitude (leaves)
      temperature: 0.6,     // molecular: kT in reduced units (m = 1)
      epsilon: 0.15,        // molecular: Lennard-Jones well depth
      sigma: 0.08,          // molecular: particle diameter
      damping: 2.0,         // molecular: Langevin friction 1/s
      reactRate: 4,         // chemicals: A + B -> C per second per contact
      dissociation: 0.3,    // chemicals: C -> A or B per second
      reactRadius: 0.12,    // chemicals: contact radius
      fracA: 0.5,           // chemicals: initial fraction of A
      simDt: 0.004,         // molecular: integration step (s) at time scale 1
      pulseHz: 0,           // swarm glow pulse frequency (Hz); fireflies
      pulseDepth: 0,        // 0 steady .. 1 full pulse
      swarmRadius: 1.2,     // swarm neighbour radius (m), kind 6
      burstEvery: 0,        // timed bursts in seconds (fireworks); 0 = off
      burstCount: 120,      // particles per timed burst
      burstAtOrigin: false, // true: timed bursts fire from the emitter origin, not random sky points
      visible: true,
    };
  };

  const P = (overrides) => Object.assign(PE.baseParams(), overrides);

  const hex = PE.hexLinear;

  // Transitions (kind 5, cube surface): the object's surface is covered by a grid of cubes or coins. They hold in
  // place, then release in a wave from one corner: each either bursts outward or falls. Each cycle reforms the object.
  const TRANSITION_PRESETS = [
    {
      id: "derez-cube", name: "Derez cube", group: "Transitions",
      blurb: "A cube derezzes: its surface cubes release in a wave from one corner, bursting or falling. Each cube that hits the floor breaks once into eight smaller cubes.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.6, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.6, speedMax: 2.6, drag: 0.4, gravity: 1.0, lifeMin: 3, lifeMax: 4,
        sizeStart: 0.075, sizeEnd: 0.075, colA: hex("#35d6ff"), colB: hex("#35d6ff"), colC: hex("#ffffff"),
        windCoupling: 0.3, bounce: 0, burstEvery: 8, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.6, holdSpread: 1.6, burstShare: 0.5, fragment: true, childLife: 1.2 },
      }),
    },
    {
      id: "cube-coins", name: "Cube to coins", group: "Transitions",
      blurb: "A gold cube of coins: the surface coins release once, bursting or falling, and bounce on the floor. Coins never split.",
      p: P({
        kind: 5, shape: 5, blend: "alpha", capacity: 384, rate: 0,
        emitShape: 4, origin: [0, 0.6, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 1.5, speedMax: 4.0, drag: 0.2, gravity: 1.0, lifeMin: 4, lifeMax: 6,
        sizeStart: 0.14, sizeEnd: 0.14, colA: hex("#ffc83d"), colB: hex("#ffc83d"), colC: hex("#fff3b0"),
        windCoupling: 0.2, bounce: 0.35, burstEvery: 6, burstCount: 384, burstAtOrigin: true,
        transition: { holdBase: 0.4, holdSpread: 0.9, burstShare: 0.85, fragment: false, childLife: 0 },
      }),
    },
    {
      id: "melt-away", name: "Melt away", group: "Transitions",
      blurb: "The object melts: surface cubes release in patches across the surface rather than a corner wave, and the pieces drift off on the wind.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.6, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.2, speedMax: 0.9, drag: 0.6, gravity: 0.2, lifeMin: 6, lifeMax: 8,
        sizeStart: 0.075, sizeEnd: 0.05, colA: hex("#ffb347"), colB: hex("#ff5a1f"), colC: hex("#fff1d6"),
        windCoupling: 1.4, bounce: 0, burstEvery: 10, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.2, holdSpread: 3.0, burstShare: 0.15, fragment: false, childLife: 0, wave: "melt", assemble: 0, shell: 0 },
      }),
    },
    {
      id: "reverse-derez", name: "Reverse derez", group: "Transitions",
      blurb: "The object rebuilds: cubes fly in from a shell around it, snap onto the surface in a wave from one corner, then hold.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.6, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.5, speedMax: 2, drag: 0.4, gravity: 0, lifeMin: 3, lifeMax: 4,
        sizeStart: 0.075, sizeEnd: 0.075, colA: hex("#35d6ff"), colB: hex("#35d6ff"), colC: hex("#ffffff"),
        windCoupling: 0, bounce: 0, burstEvery: 9, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.3, holdSpread: 2.0, burstShare: 0, fragment: false, childLife: 0, wave: "corner", assemble: 1, shell: 3.0 },
      }),
    },
    {
      id: "voxel-explosion", name: "Voxel explosion", group: "Transitions",
      blurb: "A solid sphere made of cubes: it holds for a moment, then every cube bursts radially and tumbles on its own axis. No floor breaking.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1024, rate: 0,
        emitShape: 1, origin: [0, 2.2, 0], radius: 0.9,
        speedMin: 2.0, speedMax: 4.5, drag: 0.9, gravity: 0.12, lifeMin: 2.5, lifeMax: 4,
        sizeStart: 0.12, sizeEnd: 0.1, colA: hex("#ff5a3c"), colB: hex("#ffd166"), colC: hex("#ffffff"),
        windCoupling: 0.2, bounce: 0, burstEvery: 6, burstCount: 1024, burstAtOrigin: true,
        transition: { holdBase: 0.4, holdSpread: 0.15, burstShare: 1, fragment: false, childLife: 0, wave: "corner", assemble: 0, shell: 0 },
      }),
    },
    {
      id: "coin-stack", name: "Coin stack", group: "Transitions",
      blurb: "Gold coins drop one by one into a column on the floor, settle with a small bounce and stay stacked. Scripted drops, not physics: coins do not collide.",
      p: P({
        kind: 5, shape: 5, blend: "alpha", capacity: 64, rate: 0,
        emitShape: 0, origin: [0, 0, 0], radius: 0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, lifeMin: 1, lifeMax: 1,
        sizeStart: 0.4, sizeEnd: 0.4, colA: hex("#ffc83d"), colB: hex("#ffc83d"), colC: hex("#fff3b0"),
        windCoupling: 0, bounce: 0, burstEvery: 10, burstCount: 40, burstAtOrigin: true,
        transition: { holdBase: 0.3, holdSpread: 3.0, burstShare: 0, fragment: false, childLife: 0, wave: "corner", assemble: 2, shell: 3.0 },
      }),
    },
    {
      id: "coin-fountain", name: "Coin fountain", group: "VFX",
      blurb: "Gold coins fired upward from one point, arcing over and bouncing onto the floor. They scatter into a loose heap; particles do not collide, so there is no true pile.",
      p: P({
        kind: 0, shape: 5, blend: "alpha", capacity: 400, rate: 0,
        emitShape: 0, origin: [0, 0.1, 0], dir: [0, 1, 0], spread: 0.45,
        speedMin: 5, speedMax: 8, drag: 0.08, gravity: 1.0, lifeMin: 6, lifeMax: 9,
        sizeStart: 0.16, sizeEnd: 0.16, colA: hex("#ffc83d"), colB: hex("#ffb020"), colC: hex("#fff3b0"),
        windCoupling: 0.15, bounce: 0.35, burstEvery: 3.5, burstCount: 120, burstAtOrigin: true,
      }),
    },
    {
      id: "dissolve-nothing", name: "Dissolve to nothing", group: "Transitions",
      blurb: "The object dissolves to nothing: surface pieces release in patches, drift on the wind, shrink and fade out.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.6, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.1, speedMax: 0.6, drag: 0.8, buoyancy: 0.2, gravity: 0.1, lifeMin: 2.5, lifeMax: 4,
        sizeStart: 0.075, sizeEnd: 0, colA: hex("#7fd6ff"), colB: hex("#c8f0ff"), colC: hex("#ffffff"),
        windCoupling: 1.2, bounce: 0, burstEvery: 9, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.2, holdSpread: 3.0, burstShare: 0.1, fragment: false, childLife: 0, wave: "melt", assemble: 0, shell: 0, after: "none" },
      }),
    },
    {
      id: "dissolve-butterflies", name: "Dissolve into butterflies", group: "Transitions",
      blurb: "The object melts in patches, and each released piece becomes a flapping butterfly that rises, drifts on the wind and fades. The butterflies are simple two-pair silhouettes.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.6, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.2, speedMax: 0.9, drag: 0.8, buoyancy: 0.6, gravity: 0, lifeMin: 6, lifeMax: 9,
        sizeStart: 0.075, sizeEnd: 0.075, colA: hex("#ff9a3c"), colB: hex("#3ec8ff"), colC: hex("#ffffff"),
        windCoupling: 0.9, bounce: 0, flutter: 1.2, burstEvery: 12, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.2, holdSpread: 3.0, burstShare: 0.1, fragment: false, childLife: 0, wave: "melt", assemble: 0, shell: 0, after: "butterfly" },
      }),
    },
    {
      id: "black-hole", name: "Black hole", group: "Space",
      blurb: "A black hole: a glowing accretion disc spirals inward and is swallowed, while the background bends around the horizon (screen-space lensing, approximate).",
      p: P({
        kind: 5, shape: 3, blend: "add", capacity: 3000, rate: 400,
        emitShape: 2, radius: 2.6, origin: [0, 2.2, 0], dir: [0, 1, 0], spread: 0.05,
        speedMin: 0, speedMax: 0.2, drag: 0.2, gravity: 0, lifeMin: 6, lifeMax: 10,
        sizeStart: 0.07, sizeEnd: 0.02, colA: hex("#ffd08a"), colB: hex("#ff6a1a"), colC: hex("#ffffff"),
        windCoupling: 0, bounce: 0,
        attractor: { enabled: true, strength: 7, radius: 16, swirl: 3.5, swallow: 0.4 },
        blackHole: { horizon: 0.45, einstein: 3.0 },
      }),
    },
    {
      id: "heat-shimmer", name: "Heat shimmer", group: "VFX",
      blurb: "Hot-air shimmer: a rising, almost invisible plume that bends the scene behind it. Place it over a fire or an exhaust. The bending is a screen-space approximation.",
      p: P({
        kind: 0, shape: 3, blend: "alpha", capacity: 200, rate: 30, emitShape: 1, radius: 0.5,
        origin: [0, 0.4, 0], dir: [0, 1, 0], spread: 0.3, speedMin: 0.6, speedMax: 1.4, drag: 0.5, gravity: 0,
        buoyancy: 1.5, flutter: 0.4, windCoupling: 0.6, lifeMin: 2, lifeMax: 3.5, sizeStart: 0.3, sizeEnd: 0.9,
        colA: hex("#2a323b"), colB: hex("#3d4954"), colC: hex("#ffffff"), bounce: 0,
        heatShimmer: { radius: 1.6, strength: 0.03, freq: 14, rate: 1.5 },
      }),
    },
    {
      id: "magnetic-lines", name: "Magnetic field lines", group: "Fields",
      blurb: "Light fibres drawn along the field lines of a magnetic dipole (axis in the plane of the lines). Sparks ride the lines. Pair it with Magnetic particles at the same origin.",
      p: P({
        kind: 7, capacity: 64, origin: [0, 1.2, 0],
        fibre: PE.fibreParams({
          shape: "trail", strands: 60, segments: 96, seed: 31, loopSeconds: 8, scale: 1,
          pathShape: "Dipole field line", pathSize: 3, trailLength: 0.25, phaseSpread: 6.2832,
          spread: 0.05, amplitude: 0.04, frequency: 2, thickness: 1.0, taper: 0.5, intensity: 1.2,
          halo: 0.2, baseline: 0.02,
          stops: PE.evenStops(["#ff6a3d", "#ffe0a6"]), colC: hex("#ffffff"), accentMix: 0.2,
          window: 0.5, windowCycles: 1, sparks: 0.4, sparkSize: 0.02, sparkBrightness: 3,
          pulseRate: 1.5, pulseDepth: 0.5, pulseShape: "Breathe",
        }),
      }),
    },
    {
      id: "magnetic-particles", name: "Magnetic particles", group: "Fields",
      blurb: "Glowing particles steered by a magnetic dipole: they follow the local field direction, so they circulate along the field lines. Pair it with Magnetic field lines at the same origin.",
      p: P({
        kind: 0, shape: 3, blend: "add", capacity: 1200, rate: 220, emitShape: 1, radius: 2.4,
        origin: [0, 1.2, 0], dir: [0, 1, 0], spread: 0.1, speedMin: 0, speedMax: 0.4, drag: 0.1, gravity: 0,
        lifeMin: 6, lifeMax: 10, sizeStart: 0.07, sizeEnd: 0.05,
        colA: hex("#ff8a4c"), colB: hex("#ffd7a0"), colC: hex("#ffffff"), windCoupling: 0, bounce: 0,
        magneticField: { strength: 3.0, guide: 4.0, radius: 6 },
      }),
    },
    {
      id: "bubbles", name: "Bubbles", group: "Water",
      blurb: "Soap bubbles: transparent spheres with a fresnel rim and a small highlight. They rise on buoyancy, sway, grow slightly and drift on the wind.",
      p: P({
        kind: 0, shape: 6, blend: "alpha", capacity: 400, rate: 25, emitShape: 2, radius: 0.8,
        origin: [0, 0.2, 0], dir: [0, 1, 0], spread: 0.25, speedMin: 0.2, speedMax: 0.6, drag: 0.15, gravity: 0,
        buoyancy: 1.4, flutter: 0.6, windCoupling: 0.8, lifeMin: 5, lifeMax: 8, sizeStart: 0.12, sizeEnd: 0.2,
        colA: hex("#bfe9ff"), colB: hex("#e8f8ff"), colC: hex("#ffffff"), bounce: 0,
      }),
    },
    {
      id: "foam", name: "Foam", group: "Water",
      blurb: "Dense, small bubbles that bunch up and bob near the floor, pushed by the wind. Good for surf or soapy water.",
      p: P({
        kind: 0, shape: 6, blend: "alpha", capacity: 1500, rate: 500, emitShape: 2, radius: 1.8,
        origin: [0, 0.1, 0], dir: [0, 1, 0], spread: 0.6, speedMin: 0.2, speedMax: 1.0, drag: 1.5, gravity: 1.0,
        buoyancy: 0.2, flutter: 0.3, windCoupling: 1.2, bounce: 0.2, lifeMin: 4, lifeMax: 7,
        sizeStart: 0.05, sizeEnd: 0.06, colA: hex("#f4fbff"), colB: hex("#d6eefc"), colC: hex("#ffffff"),
      }),
    },
    {
      id: "glitch-dissolve", name: "Glitch dissolve", group: "Transitions",
      blurb: "Digital dissolve: the object breaks into cubes in blocky, noise-driven steps, and each released cube jumps in short stutters as it drifts away.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.6, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.1, speedMax: 0.6, drag: 0.8, buoyancy: 0.2, gravity: 0.1, lifeMin: 2.5, lifeMax: 4,
        sizeStart: 0.075, sizeEnd: 0, colA: hex("#00ffd0"), colB: hex("#ff2d6f"), colC: hex("#ffffff"),
        windCoupling: 1.0, bounce: 0, burstEvery: 9, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.2, holdSpread: 2.5, burstShare: 0.2, fragment: false, childLife: 0, wave: "glitch", assemble: 0, shell: 0, after: "none" },
      }),
    },
  ];

  // Light fibres (kind 7): analytic streaks, ribbons and path trails, ported from the Strand Editor. Not simulated.
  const FIBRE_PRESETS = [
    {
      id: "light-streaks", name: "Light streaks", group: "Light fibres",
      blurb: "Bezier light streaks: a head runs along each fibre and leaves a fading tail, with head sparks.",
      p: P({
        kind: 7, capacity: 64, origin: [-2.2, 1.6, 0], dir: [1, 0.42, 0],
        fibre: PE.fibreParams({
          shape: "streak", strands: 200, segments: 72, seed: 11, loopSeconds: 12, scale: 0.4,
          length: 12, spread: 1.2, amplitude: 2.4, frequency: 1, thickness: 1.5, taper: 0.6,
          intensity: 1.15, halo: 0.12, baseline: 0.1,
          stops: PE.evenStops(["#18b4ff", "#5fd9ff"]), colC: hex("#eaffff"), accentMix: 0.25,
          window: 0.55, windowCycles: 1, sparks: 0.8, sparkSize: 0.02, sparkBrightness: 3,
          pulseRate: 1, pulseDepth: 0.25, pulseShape: "Breathe",
        }),
      }),
    },
    {
      id: "ember-ribbons", name: "Ember ribbons", group: "Light fibres",
      blurb: "Rippling sheets of ember light; the pulse runs outward across the sheet.",
      p: P({
        kind: 7, capacity: 64, origin: [0, 1.8, 0],
        fibre: PE.fibreParams({
          shape: "ribbon", strands: 140, segments: 120, seed: 41, loopSeconds: 10, scale: 1,
          length: 4.2, sheetWidth: 3, ripple: 1.6, waves: 0.9, phaseSpread: 1.2,
          thickness: 1.8, taper: 0.35, intensity: 1.4, halo: 0.35, baseline: 0.25,
          stops: PE.evenStops(["#ff8a4c", "#ffb37a"]), colC: hex("#fff1e6"), accentMix: 0.1,
          window: 0.8, windowCycles: 1, sparks: 0.2, sparkSize: 0.02, sparkBrightness: 3,
          pulseRate: 1, pulseDepth: 0.6, pulseShape: "Ripple",
        }),
      }),
    },
    {
      id: "weave-trails", name: "Weave trails", group: "Light fibres",
      blurb: "Light trails comet along a weaving path, each leaving a tail that pulses outward along it.",
      p: P({
        kind: 7, capacity: 64, origin: [0, 1.2, 0],
        fibre: PE.fibreParams({
          shape: "trail", strands: 150, segments: 96, seed: 81, loopSeconds: 8, scale: 1,
          pathShape: "Weave", pathSize: 5, trailLength: 0.1, phaseSpread: 6.283,
          spread: 0.12, amplitude: 0.08, frequency: 3, thickness: 1.2, taper: 0.5, intensity: 1.3,
          halo: 0.25, baseline: 0.03,
          stops: PE.evenStops(["#18b4ff", "#e6fbff"]), colC: hex("#ffffff"), accentMix: 0.2,
          window: 0.6, windowCycles: 1, sparks: 0.5, sparkSize: 0.02, sparkBrightness: 3,
          pulseRate: 2, pulseDepth: 0.7, pulseShape: "Ripple",
        }),
      }),
    },
    {
      id: "trim-trail", name: "Trim trail (stadium)", group: "Light fibres",
      blurb: "Light trails running round a flat stadium loop, the way a light guide follows dashboard trim.",
      p: P({
        kind: 7, capacity: 64, origin: [0, 1.2, 0],
        fibre: PE.fibreParams({
          shape: "trail", strands: 110, segments: 96, seed: 91, loopSeconds: 10, scale: 1,
          pathShape: "Stadium", pathSize: 4, trailLength: 0.35, phaseSpread: 6.2832,
          spread: 0.1, amplitude: 0.15, frequency: 2, thickness: 1.4, taper: 0.5, intensity: 1.2,
          halo: 0.2, baseline: 0.05,
          stops: PE.evenStops(["#18b4ff", "#e6fbff"]), colC: hex("#ffffff"), accentMix: 0.15,
          window: 0.5, windowCycles: 1, sparks: 0.3, sparkSize: 0.015, sparkBrightness: 2.5,
          pulseRate: 2, pulseDepth: 0.5, pulseShape: "Ripple",
        }),
      }),
    },
  ];

  PE.Presets = [
    {
      id: "sparks", name: "Sparks", group: "Effects",
      blurb: "Hot metal sparks: gravity, drag, ground bounce and wind coupling.",
      p: P({
        kind: 0, shape: 0, blend: "add", capacity: 3000, rate: 160,
        emitShape: 0, origin: [-3, 0.4, 0], dir: [0, 1, 0], spread: 0.6,
        speedMin: 2, speedMax: 6, drag: 0.5, gravity: 1, lifeMin: 0.6, lifeMax: 1.6,
        sizeStart: 0.035, sizeEnd: 0.01,
        colA: [1, 0.92, 0.55, 1], colB: [1, 0.35, 0.05, 0],
        windCoupling: 0.25, bounce: 0.35,
      }),
    },
    {
      id: "strikeSparks", name: "Strike sparks", group: "Effects",
      blurb: "Burst of sparks at each lightning strike point (fires on every strike).",
      p: P({
        kind: 0, shape: 0, blend: "add", capacity: 2000, rate: 0,
        emitShape: 0, origin: [0, 0, 0], dir: [0, 1, 0], spread: 1.5,
        speedMin: 1.5, speedMax: 7, drag: 0.3, gravity: 1.2, lifeMin: 0.4, lifeMax: 1.1,
        sizeStart: 0.03, sizeEnd: 0.008,
        colA: [0.85, 0.92, 1, 1], colB: [0.4, 0.6, 1, 0],
        windCoupling: 0.2, bounce: 0.4,
      }),
    },
    {
      id: "rain", name: "Rain streaks", group: "Weather",
      blurb: "Rain falling through the wind field. Drops bounce off the floor with a small splash rebound, then fade.",
      p: P({
        kind: 0, shape: 0, blend: "alpha", capacity: 5000, rate: 500,
        emitShape: 3, boxHalf: [5, 0, 5], origin: [0, 7.2, 0], dir: [0, -1, 0], spread: 0.03,
        speedMin: 7, speedMax: 9, drag: 0, gravity: 0.6, lifeMin: 1.2, lifeMax: 1.8,
        sizeStart: 0.06, sizeEnd: 0.05,
        colA: [0.75, 0.85, 1, 0.85], colB: [0.75, 0.85, 1, 0.25],
        windCoupling: 0.7, bounce: 0.25,
      }),
    },
    {
      id: "hail", name: "Hail", group: "Weather",
      blurb: "Hard ice pellets that drop fast and bounce high off the floor before settling.",
      p: P({
        kind: 0, shape: 1, blend: "alpha", capacity: 1500, rate: 70,
        emitShape: 3, boxHalf: [5, 0, 5], origin: [0, 7.2, 0], dir: [0, -1, 0], spread: 0.05,
        speedMin: 6, speedMax: 8, drag: 0.05, gravity: 1.0, lifeMin: 3, lifeMax: 4.5,
        sizeStart: 0.09, sizeEnd: 0.09,
        colA: [0.9, 0.95, 1, 1], colB: [0.8, 0.86, 0.95, 0.2],
        windCoupling: 0.8, bounce: 0.5,
      }),
    },
    {
      id: "leaves", name: "Leaves", group: "Wind-blown",
      blurb: "Leaves released from a canopy; they flutter, tumble and follow the wind field.",
      p: P({
        kind: 1, shape: 2, leafMode: 0, blend: "alpha", capacity: 1200, rate: 20,
        emitShape: 2, origin: [0, 4.6, 0], radius: 4.5, dir: [0, -1, 0], spread: 0.5,
        speedMin: 0.2, speedMax: 0.9, drag: 1.4, gravity: 0.35, lifeMin: 7, lifeMax: 12,
        sizeStart: 0.14, sizeEnd: 0.12,
        colA: [0.42, 0.72, 0.22, 1], colB: [0.8, 0.52, 0.16, 1],
        windCoupling: 2.2, flutter: 1.2, bounce: 0,
      }),
    },
    {
      id: "paper", name: "Paper & debris", group: "Wind-blown",
      blurb: "Light paper sheets: high wind coupling and strong sway.",
      p: P({
        kind: 1, shape: 2, leafMode: 1, blend: "alpha", capacity: 600, rate: 8,
        emitShape: 1, origin: [-2, 3, 2], radius: 2.5, dir: [0, -1, 0], spread: 0.4,
        speedMin: 0.1, speedMax: 0.5, drag: 0.6, gravity: 0.25, lifeMin: 6, lifeMax: 10,
        sizeStart: 0.12, sizeEnd: 0.1,
        colA: [0.95, 0.95, 0.9, 1], colB: [0.8, 0.82, 0.85, 1],
        windCoupling: 3.0, flutter: 2.0,
      }),
    },
    {
      id: "atoms", name: "Atoms (LJ gas)", group: "Physics",
      blurb: "Lennard-Jones gas on a spatial hash grid with a Langevin thermostat. Colour = speed.",
      p: P({
        kind: 3, shape: 1, blend: "alpha", capacity: 1024, emitShape: 3,
        origin: [3.2, 1.0, -2.6], boxHalf: [0.9, 0.9, 0.9],
        sigma: 0.08, epsilon: 0.15, temperature: 0.6, damping: 2.0, simDt: 0.004,
        colA: [0.35, 0.65, 1, 1], colB: [1, 0.35, 0.25, 1],
      }),
    },
    {
      id: "chemicals", name: "Chemicals A+B→C", group: "Physics",
      blurb: "Reactive particles: A + B react on contact into C, which dissociates back. Read counts back to the CPU.",
      p: P({
        kind: 4, shape: 1, blend: "alpha", capacity: 1024, emitShape: 3,
        origin: [3.2, 1.0, 2.6], boxHalf: [0.9, 0.9, 0.9],
        sigma: 0.08, temperature: 0.6, damping: 2.0, simDt: 0.004,
        reactRate: 6, dissociation: 0.3, reactRadius: 0.12, fracA: 0.5,
        colA: [1, 0.38, 0.3, 1], colB: [0.3, 0.62, 1, 1], colC: [1, 0.86, 0.3, 1],
      }),
    },
    {
      id: "fire", name: "Fire", group: "VFX",
      blurb: "Buoyant additive flame with wind-bent tongues.",
      p: P({
        kind: 5, shape: 3, blend: "add", capacity: 2500, rate: 280,
        emitShape: 1, radius: 0.12, origin: [-4.2, 0.05, -4], dir: [0, 1, 0], spread: 0.35,
        speedMin: 0.6, speedMax: 1.7, drag: 0.8, buoyancy: 2.2, gravity: 0,
        lifeMin: 0.7, lifeMax: 1.5, sizeStart: 0.35, sizeEnd: 0.12,
        colA: [1, 0.85, 0.35, 1], colB: [0.95, 0.2, 0.05, 0], windCoupling: 1.2,
      }),
    },
    {
      id: "smoke", name: "Smoke", group: "VFX",
      blurb: "Alpha-blended soft smoke that expands and drifts with the wind.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 1500, rate: 70,
        emitShape: 1, radius: 0.2, origin: [-2.6, 0.2, -4], dir: [0, 1, 0], spread: 0.3,
        speedMin: 0.3, speedMax: 0.8, drag: 0.5, buoyancy: 1.0, gravity: 0,
        lifeMin: 3, lifeMax: 6, sizeStart: 0.2, sizeEnd: 1.3,
        colA: [0.6, 0.6, 0.64, 0.5], colB: [0.25, 0.25, 0.28, 0], windCoupling: 1.5,
      }),
    },
    {
      id: "explosion", name: "Explosion", group: "VFX",
      blurb: "Spherical additive burst. Use the Burst button to fire it.",
      p: P({
        kind: 5, shape: 3, blend: "add", capacity: 2000, rate: 0,
        emitShape: 1, radius: 0.1, origin: [0, 1.2, -4], dir: [0, 1, 0], spread: 3.14159,
        speedMin: 3, speedMax: 9, drag: 2.6, buoyancy: 0.3, gravity: 0.4,
        lifeMin: 0.5, lifeMax: 1.2, sizeStart: 0.15, sizeEnd: 0.6,
        colA: [1, 0.95, 0.7, 1], colB: [1, 0.35, 0.08, 0], windCoupling: 0.6,
      }),
    },
    {
      id: "tornado", name: "Tornado debris", group: "Storms",
      blurb: "Debris lifted by a tornado. Adding it enables the Tornado wind vortex at this spot; the swirl is the grid itself.",
      windLink: { type: 2, name: "Tornado", local: true, set: { radius: 2.4, strength: 9 } },
      p: P({
        kind: 1, shape: 2, leafMode: 1, blend: "alpha", capacity: 1500, rate: 90,
        emitShape: 2, origin: [0, 0.05, -2], radius: 1.6, dir: [0, 1, 0], spread: 0.6,
        speedMin: 1, speedMax: 3, drag: 0.4, gravity: 0.3, lifeMin: 4, lifeMax: 7,
        sizeStart: 0.14, sizeEnd: 0.1, colA: [0.36, 0.31, 0.26, 1], colB: [0.52, 0.46, 0.38, 0],
        windCoupling: 3.5, flutter: 1.2, bounce: 0,
      }),
    },
    {
      id: "sandstorm", name: "Sandstorm", group: "Storms",
      blurb: "Dust driven across the scene. Adding it sets Prevailing wind to 8 m/s at bearing 70°.",
      windLink: { type: 0, name: "Prevailing wind", set: { strength: 8, bearing: 70 } },
      p: P({
        kind: 1, shape: 3, blend: "alpha", capacity: 6000, rate: 900,
        emitShape: 3, origin: [-5.6, 1.1, 0], boxHalf: [0.3, 0.9, 5.2], dir: [0.94, 0, 0.342], spread: 0.25,
        speedMin: 5, speedMax: 8, drag: 1.0, gravity: 0.2, lifeMin: 4, lifeMax: 7,
        sizeStart: 0.07, sizeEnd: 0.05, colA: [0.82, 0.66, 0.42, 0.55], colB: [0.7, 0.55, 0.34, 0],
        windCoupling: 4, flutter: 0.6, bounce: 0,
      }),
    },
    {
      id: "snow", name: "Snow", group: "Weather",
      blurb: "Slow snowfall that drifts and flutters through the wind field, with a soft rebound at the floor.",
      p: P({
        kind: 0, shape: 3, blend: "alpha", capacity: 3000, rate: 160,
        emitShape: 3, boxHalf: [5, 0, 5], origin: [0, 7.2, 0], dir: [0, -1, 0], spread: 0.2,
        speedMin: 0.4, speedMax: 0.9, drag: 1.0, gravity: 0.3, lifeMin: 9, lifeMax: 13,
        sizeStart: 0.07, sizeEnd: 0.06, colA: [0.95, 0.97, 1, 0.85], colB: [0.95, 0.97, 1, 0],
        windCoupling: 1.2, flutter: 0.9, bounce: 0.12,
      }),
    },
    {
      id: "embers", name: "Embers", group: "VFX",
      blurb: "Glowing embers lifted on hot air; additive, short-lived, bent by the wind.",
      p: P({
        kind: 5, shape: 3, blend: "add", capacity: 900, rate: 45,
        emitShape: 1, radius: 0.5, origin: [-4.0, 0.3, -4.0], dir: [0, 1, 0], spread: 0.5,
        speedMin: 0.8, speedMax: 2.2, drag: 0.7, buoyancy: 1.8, gravity: 0, lifeMin: 2, lifeMax: 4.2,
        sizeStart: 0.035, sizeEnd: 0.012, colA: [1, 0.72, 0.3, 1], colB: [0.9, 0.25, 0.05, 0],
        windCoupling: 1.5,
      }),
    },
    {
      id: "petals", name: "Cherry petals", group: "Wind-blown",
      blurb: "Petals drifting off a canopy; they tumble and follow the wind closely.",
      p: P({
        kind: 1, shape: 2, leafMode: 0, blend: "alpha", capacity: 700, rate: 14,
        emitShape: 2, origin: [2.5, 4.4, 2.5], radius: 2.2, dir: [0, -1, 0], spread: 0.5,
        speedMin: 0.2, speedMax: 0.7, drag: 1.3, gravity: 0.3, lifeMin: 7, lifeMax: 11,
        sizeStart: 0.1, sizeEnd: 0.09, colA: [1, 0.74, 0.82, 1], colB: [0.96, 0.58, 0.72, 1],
        windCoupling: 2.4, flutter: 1.6,
      }),
    },
    {
      id: "sand", name: "Sand grains", group: "Ground",
      blurb: "Sand kicked up from the ground in short hops (saltation) and carried by the wind.",
      p: P({
        kind: 0, shape: 1, blend: "alpha", capacity: 4000, rate: 350,
        emitShape: 3, boxHalf: [5, 0, 5], origin: [0, 0.08, 0], dir: [0, 1, 0], spread: 0.35,
        speedMin: 0.8, speedMax: 2.2, drag: 0.1, gravity: 1, lifeMin: 2.5, lifeMax: 4,
        sizeStart: 0.025, sizeEnd: 0.022, colA: [0.86, 0.74, 0.52, 1], colB: [0.7, 0.58, 0.4, 0],
        windCoupling: 2.5, bounce: 0.2,
      }),
    },
    {
      id: "ash", name: "Ash flakes", group: "VFX",
      blurb: "Grey ash lifted by a fire's heat, drifting and settling onto the ground.",
      p: P({
        kind: 1, shape: 3, blend: "alpha", capacity: 1500, rate: 30,
        emitShape: 1, radius: 0.6, origin: [-4.2, 1.2, -4], dir: [0, 1, 0], spread: 0.4,
        speedMin: 0.3, speedMax: 1.0, drag: 1.2, buoyancy: 0.9, gravity: 0.12, lifeMin: 6, lifeMax: 10,
        sizeStart: 0.05, sizeEnd: 0.08, colA: [0.55, 0.55, 0.55, 0.7], colB: [0.35, 0.35, 0.36, 0],
        windCoupling: 1.6, flutter: 0.7,
      }),
    },
    {
      id: "debris", name: "Small debris", group: "Ground",
      blurb: "Twigs, bark and wood chips thrown up and clattering off the ground. Small pieces only, no stones.",
      p: P({
        kind: 0, shape: 0, blend: "alpha", capacity: 800, rate: 30,
        emitShape: 1, radius: 0.6, origin: [-2, 0.25, 4], dir: [0, 1, 0], spread: 0.9,
        speedMin: 2, speedMax: 5, drag: 0.3, gravity: 1, lifeMin: 3, lifeMax: 5,
        sizeStart: 0.035, sizeEnd: 0.03, colA: [0.46, 0.32, 0.18, 1], colB: [0.3, 0.22, 0.14, 0],
        windCoupling: 1.8, bounce: 0.35,
      }),
    },
    {
      id: "flares", name: "Countermeasure flares", group: "Aircraft",
      blurb: "IR decoy flares dispensed by a jet to break missile lock: a white-hot burst that falls behind the aircraft and burns out.",
      p: P({
        kind: 5, shape: 3, blend: "add", capacity: 600, rate: 0,
        emitShape: 1, radius: 0.3, origin: [4.5, 4.5, -4], dir: [-1, -0.25, 0], spread: 0.35,
        speedMin: 4, speedMax: 7, drag: 1.4, buoyancy: 0, gravity: 1.0, lifeMin: 4, lifeMax: 7,
        sizeStart: 0.45, sizeEnd: 0.12, colA: [1, 0.98, 0.85, 1], colB: [1, 0.42, 0.1, 0],
        windCoupling: 0.5, burstEvery: 0.9, burstCount: 14, burstAtOrigin: true,
      }),
    },
    {
      id: "pollen", name: "Pollen & dust motes", group: "Ambient",
      blurb: "Slow glowing motes that drift on the wind through the air. Light shafts are not modelled.",
      p: P({
        kind: 1, shape: 3, blend: "alpha", capacity: 2000, rate: 90,
        emitShape: 3, boxHalf: [5, 1.5, 5], origin: [0, 2.5, 0], dir: [0, 1, 0], spread: 0.5,
        speedMin: 0.05, speedMax: 0.2, drag: 0.6, gravity: 0.02, lifeMin: 8, lifeMax: 12,
        sizeStart: 0.03, sizeEnd: 0.03, colA: [1, 0.95, 0.6, 0.6], colB: [1, 0.9, 0.5, 0],
        windCoupling: 1.6, flutter: 0.6,
      }),
    },
    {
      id: "fireflies", name: "Fireflies", group: "Ambient",
      blurb: "Glowing points that wander through the air and pulse. Swarm behaviour (kind 6), best at night.",
      p: P({
        kind: 6, shape: 3, blend: "add", capacity: 256, rate: 12,
        emitShape: 3, boxHalf: [4, 1.2, 4], origin: [0, 1.4, 0], dir: [0, 1, 0], spread: 0.5,
        speedMin: 0.2, speedMax: 0.9, drag: 0.4, gravity: 0, lifeMin: 10, lifeMax: 16,
        sizeStart: 0.06, sizeEnd: 0.05, colA: [0.7, 1, 0.3, 1], colB: [0.7, 1, 0.3, 0.2],
        windCoupling: 0.3, flutter: 0.6, swarmRadius: 2.0, pulseHz: 0.8, pulseDepth: 0.85,
      }),
    },
    {
      id: "insects", name: "Insect swarm", group: "Ambient",
      blurb: "Gnat-like swarm that flocks: separation, alignment and cohesion, computed on the GPU.",
      p: P({
        kind: 6, shape: 1, blend: "alpha", capacity: 512, rate: 40,
        emitShape: 3, boxHalf: [2.5, 1.2, 2.5], origin: [-2, 1.8, 2], dir: [0, 1, 0], spread: 0.5,
        speedMin: 0.5, speedMax: 2.2, drag: 0.3, gravity: 0, lifeMin: 15, lifeMax: 25,
        sizeStart: 0.04, sizeEnd: 0.04, colA: [0.62, 0.57, 0.47, 1], colB: [0.45, 0.42, 0.36, 0.6],
        windCoupling: 0.5, flutter: 1.2, swarmRadius: 1.6
      }),
    },
    {
      id: "watermist", name: "Water mist", group: "Water",
      blurb: "Fine droplets blown off a waterfall or spray: short life and high drag.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 2500, rate: 200,
        emitShape: 2, radius: 0.5, origin: [5, 3, 0], dir: [-1, 0, 0], spread: 0.9,
        speedMin: 0.4, speedMax: 1.2, drag: 2.5, gravity: 0.15, lifeMin: 1.2, lifeMax: 2.2,
        sizeStart: 0.06, sizeEnd: 0.5, colA: [0.85, 0.92, 1, 0.35], colB: [0.9, 0.95, 1, 0],
        windCoupling: 1.0,
      }),
    },
    {
      id: "splash", name: "Splash rings", group: "Water",
      blurb: "Expanding ripple rings on the floor. Placed at random ground points, not at each impact.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 600, rate: 40,
        emitShape: 3, boxHalf: [5, 0, 5], origin: [0, 0.02, 0], dir: [0, 1, 0], spread: 0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, lifeMin: 0.45, lifeMax: 0.7,
        sizeStart: 0.08, sizeEnd: 0.9, colA: [0.8, 0.9, 1, 0.5], colB: [0.8, 0.9, 1, 0],
      }),
    },
    {
      id: "glass", name: "Glass shards", group: "Effects",
      blurb: "Sharp bright fragments blown from a broken window. They bounce and glint, unlike debris.",
      p: P({
        kind: 0, shape: 0, blend: "add", capacity: 800, rate: 30,
        emitShape: 1, radius: 0.4, origin: [3, 2.5, -3], dir: [0, 0, 1], spread: 1.0,
        speedMin: 2, speedMax: 5, drag: 0.15, gravity: 1, lifeMin: 2, lifeMax: 3.5,
        sizeStart: 0.04, sizeEnd: 0.02, colA: [0.8, 0.95, 1, 1], colB: [0.6, 0.8, 1, 0],
        windCoupling: 0.4, bounce: 0.3,
      }),
    },
    {
      id: "lapilli", name: "Volcanic ash & lapilli", group: "VFX",
      blurb: "Hot, heavy ejecta thrown high by a vent: faster and bigger than ash flakes.",
      p: P({
        kind: 0, shape: 1, blend: "alpha", capacity: 500, rate: 25,
        emitShape: 1, radius: 0.4, origin: [-4.2, 0.3, -4], dir: [0, 1, 0], spread: 0.5,
        speedMin: 6, speedMax: 12, drag: 0.1, gravity: 1, lifeMin: 3, lifeMax: 5,
        sizeStart: 0.08, sizeEnd: 0.06, colA: [1, 0.45, 0.15, 1], colB: [0.25, 0.22, 0.2, 0],
        windCoupling: 0.8, bounce: 0.3,
      }),
    },
    {
      id: "seeds", name: "Dandelion seeds", group: "Wind-blown",
      blurb: "Very light seeds that float and flutter; the wind carries them.",
      p: P({
        kind: 1, shape: 3, blend: "alpha", capacity: 400, rate: 15,
        emitShape: 1, radius: 0.6, origin: [-3, 2.2, 3], dir: [0, 1, 0], spread: 0.6,
        speedMin: 0.1, speedMax: 0.4, drag: 1.4, gravity: 0.08, lifeMin: 8, lifeMax: 12,
        sizeStart: 0.04, sizeEnd: 0.035, colA: [1, 1, 0.97, 0.8], colB: [1, 1, 1, 0],
        windCoupling: 3.0, flutter: 1.4,
      }),
    },
    {
      id: "feathers", name: "Feathers", group: "Wind-blown",
      blurb: "Light feathers that drift, spin and sway in the wind.",
      p: P({
        kind: 1, shape: 2, leafMode: 0, blend: "alpha", capacity: 300, rate: 6,
        emitShape: 1, radius: 0.6, origin: [-3, 3.2, -3], dir: [0, 1, 0], spread: 0.5,
        speedMin: 0.1, speedMax: 0.3, drag: 1.2, gravity: 0.2, lifeMin: 8, lifeMax: 12,
        sizeStart: 0.13, sizeEnd: 0.12, colA: [0.97, 0.97, 0.94, 1], colB: [0.75, 0.75, 0.78, 1],
        windCoupling: 2.6, flutter: 1.8,
      }),
    },
    {
      id: "confetti", name: "Confetti & streamers", group: "Wind-blown",
      blurb: "Paper pieces that tumble and flutter; strong wind coupling.",
      p: P({
        kind: 1, shape: 2, leafMode: 1, blend: "alpha", capacity: 1200, rate: 25,
        emitShape: 1, radius: 0.5, origin: [0, 0.3, -1], dir: [0, 1, 0], spread: 0.8,
        speedMin: 1, speedMax: 3, drag: 0.8, gravity: 0.25, lifeMin: 5, lifeMax: 8,
        sizeStart: 0.09, sizeEnd: 0.08, colA: [1, 0.82, 0.3, 1], colB: [1, 0.4, 0.6, 1],
        windCoupling: 3.5, flutter: 2.2,
      }),
    },
    {
      id: "steam", name: "Steam vent", group: "VFX",
      blurb: "Hot steam rising from a vent and expanding as it cools.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 1600, rate: 60,
        emitShape: 2, radius: 0.35, origin: [4, 0.05, 4], dir: [0, 1, 0], spread: 0.12,
        speedMin: 0.8, speedMax: 1.6, drag: 1.5, buoyancy: 1.6, gravity: 0, lifeMin: 3, lifeMax: 5,
        sizeStart: 0.15, sizeEnd: 1.6, colA: [0.92, 0.94, 0.96, 0.45], colB: [0.8, 0.82, 0.85, 0],
        windCoupling: 1.2,
      }),
    },
    {
      id: "fireworks", name: "Fireworks", group: "VFX",
      blurb: "Timed shells: each burst fires from a random point in the sky, with streaked trails.",
      p: P({
        kind: 5, shape: 0, blend: "add", capacity: 3000, rate: 0,
        emitShape: 1, radius: 0.1, origin: [0, 6, 0], dir: [0, 1, 0], spread: 3.14159,
        speedMin: 4, speedMax: 9, drag: 1.8, gravity: 0.8, lifeMin: 1.2, lifeMax: 2.4,
        sizeStart: 0.06, sizeEnd: 0.02, colA: [1, 0.9, 0.6, 1], colB: [1, 0.35, 0.1, 0],
        windCoupling: 0.4, burstEvery: 2.6, burstCount: 180,
      }),
    },
  ].concat(FIBRE_PRESETS, TRANSITION_PRESETS);

  PE.presetById = (id) => PE.Presets.find((preset) => preset.id === id);

  // Wind field components. Type codes match the WGSL wind kernel:
  // 0 directional, 1 gust bands, 2 tornado, 3 radial (negative strength = suction).
  PE.WindTypes = ["Directional", "Gust bands", "Tornado", "Radial"];
  PE.defaultWind = function defaultWind() {
    return {
      windScale: 1,
      turbulence: 0.35,
      arrowRef: 6,
      showArrows: true,
      arrowStride: 3,
      showFloor: true,
      showDomain: true,
      // 🔴 KEPT ON PURPOSE, AND NO LONGER READ BY THE EDITOR. These four are what the wind panel shipped
      //    with before wind and force fields became one list. PE.Forces.DefaultForces() has to reproduce
      //    them exactly, and CheckForceFields.mjs asserts that against THIS array rather than against a
      //    description of it. Delete it and the migration loses the only witness that it changed nothing.
      components: [
        { name: "Prevailing wind", type: 0, enabled: true, x: 0, z: 0, radius: 6, strength: 3, bearing: 70, freq: 0.3 },
        { name: "Passing gust", type: 1, enabled: true, x: -4, z: 0, radius: 4, strength: 5, bearing: 70, freq: 0.3 },
        { name: "Tornado", type: 2, enabled: false, x: -2, z: -4, radius: 1.6, strength: 6, bearing: 0, freq: 0 },
        { name: "Radial suction", type: 3, enabled: false, x: 0, z: -2, radius: 3, strength: -2, bearing: 0, freq: 0 },
      ],
      swirl: 1.2,
    };
  };

  // Default scene: a few systems visible at once; the rest are one click away in "Add".
  PE.defaultScene = ["sparks", "leaves", "atoms", "chemicals"];
