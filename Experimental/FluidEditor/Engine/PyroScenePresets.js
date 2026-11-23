//============================================================================================================================================
//                                                          PYROSCENEPRESETS.JS
//               The Other Fluid Editor's Eighteen Scene Presets, Kept With Their Original Values and Translated to Fluid Studio Scenes
//============================================================================================================================================
//
// 📝 Reference only — not loaded by the editor. Since the layer stack phase 2 the 18 pyro scenes are the documents
//    Presets/pyro-*.toml (converted by script; FluidValidation checks they build the identical scenes). This file is kept because it
//    holds the other editor's original values and the calibration rules behind every translated number. Remove it once you no longer
//    need that record.
//
// The other editor (the one whose pressure warm start, gas expansion and lighting were merged in Follow-up 46) describes a
// scene as one flat block of parameters on a single box: one emitter, one blast, one obstacle, a global wind and a global
// turbulence. Fluid Studio describes a scene as nodes: a domain, emitters, force generators and colliders.
//
// The parameter blocks below are copied unchanged from that editor's presets.js, keeping only the gas, fire and look
// settings (render-debug toggles, minimaps and camera modes have no counterpart here). `translatePyroPreset` turns one block
// into a Fluid Studio scene. The two editors use different units, so every rule is a fixed ratio calibrated on the one
// scene both editors have — the camp fire — and stated next to the rule.
//
// 🔴 Forces are translated deliberately gently. The other editor relies on very strong vorticity confinement (7–11) and
//    turbulence (3–5) because its grid is coarse; carried across at face value they are exactly what makes a scene start
//    clean and then churn into noise. Turbulence becomes one curl-noise force at about a seventh of its strength, swirl
//    only becomes a vortex force when the original was a tornado, and confinement is capped.

const PyroDefaults = {gridResolution: 64, pressureIterations: 22, boundsWidth: 1.85, boundsHeight: 2.1, dynamicBounds: true, dynamicBoundsMax: 1.55, vorticityConfinement: 7.8, buoyancy: 5.8, smokeWeight: 1.15, burnRate: 1.85, burnHeat: 2.6, sootGeneration: 1.55, combustionExpansion: 4.4, coolingRate: 1.35, smokeDissipation: 0.18, velocityDamping: 0.07, turbulenceStrength: 3.5, turbulenceScale: 3.8, windX: 0.22, windZ: 0, emitterEnabled: true, emitterRate: 1, emitterRadius: 0.1, emitterHeight: 0.07, emitterUpwardVelocity: 3.2, emitterSwirl: 1.2, emitterTemperature: 3.5, emitterFuel: 2.8, emitterSmoke: 1.4, blastStrength: 9.8, blastRadius: 0.18, blastTemperature: 5.4, blastFuel: 4.6, blastSmoke: 2.6, shrapnelEnabled: true, colorPalette: 0, raymarchSteps: 96, densityExtinction: 18.5, smokeAlbedo: 0.22, fireIntensity: 5.6, phaseAnisotropy: 0.38, voxelQuantization: 0, obstacleType: 0, obstacleX: 0.5, obstacleY: 0.26, obstacleZ: 0.5, obstacleRadius: 0.16, colliderAutoMove: true, emberCount: 550, emberSize: 1, emberIntensity: 1.4, emberLifetime: 1, emberAshiness: 0, showEmbers: true};

const PyroPresetList = [
    { Key: 'ue5_pyro_default', Name: 'UE5 Niagara Pyro Plume + Blast', Blast: true,
      Summary: "Balanced hydrocarbon fireball with dynamic expanding bounds, ballistic shrapnel trails, and rolling dark soot.",
      Params: {gridResolution: 96, boundsWidth: 2.6, boundsHeight: 2.95, dynamicBounds: true, dynamicBoundsMax: 1.2, vorticityConfinement: 7.8, buoyancy: 5.8, smokeWeight: 1.15, burnRate: 1.85, burnHeat: 2.6, sootGeneration: 1.55, combustionExpansion: 4.4, coolingRate: 1.35, smokeDissipation: 0.18, turbulenceStrength: 3.5, emitterEnabled: true, emitterRate: 1, emitterRadius: 0.1, emitterUpwardVelocity: 3.2, emitterSwirl: 1.2, emitterTemperature: 3.5, emitterFuel: 2.8, emitterSmoke: 1.4, blastRadius: 0.13, shrapnelEnabled: true, colorPalette: 0, raymarchSteps: 120, densityExtinction: 18.5, smokeAlbedo: 0.22, fireIntensity: 5.6, voxelQuantization: 0, obstacleType: 0, showEmbers: true} },
    { Key: 'shrapnel_airburst', Name: 'Ordnance Shrapnel Airburst', Blast: true,
      Summary: "High-explosive airburst ejecting 6 burning ballistic shrapnel fragments that arc through the 3D volume leaving smoke streamers.",
      Params: {gridResolution: 96, boundsWidth: 3.3, boundsHeight: 3.45, dynamicBounds: true, dynamicBoundsMax: 1.2, vorticityConfinement: 8.8, buoyancy: 5.2, smokeDissipation: 0.12, emitterEnabled: false, blastStrength: 11.5, blastRadius: 0.14, blastTemperature: 6.2, shrapnelEnabled: true, colorPalette: 0, obstacleType: 0, showEmbers: true} },
    { Key: 'megaton_open_bounds', Name: 'Megaton Wide-Bounds Blast', Blast: true,
      Summary: "Extra-large 3.0x simulation bounds with dynamic surge expansion so massive explosions and salvos have full room to mushroom.",
      Params: {gridResolution: 128, boundsWidth: 3.9, boundsHeight: 4, dynamicBounds: true, dynamicBoundsMax: 1.2, vorticityConfinement: 9.2, buoyancy: 6.2, smokeWeight: 1.2, burnRate: 1.75, burnHeat: 3, sootGeneration: 1.9, combustionExpansion: 5.8, coolingRate: 1.2, smokeDissipation: 0.11, turbulenceStrength: 4.5, emitterEnabled: false, blastStrength: 13.5, blastRadius: 0.13, blastTemperature: 6.5, blastFuel: 5.4, blastSmoke: 3, shrapnelEnabled: true, colorPalette: 0, raymarchSteps: 120, densityExtinction: 20, smokeAlbedo: 0.26, fireIntensity: 6.5, voxelQuantization: 0, obstacleType: 0, showEmbers: true} },
    { Key: 'low_gpu_performance', Name: 'Low-End GPU Fast Mode', Blast: true,
      Summary: "Ultra-lightweight 24³ voxel grid (13.8K voxels) + 56 raymarch steps for reduced GPU cost; performance depends on the device.",
      Params: {gridResolution: 24, pressureIterations: 14, boundsWidth: 2, boundsHeight: 2.2, dynamicBounds: true, vorticityConfinement: 8.5, buoyancy: 6, raymarchSteps: 56, voxelQuantization: 0} },
    { Key: 'tactical_ordnance', Name: 'Tactical C4 / HE Blast Wave', Blast: true,
      Summary: "High-explosive shockwave detonation with rapid divergence expansion, blinding flash core, and billowing dust/soot cloud.",
      Params: {gridResolution: 96, boundsWidth: 3.05, boundsHeight: 3.25, dynamicBounds: true, dynamicBoundsMax: 1.2, vorticityConfinement: 9.2, buoyancy: 4.6, smokeWeight: 1.3, burnRate: 2.6, burnHeat: 3.2, sootGeneration: 2.1, combustionExpansion: 6.5, coolingRate: 1.75, smokeDissipation: 0.14, turbulenceStrength: 4.8, emitterEnabled: false, blastStrength: 12.5, blastRadius: 0.15, blastTemperature: 6.4, blastFuel: 5.2, blastSmoke: 3.2, shrapnelEnabled: true, colorPalette: 2, raymarchSteps: 120, densityExtinction: 22, smokeAlbedo: 0.34, fireIntensity: 6.8, voxelQuantization: 0, obstacleType: 0, showEmbers: true} },
    { Key: 'oil_well_inferno', Name: 'Raging Oil Well Inferno', Blast: false,
      Summary: "High-velocity pressurized fuel blowout generating a roaring flame column and dense carbonaceous black smoke.",
      Params: {boundsWidth: 1.9, boundsHeight: 2.4, dynamicBounds: true, vorticityConfinement: 8.8, buoyancy: 7.2, smokeWeight: 1.4, burnRate: 1.6, burnHeat: 3.1, sootGeneration: 2.2, combustionExpansion: 3.4, coolingRate: 1.15, smokeDissipation: 0.22, turbulenceStrength: 4.2, windX: 0.65, emitterEnabled: true, emitterRate: 1.45, emitterRadius: 0.11, emitterUpwardVelocity: 5.4, emitterSwirl: 1.8, emitterTemperature: 4.2, emitterFuel: 3.6, emitterSmoke: 1.9, colorPalette: 1, densityExtinction: 24, smokeAlbedo: 0.12, fireIntensity: 6, voxelQuantization: 0, obstacleType: 0, showEmbers: true} },
    { Key: 'obstacle_deflection', Name: 'Voxel Collider Flow Deflection', Blast: true,
      Summary: "Demonstrates a 3D solid sphere collider inside the Eulerian grid—fire and smoke split and curl around the obstacle.",
      Params: {boundsWidth: 1.85, boundsHeight: 2.1, vorticityConfinement: 8.4, buoyancy: 6.4, burnRate: 1.75, combustionExpansion: 3.8, coolingRate: 1.2, smokeDissipation: 0.18, emitterEnabled: true, emitterRate: 1.25, emitterRadius: 0.11, emitterUpwardVelocity: 4.6, colorPalette: 0, obstacleType: 1, obstacleX: 0.5, obstacleY: 0.42, obstacleZ: 0.5, obstacleRadius: 0.16} },
    { Key: 'wildfire_tornado', Name: 'Fire Tornado / Alchemical Vortex', Blast: true,
      Summary: "High-swirl fire tornado with copper-emerald blackbody radiation and intense rotational vorticity.",
      Params: {boundsWidth: 1.9, boundsHeight: 2.3, vorticityConfinement: 9.5, buoyancy: 6.8, burnRate: 1.7, combustionExpansion: 3.5, coolingRate: 1.25, smokeDissipation: 0.2, turbulenceStrength: 3.8, emitterEnabled: true, emitterRate: 1.3, emitterRadius: 0.12, emitterUpwardVelocity: 4.2, emitterSwirl: 5.5, emitterTemperature: 3.9, emitterFuel: 3.2, colorPalette: 3, obstacleType: 0} },
    { Key: 'ashfall_motes', Name: 'Ashfall / Grey Fireflies', Blast: true,
      Summary: "A cooling afterburn cloud with dense grey ash motes instead of orange sparks—use the ember sliders to tune size, brightness, lifetime, and ashiness.",
      Params: {smokeDissipation: 0.08, emitterEnabled: false, blastStrength: 7, blastRadius: 0.2, blastTemperature: 2.4, blastFuel: 1.2, blastSmoke: 3.8, colorPalette: 1, emberCount: 900, emberSize: 1.25, emberIntensity: 0.95, emberLifetime: 1.65, emberAshiness: 1, showEmbers: true} },
    { Key: 'voxelized_stylized', Name: 'Voxelized Stylized Grid', Blast: true,
      Summary: "Explicit voxelized resolution look showing discrete 3D grid cells with quantized volumetric shading.",
      Params: {vorticityConfinement: 8, voxelQuantization: 0.88} },
    { Key: 'dust_tornado', Name: 'Dust Tornado / Debris Vortex', Blast: false,
      Summary: "A cold ground-up vortex with no fuel at all: heavy dust held in a tall swirling column by strong vorticity and almost no buoyancy.",
      Params: {gridResolution: 96, boundsWidth: 2.2, boundsHeight: 3.6, dynamicBounds: false, vorticityConfinement: 11.5, buoyancy: 1.3, smokeWeight: 1.9, burnRate: 0.2, burnHeat: 0.5, sootGeneration: 0.2, combustionExpansion: 0, coolingRate: 3.2, smokeDissipation: 0.05, velocityDamping: 0.03, turbulenceStrength: 2.6, turbulenceScale: 2.4, windX: 0.12, emitterEnabled: true, emitterRate: 1.6, emitterRadius: 0.09, emitterHeight: 0.04, emitterUpwardVelocity: 2.6, emitterSwirl: 7.6, emitterTemperature: 0.3, emitterFuel: 0, emitterSmoke: 3.4, blastTemperature: 2, blastFuel: 0, blastSmoke: 5.5, colorPalette: 1, densityExtinction: 15, smokeAlbedo: 0.62, fireIntensity: 0.5, phaseAnisotropy: 0.1, obstacleType: 0, emberCount: 700, emberSize: 0.6, emberIntensity: 0.35, emberLifetime: 2.4, emberAshiness: 1, showEmbers: true} },
    { Key: 'tyre_burnout', Name: 'Tyre Burnout', Blast: false,
      Summary: "Hot rubber smoke off a spinning tyre: the ring collider sits in the grid, the source is flat on the floor, and the cloud is dragged sideways instead of climbing.",
      Params: {gridResolution: 96, boundsWidth: 2.6, boundsHeight: 1.7, dynamicBounds: false, vorticityConfinement: 6.4, buoyancy: 2.3, smokeWeight: 1.25, burnRate: 0.4, burnHeat: 0.8, sootGeneration: 3.4, combustionExpansion: 0.4, coolingRate: 2.6, smokeDissipation: 0.07, velocityDamping: 0.05, turbulenceStrength: 3.4, turbulenceScale: 4.2, windX: 1.15, windZ: -0.35, emitterEnabled: true, emitterRate: 1.9, emitterRadius: 0.13, emitterHeight: 0.02, emitterUpwardVelocity: 1.4, emitterSwirl: 3.2, emitterTemperature: 0.9, emitterFuel: 0.2, emitterSmoke: 4.2, blastTemperature: 2, blastFuel: 0, blastSmoke: 5.5, colorPalette: 1, densityExtinction: 27, smokeAlbedo: 0.17, fireIntensity: 0.8, obstacleType: 5, obstacleX: 0.5, obstacleY: 0.26, obstacleZ: 0.5, obstacleRadius: 0.17, colliderAutoMove: false, showEmbers: false} },
    { Key: 'ledge_sandfall', Name: 'Sand Falling From A Ledge', Blast: false,
      Summary: "Dry sand poured off a slab: the source sits high, smoke weight beats buoyancy so the column falls, and bright grains settle out of the base.",
      Params: {gridResolution: 96, boundsWidth: 1.9, boundsHeight: 2.8, dynamicBounds: false, vorticityConfinement: 3.6, buoyancy: 0.2, smokeWeight: 3.2, burnRate: 0.2, burnHeat: 0.5, sootGeneration: 0.2, combustionExpansion: 0, coolingRate: 3.5, smokeDissipation: 0.06, velocityDamping: 0.02, turbulenceStrength: 1.4, turbulenceScale: 5.5, windX: 0.18, emitterEnabled: true, emitterRate: 1.35, emitterRadius: 0.07, emitterHeight: 0.92, emitterUpwardVelocity: 0.5, emitterSwirl: 0.4, emitterTemperature: 0.2, emitterFuel: 0, emitterSmoke: 3.8, blastTemperature: 2, blastFuel: 0, blastSmoke: 5.5, colorPalette: 1, densityExtinction: 13, smokeAlbedo: 0.74, fireIntensity: 0.5, phaseAnisotropy: 0.22, obstacleType: 4, obstacleX: 0.5, obstacleY: 0.62, obstacleZ: 0.5, obstacleRadius: 0.2, colliderAutoMove: false, emberCount: 850, emberSize: 0.5, emberIntensity: 0.3, emberLifetime: 1.3, emberAshiness: 1, showEmbers: true} },
    { Key: 'settling_dust', Name: 'Settling Dust', Blast: false,
      Summary: "The quiet aftermath: a slow pale cloud that hangs, drifts on a light crosswind and sinks rather than rises. No fire anywhere in it.",
      Params: {gridResolution: 64, boundsWidth: 2.3, boundsHeight: 1.8, dynamicBounds: false, vorticityConfinement: 2.8, buoyancy: 0.6, smokeWeight: 2.4, burnRate: 0.2, burnHeat: 0.5, sootGeneration: 0.2, combustionExpansion: 0, coolingRate: 3.5, smokeDissipation: 0.04, velocityDamping: 0.12, turbulenceStrength: 1.2, turbulenceScale: 2.8, windX: 0.38, emitterEnabled: true, emitterRate: 0.6, emitterRadius: 0.18, emitterHeight: 0.1, emitterUpwardVelocity: 0.8, emitterSwirl: 0.6, emitterTemperature: 0.2, emitterFuel: 0, emitterSmoke: 2.4, blastTemperature: 2, blastFuel: 0, blastSmoke: 5.5, colorPalette: 1, densityExtinction: 11, smokeAlbedo: 0.68, fireIntensity: 0.5, phaseAnisotropy: 0.18, obstacleType: 0, showEmbers: false} },
    { Key: 'small_gust', Name: 'Small Gust / Drifting Haze', Blast: false,
      Summary: "A thin sheet of haze pushed across the domain by a steady crosswind and torn apart by fine turbulence. Cheap enough to leave running.",
      Params: {gridResolution: 48, boundsWidth: 2.5, boundsHeight: 1.5, dynamicBounds: false, vorticityConfinement: 4.2, buoyancy: 1.1, smokeWeight: 0.35, burnRate: 0.2, burnHeat: 0.5, sootGeneration: 0.2, combustionExpansion: 0, coolingRate: 3, smokeDissipation: 0.42, velocityDamping: 0.04, turbulenceStrength: 2.4, turbulenceScale: 6.8, windX: 1.75, windZ: 0.55, emitterEnabled: true, emitterRate: 0.45, emitterRadius: 0.12, emitterHeight: 0.22, emitterUpwardVelocity: 0.9, emitterSwirl: 0.9, emitterTemperature: 0.2, emitterFuel: 0, emitterSmoke: 1.1, blastTemperature: 2, blastFuel: 0, blastSmoke: 5.5, colorPalette: 1, raymarchSteps: 64, densityExtinction: 7, smokeAlbedo: 0.6, fireIntensity: 0.5, obstacleType: 0, showEmbers: false} },
    { Key: 'brick_fracture_dust', Name: 'Fractured Brick Burst', Blast: true,
      Summary: "Masonry breaking: a cold burst with no fuel and a great deal of smoke, throwing heavy chips off a slab. This is the shape a fracture event should spawn.",
      Params: {gridResolution: 96, boundsWidth: 2.4, boundsHeight: 2.2, dynamicBounds: true, dynamicBoundsMax: 1.2, vorticityConfinement: 6.8, buoyancy: 0.9, smokeWeight: 2.1, burnRate: 0.2, burnHeat: 0.5, sootGeneration: 0.2, combustionExpansion: 0.2, coolingRate: 3.4, smokeDissipation: 0.07, turbulenceStrength: 4.4, turbulenceScale: 3.2, emitterEnabled: false, emitterTemperature: 0.2, emitterFuel: 0, blastStrength: 8.5, blastRadius: 0.11, blastTemperature: 2, blastFuel: 0, blastSmoke: 6.5, shrapnelEnabled: true, colorPalette: 1, raymarchSteps: 110, densityExtinction: 26, smokeAlbedo: 0.56, fireIntensity: 0.5, obstacleType: 4, obstacleX: 0.5, obstacleY: 0.3, obstacleZ: 0.5, obstacleRadius: 0.22, colliderAutoMove: false, emberCount: 820, emberSize: 1.45, emberIntensity: 0.4, emberLifetime: 1.1, emberAshiness: 1, showEmbers: true} },
    { Key: 'lantern_flame', Name: 'Lantern / Oil Lamp Flame', Blast: false,
      Summary: "One small steady wick in a tight domain. The box is the smallest the editor allows, so a 96 grid spends every voxel on the flame itself.",
      Params: {gridResolution: 96, pressureIterations: 26, boundsWidth: 1, boundsHeight: 1.3, dynamicBounds: false, vorticityConfinement: 3.4, buoyancy: 3.8, smokeWeight: 0.6, burnRate: 1.15, burnHeat: 2.2, sootGeneration: 0.3, combustionExpansion: 1.6, coolingRate: 1.9, smokeDissipation: 0.82, velocityDamping: 0.1, turbulenceStrength: 0.85, turbulenceScale: 7.5, windX: 0.06, emitterEnabled: true, emitterRate: 0.4, emitterRadius: 0.05, emitterHeight: 0.02, emitterUpwardVelocity: 1.2, emitterSwirl: 0.5, emitterTemperature: 3.2, emitterFuel: 2.4, emitterSmoke: 0.3, colorPalette: 0, raymarchSteps: 128, densityExtinction: 12, smokeAlbedo: 0.3, fireIntensity: 4.2, obstacleType: 0, showEmbers: false} },
    { Key: 'camp_fire', Name: 'Camp Fire', Blast: false,
      Summary: "A wood fire in the open: a broad low base, a lazy lean on the breeze, rising sparks and a thin pale smoke rather than a black one.",
      Params: {gridResolution: 64, boundsWidth: 1.6, boundsHeight: 2.2, dynamicBounds: false, vorticityConfinement: 7.4, buoyancy: 5.2, smokeWeight: 0.85, burnRate: 1.5, burnHeat: 2.4, sootGeneration: 1.05, combustionExpansion: 2.8, coolingRate: 1.55, smokeDissipation: 0.34, velocityDamping: 0.06, turbulenceStrength: 3, turbulenceScale: 4.6, windX: 0.32, emitterEnabled: true, emitterRate: 0.95, emitterRadius: 0.15, emitterHeight: 0.05, emitterUpwardVelocity: 2.5, emitterSwirl: 1.7, emitterTemperature: 3.1, emitterFuel: 2.3, emitterSmoke: 0.95, colorPalette: 0, densityExtinction: 14, smokeAlbedo: 0.38, fireIntensity: 4.8, obstacleType: 0, emberCount: 430, emberSize: 0.8, emberIntensity: 1.6, emberLifetime: 1.9, emberAshiness: 0.25, showEmbers: true} },
];

// Icon for each preset card, from the existing glyph set.
const PyroIcons = {
    ue5_pyro_default: 'burst', shrapnel_airburst: 'sparks', megaton_open_bounds: 'shockwave', low_gpu_performance: 'gear',
    tactical_ordnance: 'dburst', oil_well_inferno: 'flame', obstacle_deflection: 'collider', wildfire_tornado: 'vortex',
    ashfall_motes: 'snow', voxelized_stylized: 'cube', dust_tornado: 'dust', tyre_burnout: 'tyre', ledge_sandfall: 'rain',
    settling_dust: 'mist', small_gust: 'wind', brick_fracture_dust: 'debris', lantern_flame: 'fireflies', camp_fire: 'flame',
};

// The other editor's six colour palettes as Fluid Studio fire colours (ember → flame → core).
const PyroPalettes = [
    { FireEmber: '#ff2a05', FireFlame: '#ff8a16', FireCore: '#fff3cf' },   // Physical Planckian blackbody
    { FireEmber: '#b3120a', FireFlame: '#ff6a00', FireCore: '#ffd27a' },   // Cinema napalm / rich hydrocarbon
    { FireEmber: '#ff5a1a', FireFlame: '#ffc070', FireCore: '#ffffff' },   // Thermobaric white-hot ordnance
    { FireEmber: '#7a3a0c', FireFlame: '#2fd17a', FireCore: '#d8ffe6' },   // Wildfire / copper emerald flame
    { FireEmber: '#1f4bff', FireFlame: '#58a6ff', FireCore: '#e6f2ff' },   // Cherenkov blue plasma core
    { FireEmber: '#5b0bb5', FireFlame: '#c84bff', FireCore: '#f6e3ff' },   // Arcane void magenta
];

const Round = (number, places = 2) => Math.round(number * 10 ** places) / 10 ** places;
const Grey = (albedo, tint = [1, 1, 1]) => '#' + tint.map((channel) => Math.round(Math.min(1, albedo * channel * 1.1) * 255).toString(16).padStart(2, '0')).join('');

// Dust and sand presets have no fuel: their smoke is tinted as dust rather than soot.
const DustTint = [1.0, 0.9, 0.74];

// Obstacles: Fluid Studio colliders are spheres, so the other editor's cylinder, slab and tyre ring are built from spheres.
function translateObstacle(params, size)
{
    const centre = [(params.obstacleX - 0.5) * size[0], (params.obstacleY - 0.5) * size[1], (params.obstacleZ - 0.5) * size[2]];
    const radius = params.obstacleRadius * size[0] * 0.5;
    const at = (dx, dy, dz, r = radius) => ({ Position: [Round(centre[0] + dx), Round(centre[1] + dy), Round(centre[2] + dz)], Radius: Round(r) });
    switch (params.obstacleType)
    {
        case 1: return [{ Name: 'Sphere Obstacle', ...at(0, 0, 0) }];
        case 2: return [-1, 0, 1].map((step, index) => ({ Name: `Column ${index + 1}`, ...at(0, step * radius * 1.4, 0, radius * 0.8) }));
        case 3: return [-1, 0, 1].map((step, index) => ({ Name: `Crossbar ${index + 1}`, ...at(step * radius * 1.4, 0, 0, radius * 0.8) }));
        case 4: return [[-1, -1], [1, -1], [-1, 1], [1, 1]].map(([x, z], index) => ({ Name: `Slab ${index + 1}`, ...at(x * radius * 0.7, 0, z * radius * 0.7, radius * 0.75) }));
        case 5: return [0, 1, 2, 3].map((index) => ({ Name: `Tyre ${index + 1}`, ...at(Math.cos(index * Math.PI / 2) * radius, 0, Math.sin(index * Math.PI / 2) * radius, radius * 0.45) }));
        default: return [];
    }
}

/** One preset block → a Fluid Studio scene preset (same schema as ScenePresets). */
export function translatePyroPreset(entry)
{
    const p = { ...PyroDefaults, ...entry.Params };
    // Size: the other editor's box is in half-scale units — its camp fire is 1.6 × 2.2, Fluid Studio's is 3 × 5 m.
    const growth = p.dynamicBounds && entry.Blast ? (p.dynamicBoundsMax || 1.2) : 1.0;
    const size = [Round(p.boundsWidth * 2 * growth), Round(p.boundsHeight * 2 * growth), Round(p.boundsWidth * 2 * growth)];
    const cells = Math.min(96, p.gridResolution);
    const longest = Math.max(...size);
    const resolution = size.map((extent) => Math.max(16, Math.round(extent / longest * cells)));
    const floor = -size[1] / 2;
    const dusty = p.emitterFuel <= 0.01 && (entry.Blast ? p.blastFuel <= 0.01 : true);
    const scaledDown = (value, ratio) => Round(value * ratio, 3);

    const domain = {
        Resolution: resolution, Size: size,
        Buoyancy: scaledDown(p.buoyancy, 0.25),                          // 5.2 → 1.3 (camp fire)
        Gravity: scaledDown(p.smokeWeight, 0.2),                         // smoke weight pulls dense smoke down
        AmbientTemperature: 0,
        Cooling: Round(p.coolingRate * 0.9),
        Vorticity: Round(Math.min(0.35, p.vorticityConfinement * 0.03)), // 7.4 → 0.22; capped so it cannot churn
        Dissipation: Round(1 - p.smokeDissipation / 60, 4),              // per second → per 60 Hz step
        VelocityDissipation: Round(1 - Math.max(0.03, p.velocityDamping) / 60, 4),
        Combustion: Round(p.burnRate),
        Expansion: Round(Math.min(0.8, p.combustionExpansion * 0.15)),   // 4.4 → 0.66
        PressureIterations: p.pressureIterations ? Math.max(24, p.pressureIterations + 8) : 36,
    };
    if (entry.Blast) domain.SceneLoop = 8;

    const look = {
        Albedo: dusty ? Grey(p.smokeAlbedo, DustTint) : Grey(p.smokeAlbedo),
        Absorption: Round(p.densityExtinction * 0.22),                   // 18.5 → 4.1
        Anisotropy: Round(p.phaseAnisotropy),
        FireIntensity: Round(p.fireIntensity * 1.1),
        ...PyroPalettes[p.colorPalette] || PyroPalettes[0],
    };
    if (p.raymarchSteps && p.raymarchSteps < 80) look.Steps = p.raymarchSteps;
    if (p.voxelQuantization > 0 || entry.Key === 'voxelized_stylized') look.Filter = 'Voxel';

    const emitters = [], forces = [];
    if (p.emitterEnabled)
    {
        emitters.push({
            Name: dusty ? 'Dust Source' : 'Fire Source', Shape: 'Disc',
            Position: [0, Round(floor + Math.max(0.12, p.emitterHeight * size[1])), 0],
            Radius: Round(p.emitterRadius * size[0] * 1.3),
            Density: Round(p.emitterSmoke * p.emitterRate * 0.35),
            Temperature: Round(p.emitterTemperature * 1.6),
            Fuel: Round(p.emitterFuel * 0.55),
            Velocity: [0, Round(p.emitterUpwardVelocity * 0.32), 0],
            Jitter: 0.4,
            ...(p.showEmbers && !dusty ? { Debris: 0.12, DebrisSpeed: 1.4, DebrisKind: 'Ember' } : {}),
        });
        // Swirl only becomes a force for the genuine tornado scenes; elsewhere it was flavour the jitter already gives.
        if (p.emitterSwirl >= 3)
            forces.push({ Type: 'VortexColumn', Name: 'Swirl', Position: [0, 0, 0], Strength: Round(p.emitterSwirl * 0.45), Radius: Round(size[0] * 0.3), Falloff: 'Gaussian', Frequency: 0.6, Secondary: 0.4 });
    }
    if (entry.Blast)
    {
        const centre = [0, Round(floor + size[1] * 0.18), 0];
        emitters.push({
            Name: 'Charge', Shape: 'Sphere', Position: centre,
            Radius: Round(p.blastRadius * size[0] * 1.6),
            Density: Round(p.blastSmoke * 0.45), Temperature: Round(p.blastTemperature * 1.1), Fuel: Round(p.blastFuel * 0.3),
            Velocity: [0, 0, 0], Jitter: 0.8, StartTime: 0.2, Duration: 0.35, Fade: 0.08,
            ...(p.shrapnelEnabled ? { Debris: 1.0, DebrisSpeed: Round(p.blastStrength * 0.9), DebrisKind: dusty ? 'Rubble' : 'Ember' } : {}),
        });
        forces.push({ Type: 'RadialBurst', Name: 'Blast', Position: centre, Strength: Round(p.blastStrength * 3.4), Radius: Round(size[0] * 0.35), Frequency: 6, Secondary: 0.12, Tertiary: 0.2, StartTime: 0.2, Duration: 0.45, Fade: 0.05 });
    }
    const wind = Math.hypot(p.windX, p.windZ);
    if (wind > 0.05)
        forces.push({ Type: 'DirectionalWind', Name: 'Breeze', Position: [0, 0, 0], Strength: Round(wind), Direction: [Round(p.windX / wind), 0, Round(p.windZ / wind)], Falloff: 'Constant', Frequency: 0.3, Secondary: 0.4 });
    if (p.turbulenceStrength > 0.2)
        forces.push({ Type: 'CurlNoiseTurbulence', Name: 'Turbulence', Position: [0, Round(floor + size[1] * 0.35), 0], Strength: Round(p.turbulenceStrength * 0.14), Radius: Round(size[0] * 0.5), Falloff: 'Gaussian', Frequency: Round(p.turbulenceScale * 0.7), Secondary: 0.8 });
    // A ceiling brake keeps tall columns from piling up against the roof, as Fluid Studio's own presets do.
    forces.push({ Type: 'DragBaffle', Name: 'Ceiling Brake', Position: [0, Round(size[1] / 2 - size[1] * 0.08), 0], Strength: 1.2, Radius: Round(size[0] * 0.6), Falloff: 'Plateau' });

    const visualizers = {};
    if (p.shrapnelEnabled && entry.Blast || p.showEmbers)
    {
        const ash = p.emberAshiness || 0;
        visualizers.Debris = { Visible: true, Count: Math.min(2048, Math.max(512, Math.round((p.emberCount || 550) * 2))), Lifetime: Round(2.5 * (p.emberLifetime || 1)), Size: Round(2.2 * (p.emberSize || 1)), Drag: dusty ? 1.2 : 2.2, Bounce: 0.2, Glow: Round((p.emberIntensity || 1.4) * (1 - ash) + 0.1) };
    }

    return {
        Id: 'pyro-' + entry.Key.replace(/_/g, '-'),
        Name: entry.Name,
        Icon: PyroIcons[entry.Key] || 'flame',
        Summary: entry.Summary,
        Family: 'Pyro',
        Domain: domain,
        Boundary: wind > 1 ? 'OpenAll' : 'OpenTop',
        Look: look,
        Visualizers: visualizers,
        Emitters: emitters,
        Forces: forces,
        Colliders: translateObstacle(p, size),
    };
}

export const PyroScenePresets = Object.freeze(PyroPresetList.map(translatePyroPreset));
