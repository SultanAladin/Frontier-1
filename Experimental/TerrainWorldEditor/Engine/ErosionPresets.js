/**
 * ErosionPresets.js — Geological Landscape Presets for the Physically Based
 * Multi-Process Erosion Engine.
 * ===========================================================================
 * All 19 `c7egoist/Frontier` landscapes, re-engineered for our coupled
 * continuum solver + Thin-Slab SDF regions, plus dedicated pitting / canyon-wind
 * showcases and shared-weather simulation groups (2+ terrains evolving under
 * ONE climate/pattern/seasonal cycle).
 *
 * Each preset bundles:
 *   - 2D world-mask polygon (paintable land bounds)
 *   - composable TerrainLayerStack landforms (multifractal / ridged / voronoi /
 *     billow / terrace / spline)
 *   - Seasonal climate configuration (regime, latitude, calendar)
 *   - Erosion physics tuning (all six processes)
 *   - Gaea texturing recipe (SatMap ramp + surface stamps)
 */

import { CreateTerrainLayer } from './TerrainLayerStack.js';
import { EROSION_DEFAULTS, CreateClimateGroup, CreateErosionState, BakeErosionYears, ApplyErosionToRegion, StepErosionDay, SampleClimate, AdvanceClimateDay } from './ErosionPhysicsEngine.js';

/*==========================================================================================================================================
                                                            PRESET REGISTRY
==========================================================================================================================================*/
export const EROSION_PRESETS = [
    {
        id: 'icelandic-highlands',
        name: 'Icelandic Glacial Highlands',
        subtitle: 'Columnar basalt, rhyolite bands, summit ice caps & black-sand sandur outwash',
        climate: { regimeId: 'subarctic-continental', latitudeDeg: 65, startDayOfYear: 120 },
        erosion: { frostSegregationRate: 0.03, freezeThawRate: 0.042, snowlineM: 150, degreeDayMelt: 0.005, bedrockErodibility: 3.2e-7, carbonateShare: 0.06 },
        texture: { rampId: 'icelandMoss', stampIds: ['hexagonalBasalt', 'talusScree', 'glacialStriations'] },
        mask: [[-1520, -980], [-780, -1140], [-210, -890], [-90, -420], [-380, -160], [-240, 180], [-680, 340], [-1320, 190], [-1640, -340]],
        layers: [
            { type: 'multifractal', name: 'Basaltic Plateau Base', amplitude: 175, scale: 2.1, octaves: 6, gain: 0.54, warp: 0.35, seed: 4029 },
            { type: 'terrace', name: 'Columnar Basalt Terraces', bedCount: 12, strength: 0.68, dipDeg: 3, strikeDeg: 20, seed: 5113 },
            { type: 'ridged', name: 'Rhyolite Arêtes', amplitude: 120, scale: 2.6, sharpness: 0.78, azimuth: 34, warp: 0.36, seed: 6007 }
        ]
    },
    {
        id: 'himalayan-karakoram',
        name: 'Himalayan Snow Mountains',
        subtitle: 'Asymmetric monarch massif, dendritic snow arêtes & U-shaped glacier gorges',
        climate: { regimeId: 'alpine-continental', latitudeDeg: 34, startDayOfYear: 60 },
        erosion: { snowlineM: 420, degreeDayMelt: 0.0042, iceFlowK: 0.02, glacialAbrasionK: 0.0026, rockfallRate: 0.85, cliffBaseAngleDeg: 56, bedrockErodibility: 3.4e-7 },
        texture: { rampId: 'alpineSnow', stampIds: ['glacialStriations', 'talusScree'] },
        mask: [[-1600, -1100], [-200, -1250], [980, -820], [1420, -180], [1180, 620], [420, 1080], [-520, 980], [-1380, 420]],
        layers: [
            { type: 'multifractal', name: 'Gneiss Dome Core', amplitude: 235, scale: 1.9, octaves: 7, gain: 0.5, warp: 0.42, seed: 4029 },
            { type: 'ridged', name: 'Karakoram Arête Spine', amplitude: 190, scale: 2.2, sharpness: 0.82, azimuth: -25, warp: 0.44, seed: 5113 },
            { type: 'billow', name: 'Glacial Moraine Lobes', amplitude: 55, scale: 3.6, duneMix: 0.15, azimuth: 70, seed: 6007 }
        ]
    },
    {
        id: 'layered-plateau',
        name: 'Stratified Canyon Plateau',
        subtitle: 'Utah-style hard sandstone caprocks & soft shale mesas incised by rain & wind',
        climate: { regimeId: 'semi-arid-high-desert', latitudeDeg: 38, startDayOfYear: 150 },
        erosion: { strataStrength: 0.86, strataFreqPerM: 0.38, strataDipDeg: 6, windAbrasionK: 0.0021, undercutGain: 2.9, carbonateShare: 0.24, bedrockErodibility: 3.6e-7 },
        texture: { rampId: 'canyonStrata', stampIds: ['crossBeddedSandstone', 'crackedPlaya'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Colorado Plateau Warp', amplitude: 95, scale: 2.4, octaves: 5, gain: 0.5, warp: 0.28, seed: 4029 },
            { type: 'terrace', name: 'Sandstone–Shale Strata', bedCount: 14, strength: 0.85, dipDeg: 6, strikeDeg: 25, seed: 5113 },
            { type: 'voronoi', name: 'Mesa Blocks', variant: 0, amplitude: 120, scale: 3.1, jitter: 0.82, sharpness: 0.74, seed: 6007 }
        ]
    },
    {
        id: 'alpine-divide',
        name: 'Tectonic Alpine Divide',
        subtitle: 'High folded thrust ridge with snow-dusted summits, valleys & alluvial fans',
        climate: { regimeId: 'alpine-continental', latitudeDeg: 46, startDayOfYear: 100 },
        erosion: { rockfallRate: 0.75, talusCoarseReposeDeg: 36, bedrockErodibility: 3.0e-7, snowlineM: 250 },
        texture: { rampId: 'alpineSnow', stampIds: ['talusScree', 'glacialStriations'] },
        mask: [[-1560, -980], [-320, -1180], [1240, -880], [1560, -120], [1180, 760], [120, 1120], [-1080, 840], [-1660, 60]],
        layers: [
            { type: 'ridged', name: 'Thrust Ridge System', amplitude: 195, scale: 2.3, sharpness: 0.75, azimuth: 18, warp: 0.4, seed: 4029 },
            { type: 'multifractal', name: 'Folded Back-Ranges', amplitude: 130, scale: 2.7, octaves: 6, gain: 0.52, warp: 0.33, seed: 5113 },
            { type: 'spline', name: 'Glacial Valley', splineMode: 'canyon', width: 120, amount: 40, bankSmooth: 0.62, meander: 0.2, seed: 6007, points: [[-1150, -420], [-520, -180], [180, 60], [880, 320]] }
        ]
    },
    {
        id: 'fault-escarpment',
        name: 'Tilted Fault Escarpment',
        subtitle: 'Dip-slope mountain face & triangular facets above a sedimentary bajada',
        climate: { regimeId: 'semi-arid-high-desert', latitudeDeg: 36, startDayOfYear: 200 },
        erosion: { rockfallRate: 0.9, cliffBaseAngleDeg: 55, talusCoarseReposeDeg: 35, thermalFatigueRate: 0.022, windAbrasionK: 0.0018 },
        texture: { rampId: 'toxicOchre', stampIds: ['talusScree', 'crossBeddedSandstone'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Sedimentary Basin Fill', amplitude: 70, scale: 3.0, octaves: 5, gain: 0.48, warp: 0.25, seed: 4029 },
            { type: 'terrace', name: 'Dip-Slope Strata', bedCount: 10, strength: 0.8, dipDeg: 22, strikeDeg: 95, seed: 5113 },
            { type: 'voronoi', name: 'Fault Scarp Facets', variant: 1, amplitude: 150, scale: 2.6, jitter: 0.75, sharpness: 0.8, seed: 6007 }
        ]
    },
    {
        id: 'volcanic-caldera',
        name: 'Glaciated Stratocone & Rim',
        subtitle: 'Explosive caldera rim, radial ravines & nested summit craters',
        climate: { regimeId: 'alpine-continental', latitudeDeg: 44, startDayOfYear: 80 },
        erosion: { snowlineM: 300, freezeThawRate: 0.045, bedrockErodibility: 3.4e-7, rockfallRate: 0.8, carbonateShare: 0.04 },
        texture: { rampId: 'basaltNight', stampIds: ['hexagonalBasalt', 'talusScree'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'voronoi', name: 'Caldera Rim', variant: 2, amplitude: 210, scale: 2.4, jitter: 0.55, sharpness: 0.85, seed: 4029 },
            { type: 'ridged', name: 'Radial Ravines', amplitude: 110, scale: 3.2, sharpness: 0.7, azimuth: 62, warp: 0.5, seed: 5113 },
            { type: 'multifractal', name: 'Lava Flow Roughness', amplitude: 55, scale: 3.6, octaves: 6, gain: 0.55, warp: 0.3, seed: 6007 }
        ]
    },
    {
        id: 'badlands-domes',
        name: 'Wind-Carved Badlands & Dunes',
        subtitle: 'Bentonic clay domes, hoodoos & migrating dune sheets between them',
        climate: { regimeId: 'arid-desert', latitudeDeg: 33, startDayOfYear: 170 },
        erosion: { windAbrasionK: 0.0026, thermalFatigueRate: 0.026, haloclastyK: 0.0042, saltationThresholdMs: 4.6, slipfaceDeg: 34, sandSupply: 1.35, hydrolysisK: 0.006 },
        texture: { rampId: 'satelliteDesert', stampIds: ['aeolianRipples', 'crackedPlaya', 'tafoniPits'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Clay Hills Base', amplitude: 110, scale: 2.9, octaves: 5, gain: 0.5, warp: 0.3, seed: 4029 },
            { type: 'voronoi', name: 'Eroded Clay Domes', variant: 3, amplitude: 95, scale: 3.4, jitter: 0.88, sharpness: 0.68, seed: 5113 },
            { type: 'billow', name: 'Dune Sheets', amplitude: 38, scale: 4.2, duneMix: 0.65, azimuth: 45, seed: 6007 }
        ]
    },
    {
        id: 'coastal-massif',
        name: 'Fjord & Coastal Massif',
        subtitle: 'Drowned glacial fjords, sea cliffs & hanging valleys on a granite coast',
        climate: { regimeId: 'temperate-oceanic', latitudeDeg: 62, startDayOfYear: 140 },
        erosion: { frostSegregationRate: 0.028, degreeDayMelt: 0.005, iceFlowK: 0.018, bedrockErodibility: 3.2e-7, rockfallRate: 0.8 },
        texture: { rampId: 'icelandMoss', stampIds: ['glacialStriations', 'hexagonalBasalt'] },
        mask: [[-1620, -760], [-760, -1120], [-60, -820], [220, -260], [560, -380], [860, 60], [520, 620], [-180, 880], [-1040, 760], [-1620, 180]],
        layers: [
            { type: 'multifractal', name: 'Granite Massif', amplitude: 215, scale: 2.0, octaves: 6, gain: 0.52, warp: 0.38, seed: 4029 },
            { type: 'ridged', name: 'Fjord Carve Spine', amplitude: 130, scale: 2.7, sharpness: 0.8, azimuth: 110, warp: 0.42, seed: 5113 },
            { type: 'spline', name: 'Drowned Fjord', splineMode: 'canyon', width: 150, amount: 60, bankSmooth: 0.55, meander: 0.12, seed: 6007, points: [[-1300, -320], [-700, -420], [-150, -300], [320, -120]] }
        ]
    },
    {
        id: 'grand-canyon',
        name: 'Grand Canyon Gorge',
        subtitle: 'Incised meander gorge with side canyons, temples & redwall alcoves',
        climate: { regimeId: 'arid-desert', latitudeDeg: 36, startDayOfYear: 15 },
        erosion: { bedrockErodibility: 4.6e-7, strataStrength: 0.88, strataFreqPerM: 0.34, strataDipDeg: 3, windAbrasionK: 0.0022, undercutGain: 3.1, carbonateShare: 0.3, depositionRate: 0.5 },
        texture: { rampId: 'canyonStrata', stampIds: ['crossBeddedSandstone', 'karstGrikes'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Kaibab Plateau', amplitude: 130, scale: 2.2, octaves: 6, gain: 0.48, warp: 0.3, seed: 4029 },
            { type: 'terrace', name: 'Redwall Strata Staircase', bedCount: 16, strength: 0.9, dipDeg: 2, strikeDeg: 15, seed: 5113 },
            { type: 'spline', name: 'Colorado Meander Gorge', splineMode: 'river', width: 95, amount: 85, bankSmooth: 0.72, meander: 0.42, seed: 6007, points: [[-1350, -520], [-760, -120], [-240, -360], [260, -60], [820, -320], [1320, 60]] }
        ]
    },
    {
        id: 'studlagil-basalt',
        name: 'Stuðlagil Basalt Canyon',
        subtitle: 'Jökulsá canyon of towering hexagonal basalt colonnades & teal pools',
        climate: { regimeId: 'subarctic-continental', latitudeDeg: 65, startDayOfYear: 100 },
        erosion: { bedrockErodibility: 4.2e-7, strataStrength: 0.78, strataFreqPerM: 0.55, frostSegregationRate: 0.032, freezeThawRate: 0.044, carbonateShare: 0.05 },
        texture: { rampId: 'basaltNight', stampIds: ['hexagonalBasalt', 'tafoniPits'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Basalt Lava Field', amplitude: 105, scale: 2.5, octaves: 6, gain: 0.5, warp: 0.3, seed: 4029 },
            { type: 'terrace', name: 'Colonnade Layers', bedCount: 18, strength: 0.88, dipDeg: 1, strikeDeg: 40, seed: 5113 },
            { type: 'spline', name: 'Jökulsá Gorge', splineMode: 'canyon', width: 110, amount: 75, bankSmooth: 0.5, meander: 0.18, seed: 6007, points: [[-1300, -620], [-680, -380], [-60, -420], [520, -180], [1180, -260]] }
        ]
    },
    {
        id: 'fjadrargljufur-palagonite',
        name: 'Fjaðrárgljúfur Canyon',
        subtitle: 'Twisting mossy palagonite serpentine gorge with potholes & plunge pools',
        climate: { regimeId: 'temperate-oceanic', latitudeDeg: 64, startDayOfYear: 130 },
        erosion: { bedrockErodibility: 5.2e-7, soilErodibility: 5.0e-6, hydrolysisK: 0.006, frostSegregationRate: 0.026, carbonateShare: 0.08 },
        texture: { rampId: 'icelandMoss', stampIds: ['tafoniPits', 'karstGrikes'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Palagonite Ridgeland', amplitude: 120, scale: 2.6, octaves: 6, gain: 0.52, warp: 0.36, seed: 4029 },
            { type: 'spline', name: 'Serpentine Gorge', splineMode: 'river', width: 70, amount: 68, bankSmooth: 0.55, meander: 0.55, seed: 5113, points: [[-1150, -720], [-620, -180], [-180, -520], [260, -40], [640, -420], [1150, -60]] }
        ]
    },
    {
        id: 'namib-erg-dunes',
        name: 'Namib Sand Sea & Wind',
        subtitle: 'Star & linear dune sea with gravel plains, inselbergs & wind scour',
        climate: { regimeId: 'arid-desert', latitudeDeg: -24, startDayOfYear: 300 },
        erosion: { sandSupply: 1.6, saltationThresholdMs: 4.4, slipfaceDeg: 34, windAbrasionK: 0.0018, thermalFatigueRate: 0.024, haloclastyK: 0.004, hydrolysisK: 0.0016, baseflowK: 0.05 },
        texture: { rampId: 'satelliteDesert', stampIds: ['aeolianRipples', 'tafoniPits'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'billow', name: 'Erg Dune Sea', amplitude: 82, scale: 3.4, duneMix: 0.75, azimuth: 225, seed: 4029 },
            { type: 'multifractal', name: 'Gravel Plain', amplitude: 35, scale: 3.8, octaves: 5, gain: 0.46, warp: 0.2, seed: 5113 },
            { type: 'voronoi', name: 'Inselbergs', variant: 3, amplitude: 70, scale: 4.4, jitter: 0.9, sharpness: 0.7, seed: 6007 }
        ]
    },
    {
        id: 'sahara-tassili-erg',
        name: 'Sahara Grand Erg Dunes',
        subtitle: 'Transverse mega-dunes, hamada pavement & tassili sandstone arches',
        climate: { regimeId: 'arid-desert', latitudeDeg: 25, startDayOfYear: 190 },
        erosion: { sandSupply: 1.5, windAbrasionK: 0.0024, undercutGain: 2.8, thermalFatigueRate: 0.028, strataStrength: 0.72, haloclastyK: 0.0045 },
        texture: { rampId: 'satelliteDesert', stampIds: ['crossBeddedSandstone', 'aeolianRipples'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'billow', name: 'Grand Erg Transverse Dunes', amplitude: 95, scale: 3.1, duneMix: 0.7, azimuth: 40, seed: 4029 },
            { type: 'terrace', name: 'Tassili Sandstone', bedCount: 8, strength: 0.72, dipDeg: 4, strikeDeg: 60, seed: 5113 },
            { type: 'voronoi', name: 'Rock Arch Towers', variant: 1, amplitude: 105, scale: 3.9, jitter: 0.85, sharpness: 0.78, seed: 6007 }
        ]
    },
    {
        id: 'rugged-desert-crags',
        name: 'Wadi Rum Rugged Desert',
        subtitle: 'Jabal sandstone crags, mushroom buttes & braided wadi floors',
        climate: { regimeId: 'arid-desert', latitudeDeg: 30, startDayOfYear: 220 },
        erosion: { windAbrasionK: 0.0026, undercutGain: 3.2, thermalFatigueRate: 0.027, rockfallRate: 0.85, cliffBaseAngleDeg: 57, strataStrength: 0.78 },
        texture: { rampId: 'toxicOchre', stampIds: ['crossBeddedSandstone', 'talusScree', 'braidedAlluvium'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Sandstone Massifs', amplitude: 165, scale: 2.4, octaves: 6, gain: 0.5, warp: 0.4, seed: 4029 },
            { type: 'voronoi', name: 'Isolated Jabal Buttes', variant: 0, amplitude: 130, scale: 3.5, jitter: 0.9, sharpness: 0.8, seed: 5113 },
            { type: 'terrace', name: 'Crag Strata', bedCount: 9, strength: 0.78, dipDeg: 5, strikeDeg: 100, seed: 6007 }
        ]
    },
    {
        id: 'desert-oasis-erg',
        name: 'Desert Dunes & Oasis',
        subtitle: 'Dune sea with a spring-fed palm oasis in a deflation hollow',
        climate: { regimeId: 'arid-desert', latitudeDeg: 27, startDayOfYear: 240 },
        erosion: { sandSupply: 1.45, saltationThresholdMs: 4.6, baseflowK: 0.6, hydraulicConductivity: 4.0e-6, hydrolysisK: 0.003 },
        texture: { rampId: 'satelliteDesert', stampIds: ['aeolianRipples', 'braidedAlluvium'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'billow', name: 'Oasis Erg', amplitude: 75, scale: 3.6, duneMix: 0.72, azimuth: 65, seed: 4029 },
            { type: 'multifractal', name: 'Oasis Hollow', amplitude: 42, scale: 4.2, octaves: 5, gain: 0.44, warp: 0.18, seed: 5113 },
            { type: 'spline', name: 'Oasis Spring Channel', splineMode: 'river', width: 60, amount: 18, bankSmooth: 0.7, meander: 0.35, seed: 6007, points: [[-260, -180], [-80, -40], [60, 90], [220, 210]] }
        ]
    },
    {
        id: 'icelandic-green-highlands',
        name: 'Icelandic Green Highlands',
        subtitle: 'Mossy hyaloclastite ridges, braided glacial rivers & steam-weathered scars',
        climate: { regimeId: 'temperate-oceanic', latitudeDeg: 64, startDayOfYear: 160 },
        erosion: { hydrolysisK: 0.007, frostSegregationRate: 0.024, bedrockErodibility: 3.8e-7, soilErodibility: 5.4e-6, baseflowK: 0.5, snowlineM: 180 },
        texture: { rampId: 'icelandMoss', stampIds: ['braidedAlluvium', 'hexagonalBasalt'] },
        mask: [[-1520, -980], [-780, -1140], [-210, -890], [-90, -420], [-380, -160], [-240, 180], [-680, 340], [-1320, 190], [-1640, -340]],
        layers: [
            { type: 'multifractal', name: 'Hyaloclastite Highlands', amplitude: 150, scale: 2.2, octaves: 6, gain: 0.52, warp: 0.34, seed: 4029 },
            { type: 'billow', name: 'Mossy Rounded Ridges', amplitude: 48, scale: 3.2, duneMix: 0.1, azimuth: 130, seed: 5113 },
            { type: 'spline', name: 'Braided Sandur', splineMode: 'river', width: 130, amount: 30, bankSmooth: 0.72, meander: 0.32, seed: 6007, points: [[-1100, -560], [-560, -320], [-120, -360], [320, -120]] }
        ]
    },
    {
        id: 'lunar-mare-craters',
        name: 'Moon Mare & Craters',
        subtitle: 'Impact crater field on basalt mare with regolithgardens & central peaks',
        climate: { regimeId: 'polar-desert', latitudeDeg: 0, startDayOfYear: 1, temperatureOffsetC: -20, precipitationScale: 0.0, windScale: 0.05 },
        erosion: { thermalFatigueRate: 0.034, freezeThawRate: 0.0, frostSegregationRate: 0.0, haloclastyK: 0.0, hydrolysisK: 0.0, dissolutionK: 0.0, rockfallRate: 0.95, cliffBaseAngleDeg: 60, bedrockErodibility: 2.0e-7, baseflowK: 0.0 },
        texture: { rampId: 'moonRegolith', stampIds: ['tafoniPits', 'talusScree'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Mare Basalt Plain', amplitude: 60, scale: 2.6, octaves: 6, gain: 0.5, warp: 0.25, seed: 4029 },
            { type: 'voronoi', name: 'Impact Crater Field', variant: 2, amplitude: 120, scale: 2.8, jitter: 0.95, sharpness: 0.75, seed: 5113 },
            { type: 'billow', name: 'Regolith Blanket', amplitude: 22, scale: 5.0, duneMix: 0.05, azimuth: 0, seed: 6007 }
        ]
    },
    {
        id: 'yardang-wind-corridor',
        name: 'Aeolian Yardang Desert',
        subtitle: 'Wind-fluted yardang corridors, deflation corridors & barchan trains',
        climate: { regimeId: 'arid-desert', latitudeDeg: 32, startDayOfYear: 120 },
        erosion: { windAbrasionK: 0.0034, venturiGain: 1.9, undercutGain: 3.4, undercutZoneM: 3.2, saltationThresholdMs: 4.2, sandSupply: 1.3, strataStrength: 0.7, strataFreqPerM: 0.5, thermalFatigueRate: 0.026 },
        texture: { rampId: 'toxicOchre', stampIds: ['crossBeddedSandstone', 'aeolianRipples'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Taklamakan Corridor Floor', amplitude: 85, scale: 2.8, octaves: 5, gain: 0.48, warp: 0.32, seed: 4029 },
            { type: 'billow', name: 'Barchan Trains', amplitude: 55, scale: 3.9, duneMix: 0.8, azimuth: 115, seed: 5113 },
            { type: 'terrace', name: 'Yardang Strata', bedCount: 11, strength: 0.7, dipDeg: 8, strikeDeg: 115, seed: 6007 }
        ]
    },
    {
        id: 'glacial-lake-watershed',
        name: 'Glacial Lakes & River',
        subtitle: 'Paternoster lake chain, moraine dams & a braided meltwater river',
        climate: { regimeId: 'alpine-continental', latitudeDeg: 50, startDayOfYear: 70 },
        erosion: { snowlineM: 210, degreeDayMelt: 0.0055, iceFlowK: 0.016, glacialAbrasionK: 0.0022, bedrockErodibility: 3.4e-7, depositionRate: 0.55, baseflowK: 0.55 },
        texture: { rampId: 'alpineSnow', stampIds: ['glacialStriations', 'braidedAlluvium'] },
        mask: [[-1560, -980], [-320, -1180], [1240, -880], [1560, -120], [1180, 760], [120, 1120], [-1080, 840], [-1660, 60]],
        layers: [
            { type: 'multifractal', name: 'Glacier Valley Head', amplitude: 185, scale: 2.1, octaves: 6, gain: 0.52, warp: 0.36, seed: 4029 },
            { type: 'billow', name: 'Moraine Ridges', amplitude: 60, scale: 3.8, duneMix: 0.12, azimuth: 250, seed: 5113 },
            { type: 'spline', name: 'Paternoster Lake Chain', splineMode: 'lake', width: 170, amount: 42, bankSmooth: 0.6, meander: 0.16, seed: 6007, points: [[-980, -520], [-520, -300], [-80, -320], [380, -80], [860, -140]] }
        ]
    },

    /* ============================ NEW PHYSICS SHOWCASES ============================ */
    {
        id: 'tafoni-pitting-coast',
        name: 'Tafoni Pitting Sea Cliffs',
        subtitle: 'Ice + thermal-expansion pitting showcase: tafoni honeycomb, spalled headlands & rock pools',
        climate: { regimeId: 'semi-arid-high-desert', latitudeDeg: 39, startDayOfYear: 40 },
        erosion: {
            thermalFatigueRate: 0.05, frostSegregationRate: 0.042, freezeThawRate: 0.06,
            crackClusterDiffusion: 0.34, spallDamageThreshold: 0.48, spallDepthM: 0.22,
            pitProtectDepthM: 0.5, rockfallRate: 0.9, cliffBaseAngleDeg: 54,
            hydrolysisK: 0.004, haloclastyK: 0.005
        },
        texture: { rampId: 'canyonStrata', stampIds: ['tafoniPits', 'hexagonalBasalt'] },
        mask: [[-1500, -820], [-620, -1020], [140, -760], [420, -300], [760, -420], [1060, 40], [720, 640], [-120, 880], [-980, 720], [-1560, 120]],
        layers: [
            { type: 'multifractal', name: 'Granite Headland', amplitude: 175, scale: 2.3, octaves: 6, gain: 0.52, warp: 0.34, seed: 4029 },
            { type: 'voronoi', name: 'Jointed Crag Blocks', variant: 1, amplitude: 110, scale: 3.0, jitter: 0.86, sharpness: 0.76, seed: 5113 },
            { type: 'terrace', name: 'Exfoliation Sheets', bedCount: 7, strength: 0.6, dipDeg: 8, strikeDeg: 140, seed: 6007 }
        ]
    },
    {
        id: 'slot-canyon-wind-corridor',
        name: 'Sandstone Slot Canyon Wind Corridor',
        subtitle: 'Venturi slot cuts, alcoves, fins & yardangs carved by saltation-curtain winds',
        climate: { regimeId: 'arid-desert', latitudeDeg: 37, startDayOfYear: 175 },
        erosion: {
            windAbrasionK: 0.0038, venturiGain: 2.1, undercutGain: 3.6, undercutZoneM: 2.8,
            strataStrength: 0.82, strataFreqPerM: 0.52, strataDipDeg: 9,
            thermalFatigueRate: 0.026, haloclastyK: 0.0042,
            bedrockErodibility: 4.2e-7, sandSupply: 1.25, saltationThresholdMs: 4.3
        },
        texture: { rampId: 'canyonStrata', stampIds: ['crossBeddedSandstone', 'aeolianRipples', 'tafoniPits'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'terrace', name: 'Navajo Sandstone Beds', bedCount: 15, strength: 0.85, dipDeg: 9, strikeDeg: 55, seed: 4029 },
            { type: 'multifractal', name: 'Canyon Bench Platform', amplitude: 120, scale: 2.5, octaves: 6, gain: 0.5, warp: 0.34, seed: 5113 },
            { type: 'spline', name: 'Slot Corridor', splineMode: 'canyon', width: 80, amount: 95, bankSmooth: 0.45, meander: 0.38, seed: 6007, points: [[-1300, -680], [-720, -220], [-240, -460], [180, -20], [620, -320], [1180, 60]] }
        ]
    },
    {
        id: 'karst-tower-limestone',
        name: 'Karst Tower Limestone',
        subtitle: 'Carbonate dissolution fengcon towers, grikes & travertine pools',
        climate: { regimeId: 'monsoon', latitudeDeg: 25, startDayOfYear: 130 },
        erosion: { carbonateShare: 0.75, dissolutionK: 0.006, hydrolysisK: 0.004, bedrockErodibility: 4.6e-7, depositionRate: 0.6, baseflowK: 0.5 },
        texture: { rampId: 'tropicalHighland', stampIds: ['karstGrikes', 'tafoniPits'] },
        mask: [[-1500, -1050], [1500, -1050], [1500, 1050], [-1500, 1050]],
        layers: [
            { type: 'multifractal', name: 'Limestone Plateau', amplitude: 135, scale: 2.6, octaves: 6, gain: 0.5, warp: 0.3, seed: 4029 },
            { type: 'voronoi', name: 'Fengcon Tower Cluster', variant: 3, amplitude: 120, scale: 3.2, jitter: 0.88, sharpness: 0.82, seed: 5113 }
        ]
    },
    {
        id: 'twin-shared-weather',
        name: 'Twin Terrains · Shared Weather Group',
        subtitle: 'TWO mask layers under ONE shared seasonal climate/pattern cycle (multi-terrain simulation)',
        climate: { regimeId: 'alpine-continental', latitudeDeg: 47, startDayOfYear: 90 },
        erosion: { bedrockErodibility: 3.2e-7, rockfallRate: 0.7, frostSegregationRate: 0.028, snowlineM: 230 },
        texture: { rampId: 'alpineSnow', stampIds: ['talusScree', 'glacialStriations'] },
        twin: true,
        mask: [[-1520, -980], [-780, -1140], [-210, -890], [-90, -420], [-380, -160], [-240, 180], [-680, 340], [-1320, 190], [-1640, -340]],
        maskB: [[620, -980], [1420, -1080], [1660, -360], [1520, 320], [900, 520], [560, 80]],
        layers: [
            { type: 'multifractal', name: 'Shared Alpine Base', amplitude: 170, scale: 2.15, octaves: 6, gain: 0.52, warp: 0.33, seed: 4029 },
            { type: 'ridged', name: 'Shared Arêtes', amplitude: 130, scale: 2.5, sharpness: 0.76, azimuth: 30, warp: 0.38, seed: 5113 }
        ]
    }
];

export const PRESET_IDS = EROSION_PRESETS.map(P => P.id);

export function GetErosionPreset(PresetId)
{
    return EROSION_PRESETS.find(P => P.id === PresetId) || EROSION_PRESETS[0];
}

/*==========================================================================================================================================
                                            WORLD BUILDING FROM PRESETS
==========================================================================================================================================*/
/**
 * Paint a preset's world mask + create its TerrainRegion(s) inside the shared
 * WorldMask2D world. Returns the primary region. `twin` presets create two
 * regions meant to be simulated with ONE shared ClimateGroup.
 */
export function BuildPresetWorld(World, PresetId, Options = {})
{
    const Preset = GetErosionPreset(PresetId);
    World.maskLayers = World.maskLayers || [];
    World.regions = World.regions || [];
    World.seaLevel = World.seaLevel ?? 0.0;

    const RegionA = MakePresetRegion(World, Preset, Preset.mask, 'A', Options);
    let RegionB = null;
    if (Preset.twin && Preset.maskB)
    {
        RegionB = MakePresetRegion(World, Preset, Preset.maskB, 'B', Options);
    }
    return { preset: Preset, region: RegionA, regionB: RegionB, climateOptions: Preset.climate, erosionOptions: Preset.erosion };
}

function MakePresetRegion(World, Preset, Polygon, Suffix, Options)
{
    const Sfx = Suffix === 'A' ? '' : ` ${Suffix}`;
    const Mask = World.addMaskLayer({
        id: `mask-${Preset.id}${Suffix === 'A' ? '' : '-' + Suffix.toLowerCase()}`,
        name: `Mask · ${Preset.name}${Sfx}`,
        color: '#58b982'
    });
    World.paintPolygonMeters(Mask, Polygon, false, 0.34, 4029);
    const Bounds = World.computeLayerPaintedBounds(Mask, 95.0);
    const Region = World.addTerrainRegion({
        id: `terrain-${Preset.id}${Suffix === 'A' ? '' : '-' + Suffix.toLowerCase()}`,
        name: `${Preset.name}${Sfx}`,
        maskLayerId: Mask.id,
        color: '#e8c27a',
        bounds: Bounds,
        resolution: Options.resolution ?? 148,
        baseElevation: 3.0,
        slabThickness: 6.5,
        shorelineBevel: 22.0,
        stack: Preset.layers.map((L, IX) => CreateTerrainLayer(L.type, {
            ...L,
            falloff: L.falloff || { mode: IX === 0 ? 'soft' : 'none', distance: 340.0, edgeFloor: IX === 0 ? 0.04 : 1.0 }
        }))
    });
    Region.presetId = Preset.id;
    Region.erosionOptions = { ...EROSION_DEFAULTS, ...Preset.erosion };
    Region.textureRecipe = Preset.texture;
    return Region;
}

/*==========================================================================================================================================
                        SHARED-WEATHER SIMULATION GROUPS (multi-terrain)
==========================================================================================================================================*/
/**
 * A SimulationGroup binds 2+ terrain regions to ONE ClimateGroup — literally
 * the same weather/pattern/seasonal cycle applied to every member. Solo mode
 * just wraps a single region in its own group.
 */
export function CreateSimulationGroup(Name, Regions, ClimateOptions = {}, ErosionOverrides = {})
{
    const Climate = CreateClimateGroup(ClimateOptions);
    const Members = [];
    for (const Region of (Array.isArray(Regions) ? Regions : [Regions]))
    {
        Members.push({ region: Region, state: null });
    }
    return {
        name: Name,
        climate: Climate,
        erosionOverrides: ErosionOverrides,
        members: Members,
        playing: false,
        day: 0
    };
}

/** Initialize erosion states for every member (call after regions are evaluated). */
export function InitSimulationGroup(World, Group, Seed = 4029)
{
    for (const Member of Group.members)
    {
        const Region = Member.region;
        if (!Region.runtime) throw new Error(`InitSimulationGroup: evaluate region ${Region.id} first`);
        Member.state = CreateErosionState(World, Region, {
            ...(Region.erosionOptions || {}),
            ...Group.erosionOverrides,
            seed: Seed + (Region.id.length * 131)
        });
    }
    return Group;
}

/** Advance ONE day across every member with the shared climate sample (same weather for all). */
export function StepSimulationGroupDay(World, Group)
{
    // Sample the shared weather ONCE — every member steps with this exact sample.
    const Sample = SampleClimate(Group.climate);
    for (const Member of Group.members)
    {
        if (Member.state) StepErosionDay(Member.state, Group.climate, Sample);
    }
    AdvanceClimateDay(Group.climate);
    Group.day += 1;
    return Group.day;
}

/** Bake N years for every member under the shared cycle. */
export function BakeSimulationGroup(World, Group, Years, OnProgress = null)
{
    const Days = Math.max(1, Math.round(Years * 365));
    for (let D = 0; D < Days; ++D)
    {
        StepSimulationGroupDay(World, Group);
        if (OnProgress && (D % 32 === 0)) OnProgress(D / Days);
    }
    if (OnProgress) OnProgress(1.0);
    return Group;
}

/** Push all member surfaces back to their regions. */
export function ApplySimulationGroup(World, Group)
{
    for (const Member of Group.members)
    {
        if (Member.state) ApplyErosionToRegion(World, Member.region, Member.state);
    }
    return Group;
}
