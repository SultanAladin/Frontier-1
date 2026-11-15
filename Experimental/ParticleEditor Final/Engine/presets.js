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
      colC: [1, 1, 0.3, 0],
      colorStops: null,     // optional arbitrary-length array of RGBA stops or { color:[r,g,b,a], pos:0..1 }
      colorBy: "modeDefault", // modeDefault | lifetime | age | speed | velY | angle | velAngle | distOrigin | height | noise3D | random | spinAngle | distCamera | particleSize | lifeDuration
      colorMin: 0,
      colorMax: 1,
      colorRepeat: 1,
      colorWrap: "clamp",   // "clamp" | "repeat" | "mirror"
      colorPhase: 0,
      colorInterp: "linear", // "linear" | "smooth" | "stepped"
      windCoupling: 0,      // 1/s, how quickly the particle follows the wind
      bounce: 0,
      buoyancy: 0,          // m/s^2 upward
      flutter: 0,           // lateral sway amplitude (leaves)
      curlMode: 0,          // 0 standard particle, 1 planar 3D curl wave field, 2 spherical tangent curl plasma
      curlFreq: 0.85,       // spatial frequency of 3D curl noise (1/m)
      curlAmp: 2.2,         // curl flow drive speed (m/s)
      curlRate: 0.24,       // temporal evolution rate of curl field
      shellRadius: 1.75,    // target manifold radius for curlMode 2 (m)
      streakThin: 1.0,      // cross-section thinness multiplier for Streak shape (<1 = ultra-fine silk hairs)
      webMode: 0,           // 0 off, 1 real-time spinning Orb-Weave spiderweb, 2 tangled Messy Cobwebs
      webSpokes: 18,        // radial spokes (orb-weave) or structural anchor chords factor (cobweb)
      webRings: 14,         // capture spiral turns (orb-weave) or tangle mesh depth (cobweb)
      webSpinRate: 1.0,     // real-time silk extrusion / weaving speed multiplier
      webSag: 0.16,         // catenary silk gravity droop (m)
      webDew: 0.85,         // morning dew / cobweb dust highlight intensity
      dynMode: 0,           // 0 off, 1 cables/chains, 2 vines/ropes, 3 flag/sail, 4 grass/wheat, 5 tall reeds, 6 broad fern, 7 canopy tree, 8 underwater kelp, 9 sea anemone tentacles
      dynSub: 0,            // mode variant (e.g. flag vs sail, cable vs chain, meadow vs wheat, summer vs autumn tree)
      dynStiff: 1.25,       // elastic spring recovery rate (1/s multiplier)
      dynTearThresh: 0.0,   // projectile speed threshold for strand tearing or leaf detachment (m/s, 0 = unbreakable)
      dynRows: 28,          // cloth strip rows / cable spans / fern fronds / canopy branches
      dynSegs: 16,          // curve resolution: subdivided segments per cable/vine/kelp/anemone curve (2..32)
      dynCurveDebug: false, // true: show curve control vertices & segment boundaries debug wireframe
      dynAnemThick: 1.0,    // sea anemone tentacle thickness multiplier (dynMode 9)
      dynHeight: 2.4,       // vertical elevation / mast / stalk / canopy height (m)
      dynSag: 0.35,         // gravity catenary droop or frond arch curvature (m)
      dynWaveSpeed: 2.2,    // travelling flutter / stadium wind wave speed (Hz/rad)
      mudSplashMode: 0,     // 0 off, 1 puddle ripple rings + crown splash, 2 car tire puddle & mud splash, 3 heavy mud splatter
      carSpeed: 4.2,        // vehicle loop tangential speed (m/s)
      carTrackRadius: 3.15, // figure-8 vehicle driving loop radius (m)
      ferroMode: 0,         // 0 off, 1 Rosensweig petri-dish pool + magnet, 2 3D levitated ferrofluid crown
      ferroSpikes: 520,     // magnetic Rosensweig spike count across fluid meniscus
      ferroMagnetSpeed: 1.35, // orbiting neodymium magnet angular velocity (rad/s)
      ferroMagnetFree: false, // true: magnet sits at effPos and is freely draggable in 3D viewport
      crackMode: 0,         // 0 off, 1 movie cracked glacial ice, 2 earthquake stone ground fracture
      crackSpeed: 2.6,      // fracture front propagation speed (m/s)
      crackDepth: 0.44,     // fissure heave / abyss tilt amplitude (m)
      beadsMode: 0,         // 0 off, 1 hanging crystal bead curtain, 2 bamboo/brass wind chimes, 3 velvet theater drape strips
      seedHeadMode: 0,      // 0 off, 1 dandelion puff pappus seed heads, 2 spring blossom detachment boughs
      jellyMode: 0,         // 0 off, 1 soft-body neon jelly cube/blob + sticky tendrils, 2 elastic slime cluster + tendrils
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
      swarmRadius: 1.4,     // swarm neighbour radius (m), kind 6
      boidsCohere: 1.15,    // boids cohesion weight
      boidsAlign: 1.65,     // boids alignment weight
      boidsSeparate: 2.85,  // boids separation weight
      boidsSpeed: 3.4,      // boids target flight speed (m/s)
      trailMode: 0,         // 1 = curved tapered particle trail (projectiles / viscous drip)
      beamMode: 0,          // 1 = continuous coherent laser / plasma beam between origin and targetPos
      beamWidth: 0.048,     // laser / plasma core radius (m)
      beamPulse: 6.0,       // plasma wave pulse speed along beam
      glitchRate: 14,       // hologram matrix / scanline glitch tick rate (Hz)
      shieldForm: 0,        // 0 Sphere Dome, 1 Circle Disc, 2 Triangle Aegis, 3 Square Pane, 4 Hexagon Wall, 5 Diamond Barrier
      shieldMask: 1,        // Bitmask of layered shield patterns: 1 Hex, 2 Circles, 4 Triangles, 8 Squares, 16 Diamonds, 32 Voronoi Cracks, 64 Stars
      shieldSpin: 0.20,     // Pattern rotation / drift rate
      gridMode: 0,          // 0 off, 1 motion-graphics effector particle grid / halftone lattice
      gridSurface: 0,       // 0 Plane XZ, 1 Wall XY, 2 Sphere, 3 Hand & Orb, 4 Torus, 5 3D Volume, 6..15 Built-in 3D Objects, 16 Custom Imported Mesh
      gridShape: 2,         // 0 Circles, 1 Boxes (Uniform Scale), 2 Cubes (Height Up/Down), 3 Flat Tiles, 4 Triangles, 5 Hexagons, 6 Auto (Dots + Upper Cubes)
      gridCols: 24,         // lattice columns / angular divisions
      gridRows: 24,         // lattice rows
      gridEffector: 0,      // 0 Organic Sweep Patches, 1 Twin Orbiting Effectors, 2 Curl Islands, 3 Radial Ripple, 4 Force Fields & Ball Only, 5 Placeable Effectors (drag in 3D)
      effPos: [0, 1.5, 0],  // placeable effector A centre (m, drag in viewport)
      effShape: 0,          // placeable effector A shape: 0 Sphere, 1 Box, 2 Torus, 3 Disc, 4 Cone
      effSize: 1.2,         // placeable effector A radius/size (m)
      effPos2: [2.5, 1.5, 0], // placeable effector B centre (m, drag in viewport)
      effShape2: 0,         // placeable effector B shape
      effSize2: 0,          // placeable effector B size (0 = off)
      effBlend: false,      // true: also blend placeable effectors A/B on top of procedural effector waves
      objRot: [0, 0, 0],    // 3D object Euler rotation in degrees [rx, ry, rz] for gridSurface >= 6
      objStyle: 0,          // 3D backing style: 0 Obsidian Dark, 1 Brushed Chrome, 2 Ghost Wireframe, 3 Hidden
      objSampleMode: 0,     // 3D anchor sampling: 0 Even Surface, 1 Surface + Volume, 2 Ridge & Crease Accent
      customMeshId: "",     // runtime registry ID for user-imported OBJ/STL/JSON/Facet mesh (gridSurface === 16)
      customMeshName: "",   // display filename for user-imported 3D mesh
      headStyle: 0,         // bokeh head style: 0 Mixed (per-particle), 1 Sharp core, 2 Creamy disc, 3 Rim ring
      headScale: 1,         // bokeh head size multiplier
      gridMinScale: 0.10,   // idle scale / height fraction outside effector zone
      gridMaxHeight: 0.95,  // max height (m) for height-extruded cubes or max scale boost
      gridContrast: 1.85,   // falloff contrast exponent
      gridBillboard: 0,     // 0 surface-normal aligned, 1 camera-billboarded
      burstEvery: 0,        // timed bursts in seconds (fireworks); 0 = off
      burstCount: 120,      // particles per timed burst
      burstAtOrigin: false, // true: timed bursts fire from the emitter origin, not random sky points
      flareMode: 0,         // 0 off, 1 military IR countermeasure decoy flare V-salvo with billowing smoke ribbons
      fireworkMode: 0,      // 0 off, 1 multi-stage pyro shells, 2 New Year's Text & Object spectacle shells
      fireworkShape: 0,     // 0 Bouquet, 1 Sky Text, 2 Heart, 3 Star, 4 Smiley, 5 Saturn, 6 Crown, 7 Butterfly, 8 Spiral, 9 Grand Finale
      fireworkText: "2026", // custom sky-writing text for fireworkShape === 1 or 9
      fireworkScale: 2.6,   // sky pattern radius in meters
      visible: true,
    };
  };

  const P = (overrides) => Object.assign(PE.baseParams(), overrides);

  const hex = PE.hexLinear;

  PE.Presets = [
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
      id: "magnetic-particles", name: "Magnetic particles", group: "Fields",
      blurb: "Glowing particles steered by a magnetic dipole: they follow the local field direction, so they circulate along the field lines.",
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
      id: "sparks", name: "Sparks", group: "Effects",
      blurb: "Hot metal sparks: gravity, drag, ground bounce and wind coupling.",
      p: P({
        kind: 0, shape: 0, blend: "add", capacity: 3000, rate: 160,
        emitShape: 0, origin: [-3, 0.4, 0], dir: [0, 1, 0], spread: 0.6,
        speedMin: 2, speedMax: 6, drag: 0.5, gravity: 1, lifeMin: 0.6, lifeMax: 1.6,
        sizeStart: 0.035, sizeEnd: 0.01,
        colA: [1, 0.92, 0.55, 1], colC: [1, 1, 0.3, 1], colB: [1, 0.35, 0.05, 0],
        windCoupling: 0.25, bounce: 0.35,
      }),
    },
    {
      id: "rain", name: "Rain streaks", group: "Weather",
      blurb: "High-speed rain sheets falling through wind and bursting into ground splash rings.",
      p: P({
        kind: 0, shape: 0, blend: "alpha", capacity: 5500, rate: 850,
        emitShape: 3, boxHalf: [5.5, 0.35, 5.5], origin: [0, 7.6, 0], dir: [0, -1, 0], spread: 0.04,
        speedMin: 8.5, speedMax: 11.5, drag: 0.04, gravity: 0.95, lifeMin: 1.0, lifeMax: 1.6,
        sizeStart: 0.055, sizeEnd: 0.045,
        colA: [0.76, 0.90, 1, 0.88], colB: [0.64, 0.82, 0.98, 0.25],
        windCoupling: 0.85, bounce: 0.26, flutter: 0.05,
      }),
    },
    {
      id: "hail", name: "Hail", group: "Weather",
      blurb: "Translucent faceted ice pellets dropping fast and ricocheting off vehicle bodywork and the ground.",
      p: P({
        kind: 0, shape: 8, blend: "alpha", capacity: 2000, rate: 160,
        emitShape: 3, boxHalf: [5.5, 0.35, 5.5], origin: [0, 7.6, 0], dir: [0, -1, 0], spread: 0.06,
        speedMin: 7.2, speedMax: 10.5, drag: 0.03, gravity: 1.15, lifeMin: 2.5, lifeMax: 4.0,
        sizeStart: 0.09, sizeEnd: 0.085,
        colA: [0.92, 0.97, 1, 1], colB: [0.74, 0.88, 1, 0.28],
        windCoupling: 0.55, bounce: 0.58, flutter: 0.04,
      }),
    },
    {
      id: "leaves", name: "Leaves", group: "Wind-Blown",
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
      id: "paper", name: "Paper & debris", group: "Wind-Blown",
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
      blurb: "Six-point crystalline snowflakes drifting and fluttering through wind currents, settling softly across the stage and vehicle.",
      p: P({
        kind: 0, shape: 7, blend: "alpha", capacity: 3500, rate: 280,
        emitShape: 3, boxHalf: [5.5, 0.35, 5.5], origin: [0, 6.8, 0], dir: [0, -1, 0], spread: 0.22,
        speedMin: 0.45, speedMax: 1.0, drag: 0.75, gravity: 0.14, lifeMin: 8, lifeMax: 12,
        sizeStart: 0.095, sizeEnd: 0.07,
        colA: [0.96, 0.99, 1, 0.94], colB: [0.82, 0.92, 1, 0],
        windCoupling: 2.8, flutter: 0.95, bounce: 0.08,
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
      id: "petals", name: "Cherry petals", group: "Wind-Blown",
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
      blurb: "Military aircraft IR decoy flare salvo ('Angel of Death'): alternating twin-pod V-bursts of blinding white-hot magnesium flares trailing thick billowing curling smoke plumes and molten sparks.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 480, rate: 0,
        emitShape: 1, radius: 0.25, origin: [0, 4.6, 0], dir: [-0.85, -0.35, 0], spread: 0.42,
        speedMin: 8.5, speedMax: 13.5, drag: 1.55, buoyancy: 0.35, gravity: 0.68, lifeMin: 3.4, lifeMax: 5.2,
        sizeStart: 0.34, sizeEnd: 0.14, colA: [1.0, 0.99, 0.92, 1], colC: [1.0, 0.66, 0.20, 1], colB: [0.84, 0.88, 0.94, 0],
        windCoupling: 0.85, bounce: 0.25, burstEvery: 2.2, burstCount: 16, burstAtOrigin: true,
        flareMode: 1, showObject: true,
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
      id: "watermist", name: "Water mist", group: "Water",
      blurb: "Volumetric sunlit water mist billowing from a high-pressure spray bar: multi-lobe turbulent vapor plumes, subtle prismatic rainbow refraction, and glittering micro-droplet aerosol shimmer.",
      p: P({
        kind: 5, shape: 19, blend: "alpha", capacity: 1800, rate: 220,
        emitShape: 2, radius: 0.95, origin: [2.6, 2.35, 0], dir: [-0.95, -0.18, 0], spread: 0.82,
        speedMin: 0.9, speedMax: 2.6, drag: 1.9, buoyancy: 0.22, gravity: 0.08, lifeMin: 1.9, lifeMax: 3.8,
        sizeStart: 0.30, sizeEnd: 1.55, colA: [0.90, 0.97, 1.0, 0.32], colC: [0.76, 0.92, 1.0, 0.22], colB: [0.88, 0.96, 1.0, 0],
        windCoupling: 1.35, flutter: 0.90, showObject: true,
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
      blurb: "Real 3D faceted glass shard meshes blown from a broken window, tumbling in 3D, bouncing, and catching specular glints.",
      p: P({
        kind: 0, shape: 18, blend: "alpha", capacity: 420, rate: 26,
        emitShape: 1, radius: 0.42, origin: [3, 2.5, -3], dir: [0, 0, 1], spread: 1.0,
        speedMin: 2.2, speedMax: 5.4, drag: 0.14, gravity: 1.0, lifeMin: 2.2, lifeMax: 3.8,
        sizeStart: 0.24, sizeEnd: 0.16, colA: [0.80, 0.94, 1.0, 0.84], colB: [0.56, 0.84, 0.98, 0.0],
        windCoupling: 0.4, bounce: 0.42,
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
      id: "seeds", name: "Dandelion seeds", group: "Wind-Blown",
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
      id: "feathers", name: "Feathers", group: "Wind-Blown",
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
      id: "confetti", name: "Confetti & streamers", group: "Wind-Blown",
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
      blurb: "Choreographed multi-stage Hanabi fireworks: ascending golden mortar rockets bursting into Chrysanthemum, Golden Willow, Brocade Palm, and Crossette shells with drooping ember trails and instant impact pop-vanish.",
      p: P({
        kind: 5, shape: 0, blend: "add", capacity: 1200, rate: 0,
        emitShape: 1, radius: 0.15, origin: [0, 0.05, 0], dir: [0, 1, 0], spread: 3.14159,
        speedMin: 4, speedMax: 9, drag: 1.45, gravity: 0.42, lifeMin: 1.8, lifeMax: 2.9,
        sizeStart: 0.15, sizeEnd: 0.035, colA: [1, 0.94, 0.65, 1], colC: [1, 0.48, 0.22, 1], colB: [1, 0.22, 0.14, 0],
        windCoupling: 0.45, burstEvery: 3.1, burstCount: 240,
        fireworkMode: 1, fireworkShape: 0, fireworkText: "2026", fireworkScale: 2.55, showObject: true,
      }),
    },
    {
      id: "fireworks-spectacle", name: "New Year's Text & Shape Fireworks", group: "VFX",
      blurb: "Grand New Year's spectacle shells bursting into luminous 3D sky text (customizable: '2026', 'HAPPY NEW YEAR', etc.) plus Heart, Saturn, Star, Smiley, Crown, and Butterfly pyro formations with impact pop-vanish.",
      p: P({
        kind: 5, shape: 0, blend: "add", capacity: 1400, rate: 0,
        emitShape: 1, radius: 0.15, origin: [0, 0.05, 0], dir: [0, 1, 0], spread: 3.14159,
        speedMin: 4, speedMax: 9, drag: 1.65, gravity: 0.32, lifeMin: 2.0, lifeMax: 3.2,
        sizeStart: 0.16, sizeEnd: 0.04, colA: [1, 0.92, 0.42, 1], colC: [0.32, 0.92, 1.0, 1], colB: [1, 0.24, 0.64, 0],
        windCoupling: 0.35, burstEvery: 3.5, burstCount: 280,
        fireworkMode: 2, fireworkShape: 9, fireworkText: "2026", fireworkScale: 2.85, showObject: true,
      }),
    },
    {
      id: "curl-fur-field", name: "Curl Wave Field", group: "Silk & Curl",
      blurb: "Dense volumetric bed of glowing micro-filaments sculpted by multi-octave 3D curl noise into indigo-to-cyan velvet dunes and crests.",
      p: P({
        kind: 5, shape: 0, blend: "add", capacity: 10000, rate: 1800,
        emitShape: 3, boxHalf: [1.9, 0.24, 1.9], origin: [-2.1, 1.35, 1.2], dir: [0, 1, 0], spread: 0.25,
        speedMin: 0.3, speedMax: 1.1, drag: 2.6, gravity: 0, buoyancy: 0,
        lifeMin: 2.8, lifeMax: 4.6, sizeStart: 0.042, sizeEnd: 0.036, sizeScale: 1.0,
        colA: hex("#3b49ff"), colC: hex("#2698ff"), colB: hex("#8cf5ff"),
        windCoupling: 0.55, bounce: 0, flutter: 0,
        curlMode: 1, curlFreq: 0.82, curlAmp: 2.45, curlRate: 0.24, streakThin: 0.26,
      }),
    },
    {
      id: "curl-sphere-plasma", name: "Spherical Curl Plasma", group: "Silk & Curl",
      blurb: "Luminous 3D sphere manifold of deep electric cobalt dust and fast converging ice-cyan filament streams tangentially curving over the shell.",
      p: P({
        kind: 5, shape: 0, blend: "add", capacity: 9000, rate: 1500,
        emitShape: 1, radius: 1.65, origin: [2.1, 2.2, 0.0], dir: [0, 1, 0], spread: 3.14159,
        speedMin: 0.4, speedMax: 1.6, drag: 2.2, gravity: 0, buoyancy: 0,
        lifeMin: 3.0, lifeMax: 5.2, sizeStart: 0.038, sizeEnd: 0.030, sizeScale: 1.0,
        colA: hex("#0e55ff"), colC: hex("#28b6ff"), colB: hex("#d2f8ff"),
        windCoupling: 0.45, bounce: 0, flutter: 0,
        curlMode: 2, curlFreq: 1.12, curlAmp: 2.85, curlRate: 0.30, shellRadius: 1.65, streakThin: 0.24,
      }),
    },
    {
      id: "bokeh-stream-ribbons", name: "Bokeh Stream Ribbons", group: "Silk & Curl",
      blurb: "Luminous white & mist-teal bokeh particles flowing along nested swirling eddies with smooth tapered ribbons trailing & fading behind every particle.",
      p: P({
        kind: 5, shape: 9, blend: "add", capacity: 3200, rate: 580,
        emitShape: 3, boxHalf: [3.8, 2.1, 3.2], origin: [0.0, 2.0, 0.0], dir: [0, 1, 0], spread: 0.2,
        speedMin: 0.3, speedMax: 1.0, drag: 1.8, gravity: 0, buoyancy: 0,
        lifeMin: 4.5, lifeMax: 7.5, sizeStart: 0.046, sizeEnd: 0.032, sizeScale: 1.0,
        colA: hex("#3b5a5b"), colC: hex("#8ec7c7"), colB: hex("#efffff"),
        windCoupling: 0.40, bounce: 0, flutter: 0,
        curlMode: 3, curlFreq: 0.62, curlAmp: 1.95, curlRate: 0.16, streakThin: 0.34,
      }),
    },
    {
      id: "bokeh-object-pulse", name: "Object Surface Pour Ribbons", group: "Silk & Curl",
      blurb: "Like pouring water over a sphere: glowing bokeh particles & curved fading ribbons cascade directly over the 3D object surface from top pole to bottom.",
      p: P({
        kind: 5, shape: 9, blend: "add", capacity: 3400, rate: 620,
        emitShape: 1, radius: 1.65, boxHalf: [1.65, 1.65, 1.65], origin: [0.0, 2.25, 0.0], dir: [0, -1, 0], spread: 0.35,
        speedMin: 0.4, speedMax: 1.1, drag: 1.4, gravity: 0, buoyancy: 0,
        lifeMin: 3.4, lifeMax: 5.2, sizeStart: 0.048, sizeEnd: 0.032, sizeScale: 1.0,
        colA: hex("#2d4e50"), colC: hex("#7ecbc9"), colB: hex("#f4ffff"),
        windCoupling: 0.25, bounce: 0, flutter: 0,
        curlMode: 3, curlFreq: 0.85, curlAmp: 1.55, curlRate: 0.18, streakThin: 0.34,
        objectShape: 1, pulseMode: 2, pulseAxis: 1, pulseSpeed: 0.36, pulseWidth: 0.24, showObject: true,
      }),
    },
    {
      id: "tesla-sphere-lightning", name: "Tesla Sphere Lightning", group: "Silk & Curl",
      blurb: "Instantaneous high-voltage dielectric spark arc & forked stepped leaders discharging between two placeable 3D spheres (looping optional).",
      p: P({
        kind: 0, shape: 0, blend: "add", capacity: 600, rate: 0,
        emitShape: 1, radius: 0.46, origin: [-2.15, 2.65, 0.45], dir: [1, 0, 0], spread: 1.4,
        speedMin: 1.8, speedMax: 5.2, drag: 2.2, gravity: 0.65, buoyancy: 0,
        lifeMin: 0.16, lifeMax: 0.42, sizeStart: 0.026, sizeEnd: 0.004, sizeScale: 1.0,
        colA: hex("#3b7cff"), colC: hex("#8ce4ff"), colB: hex("#ffffff"),
        windCoupling: 0.25, bounce: 0, flutter: 0,
        arcMode: 0, targetPos: [2.15, 2.65, 0.45], targetRadius: 0.46,
        arcLoop: false, arcHold: 0.30, arcInterval: 1.40,
        arcBow: 0.14, arcCount: 1, arcJitter: 0.62, arcRate: 14, showElectrodes: true,
      }),
    },
    {
      id: "static-sphere-shock", name: "Static Shock Arcs", group: "Silk & Curl",
      blurb: "Delicate high-voltage corona filaments & crackling electrostatic arcs jumping between two placeable 3D spheres (optional looping).",
      p: P({
        kind: 0, shape: 9, blend: "add", capacity: 600, rate: 0,
        emitShape: 1, radius: 0.40, origin: [-1.75, 1.75, -0.75], dir: [1, 0, 0], spread: 1.8,
        speedMin: 1.2, speedMax: 3.4, drag: 2.6, gravity: 0.35, buoyancy: 0,
        lifeMin: 0.14, lifeMax: 0.36, sizeStart: 0.022, sizeEnd: 0.004, sizeScale: 1.0,
        colA: hex("#9c4dff"), colC: hex("#5ef2ff"), colB: hex("#f5feff"),
        windCoupling: 0.20, bounce: 0, flutter: 0,
        arcMode: 1, targetPos: [1.75, 1.75, -0.75], targetRadius: 0.40,
        arcLoop: true, arcHold: 0.24, arcInterval: 0.55,
        arcBow: 0.32, arcCount: 3, arcJitter: 0.42, arcRate: 28, showElectrodes: true,
      }),
    },
    {
      id: "shockwave-ring", name: "Blast & Sonic Shockwave", group: "Effects",
      blurb: "Supersonic expanding pressure rings and secondary Mach shockfronts bursting radially from an impact origin.",
      p: P({
        kind: 5, shape: 10, leafMode: 0, blend: "add", capacity: 96, rate: 2.2,
        emitShape: 0, origin: [0, 0.32, 0], dir: [0, 1, 0], spread: 0.04,
        speedMin: 0.02, speedMax: 0.14, drag: 0.8, gravity: 0, buoyancy: 0.12,
        lifeMin: 0.72, lifeMax: 1.05, sizeStart: 0.38, sizeEnd: 6.4, sizeScale: 1.0,
        colA: hex("#ffffff"), colC: hex("#8ce0ff"), colB: hex("#2c7cff00"),
        windCoupling: 0.05, bounce: 0, burstEvery: 1.45, burstCount: 4, burstAtOrigin: true,
      }),
    },
    {
      id: "impact-dust-ring", name: "Ground Impact Dust Ring", group: "Effects",
      blurb: "Ground-hugging radial dust torus & shock compression wave rolling outward across the floor.",
      p: P({
        kind: 5, shape: 10, leafMode: 1, blend: "alpha", capacity: 160, rate: 1.5,
        emitShape: 2, radius: 0.28, origin: [0, 0.16, 0], dir: [0, 1, 0], spread: 0.06,
        speedMin: 0.05, speedMax: 0.22, drag: 1.2, gravity: 0, buoyancy: 0.24,
        lifeMin: 0.95, lifeMax: 1.45, sizeStart: 0.45, sizeEnd: 6.8, sizeScale: 1.0,
        colA: hex("#dfd0b8e6"), colC: hex("#b59c7ea8"), colB: hex("#7c685200"),
        windCoupling: 0.35, bounce: 0, burstEvery: 1.85, burstCount: 5, burstAtOrigin: true,
      }),
    },
    {
      id: "emp-pulse-dome", name: "EMP Energy Pulse Dome", group: "Effects",
      blurb: "Expanding electromagnetic hemisphere pulse shell with interference ripples and high-energy Fresnel edge.",
      p: P({
        kind: 5, shape: 11, leafMode: 0, blend: "add", capacity: 96, rate: 1.1,
        emitShape: 0, origin: [0, 0.14, 0], dir: [0, 1, 0], spread: 0.02,
        speedMin: 0.0, speedMax: 0.05, drag: 0.4, gravity: 0, buoyancy: 0,
        lifeMin: 0.90, lifeMax: 1.25, sizeStart: 0.36, sizeEnd: 6.8, sizeScale: 1.0,
        colA: hex("#ffffff"), colC: hex("#49e8ff"), colB: hex("#663dff00"),
        windCoupling: 0, bounce: 0, burstEvery: 1.75, burstCount: 3, burstAtOrigin: true,
      }),
    },
    {
      id: "radar-sweep", name: "Tactical Radar Sweep & Blips", group: "Effects",
      blurb: "3D tactical radar grid with concentric range rings, compass ticks, phosphor sector sweep arm, and flaring contact ping blips.",
      p: P({
        kind: 5, shape: 15, leafMode: 0, blend: "add", capacity: 16, rate: 0.25,
        emitShape: 0, origin: [0, 0.16, 0], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0.0, speedMax: 0.0, drag: 0.0, gravity: 0, buoyancy: 0,
        lifeMin: 7.5, lifeMax: 8.5, sizeStart: 6.2, sizeEnd: 6.2, sizeScale: 1.0, curlRate: 2.2,
        colA: hex("#4effb8"), colC: hex("#17d492"), colB: hex("#0d9e6b"),
        windCoupling: 0, bounce: 0, burstEvery: 0, burstCount: 1, burstAtOrigin: true,
      }),
    },
    {
      id: "sonar-ping", name: "Acoustic Sonar Pulse Ping", group: "Effects",
      blurb: "Expanding concentric cyan-emerald sonar pulse waves with bearing crosshairs and rim notches.",
      p: P({
        kind: 5, shape: 15, leafMode: 1, blend: "add", capacity: 64, rate: 1.2,
        emitShape: 0, origin: [0, 0.15, 0], dir: [0, 1, 0], spread: 0.02,
        speedMin: 0.0, speedMax: 0.04, drag: 0.2, gravity: 0, buoyancy: 0,
        lifeMin: 1.1, lifeMax: 1.5, sizeStart: 0.45, sizeEnd: 7.4, sizeScale: 1.0,
        colA: hex("#d2fff6"), colC: hex("#32f5d4"), colB: hex("#0e7d8a00"),
        windCoupling: 0, bounce: 0, burstEvery: 1.65, burstCount: 2, burstAtOrigin: true,
      }),
    },
    {
      id: "plasma-laser-beam", name: "Plasma & Laser Beam", group: "Effects",
      blurb: "Continuous coherent laser & plasma beam between a placeable muzzle and target, with muzzle corona flare and ricocheting impact sparks.",
      p: P({
        kind: 0, shape: 0, blend: "add", capacity: 1200, rate: 260,
        emitShape: 0, origin: [-2.45, 2.25, 0.25], radius: 0.22,
        dir: [1, -0.18, 0], spread: 0.95, speedMin: 3.2, speedMax: 9.5, drag: 1.4, gravity: 0.65,
        lifeMin: 0.18, lifeMax: 0.55, sizeStart: 0.036, sizeEnd: 0.008, sizeScale: 1.0,
        colA: hex("#ffffff"), colC: hex("#3cf0ff"), colB: hex("#2e6eff00"),
        windCoupling: 0.18, bounce: 0.35,
        beamMode: 1, beamWidth: 0.048, beamPulse: 6.0,
        targetPos: [2.45, 1.35, 0.25], targetRadius: 0.26, showElectrodes: true,
      }),
    },
    {
      id: "plasma-tracers", name: "Bullet & Plasma Tracers", group: "Effects",
      blurb: "High-velocity supersonic tracer rounds streaking downrange with crisp floor ricochets and muzzle cadence.",
      p: P({
        kind: 0, shape: 0, blend: "add", capacity: 1200, rate: 36,
        emitShape: 0, origin: [-3.4, 1.55, -0.4], radius: 0.04,
        dir: [1, -0.09, 0.06], spread: 0.024, speedMin: 19, speedMax: 25, drag: 0.04, gravity: 0.22,
        lifeMin: 0.55, lifeMax: 0.95, sizeStart: 0.068, sizeEnd: 0.045, sizeScale: 1.0, streakThin: 0.28,
        colA: hex("#fff8d6"), colC: hex("#ffaa2b"), colB: hex("#ff4a0e00"),
        windCoupling: 0.05, bounce: 0.58,
      }),
    },
    {
      id: "projectile-trails", name: "Glowing Projectile Trails", group: "Effects",
      blurb: "Arcing ballistic plasma projectiles leaving smooth glowing tapered 3D ribbon trails through wind and gravity.",
      p: P({
        kind: 0, shape: 9, blend: "add", capacity: 256, rate: 14,
        emitShape: 1, radius: 0.12, origin: [-2.8, 0.55, 0.6],
        dir: [0.82, 0.78, -0.14], spread: 0.19, speedMin: 6.4, speedMax: 9.2, drag: 0.08, gravity: 0.85,
        lifeMin: 2.0, lifeMax: 3.1, sizeStart: 0.075, sizeEnd: 0.030, sizeScale: 1.0, streakThin: 0.56,
        colA: hex("#265cff"), colC: hex("#52f0ff"), colB: hex("#ffffff"),
        windCoupling: 0.35, bounce: 0.48, trailMode: 1,
      }),
    },
    {
      id: "welding-grinder-sparks", name: "Welding & Grinder Arc Spray", group: "Effects",
      blurb: "Dense directional spray of incandescent angle-grinder sparks showering down and scattering violently off the floor.",
      p: P({
        kind: 0, shape: 0, blend: "add", capacity: 3200, rate: 420,
        emitShape: 0, origin: [-1.85, 1.50, 0.15], radius: 0.04,
        dir: [0.88, -0.46, 0.08], spread: 0.18, speedMin: 7.0, speedMax: 13.8, drag: 0.36, gravity: 1.28,
        lifeMin: 0.65, lifeMax: 1.45, sizeStart: 0.042, sizeEnd: 0.012, sizeScale: 1.0, streakThin: 0.65,
        colA: hex("#fffbe8"), colC: hex("#ffbc36"), colB: hex("#ff500800"),
        windCoupling: 0.15, bounce: 0.66,
      }),
    },
    {
      id: "hologram-data-glitch", name: "Hologram & Data Glitch", group: "Silk & Curl",
      blurb: "Rising procedural digital matrix glyphs and flickering horizontal holographic scanline glitch slices.",
      p: P({
        kind: 5, shape: 12, leafMode: 0, blend: "add", capacity: 900, rate: 110,
        emitShape: 3, boxHalf: [1.35, 0.12, 1.35], origin: [0, 0.28, 0],
        dir: [0, 1, 0], spread: 0.03, speedMin: 0.85, speedMax: 1.95, drag: 0.05, gravity: 0, buoyancy: 0.04,
        lifeMin: 1.8, lifeMax: 3.3, sizeStart: 0.20, sizeEnd: 0.16, sizeScale: 1.0, glitchRate: 14,
        colA: hex("#18ffa6"), colC: hex("#14dcff"), colB: hex("#8cffec00"),
        windCoupling: 0.18, bounce: 0,
      }),
    },
    {
      id: "boids-flock", name: "Boids Flock (Birds / Bats / Fish)", group: "Wind-Blown",
      blurb: "True cohesion, alignment & separation flocking with animated wingbeats (switch between Birds, Butterflies, or Fish schools).",
      p: P({
        kind: 6, shape: 13, leafMode: 0, blend: "alpha", capacity: 220, rate: 48,
        emitShape: 1, radius: 1.65, origin: [0, 3.0, 0],
        dir: [1, 0.1, 0], spread: 3.14, speedMin: 2.2, speedMax: 3.8, drag: 0.05, gravity: 0,
        lifeMin: 12, lifeMax: 18, sizeStart: 0.25, sizeEnd: 0.25, sizeScale: 1.0,
        colA: hex("#263142"), colC: hex("#496385"), colB: hex("#94adc9"),
        windCoupling: 0.65, flutter: 1.5, swarmRadius: 1.5,
        boidsCohere: 1.2, boidsAlign: 1.8, boidsSeparate: 3.0, boidsSpeed: 3.6,
      }),
    },
    {
      id: "boids-butterflies", name: "Butterfly & Wisp Swarm", group: "Wind-Blown",
      blurb: "Luminous dual-wing butterflies & glowing wisps flocking cleanly with foreshortened wing-flap animation.",
      p: P({
        kind: 6, shape: 13, leafMode: 1, blend: "add", capacity: 180, rate: 36,
        emitShape: 1, radius: 1.8, origin: [0, 2.0, 0],
        dir: [0, 1, 0], spread: 3.14, speedMin: 1.2, speedMax: 2.3, drag: 0.05, gravity: 0,
        lifeMin: 10, lifeMax: 16, sizeStart: 0.26, sizeEnd: 0.24, sizeScale: 1.0,
        colA: hex("#ff9e44"), colC: hex("#ffe082"), colB: hex("#5ef0ff"),
        windCoupling: 1.0, flutter: 2.1, pulseHz: 1.25, pulseDepth: 0.42, swarmRadius: 1.35,
        boidsCohere: 0.95, boidsAlign: 1.2, boidsSeparate: 2.5, boidsSpeed: 2.3,
      }),
    },
    {
      id: "boids-fish-school", name: "Bioluminescent Fish School", group: "Wind-Blown",
      blurb: "Iridescent cyan & reef-coral schooling fish undulating with side-to-side tail propulsion and scattering around thrown spheres.",
      p: P({
        kind: 6, shape: 13, leafMode: 2, blend: "alpha", capacity: 260, rate: 64,
        emitShape: 1, radius: 1.75, origin: [0.2, 2.35, -0.1],
        dir: [1, 0.06, 0.2], spread: 3.14, speedMin: 2.1, speedMax: 3.4, drag: 0.06, gravity: 0,
        lifeMin: 14, lifeMax: 22, sizeStart: 0.22, sizeEnd: 0.21, sizeScale: 1.0,
        colA: hex("#1a8cff"), colC: hex("#3cf5ff"), colB: hex("#ff8f5c"),
        windCoupling: 0.85, flutter: 1.6, swarmRadius: 1.45,
        boidsCohere: 1.35, boidsAlign: 2.1, boidsSeparate: 2.8, boidsSpeed: 3.2,
      }),
    },
    {
      id: "viscous-dripping", name: "Viscous Dripping & Trails", group: "Pyro & Water",
      blurb: "Pure-particle surface-tension liquid droplets slowly detaching and stretching viscous drip trails before splashing.",
      p: P({
        kind: 0, shape: 14, blend: "alpha", capacity: 320, rate: 26,
        emitShape: 2, radius: 0.95, origin: [0, 3.85, 0],
        dir: [0, -1, 0], spread: 0.08, speedMin: 0.04, speedMax: 0.24, drag: 0.14, gravity: 0.96,
        lifeMin: 1.8, lifeMax: 2.9, sizeStart: 0.13, sizeEnd: 0.10, sizeScale: 1.0, streakThin: 0.28,
        colA: hex("#32e092"), colC: hex("#a6ffdc"), colB: hex("#178c59"),
        windCoupling: 0.25, bounce: 0.06, trailMode: 1,
      }),
    },
    {
      id: "splatter-burst", name: "Slime / Paint / Mud Splatter", group: "Pyro & Water",
      blurb: "Pure-particle viscous droplet splatter bursts forming glossy wet splatter puddles on floor impact (no volume grids).",
      p: P({
        kind: 0, shape: 14, blend: "alpha", capacity: 900, rate: 0,
        emitShape: 1, radius: 0.16, origin: [0, 1.35, 0],
        dir: [0, 0.85, 0], spread: 1.18, speedMin: 2.8, speedMax: 6.8, drag: 0.28, gravity: 1.12,
        lifeMin: 2.6, lifeMax: 4.2, sizeStart: 0.17, sizeEnd: 0.15, sizeScale: 1.0,
        colA: hex("#ff2c86"), colC: hex("#ff9ed0"), colB: hex("#9c0c4a"),
        windCoupling: 0.22, bounce: 0.02, burstEvery: 2.05, burstCount: 150, burstAtOrigin: true,
      }),
    },
    {
      id: "derez-cube", name: "Derez Cube", group: "Transitions",
      blurb: "A cube lattice derezzes in a corner wave, bursting or falling. Each cube hitting the floor shatters into 8 smaller sub-cubes.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.65, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.6, speedMax: 2.6, drag: 0.4, gravity: 1.0, lifeMin: 3, lifeMax: 4,
        sizeStart: 0.075, sizeEnd: 0.075, colA: hex("#35d6ff"), colB: hex("#35d6ff"), colC: hex("#ffffff"),
        windCoupling: 0.3, bounce: 0, burstEvery: 8, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.6, holdSpread: 1.6, burstShare: 0.5, fragment: true, childLife: 1.2 },
      }),
    },
    {
      id: "cube-coins", name: "Cube to Coins", group: "Transitions",
      blurb: "A gold cube of spun coins releasing in a wave, bursting outward and bouncing along the floor.",
      p: P({
        kind: 5, shape: 5, blend: "alpha", capacity: 384, rate: 0,
        emitShape: 4, origin: [0, 0.65, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 1.5, speedMax: 4.0, drag: 0.2, gravity: 1.0, lifeMin: 4, lifeMax: 6,
        sizeStart: 0.14, sizeEnd: 0.14, colA: hex("#ffc83d"), colB: hex("#ffc83d"), colC: hex("#fff3b0"),
        windCoupling: 0.2, bounce: 0.35, burstEvery: 6, burstCount: 384, burstAtOrigin: true,
        transition: { holdBase: 0.4, holdSpread: 0.9, burstShare: 0.85, fragment: false, childLife: 0 },
      }),
    },
    {
      id: "melt-away", name: "Melt Away", group: "Transitions",
      blurb: "Surface blocks detach in organic patches across the cube and drift off on the wind.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.65, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.2, speedMax: 0.9, drag: 0.6, gravity: 0.2, lifeMin: 6, lifeMax: 8,
        sizeStart: 0.075, sizeEnd: 0.05, colA: hex("#ffb347"), colB: hex("#ff5a1f"), colC: hex("#fff1d6"),
        windCoupling: 1.4, bounce: 0, burstEvery: 10, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.2, holdSpread: 3.0, burstShare: 0.15, fragment: false, childLife: 0, wave: "melt", assemble: 0, shell: 0 },
      }),
    },
    {
      id: "reverse-derez", name: "Reverse Derez", group: "Transitions",
      blurb: "Cubes fly in from an outer shell and snap onto the cube surface in a wave from one corner.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.65, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.5, speedMax: 2, drag: 0.4, gravity: 0, lifeMin: 3, lifeMax: 4,
        sizeStart: 0.075, sizeEnd: 0.075, colA: hex("#35d6ff"), colB: hex("#35d6ff"), colC: hex("#ffffff"),
        windCoupling: 0, bounce: 0, burstEvery: 9, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.3, holdSpread: 2.0, burstShare: 0, fragment: false, childLife: 0, wave: "corner", assemble: 1, shell: 3.0 },
      }),
    },
    {
      id: "voxel-explosion", name: "Voxel Explosion", group: "Transitions",
      blurb: "A solid sphere of cubes holds briefly, then every voxel bursts radially and tumbles.",
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
      id: "coin-stack", name: "Coin Stack", group: "Transitions",
      blurb: "Gold coins drop sequentially into a vertical floor column and settle cleanly into a stack.",
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
      id: "dissolve-nothing", name: "Dissolve to Nothing", group: "Transitions",
      blurb: "Surface blocks release in organic patches, drift on the wind, shrink and vanish.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.65, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.1, speedMax: 0.6, drag: 0.8, buoyancy: 0.2, gravity: 0.1, lifeMin: 2.5, lifeMax: 4,
        sizeStart: 0.075, sizeEnd: 0, colA: hex("#7fd6ff"), colB: hex("#c8f0ff"), colC: hex("#ffffff"),
        windCoupling: 1.2, bounce: 0, burstEvery: 9, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.2, holdSpread: 3.0, burstShare: 0.1, fragment: false, childLife: 0, wave: "melt", assemble: 0, shell: 0, after: "none" },
      }),
    },
    {
      id: "dissolve-butterflies", name: "Dissolve into Butterflies", group: "Transitions",
      blurb: "Surface blocks melt away in patches, each transforming into a flapping butterfly rising on the wind.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.65, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.2, speedMax: 0.9, drag: 0.8, buoyancy: 0.6, gravity: 0, lifeMin: 6, lifeMax: 9,
        sizeStart: 0.075, sizeEnd: 0.075, colA: hex("#ff9a3c"), colB: hex("#3ec8ff"), colC: hex("#ffffff"),
        windCoupling: 0.9, bounce: 0, flutter: 1.2, burstEvery: 12, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.2, holdSpread: 3.0, burstShare: 0.1, fragment: false, childLife: 0, wave: "melt", assemble: 0, shell: 0, after: "butterfly" },
      }),
    },
    {
      id: "glitch-dissolve", name: "Glitch Dissolve", group: "Transitions",
      blurb: "Digital dissolve breaking surface cubes in quantized grid steps with staccato glitch jumps.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 1536, rate: 0,
        emitShape: 4, origin: [0, 0.65, 0], boxHalf: [0.6, 0.6, 0.6], radius: 0,
        speedMin: 0.1, speedMax: 0.6, drag: 0.8, buoyancy: 0.2, gravity: 0.1, lifeMin: 2.5, lifeMax: 4,
        sizeStart: 0.075, sizeEnd: 0, colA: hex("#00ffd0"), colB: hex("#ff2d6f"), colC: hex("#ffffff"),
        windCoupling: 1.0, bounce: 0, burstEvery: 9, burstCount: 1536, burstAtOrigin: true,
        transition: { holdBase: 0.2, holdSpread: 2.5, burstShare: 0.2, fragment: false, childLife: 0, wave: "glitch", assemble: 0, shell: 0, after: "none" },
      }),
    },
    {
      id: "black-hole", name: "Black Hole Accretion", group: "Space",
      blurb: "Glowing accretion disc spiraling into a dark event horizon with screen-space Einstein ring gravitational lensing.",
      p: P({
        kind: 5, shape: 3, blend: "add", capacity: 3000, rate: 400,
        emitShape: 2, radius: 2.6, origin: [0, 2.2, 0], dir: [0, 1, 0], spread: 0.05,
        speedMin: 0, speedMax: 0.2, drag: 0.2, gravity: 0, lifeMin: 6, lifeMax: 10,
        sizeStart: 0.075, sizeEnd: 0.02, colA: hex("#ffd08a"), colB: hex("#ff6a1a"), colC: hex("#ffffff"),
        windCoupling: 0, bounce: 0,
        attractor: { enabled: true, strength: 7, radius: 16, swirl: 3.5, swallow: 0.45 },
        blackHole: { horizon: 0.45, einstein: 3.0 },
      }),
    },
    {
      id: "heat-shimmer", name: "Heat Shimmer Distortion", group: "VFX",
      blurb: "Rising thermal plume bending the scene behind it via screen-space refractive noise distortion.",
      p: P({
        kind: 0, shape: 3, blend: "alpha", capacity: 200, rate: 30, emitShape: 1, radius: 0.5,
        origin: [0, 0.4, 0], dir: [0, 1, 0], spread: 0.3, speedMin: 0.6, speedMax: 1.4, drag: 0.5, gravity: 0,
        buoyancy: 1.5, flutter: 0.4, windCoupling: 0.6, lifeMin: 2, lifeMax: 3.5, sizeStart: 0.3, sizeEnd: 0.9,
        colA: hex("#2a323b"), colB: hex("#3d4954"), colC: hex("#ffffff"), bounce: 0,
        heatShimmer: { radius: 1.6, strength: 0.03, freq: 14, rate: 1.5 },
      }),
    },
    {
      id: "strikeSparks", name: "Arc Strike Sparks", group: "Effects",
      blurb: "Dedicated ground-scatter spark burst triggered automatically at arc contact whenever lightning fires.",
      p: P({
        kind: 0, shape: 0, blend: "add", capacity: 1600, rate: 0,
        emitShape: 0, origin: [0, 0.12, 0], dir: [0, 1, 0], spread: 1.5,
        speedMin: 1.8, speedMax: 7.5, drag: 0.3, gravity: 1.1, lifeMin: 0.45, lifeMax: 1.15,
        sizeStart: 0.034, sizeEnd: 0.008,
        colA: [0.85, 0.94, 1, 1], colB: [0.35, 0.62, 1, 0], colC: [1, 1, 1, 1],
        windCoupling: 0.25, bounce: 0.46, burstEvery: 2.2, burstCount: 140, burstAtOrigin: true,
      }),
    },
    {
      id: "energy-shield-hex", name: "Energy Shield & Hex Ripples", group: "Effects",
      blurb: "Volumetric spherical force-field barrier with stackable hex, circle, triangle, square, diamond, Voronoi shatter, and star pattern layers plus geodesic ball impact ripples.",
      p: P({
        kind: 5, shape: 16, leafMode: 0, blend: "add", capacity: 24, rate: 0.28,
        emitShape: 0, origin: [-2.25, 1.9, 0.3], dir: [0, 0, 1], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0,
        lifeMin: 6.5, lifeMax: 7.5, sizeStart: 3.1, sizeEnd: 3.1, sizeScale: 1.0, curlFreq: 2.1, curlAmp: 1.15, curlRate: 0.55,
        shieldForm: 0, shieldMask: 1, shieldSpin: 0.18,
        colA: hex("#22b8ff"), colC: hex("#8cf5ff"), colB: hex("#3d6bff"),
        windCoupling: 0, bounce: 0, burstEvery: 0, burstCount: 1, burstAtOrigin: true,
      }),
    },
    {
      id: "energy-shield-prism", name: "Multi-Shape Prism Barrier", group: "Effects",
      blurb: "Layered geometric energy shield combining Triangles + Circles + Diamonds + Voronoi crack lines with directional face orientation and projectile impact ripples.",
      p: P({
        kind: 5, shape: 16, leafMode: 0, blend: "add", capacity: 24, rate: 0.28,
        emitShape: 0, origin: [2.25, 1.9, -0.3], dir: [-0.28, 0.05, 0.96], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0,
        lifeMin: 6.5, lifeMax: 7.5, sizeStart: 3.1, sizeEnd: 3.1, sizeScale: 1.0, curlFreq: 2.2, curlAmp: 1.25, curlRate: 0.62,
        shieldForm: 4, shieldMask: 1 | 2 | 4 | 16 | 32, shieldSpin: 0.24,
        colA: hex("#7c4dff"), colC: hex("#38f9d7"), colB: hex("#ff4ecd"),
        windCoupling: 0, bounce: 0, burstEvery: 0, burstCount: 1, burstAtOrigin: true,
      }),
    },
    {
      id: "aurora-borealis", name: "Aurora Borealis Curtains", group: "Silk & Curl",
      blurb: "Shimmering vertical volumetric aurora ribbon curtains undulating across the sky with emerald-cyan-violet altitude gradients.",
      p: P({
        kind: 5, shape: 17, blend: "add", capacity: 180, rate: 16,
        emitShape: 3, boxHalf: [6.4, 0.35, 3.6], origin: [0, 4.45, -1.6],
        dir: [1, 0, 0.25], spread: 0.15, speedMin: 0.08, speedMax: 0.24, drag: 0.85, gravity: 0, buoyancy: 0.04,
        lifeMin: 6.5, lifeMax: 10.5, sizeStart: 2.4, sizeEnd: 2.75, sizeScale: 1.0, curlFreq: 1.0, curlRate: 0.52,
        colA: hex("#2eff9e"), colC: hex("#1ed8ff"), colB: hex("#9c3dff"),
        windCoupling: 0.18, flutter: 0.16, bounce: 0,
      }),
    },
    {
      id: "spiderweb-orb", name: "Orb-Weave Spiderweb", group: "Silk & Curl",
      blurb: "Real-time spinning orb-weaver web: 6-anchor silken bridge frame, outer guy lines, radial spokes, open hub zone, and a glowing spinneret extruding concentric Archimedean capture spirals with dewdrops and live wind-flex catenary sag.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 320, rate: 0,
        emitShape: 2, radius: 2.0, origin: [1.65, 2.05, 0.25], dir: [0.18, 0.18, 0.97], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.6, gravity: 0.45, buoyancy: 0, flutter: 0.14,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.015, sizeEnd: 0.015, sizeScale: 1.0, streakThin: 0.44,
        webMode: 1, webSpokes: 18, webRings: 14, webSpinRate: 1.0, webSag: 0.13, webDew: 0.85, dynTearThresh: 8.2,
        curlFreq: 1.1, curlAmp: 0.34, windCoupling: 1.15, bounce: 0,
        colA: hex("#f2f7ff"), colC: hex("#b8d9f8"), colB: hex("#6e94ba"),
      }),
    },
    {
      id: "spiderweb-cobweb", name: "Messy Attic Cobwebs", group: "Silk & Curl",
      blurb: "Real-time tangled 3D corner cobwebs: multi-pocket attic drapes, ragged overlapping gossamer hammock pockets, dangling broken tendrils fluttering in the wind, and drifting dust specks.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 300, rate: 0,
        emitShape: 3, boxHalf: [2.25, 1.55, 1.55], origin: [-3.55, 2.35, -0.25], dir: [0.18, 0.08, 0.98], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.45, gravity: 0.75, buoyancy: 0, flutter: 0.55,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.016, sizeEnd: 0.016, sizeScale: 1.0, streakThin: 0.46,
        webMode: 2, webSpokes: 16, webRings: 12, webSpinRate: 1.0, webSag: 0.44, webDew: 0.65, dynTearThresh: 7.4,
        curlFreq: 1.35, curlAmp: 0.50, windCoupling: 1.45, bounce: 0,
        colA: hex("#e4e9ee"), colC: hex("#b7c0ca"), colB: hex("#727d8a"),
      }),
    },
    {
      id: "cables-chains", name: "Swinging Cables & Chains", group: "Cloth & Rigging",
      blurb: "Multi-span catenary steel cables and heavy industrial chains suspended between 3D rigging pylons, swinging dynamically in gusts and rebounding or snapping when struck by thrown balls.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 64, rate: 0,
        emitShape: 3, boxHalf: [2.15, 2.0, 1.25], origin: [-1.15, 0.0, 1.45], dir: [1, 0, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.35, gravity: 1.0, buoyancy: 0, flutter: 0.3,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.045, sizeEnd: 0.045, sizeScale: 1.0, streakThin: 1.0,
        dynMode: 1, dynSub: 1, dynStiff: 1.75, dynTearThresh: 11.2, dynRows: 18, dynHeight: 3.1, dynSag: 0.54, dynWaveSpeed: 1.8,
        curlFreq: 0.75, curlAmp: 0.55, windCoupling: 1.25, bounce: 0,
        colA: hex("#586776"), colC: hex("#99a8b8"), colB: hex("#d2dce6"),
      }),
    },
    {
      id: "vines-ropes", name: "Hanging Vines & Ropes", group: "Cloth & Rigging",
      blurb: "Braided overhead ropes and leafy jungle ivy vines dangling in tiered canopy spans, whipping cleanly away from thrown balls and responding to Force Field gusts.",
      p: P({
        kind: 5, shape: 2, leafMode: 0, blend: "alpha", capacity: 96, rate: 0,
        emitShape: 3, boxHalf: [1.65, 1.8, 1.25], origin: [4.05, 0.0, 1.35], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.4, gravity: 1.0, buoyancy: 0, flutter: 0.5,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.036, sizeEnd: 0.032, sizeScale: 1.0, streakThin: 0.9,
        dynMode: 2, dynSub: 0, dynStiff: 1.10, dynTearThresh: 8.6, dynRows: 28, dynHeight: 3.7, dynSag: 2.35, dynWaveSpeed: 1.9,
        curlFreq: 0.95, curlAmp: 0.65, windCoupling: 1.45, bounce: 0,
        colA: hex("#3b5a2d"), colC: hex("#568c39"), colB: hex("#8fd158"),
      }),
    },
    {
      id: "flag-banner", name: "Wind Flag & Tearing Banner", group: "Cloth & Rigging",
      blurb: "Continuous 3D shaded fabric banner pinned to a flagpole mast, rippling with travelling wind waves, wrapping elastically around thrown balls, and tearing cleanly on high-speed impacts.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 64, rate: 0,
        emitShape: 3, boxHalf: [1.75, 1.15, 0.2], origin: [-3.95, 0.0, -0.75], dir: [1, 0, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.35, gravity: 0.4, buoyancy: 0, flutter: 0.55,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.065, sizeEnd: 0.065, sizeScale: 1.0, streakThin: 1.0,
        dynMode: 3, dynSub: 0, dynStiff: 1.45, dynTearThresh: 9.0, dynRows: 30, dynHeight: 3.65, dynSag: 0.22, dynWaveSpeed: 2.9,
        curlFreq: 1.15, curlAmp: 0.85, windCoupling: 1.65, bounce: 0,
        colA: hex("#d62b2b"), colC: hex("#f2b636"), colB: hex("#8a1822"),
      }),
    },
    {
      id: "ship-sail", name: "Full-Rigged Ship Sail", group: "Cloth & Rigging",
      blurb: "Wide billowing square-rigged canvas sail stretched between an upper yardarm spar and lower boom, catching prevailing Force Field wind bellies and tearing or flexing when struck.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 64, rate: 0,
        emitShape: 3, boxHalf: [1.75, 1.45, 0.4], origin: [1.65, 0.0, -1.25], dir: [0, 0, 1], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.35, gravity: 0.2, buoyancy: 0, flutter: 0.35,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.075, sizeEnd: 0.075, sizeScale: 1.0, streakThin: 1.0,
        dynMode: 3, dynSub: 1, dynStiff: 1.30, dynTearThresh: 9.8, dynRows: 30, dynHeight: 3.9, dynSag: 0.55, dynWaveSpeed: 2.1,
        curlFreq: 0.85, curlAmp: 0.72, windCoupling: 1.75, bounce: 0,
        colA: hex("#f3ead7"), colC: hex("#dcd0b6"), colB: hex("#9e8f76"),
      }),
    },
    {
      id: "grass-wheat-field", name: "Grass & Golden Wheat Field", group: "Foliage & Flora",
      blurb: "Dense interactive meadow of pointed grass blades and plumed golden wheat stalks driven by travelling stadium wind gusts, Force Field vortices, and parted cleanly by rolling or thrown balls.",
      p: P({
        kind: 5, shape: 2, leafMode: 0, blend: "alpha", capacity: 520, rate: 0,
        emitShape: 3, boxHalf: [3.2, 0.0, 2.4], origin: [-0.2, 0.0, 0.4], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.4, gravity: 0.0, buoyancy: 0, flutter: 0.4,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.048, sizeEnd: 0.015, sizeScale: 1.0, streakThin: 1.0,
        dynMode: 4, dynSub: 1, dynStiff: 1.35, dynTearThresh: 0.0, dynRows: 1, dynHeight: 1.08, dynSag: 0.24, dynWaveSpeed: 2.35,
        curlFreq: 0.9, curlAmp: 0.65, windCoupling: 1.55, bounce: 0,
        colA: hex("#265924"), colC: hex("#6e9f34"), colB: hex("#e8bc4a"),
      }),
    },
    {
      id: "tall-reeds", name: "Tall Lakeside Reeds", group: "Foliage & Flora",
      blurb: "Clustered wetland reeds topped with brown velvet cattail spikes arching gracefully in breezes and bowing aside elastically when hit by thrown projectiles.",
      p: P({
        kind: 5, shape: 3, blend: "alpha", capacity: 220, rate: 0,
        emitShape: 3, boxHalf: [1.55, 0.0, 1.35], origin: [-3.35, 0.0, 0.85], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.4, gravity: 0.0, buoyancy: 0, flutter: 0.35,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.056, sizeEnd: 0.018, sizeScale: 1.0, streakThin: 1.0,
        dynMode: 5, dynSub: 0, dynStiff: 1.10, dynTearThresh: 0.0, dynRows: 1, dynHeight: 2.20, dynSag: 0.38, dynWaveSpeed: 1.80,
        curlFreq: 0.8, curlAmp: 0.60, windCoupling: 1.45, bounce: 0,
        colA: hex("#1f4d28"), colC: hex("#558237"), colB: hex("#72472a"),
      }),
    },
    {
      id: "interactive-fern", name: "Lush Interactive Fern", group: "Foliage & Flora",
      blurb: "Broad woodland fern built from tiered arched compound fronds with feathered pinnate leaflets and glowing central veins; individual fronds flex, twist, and rebound when hit.",
      p: P({
        kind: 5, shape: 2, blend: "alpha", capacity: 128, rate: 0,
        emitShape: 3, boxHalf: [1.85, 0.85, 1.85], origin: [-0.95, 0.0, 2.15], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.35, gravity: 0.0, buoyancy: 0, flutter: 0.35,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.34, sizeEnd: 0.06, sizeScale: 1.0, streakThin: 1.0,
        dynMode: 6, dynSub: 0, dynStiff: 1.35, dynTearThresh: 0.0, dynRows: 38, dynHeight: 1.55, dynSag: 0.48, dynWaveSpeed: 1.65,
        curlFreq: 1.05, curlAmp: 0.52, windCoupling: 1.45, bounce: 0,
        colA: hex("#1a5224"), colC: hex("#429836"), colB: hex("#8ed952"),
      }),
    },
    {
      id: "interactive-tree", name: "Swaying Interactive Canopy Tree", group: "Foliage & Flora",
      blurb: "Articulated swaying tree with flexing trunk, 24 wind-responsive canopy boughs, and 490 trembling foliage sprigs; thrown balls shake the branches and knock loose falling leaves.",
      p: P({
        kind: 5, shape: 2, leafMode: 0, blend: "alpha", capacity: 520, rate: 0,
        emitShape: 3, boxHalf: [2.25, 2.2, 2.25], origin: [2.45, 0.0, -0.75], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0.4, gravity: 0.55, buoyancy: 0, flutter: 0.65,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.28, sizeEnd: 0.24, sizeScale: 1.0, streakThin: 1.0,
        dynMode: 7, dynSub: 0, dynStiff: 1.25, dynTearThresh: 4.5, dynRows: 24, dynHeight: 4.15, dynSag: 0.30, dynWaveSpeed: 1.55,
        curlFreq: 0.85, curlAmp: 0.62, windCoupling: 1.50, bounce: 0,
        colA: hex("#4e3622"), colC: hex("#3b8732"), colB: hex("#82cf4e"),
      }),
    },
    {
      id: "effector-grid-cubes", name: "Effector Cube Matrix · Height Up/Down", group: "Motion & Grids",
      blurb: "Motion-graphics 3D cube matrix with fixed footprint where effector waves, Force Fields, and thrown balls drive column heights cleanly up and down.",
      p: P({
        kind: 5, shape: 4, blend: "alpha", capacity: 576, rate: 0,
        emitShape: 3, boxHalf: [1.7, 0.25, 1.7], origin: [3.55, 0.04, 0.2], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.125, sizeEnd: 0.125, sizeScale: 1.0,
        gridMode: 1, gridSurface: 0, gridShape: 2, gridCols: 24, gridRows: 24,
        gridEffector: 0, gridMinScale: 0.05, gridMaxHeight: 1.15, gridContrast: 1.9, gridBillboard: 0,
        curlFreq: 1.15, curlAmp: 0.65, windCoupling: 0, bounce: 0, showObject: true,
        colA: hex("#1a1e26"), colC: hex("#ff2236"), colB: hex("#ff8844"),
      }),
    },
    {
      id: "effector-sphere-dots", name: "Halftone Effector Sphere · Circles", group: "Motion & Grids",
      blurb: "Dark obsidian 3D sphere wrapped in surface-conforming halftone dots and tiles scaled by orbiting effectors, Force Fields, and thrown balls.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 720, rate: 0,
        emitShape: 1, radius: 1.32, boxHalf: [1.32, 1.32, 1.32], origin: [-3.35, 2.05, 0.0], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.12, sizeEnd: 0.12, sizeScale: 1.0,
        gridMode: 1, gridSurface: 2, gridShape: 0, gridCols: 28, gridRows: 26,
        gridEffector: 1, gridMinScale: 0.12, gridMaxHeight: 1.15, gridContrast: 1.85, gridBillboard: 0,
        curlFreq: 1.35, curlAmp: 0.75, windCoupling: 0, bounce: 0, showObject: true,
        colA: hex("#1a1d24"), colC: hex("#ff1c30"), colB: hex("#ff6078"),
      }),
    },
    {
      id: "effector-hand-halftone", name: "Obsidian Hand · Halftone & Tiles", group: "Motion & Grids",
      blurb: "Sculpted obsidian hand holding a chrome sphere—swappable to any 3D object or imported OBJ/STL mesh—wrapped in wave- and effector-modulated halftone dots & raised tiles.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 1200, rate: 0,
        emitShape: 1, radius: 1.35, boxHalf: [1.35, 1.35, 1.35], origin: [0.0, 2.05, 0.1], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.068, sizeEnd: 0.068, sizeScale: 1.0,
        gridMode: 1, gridSurface: 3, gridShape: 6, gridCols: 34, gridRows: 35,
        gridEffector: 0, effPos: [0.45, 2.35, 0.45], effSize: 1.1, effPos2: [-0.65, 1.75, 0.3], effSize2: 0,
        gridMinScale: 0.10, gridMaxHeight: 0.16, gridContrast: 1.75, gridBillboard: 0,
        curlFreq: 1.45, curlAmp: 0.58, windCoupling: 0, bounce: 0, showObject: true,
        colA: hex("#1c1d22"), colC: hex("#ff1b2d"), colB: hex("#ff4a6e"),
      }),
    },
    {
      id: "effector-torus-hex", name: "Kinetic Hex Torus Matrix", group: "Motion & Grids",
      blurb: "Revolving torus lattice tiled with crisp hexagonal effector plates that ripple in radial waves and flare along passing Force Fields & balls.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 760, rate: 0,
        emitShape: 1, radius: 1.35, boxHalf: [1.35, 1.35, 1.35], origin: [0.0, 2.15, -3.25], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.072, sizeEnd: 0.072, sizeScale: 1.0,
        gridMode: 1, gridSurface: 4, gridShape: 5, gridCols: 38, gridRows: 20,
        gridEffector: 3, gridMinScale: 0.14, gridMaxHeight: 0.22, gridContrast: 1.55, gridBillboard: 0,
        curlFreq: 1.25, curlAmp: 0.72, windCoupling: 0, bounce: 0, showObject: true,
        colA: hex("#15192a"), colC: hex("#49d2ff"), colB: hex("#ffe066"),
      }),
    },
    {
      id: "effector-object-placer", name: "Placeable 3D Object · MoGraph & Effectors", group: "Motion & Grids",
      blurb: "Place any 3D object from the built-in library or import any custom OBJ / STL / JSON mesh, transform it in 3D, and sculpt its particle surface with draggable Effectors A & B.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 1400, rate: 0,
        emitShape: 1, radius: 1.55, boxHalf: [1.55, 1.55, 1.55], origin: [0.0, 1.85, 0.0], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.068, sizeEnd: 0.068, sizeScale: 1.0,
        gridMode: 1, gridSurface: 6, gridShape: 6, gridCols: 36, gridRows: 36,
        gridEffector: 5, effPos: [0.85, 2.25, 0.55], effShape: 0, effSize: 1.15,
        effPos2: [-0.95, 1.55, 0.45], effShape2: 1, effSize2: 0.95, effBlend: false,
        objRot: [0, -28, 0], objStyle: 0, objSampleMode: 0,
        gridMinScale: 0.16, gridMaxHeight: 0.24, gridContrast: 1.55, gridBillboard: 0,
        curlFreq: 1.35, curlAmp: 0.65, windCoupling: 0, bounce: 0, showObject: true,
        colA: hex("#181c28"), colC: hex("#3ec46d"), colB: hex("#f0d878"),
      }),
    },
    // Priority 1A: Fluid-Coupled Mud, Car Puddle Rooster-Tails & Crown Splashes (compact particle budget, realistic micro-droplets & ligaments)
    {
      id: "car-puddle-splash", name: "Car Puddle & Mud Splash", group: "Pyro & Water",
      blurb: "3D rally car cutting through a shallow-water puddle, shedding thin tire rooster-tail mud ligaments, bow-wave spray sheets, and fine droplets.",
      p: P({
        kind: 0, shape: 14, blend: "alpha", capacity: 240, rate: 88,
        emitShape: 2, origin: [0, 0.02, 0], radius: 1.85, dir: [0, 1, 0], spread: 0.36,
        speedMin: 2.0, speedMax: 6.2, drag: 1.55, gravity: 1.28, lifeMin: 0.42, lifeMax: 0.92,
        sizeStart: 0.022, sizeEnd: 0.008, sizeScale: 1.0, streakThin: 0.24,
        mudSplashMode: 2, carSpeed: 4.2, carTrackRadius: 3.15, curlFreq: 1.15, curlAmp: 1.1,
        colA: hex("#2b1b0e"), colC: hex("#4d351e"), colB: hex("#998e7a"),
        windCoupling: 0.28, bounce: 0.04, flutter: 0.02, showObject: true,
      }),
    },
    {
      id: "puddle-ripple-splash", name: "Puddle Ripple Rings & Splash", group: "Pyro & Water",
      blurb: "Shallow-water wave puddle with crisp Rayleigh-Taylor drop crown fingers, central Worthington jets, and ball splash craters.",
      p: P({
        kind: 0, shape: 14, blend: "alpha", capacity: 150, rate: 50,
        emitShape: 2, origin: [-3.8, 0.02, -3.2], radius: 2.0, dir: [0, 1, 0], spread: 0.30,
        speedMin: 1.15, speedMax: 3.6, drag: 1.65, gravity: 1.15, lifeMin: 0.44, lifeMax: 0.94,
        sizeStart: 0.016, sizeEnd: 0.006, sizeScale: 1.0, streakThin: 0.20,
        mudSplashMode: 1, curlFreq: 1.0, curlAmp: 0.9,
        colA: hex("#3b5263"), colC: hex("#86abc2"), colB: hex("#e6f2f8"),
        windCoupling: 0.22, bounce: 0.04, flutter: 0.01, showObject: true,
      }),
    },
    {
      id: "mud-splatter-heavy", name: "Mud Splash & Sticky Clods", group: "Pyro & Water",
      blurb: "Compact multi-fingered mud impact eruption firing ragged micro-clods, thin liquid filaments, and fine mist spray.",
      p: P({
        kind: 0, shape: 14, blend: "alpha", capacity: 180, rate: 0,
        emitShape: 2, origin: [3.8, 0.04, 3.0], radius: 0.65, dir: [0, 1, 0], spread: 0.52,
        speedMin: 2.2, speedMax: 5.8, drag: 1.45, gravity: 1.32, lifeMin: 0.55, lifeMax: 1.15,
        sizeStart: 0.024, sizeEnd: 0.010, sizeScale: 1.0, streakThin: 0.22,
        mudSplashMode: 3, curlFreq: 1.2, curlAmp: 1.15,
        burstEvery: 1.85, burstCount: 84, burstAtOrigin: true,
        colA: hex("#211208"), colC: hex("#422713"), colB: hex("#6b4627"),
        windCoupling: 0.18, bounce: 0.02, flutter: 0.01, showObject: true,
      }),
    },
    // Priority 1B: Liquid-Chrome Particle Ferrofluid (Sculpted 3D Rosensweig Metallic Cones + Swarming Chrome Beads)
    {
      id: "ferrofluid-spikes", name: "Magnetic Ferrofluid Dish", group: "Ferrofluid & Fields",
      blurb: "Glossy wet jet-black obsidian ferrofluid pool swelling into smooth concave hexagonal Rosensweig cusps with crisp studio softbox reflections pulled by an orbiting neodymium magnet.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 220, rate: 0,
        emitShape: 2, radius: 2.05, boxHalf: [2.05, 1.05, 2.05], origin: [-1.95, 0.04, 0.25], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.235, sizeEnd: 0.045, sizeScale: 1.0,
        ferroMode: 1, ferroSpikes: 135, ferroMagnetSpeed: 1.25, curlFreq: 1.15, curlAmp: 1.35, streakThin: 1.0,
        windCoupling: 0.15, bounce: 0, showObject: true,
        colA: hex("#030407"), colC: hex("#2d4363"), colB: hex("#f4faff"),
      }),
    },
    {
      id: "ferrofluid-orb", name: "Levitated Ferrofluid Crown", group: "Ferrofluid & Fields",
      blurb: "Free-floating 3D glossy wet-black obsidian ferrofluid sphere bristling with smooth concave Rosensweig liquid peaks and razor-sharp studio highlights.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 220, rate: 0,
        emitShape: 1, radius: 1.22, boxHalf: [1.22, 0.98, 1.22], origin: [2.45, 2.05, -0.2], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.205, sizeEnd: 0.038, sizeScale: 1.0,
        ferroMode: 2, ferroSpikes: 110, ferroMagnetSpeed: 1.45, curlFreq: 1.20, curlAmp: 1.25, streakThin: 1.0,
        windCoupling: 0.15, bounce: 0, showObject: true,
        colA: hex("#030407"), colC: hex("#2c4568"), colB: hex("#e8f7ff"),
      }),
    },
    // Priority 1C: Real Branching Ice Crack & Tectonic Fracture Propagation with 3D Shard Particles
    {
      id: "cracked-ice-sheet", name: "Movie Cracked Glacial Ice", group: "Fracture & Cracks",
      blurb: "Real branching glacial ice cracks racing outward with 3D internal cleavage depth, sparkling frost bursts, and tumbling 3D ice shard meshes erupting from advancing crack tips.",
      p: P({
        kind: 5, shape: 18, blend: "alpha", capacity: 640, rate: 0,
        emitShape: 3, boxHalf: [2.35, 0.14, 2.35], origin: [-2.45, 0.02, 0.2], dir: [0, 1, 0], spread: 0.45,
        speedMin: 1.4, speedMax: 3.8, drag: 0.85, gravity: 0.95, lifeMin: 0.65, lifeMax: 1.65,
        sizeStart: 0.155, sizeEnd: 0.09, sizeScale: 1.0,
        crackMode: 1, crackSpeed: 2.8, crackDepth: 0.55, curlFreq: 1.15, curlAmp: 1.0,
        windCoupling: 0.30, bounce: 0.38, showObject: true,
        colA: hex("#a8eeff"), colC: hex("#dff8ff"), colB: hex("#ffffff"),
      }),
    },
    {
      id: "ground-crack-quake", name: "Seismic Ground Crack Fissure", group: "Fracture & Cracks",
      blurb: "Branching tectonic ground chasm splitting dark basalt rock open over incandescent molten lava fissures with flying 3D basalt rock shards and rising fissure embers.",
      p: P({
        kind: 5, shape: 18, blend: "alpha", capacity: 640, rate: 0,
        emitShape: 3, boxHalf: [2.35, 0.16, 2.35], origin: [2.45, 0.02, -0.2], dir: [0, 1, 0], spread: 0.55,
        speedMin: 1.8, speedMax: 4.5, drag: 0.75, gravity: 1.05, lifeMin: 0.7, lifeMax: 1.65,
        sizeStart: 0.165, sizeEnd: 0.09, sizeScale: 1.0,
        crackMode: 2, crackSpeed: 3.0, crackDepth: 0.60, curlFreq: 1.25, curlAmp: 1.15,
        windCoupling: 0.25, bounce: 0.32, showObject: true,
        colA: hex("#3b3430"), colC: hex("#ff5a1f"), colB: hex("#ffd166"),
      }),
    },
    // Priority 2: #9 Dynamic Lightning Webs / Plasma Cage Arcs
    {
      id: "plasma-cage-arcs", name: "Plasma Cage & Arc Collector", group: "Silk & Curl",
      blurb: "Perimeter conductor ring and central plasma orb trapping crackling branching arcs that violently snap onto thrown balls passing near the cage.",
      p: P({
        kind: 5, shape: 3, blend: "add", capacity: 640, rate: 45,
        emitShape: 1, radius: 1.75, boxHalf: [1.75, 1.45, 1.75], origin: [-2.4, 2.15, 0.2], dir: [0, 1, 0], spread: 3.14159,
        speedMin: 1.4, speedMax: 4.8, drag: 3.0, buoyancy: 0.08, gravity: 0.1,
        lifeMin: 0.14, lifeMax: 0.36, sizeStart: 0.095, sizeEnd: 0.014,
        colA: hex("#72e2ff"), colC: hex("#ffffff"), colB: hex("#5270ff"),
        arcMode: 2, arcCount: 6, arcJitter: 0.62, arcBow: 0.22, arcHold: 0.34, arcInterval: 0.55, arcLoop: true,
        windCoupling: 0.2,
      }),
    },
    {
      id: "lightning-web-mesh", name: "Dynamic Lightning Web & Storm Grid", group: "Silk & Curl",
      blurb: "Floating 3D matrix of high-voltage storm nodes woven together by multi-node zigzag lightning webs that redirect through passing balls.",
      p: P({
        kind: 5, shape: 3, blend: "add", capacity: 680, rate: 52,
        emitShape: 3, radius: 2.05, boxHalf: [2.1, 1.5, 2.1], origin: [2.4, 2.15, -0.2], dir: [0, 1, 0], spread: 3.14159,
        speedMin: 1.6, speedMax: 5.4, drag: 2.8, buoyancy: 0.12, gravity: 0.06,
        lifeMin: 0.12, lifeMax: 0.34, sizeStart: 0.09, sizeEnd: 0.012,
        colA: hex("#99e8ff"), colC: hex("#e8f7ff"), colB: hex("#8a66ff"),
        arcMode: 3, arcCount: 8, arcJitter: 0.72, arcBow: 0.26, arcHold: 0.30, arcInterval: 0.42, arcLoop: true,
        windCoupling: 0.25,
      }),
    },
    // Priority 3: #6 Underwater Kelp Forests & Sea Anemone Tentacles
    {
      id: "kelp-forest", name: "Underwater Giant Kelp Forest", group: "Foliage & Flora",
      blurb: "Buoyant giant kelp stipes, ruffled side blades, and golden gas bladders drifting in gentle underwater currents and parting around swimming balls.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 240, rate: 0,
        emitShape: 3, boxHalf: [2.25, 2.2, 2.25], origin: [-1.55, 0.0, 0.35], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0.45,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.16, sizeEnd: 0.04, sizeScale: 1.0,
        dynMode: 8, dynSub: 0, dynRows: 42, dynHeight: 4.15, dynSag: 0.36, dynStiff: 0.85, dynWaveSpeed: 1.25, dynTearThresh: 0.0,
        curlFreq: 0.85, curlAmp: 1.2, windCoupling: 1.25, bounce: 0.2,
        colA: hex("#1a421e"), colC: hex("#4d8a2d"), colB: hex("#e0a630"),
      }),
    },
    {
      id: "sea-anemone", name: "Bioluminescent Sea Anemone Colony", group: "Foliage & Flora",
      blurb: "Clusters of glowing violet-and-cyan reef anemone tentacles waving hypnotically in water flow and curling inward defensively when touched.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 220, rate: 0,
        emitShape: 2, radius: 1.55, boxHalf: [1.55, 1.0, 1.55], origin: [2.35, 0.0, -0.4], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0, buoyancy: 0.25,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.072, sizeEnd: 0.015, sizeScale: 1.0,
        dynMode: 9, dynSub: 0, dynRows: 48, dynHeight: 1.55, dynSag: 0.45, dynStiff: 1.45, dynWaveSpeed: 1.55, dynTearThresh: 0.0,
        curlFreq: 1.1, curlAmp: 0.95, windCoupling: 1.0, bounce: 0.2, showObject: true,
        colA: hex("#541c6b"), colC: hex("#ff3e8a"), colB: hex("#32ffe6"),
      }),
    },
    // Priority 4 (§6): #3 Hanging Beads, Curtains & Bamboo Chimes
    {
      id: "beaded-door-curtain", name: "Crystal Bead Door Curtain", group: "Cloth & Rigging",
      blurb: "Lintel-pinned vertical strands of shimmering faceted crystal & magenta beads that part elastically around thrown balls and clatter cleanly.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 280, rate: 0,
        emitShape: 3, boxHalf: [2.15, 1.55, 0.26], origin: [-2.15, 0.0, 0.2], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 0.9, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.072, sizeEnd: 0.072, sizeScale: 1.0,
        beadsMode: 1, dynRows: 24, dynHeight: 3.15, dynSag: 0.04, dynStiff: 1.25, dynWaveSpeed: 1.75, dynTearThresh: 14.0,
        curlFreq: 0.95, curlAmp: 1.15, windCoupling: 1.1, showObject: true,
        colA: hex("#382061"), colC: hex("#5ce1ff"), colB: hex("#ff88d6"),
      }),
    },
    {
      id: "bamboo-wind-chimes", name: "Bamboo & Brass Wind Chimes", group: "Cloth & Rigging",
      blurb: "Hollow tuned bamboo & brushed-brass chimes suspended from an arched striker crown around a dangling sail clapper that ring & glow when struck.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 120, rate: 0,
        emitShape: 2, radius: 0.56, boxHalf: [0.62, 1.45, 0.62], origin: [0.15, 0.0, 0.0], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 1.0, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.088, sizeEnd: 0.088, sizeScale: 1.0,
        beadsMode: 2, dynRows: 12, dynHeight: 2.95, dynSag: 0.02, dynStiff: 1.48, dynWaveSpeed: 2.15, dynTearThresh: 0.0,
        curlFreq: 0.85, curlAmp: 1.0, windCoupling: 1.25, showObject: true,
        colA: hex("#80521f"), colC: hex("#dfa949"), colB: hex("#fff5bd"),
      }),
    },
    {
      id: "velvet-theater-drapes", name: "Velvet Theatre Drape Strips", group: "Cloth & Rigging",
      blurb: "Tall top-rail doorway curtain pleats billowing gently in indoor air drafts and parting softly around passing 3D projectiles.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 180, rate: 0,
        emitShape: 3, boxHalf: [2.05, 1.5, 0.22], origin: [2.3, 0.0, -0.2], dir: [0, 1, 0], spread: 0.0,
        speedMin: 0, speedMax: 0, drag: 0, gravity: 1.0, buoyancy: 0,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.115, sizeEnd: 0.115, sizeScale: 1.0,
        beadsMode: 3, dynRows: 26, dynHeight: 3.08, dynSag: 0.06, dynStiff: 0.95, dynWaveSpeed: 1.5, dynTearThresh: 15.0,
        curlFreq: 0.8, curlAmp: 1.35, windCoupling: 1.35, showObject: true,
        colA: hex("#4d081c"), colC: hex("#aa1840"), colB: hex("#ffd57a"),
      }),
    },
    // #4 Dandelion Puff / Seed Heads & Blossom Detachment
    {
      id: "dandelion-puff", name: "Dandelion Puff & Wish Seeds", group: "Foliage & Flora",
      blurb: "Delicate spherical pappus parachute seeds poised on swaying meadow stalks — blow away in swirling gusts or shatter into floating plumes on ball pass.",
      p: P({
        kind: 5, shape: 2, blend: "alpha", capacity: 460, rate: 0,
        emitShape: 1, radius: 0.54, boxHalf: [1.75, 1.0, 1.75], origin: [-1.85, 0.0, 0.15], dir: [0, 1, 0], spread: 0.4,
        speedMin: 0.6, speedMax: 2.2, drag: 1.35, gravity: 0.18, buoyancy: 0.32, flutter: 1.45,
        lifeMin: 12, lifeMax: 18, sizeStart: 0.088, sizeEnd: 0.085, sizeScale: 1.0,
        seedHeadMode: 1, dynRows: 7, dynHeight: 1.9, dynStiff: 1.15, dynWaveSpeed: 1.6, dynTearThresh: 3.2,
        curlFreq: 0.95, curlAmp: 1.45, windCoupling: 2.25, showObject: true,
        colA: hex("#316b24"), colC: hex("#eef7e3"), colB: hex("#ffffff"),
      }),
    },
    {
      id: "blossom-branch-detach", name: "Spring Blossom Detachment", group: "Foliage & Flora",
      blurb: "Flowering orchard boughs trembling under wind eddies and shaking loose clouds of spinning sakura & apple blossoms when brushed or gusted.",
      p: P({
        kind: 5, shape: 2, blend: "alpha", capacity: 480, rate: 0,
        emitShape: 3, radius: 0.48, boxHalf: [1.95, 1.3, 1.75], origin: [2.1, 0.0, -0.15], dir: [0, 1, 0], spread: 0.5,
        speedMin: 0.5, speedMax: 1.9, drag: 1.15, gravity: 0.24, buoyancy: 0.15, flutter: 1.85,
        lifeMin: 10, lifeMax: 16, sizeStart: 0.098, sizeEnd: 0.09, sizeScale: 1.0,
        seedHeadMode: 2, dynRows: 10, dynHeight: 2.65, dynStiff: 1.18, dynWaveSpeed: 1.85, dynTearThresh: 2.8,
        curlFreq: 1.05, curlAmp: 1.35, windCoupling: 2.45, showObject: true,
        colA: hex("#543326"), colC: hex("#ffb5ca"), colB: hex("#fff2f6"),
      }),
    },
    // #8 Soft-Body Jelly / Slime Blobs & Stretchy Tendrils
    {
      id: "jelly-slime-blob", name: "Soft-Body Jelly Cube & Tendrils", group: "Pyro & Water",
      blurb: "Volume-preserving neon-lime gelatin body that wobbles harmonically, squashes cleanly against floor/balls, and pulls glossy sticky tendrils.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 440, rate: 0,
        emitShape: 3, radius: 1.15, boxHalf: [1.15, 1.15, 1.15], origin: [-1.8, 1.25, 0.2], dir: [0, 1, 0], spread: 0.2,
        speedMin: 0, speedMax: 0, drag: 0.4, gravity: 1.0, buoyancy: 0.0, bounce: 0.72,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.115, sizeEnd: 0.115, sizeScale: 1.0,
        jellyMode: 1, dynSag: 0.28, dynStiff: 1.65, dynWaveSpeed: 4.8, dynTearThresh: 5.5,
        curlFreq: 1.25, curlAmp: 0.85, windCoupling: 0.85, showObject: true,
        colA: hex("#09a864"), colC: hex("#32ffaa"), colB: hex("#dcfff0"),
      }),
    },
    {
      id: "slime-tendril-cluster", name: "Elastic Slime Tendril Cluster", group: "Pyro & Water",
      blurb: "Glossy magenta-amber viscoelastic slime colony reaching rubbery adhesion strands toward passing spheres & wind fields before snapping back.",
      p: P({
        kind: 5, shape: 1, blend: "alpha", capacity: 440, rate: 0,
        emitShape: 1, radius: 1.08, boxHalf: [1.1, 1.0, 1.1], origin: [2.05, 1.12, -0.15], dir: [0, 1, 0], spread: 0.2,
        speedMin: 0, speedMax: 0, drag: 0.5, gravity: 1.0, buoyancy: 0.0, bounce: 0.58,
        lifeMin: 9999, lifeMax: 9999, sizeStart: 0.11, sizeEnd: 0.11, sizeScale: 1.0,
        jellyMode: 2, dynSag: 0.38, dynStiff: 1.32, dynWaveSpeed: 4.1, dynTearThresh: 4.8,
        curlFreq: 1.35, curlAmp: 1.0, windCoupling: 1.0, showObject: true,
        colA: hex("#85126e"), colC: hex("#ff3ec8"), colB: hex("#ffd1f5"),
      }),
    },
    // #11 Dense Starling Murmuration + Predator Scattering
    {
      id: "starling-murmuration", name: "Starling Murmuration & Predator Scatter", group: "Wind-Blown",
      blurb: "Hundreds of dusk-lit starlings sweeping in fluid murmuration waves and parting around fast thrown predator balls with flash-split evasion.",
      p: P({
        kind: 6, shape: 13, leafMode: 0, blend: "alpha", capacity: 520, rate: 95,
        emitShape: 1, radius: 2.35, origin: [0, 3.25, 0], dir: [1, 0.14, 0], spread: 3.14,
        speedMin: 3.4, speedMax: 5.4, drag: 0.04, gravity: 0,
        lifeMin: 16, lifeMax: 24, sizeStart: 0.21, sizeEnd: 0.20, sizeScale: 1.0,
        colA: hex("#1a2236"), colC: hex("#576e96"), colB: hex("#ffaa6e"),
        windCoupling: 0.9, flutter: 1.85, swarmRadius: 1.85,
        boidsCohere: 1.55, boidsAlign: 2.35, boidsSeparate: 3.25, boidsSpeed: 5.2,
      }),
    },
  ];

  PE.QUARANTINE_IDS = new Set([
    "blossom-branch-detach",
    "jelly-slime-blob",
    "slime-tendril-cluster",
    "splatter-burst",
    "impact-dust-ring",
    "splash",
    "puddle-ripple-splash",
    "car-puddle-splash",
    "mud-splatter-heavy",
  ]);
  PE.isQuarantined = (presetOrId) => {
    const id = typeof presetOrId === "string" ? presetOrId : presetOrId?.id;
    return !!id && PE.QUARANTINE_IDS.has(id);
  };

  PE.VFX_CATEGORIES = [
    { id: "Cloth & Rigging", label: "Cloth & Rigging", badge: "CLOTH", blurb: "Ball-interactive wind flags, ship sails, hanging bead curtains, wind chimes, doorway drapes, cables & vines" },
    { id: "Foliage & Flora", label: "Foliage & Flora", badge: "FLORA", blurb: "Interactive grass & wheat, reeds, ferns, trees, kelp, anemones & dandelion seed heads" },
    { id: "Ferrofluid & Fields", label: "Ferrofluid & Fields", badge: "FERRO", blurb: "Black liquid-chrome Rosensweig ferrofluid spike pools and 3D levitated crowns tracking magnets & Force Fields" },
    { id: "Fracture & Cracks", label: "Fracture & Cracks", badge: "CRACK", blurb: "Movie-style glacial ice cracking & earthquake Voronoi basalt ground fracture propagation with fissure debris" },
    { id: "Motion & Grids", label: "Motion & Grids", badge: "MOGRAPH", blurb: "Effector-driven halftone dot/box/triangle grids, height-extruded cube matrices, and obsidian surface lattices" },
    { id: "Silk & Curl", label: "Silk & Curl", badge: "CURL", blurb: "Dense 3D curl-noise wave fur, plasma spheres, bokeh ribbons, lightning webs, plasma cages & spinning spiderwebs" },
    { id: "Transitions", label: "Transitions & Derez", badge: "DEREZ", blurb: "Solid surface lattice derez, 8-fragment floor shatter, coin stacks, glitch & butterfly dissolves" },
    { id: "Space & Distortion", label: "Space & Distortion", badge: "LENS", blurb: "Black hole accretion spiral with relativistic gravitational lensing & rising heat shimmer refraction" },
    { id: "Shields & Tech", label: "Shields & Tech", badge: "TECH", blurb: "Multi-shape energy shield barriers with hex/circle/triangle/diamond/Voronoi cracks, radar sweeps, sonar & EMP domes" },
    { id: "Atmosphere & Sky", label: "Atmosphere & Sky", badge: "SKY", blurb: "Volumetric aurora borealis curtains, fireworks bursts, flares & floating atmospheric dust motes" },
    { id: "Ballistics & Sparks", label: "Ballistics & Sparks", badge: "BALLISTIC", blurb: "Shockwaves, beams, tracers, strike sparks, 3D glass shards, welding spray & high-velocity impact ballistics" },
    { id: "Pyro & Water", label: "Pyro & Water", badge: "VFX", blurb: "Fire, smoke, embers, steam, mist, bubbles & foam" },
    { id: "Wind-Blown", label: "Wind-Blown", badge: "WIND", blurb: "Leaves, petals, seeds, starling murmurations & butterfly boids driven by Force Fields & predator balls" },
    { id: "Storms & Weather", label: "Storms & Weather", badge: "WEATHER", blurb: "Rain, snow, hail, sandstorms, tornado debris, ash & sand" },
    { id: "Quarantine", label: "Quarantine", badge: "HOLD", blurb: "Shelved presets kept out of active shelves (blossom detachment, slime/jelly, dust rings, splashes & mud)" },
  ];

  PE.vfxCategoryOf = (paramsOrPreset, maybePreset = null) => {
    const preset = maybePreset || (paramsOrPreset?.p ? paramsOrPreset : null);
    const p = paramsOrPreset?.p || paramsOrPreset;
    if (!p) return "Ballistics & Sparks";
    const g = preset?.group || "";
    const id = preset?.id || "";
    if (PE.QUARANTINE_IDS.has(id)) return "Quarantine";
    if ((p.ferroMode || 0) > 0 || g === "Ferrofluid & Fields") return "Ferrofluid & Fields";
    if ((p.crackMode || 0) > 0 || g === "Fracture & Cracks") return "Fracture & Cracks";
    if ((p.gridMode || 0) > 0 || g === "Motion & Grids") return "Motion & Grids";
    if ((p.beadsMode || 0) > 0 || ((p.dynMode || 0) >= 1 && (p.dynMode || 0) <= 3) || g === "Cloth & Rigging") return "Cloth & Rigging";
    if ((p.seedHeadMode || 0) > 0 || (p.dynMode || 0) >= 4 || g === "Foliage & Flora") return "Foliage & Flora";
    if ((p.jellyMode || 0) > 0 || (p.mudSplashMode || 0) > 0) return "Pyro & Water";
    if (p.transition || g === "Transitions") return "Transitions";
    if (p.blackHole || p.heatShimmer || g === "Space") return "Space & Distortion";
    if (p.shape === 11 || p.shape === 12 || p.shape === 15 || p.shape === 16) return "Shields & Tech";
    if (p.shape === 17 || (p.fireworkMode || 0) > 0 || (p.flareMode || 0) > 0 || id === "fireworks" || id === "fireworks-spectacle" || id === "flares" || id === "pollen") return "Atmosphere & Sky";
    if (p.curlMode > 0 || p.webMode > 0 || (p.arcMode ?? -1) >= 2 || g === "Silk & Curl") return "Silk & Curl";
    if (g === "Weather" || g === "Storms" || g === "Ground" || id === "ash") return "Storms & Weather";
    if (g === "Wind-Blown" || g === "Wind-blown" || g === "Ambient" || p.kind === 1 || p.kind === 6) return "Wind-Blown";
    if (g === "Water" || g === "Pyro & Water" || p.shape === 14 || p.shape === 19 || (g === "VFX" && p.shape !== 0 && p.shape !== 5)) return "Pyro & Water";
    return "Ballistics & Sparks";
  };

  PE.vfxSublabelOf = (paramsOrPreset) => {
    const p = paramsOrPreset?.p || paramsOrPreset;
    if (!p) return "Particles";
    if ((p.flareMode || 0) > 0) return "IR Countermeasure Flares";
    if ((p.fireworkMode || 0) === 2) return "New Year's Pyro Spectacle";
    if ((p.fireworkMode || 0) === 1) return "Multi-Stage Fireworks";
    if (p.shape === 19) return "Volumetric Water Mist";
    if (p.arcMode === 0) return "Tesla Lightning";
    if (p.arcMode === 1) return "Static Shock";
    if (p.arcMode === 2) return "Plasma Cage";
    if (p.arcMode === 3) return "Lightning Web";
    if ((p.beadsMode || 0) === 1) return "Bead Curtain";
    if ((p.beadsMode || 0) === 2) return "Bamboo Chimes";
    if ((p.beadsMode || 0) === 3) return "Doorway Drapes";
    if ((p.seedHeadMode || 0) === 1) return "Dandelion Seeds";
    if ((p.seedHeadMode || 0) === 2) return "Blossom Boughs";
    if ((p.jellyMode || 0) === 1) return "Soft-Body Jelly";
    if ((p.jellyMode || 0) === 2) return "Slime Tendrils";
    if ((p.ferroMode || 0) === 1) return "Ferrofluid Pool";
    if ((p.ferroMode || 0) === 2) return "Levitated Ferrofluid";
    if ((p.crackMode || 0) === 1) return "Cracked Ice";
    if ((p.crackMode || 0) === 2) return "Ground Fracture";
    if ((p.mudSplashMode || 0) === 1) return "Puddle Ripples";
    if ((p.mudSplashMode || 0) === 2) return "Car Mud Splash";
    if ((p.mudSplashMode || 0) === 3) return "Mud Splatter";
    if ((p.beamMode || 0) > 0) return "Laser / Plasma Beam";
    if (p.blackHole) return "Black Hole Lens";
    if (p.heatShimmer) return "Heat Shimmer";
    if (p.transition) {
      if (p.transition.after === "butterfly") return "Butterfly Dissolve";
      if (p.transition.assemble === 1) return "Reverse Derez";
      if (p.transition.assemble === 2) return "Coin Stack";
      if (p.transition.wave === "glitch") return "Glitch Dissolve";
      if (p.transition.wave === "melt") return "Melt / Dissolve";
      if (p.transition.fragment) return "Shatter Derez";
      return "Mesh Transition";
    }
    if ((p.gridMode || 0) > 0) {
      return ["Halftone Circles", "Scaling Boxes", "Height Cubes", "Surface Tiles", "Triangle Grid", "Hex Grid", "Halftone & Tiles"][p.gridShape | 0] || "Effector Grid";
    }
    if ((p.webMode || 0) === 1) return "Orb-Weave Web";
    if ((p.webMode || 0) === 2) return "Messy Cobwebs";
    if ((p.dynMode || 0) === 1) return p.dynSub ? "Chain Links" : "Swinging Cables";
    if ((p.dynMode || 0) === 2) return p.dynSub ? "Hanging Ropes" : "Leafy Vines";
    if ((p.dynMode || 0) === 3) return p.dynSub ? "Ship Sail" : "Wind Flag";
    if ((p.dynMode || 0) === 4) return p.dynSub ? "Wheat & Grass" : "Meadow Grass";
    if ((p.dynMode || 0) === 5) return "Tall Reeds";
    if ((p.dynMode || 0) === 6) return "Pinnate Fern";
    if ((p.dynMode || 0) === 7) return "Swaying Tree";
    if ((p.dynMode || 0) === 8) return "Kelp Forest";
    if ((p.dynMode || 0) === 9) return "Sea Anemone";
    if (p.curlMode === 1) return "Curl Wave";
    if (p.curlMode === 2) return "Curl Sphere";
    if (p.curlMode === 3) return (p.pulseMode || 0) > 0 ? "Surface Pour" : "Ribbon Stream";
    if (p.kind === 6) {
      const pid = paramsOrPreset?.id || "";
      if (pid === "starling-murmuration") return "Murmuration Flock";
      return p.leafMode === 1 ? "Butterfly Flock" : (p.leafMode === 2 ? "Fish School" : "Boids Flock");
    }
    if ((p.trailMode || 0) > 0) return p.shape === 14 ? "Viscous Drip" : "Projectile Trail";
    if (p.magneticField) return "Dipole Guided";
    if (p.kind === 1) return "Wind-driven";
    return ["Streak", "Sphere", "Leaf", "Pyro / Glow", "Cube", "Coin", "Bubble", "Snowflake", "Ice Hail", "Bokeh Bead", (p.leafMode ? "Dust Shockwave" : "Mach Shockwave"), "EMP Pulse Dome", "Holo Glitch", "Winged Boid", "Viscous Drop", (p.leafMode ? "Sonar Ping" : "Radar Sweep"), ((p.shieldMask ?? 1) !== 1 || (p.shieldForm ?? 0) > 0 ? "Multi-Shape Shield" : "Hex Energy Shield"), "Aurora Curtain", "3D Glass Shard"][p.shape] || "Ballistic";
  };

  for (const preset of PE.Presets) {
    preset.vfxCategory = PE.vfxCategoryOf(preset.p, preset);
    preset.vfxSublabel = PE.vfxSublabelOf(preset);
  }

  PE.presetById = (id) => PE.Presets.find((preset) => preset.id === id);

  // Arbitrary-length colour ramp & attribute-driven colouring helpers.
  PE.COLOR_BY_MODES = [
    { id: "modeDefault",  code: 0,  label: "Mode default (lifetime / geometry)", defaultMin: 0,    defaultMax: 1,    unit: "" },
    { id: "lifetime",     code: 1,  label: "Normalised lifetime (age / life)",   defaultMin: 0,    defaultMax: 1,    unit: "0–1" },
    { id: "age",          code: 2,  label: "Particle age (s)",                   defaultMin: 0,    defaultMax: 3,    unit: "s" },
    { id: "speed",        code: 3,  label: "Velocity speed |v| (m/s)",           defaultMin: 0,    defaultMax: 8,    unit: "m/s" },
    { id: "velY",         code: 4,  label: "Vertical velocity vy (m/s)",         defaultMin: -6,   defaultMax: 6,    unit: "m/s" },
    { id: "angle",        code: 5,  label: "Position angle around origin (°)",   defaultMin: 0,    defaultMax: 1,    unit: "turn" },
    { id: "velAngle",     code: 6,  label: "Velocity heading angle (°)",         defaultMin: 0,    defaultMax: 1,    unit: "turn" },
    { id: "distOrigin",   code: 7,  label: "Distance from origin (m)",           defaultMin: 0,    defaultMax: 4,    unit: "m" },
    { id: "height",       code: 8,  label: "World height Y (m)",                 defaultMin: 0,    defaultMax: 4,    unit: "m" },
    { id: "noise3D",      code: 9,  label: "3D spatial noise",                   defaultMin: 0,    defaultMax: 1,    unit: "" },
    { id: "random",       code: 10, label: "Per-particle random seed",           defaultMin: 0,    defaultMax: 1,    unit: "" },
    { id: "spinAngle",    code: 11, label: "Spin / orientation angle",           defaultMin: 0,    defaultMax: 1,    unit: "turn" },
    { id: "distCamera",   code: 12, label: "Distance to camera (m)",             defaultMin: 2,    defaultMax: 16,   unit: "m" },
    { id: "particleSize", code: 13, label: "Particle size (m)",                  defaultMin: 0.02, defaultMax: 0.45, unit: "m" },
    { id: "lifeDuration", code: 14, label: "Assigned total lifespan (s)",        defaultMin: 0.2,  defaultMax: 6,    unit: "s" },
  ];
  PE.COLOR_BY_CODE = Object.fromEntries(PE.COLOR_BY_MODES.map((m) => [m.id, m.code]));
  PE.COLOR_WRAP_MODES = [["Clamp", "clamp"], ["Repeat", "repeat"], ["Mirror", "mirror"]];
  PE.COLOR_INTERP_MODES = [["Linear", "linear"], ["Smooth", "smooth"], ["Stepped", "stepped"]];
  PE.COLOR_PALETTES = [
    { id: "rainbow", label: "Spectrum (7 stops)", colors: ["#ff3355", "#ff8822", "#ffe033", "#38e86b", "#22ccff", "#5566ff", "#d44cff"] },
    { id: "thermal", label: "Blackbody (6 stops)", colors: ["#140505", "#961408", "#f05a14", "#ffbe38", "#fff6c8", "#9be2ff"] },
    { id: "aurora",  label: "Aurora (5 stops)",   colors: ["#1a1464", "#10b981", "#38f2d6", "#a3ff4d", "#a855f7"] },
    { id: "plasma",  label: "Plasma (6 stops)",   colors: ["#0d0887", "#6a00a8", "#b12a90", "#e16462", "#fca636", "#f0f921"] },
    { id: "ocean",   label: "Ocean (5 stops)",    colors: ["#071e3d", "#134e6f", "#1e96a8", "#7be3d0", "#eefbff"] },
    { id: "sunset",  label: "Sunset Neon (6 stops)", colors: ["#2b1055", "#9d1763", "#f0544f", "#ffad42", "#ffd57e", "#3b82f6"] },
  ];

  PE.usesMidStop = function usesMidStop(p) {
    if (!p) return false;
    const isTrans = !!p.transition;
    const sh = p.shape ?? 3;
    return !isTrans && (
      (p.curlMode || 0) > 0 || (p.webMode || 0) > 0 || (p.dynMode || 0) > 0 ||
      (p.gridMode || 0) > 0 || (p.ferroMode || 0) > 0 || (p.crackMode || 0) > 0 ||
      (p.mudSplashMode || 0) > 0 || (p.beadsMode || 0) > 0 || (p.seedHeadMode || 0) > 0 ||
      (p.jellyMode || 0) > 0 || (p.trailMode || 0) > 0 || p.kind === 6 ||
      (p.beamMode || 0) > 0 || sh === 0 || sh >= 10
    );
  };

  function normRgba(raw, fallbackAlpha = 1) {
    if (typeof raw === "string") return PE.hexLinear(raw, fallbackAlpha);
    if (Array.isArray(raw) && raw.length >= 3) {
      return [
        Math.max(0, Math.min(4, Number(raw[0]) || 0)),
        Math.max(0, Math.min(4, Number(raw[1]) || 0)),
        Math.max(0, Math.min(4, Number(raw[2]) || 0)),
        raw.length > 3 && Number.isFinite(Number(raw[3])) ? Math.max(0, Math.min(1, Number(raw[3]))) : fallbackAlpha,
      ];
    }
    return [1, 1, 1, fallbackAlpha];
  }

  PE.getColorStops = function getColorStops(p) {
    if (!p) return [{ color: [1, 1, 1, 1], pos: 0 }, { color: [1, 1, 1, 0], pos: 1 }];
    if (Array.isArray(p.colorStops) && p.colorStops.length >= 1) {
      const n = p.colorStops.length;
      const out = p.colorStops.map((entry, idx) => {
        if (entry && typeof entry === "object" && !Array.isArray(entry)) {
          const pos = Number.isFinite(Number(entry.pos)) ? Math.max(0, Math.min(1, Number(entry.pos))) : (n > 1 ? idx / (n - 1) : 0);
          return { color: normRgba(entry.color ?? entry.rgba ?? entry, 1), pos };
        }
        return { color: normRgba(entry, 1), pos: n > 1 ? idx / (n - 1) : 0 };
      });
      out.sort((a, b) => a.pos - b.pos);
      return out;
    }
    const isAssembly = !!(p.transition && (p.transition.assemble | 0) > 0);
    const cA = normRgba(p.colA, 1);
    if (isAssembly) return [{ color: cA, pos: 0 }];
    const cB = normRgba(p.colB, 0);
    const hasC = p.colC && (p.colC[3] ?? 0) > 0.01 && PE.usesMidStop(p);
    if (hasC) {
      const sh = p.shape ?? 3;
      const midPos = p.kind === 4 ? 0.5 : (((p.curlMode || 0) === 0 && (sh === 0 || sh >= 10) && !(p.gridMode || p.ferroMode || p.crackMode || p.beadsMode || p.seedHeadMode || p.jellyMode || p.webMode || p.dynMode || p.mudSplashMode || p.kind === 6))
        ? 0.38 : 0.48);
      return [{ color: cA, pos: 0 }, { color: normRgba(p.colC, 1), pos: midPos }, { color: cB, pos: 1 }];
    }
    return [{ color: cA, pos: 0 }, { color: cB, pos: 1 }];
  };

  PE.syncColorStops = function syncColorStops(p, stops, keepOrder = false) {
    if (!p || !Array.isArray(stops) || stops.length === 0) return [];
    const n = stops.length;
    const norm = stops.map((entry, idx) => {
      if (entry && typeof entry === "object" && !Array.isArray(entry)) {
        const pos = Number.isFinite(Number(entry.pos)) ? Math.max(0, Math.min(1, Number(entry.pos))) : (n > 1 ? idx / (n - 1) : 0);
        return { color: normRgba(entry.color ?? entry.rgba ?? entry, 1), pos };
      }
      return { color: normRgba(entry, 1), pos: n > 1 ? idx / (n - 1) : 0 };
    });
    if (!keepOrder) norm.sort((a, b) => a.pos - b.pos);
    p.colorStops = norm.map((s) => ({ color: [...s.color], pos: s.pos }));
    p.colA = [...norm[0].color];
    p.colB = [...norm[norm.length - 1].color];
    if (norm.length >= 3) {
      const mid = norm[Math.floor((norm.length - 1) / 2)];
      p.colC = [...mid.color];
    } else if (!p.colC) {
      p.colC = [...p.colB];
    }
    return p.colorStops;
  };

  PE.packColorRamp = function packColorRamp(p) {
    const stops = PE.getColorStops(p);
    const floats = new Float32Array(stops.length * 8);
    for (let i = 0; i < stops.length; i++) {
      const c = stops[i].color;
      const o = i * 8;
      floats[o] = c[0]; floats[o + 1] = c[1]; floats[o + 2] = c[2]; floats[o + 3] = c[3];
      floats[o + 4] = stops[i].pos;
    }
    const byCode = PE.COLOR_BY_CODE[p?.colorBy || "modeDefault"] ?? 0;
    const wrapCode = p?.colorWrap === "repeat" ? 1 : p?.colorWrap === "mirror" ? 2 : 0;
    const interpCode = p?.colorInterp === "smooth" ? 1 : p?.colorInterp === "stepped" ? 2 : 0;
    const min = Number.isFinite(Number(p?.colorMin)) ? Number(p.colorMin) : 0;
    const max = Number.isFinite(Number(p?.colorMax)) ? Number(p.colorMax) : 1;
    const repeat = Math.max(0.05, Number.isFinite(Number(p?.colorRepeat)) ? Number(p.colorRepeat) : 1);
    const phase = Number.isFinite(Number(p?.colorPhase)) ? Number(p.colorPhase) : 0;
    return { stops, floats, count: stops.length, byCode, min, max, repeat, wrapCode, phase, interpCode };
  };

  function hash3u(ix, iy, iz) {
    let h = (Math.imul(ix | 0, 374761393) + Math.imul(iy | 0, 668265263) + Math.imul(iz | 0, 2147483647)) >>> 0;
    h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0;
    return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
  }

  PE.rampVNoise3 = function rampVNoise3(x, y, z) {
    const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
    const fx = x - ix, fy = y - iy, fz = z - iz;
    const ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy), uz = fz * fz * (3 - 2 * fz);
    const c000 = hash3u(ix, iy, iz),         c100 = hash3u(ix + 1, iy, iz);
    const c010 = hash3u(ix, iy + 1, iz),     c110 = hash3u(ix + 1, iy + 1, iz);
    const c001 = hash3u(ix, iy, iz + 1),     c101 = hash3u(ix + 1, iy, iz + 1);
    const c011 = hash3u(ix, iy + 1, iz + 1), c111 = hash3u(ix + 1, iy + 1, iz + 1);
    const x00 = c000 + (c100 - c000) * ux, x10 = c010 + (c110 - c010) * ux;
    const x01 = c001 + (c101 - c001) * ux, x11 = c011 + (c111 - c011) * ux;
    const y0 = x00 + (x10 - x00) * uy, y1 = x01 + (x11 - x01) * uy;
    return y0 + (y1 - y0) * uz;
  };

  function hashFJs(seed, k) {
    let x = ((Math.floor(Math.max(0, seed)) * 31 + k * 977 + 17) >>> 0);
    x ^= x >>> 16; x = Math.imul(x, 0x7feb352d) >>> 0;
    x ^= x >>> 15; x = Math.imul(x, 0x846ca68b) >>> 0;
    x ^= x >>> 16;
    return (x & 0xffffff) / 16777216;
  }

  PE.evalColorRampAt = function evalColorRampAt(stops, tRaw, p) {
    if (!stops || stops.length === 0) return [1, 1, 1, 1];
    if (stops.length === 1) return [...stops[0].color];
    const rep = Math.max(0.05, Number.isFinite(Number(p?.colorRepeat)) ? Number(p.colorRepeat) : 1);
    const phase = Number.isFinite(Number(p?.colorPhase)) ? Number(p.colorPhase) : 0;
    const u = tRaw * rep + phase;
    let t;
    if (p?.colorWrap === "repeat") t = u - Math.floor(u);
    else if (p?.colorWrap === "mirror") {
      const m2 = u - 2 * Math.floor(u * 0.5);
      t = m2 <= 1 ? m2 : 2 - m2;
    } else {
      t = Math.max(0, Math.min(1, u));
    }
    if (t <= stops[0].pos) return [...stops[0].color];
    const last = stops.length - 1;
    if (t >= stops[last].pos) return [...stops[last].color];
    let lo = 0, hi = last;
    while (lo + 1 < hi) {
      const mid = (lo + hi) >> 1;
      if (stops[mid].pos <= t) lo = mid;
      else hi = mid;
    }
    const a = stops[lo], b = stops[lo + 1];
    let frac = Math.max(0, Math.min(1, (t - a.pos) / Math.max(1e-5, b.pos - a.pos)));
    if (p?.colorInterp === "smooth") frac = frac * frac * (3 - 2 * frac);
    else if (p?.colorInterp === "stepped") frac = 0;
    return [
      a.color[0] + (b.color[0] - a.color[0]) * frac,
      a.color[1] + (b.color[1] - a.color[1]) * frac,
      a.color[2] + (b.color[2] - a.color[2]) * frac,
      a.color[3] + (b.color[3] - a.color[3]) * frac,
    ];
  };

  PE.evalParticleColorCPU = function evalParticleColorCPU(p, defaultT, px, py, pz, vx, vy, vz, age, life, size, ang, seed, camX, camY, camZ, time) {
    const stops = PE.getColorStops(p);
    const byCode = PE.COLOR_BY_CODE[p?.colorBy || "modeDefault"] ?? 0;
    let scalar = defaultT;
    if (byCode !== 0) {
      const lo = Number.isFinite(Number(p?.colorMin)) ? Number(p.colorMin) : 0;
      const hi = Number.isFinite(Number(p?.colorMax)) ? Number(p.colorMax) : 1;
      const invSpan = 1 / Math.max(1e-4, Math.abs(hi - lo));
      const signSpan = hi >= lo ? 1 : -1;
      const org = p?.origin || [0, 0, 0];
      let raw = defaultT;
      if (byCode === 1) raw = age / (life > 1e6 ? 3 : Math.max(0.05, life));
      else if (byCode === 2) raw = age;
      else if (byCode === 3) raw = Math.hypot(vx, vy, vz);
      else if (byCode === 4) raw = vy;
      else if (byCode === 5) raw = (Math.atan2(pz - org[2], px - org[0]) + Math.PI) / (Math.PI * 2);
      else if (byCode === 6) raw = (Math.atan2(vz, vx) + Math.PI) / (Math.PI * 2);
      else if (byCode === 7) raw = Math.hypot(px - org[0], py - org[1], pz - org[2]);
      else if (byCode === 8) raw = py;
      else if (byCode === 9) {
        const fq = Math.max(0.25, p?.curlFreq ?? 0.85);
        raw = PE.rampVNoise3(px * fq, py * fq + (time || 0) * 0.18, pz * fq);
      } else if (byCode === 10) raw = hashFJs(seed || 0, 99);
      else if (byCode === 11) {
        const an = (ang || 0) / (Math.PI * 2);
        raw = an - Math.floor(an);
      } else if (byCode === 12) raw = Math.hypot(px - camX, py - camY, pz - camZ);
      else if (byCode === 13) raw = size || 0;
      else if (byCode === 14) raw = life > 1e6 ? 3 : life;
      scalar = (raw - lo) * invSpan * signSpan;
      if (p?.colorWrap === "clamp" || !p?.colorWrap) scalar = Math.max(0, Math.min(1, scalar));
    }
    return PE.evalColorRampAt(stops, scalar, p);
  };

  // Wind field components. Type codes match the WGSL wind kernel:
  // 0 directional, 1 gust bands, 2 tornado, 3 radial (negative strength = suction).
  PE.WindTypes = ["Directional", "Gust bands", "Tornado", "Radial"];
  PE.defaultWind = function defaultWind() {
    return {
      windScale: 1,
      turbulence: 0.35,
      arrowRef: 6,
      showArrows: false,
      arrowStride: 3,
      showFloor: true,
      showDomain: false,
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

  // Default scene showcases Object Pulse Ribbons, Dual-Sphere Lightning & Static Shock, Bokeh Ribbons, and classic emitters.
  PE.defaultScene = ["bokeh-object-pulse", "tesla-sphere-lightning", "static-sphere-shock", "bokeh-stream-ribbons", "sparks", "fire"];
