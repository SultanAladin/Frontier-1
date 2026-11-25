#!/usr/bin/env node
/**
 * Frontier Headless CLI Asset Baker for AAA Game Pipelines.
 *
 * Usage:
 *   npm run generate -- --archetype desert-arch --seed 4029 --quality high --out exports/arch.glb
 *   npm run generate -- --archetype sedimentary-cliff --seed 4029 --out exports/cliff.glb
 *   npm run generate -- --archetype cliff-wall --rain 0.6 --rain-droplets 14000 --quality ultra --decimation 0.3
 *   npm run generate -- --archetype cliff-wall --sdf-source mesh --erosion-res 320 --wind 0.45 --thermal 0.25 --decimation 0.3
 *   npm run generate -- --archetype utah-canyon --wind 0.5 --wind-shift sectors --wind-shift-range 75 --wind-shift-cycles 2
 *   npm run generate -- --archetype slot-canyon --rain 0.55 --slot-flood 0.85 --slot-flood-particles 6000 --slot-sediment 0.62 --wind 0.35
 */

import fs from 'node:fs';
import path from 'node:path';
import { generateRockFormation } from '../src/geology/generator.js';
import { exportFormationToGLB } from '../src/io/glb-exporter.js';
import { exportFormationToOBJ } from '../src/io/obj-exporter.js';
import { FORMATION_PRESETS } from '../src/geology/presets.js';

function parseArgs(argv) {
  const opts = {
    archetype: 'desert-arch',
    seed: 4029,
    quality: 'high',
    decimation: 1.0,
    lods: false,
    out: null,
  };

  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--archetype' && argv[i + 1]) opts.archetype = argv[++i];
    else if (a === '--preset' && argv[i + 1]) opts.archetype = argv[++i];
    else if (a === '--seed' && argv[i + 1]) opts.seed = Number(argv[++i]);
    else if (a === '--quality' && argv[i + 1]) opts.quality = argv[++i];
    else if (a === '--decimation' && argv[i + 1]) opts.decimation = Number(argv[++i]);
    else if (a === '--scale' && argv[i + 1]) opts.scale = Number(argv[++i]);
    else if (a === '--dip' && argv[i + 1]) opts.dipAngle = Number(argv[++i]);
    else if (a === '--rain' && argv[i + 1]) opts.rainErosionAmount = Number(argv[++i]);
    else if (a === '--rain-droplets' && argv[i + 1]) opts.rainDroplets = Number(argv[++i]);
    else if (a === '--rain-edge' && argv[i + 1]) opts.rainEdgeProtection = Number(argv[++i]);
    else if (a === '--slot-flood' && argv[i + 1]) opts.slotFloodAmount = Number(argv[++i]);
    else if (a === '--slot-flood-particles' && argv[i + 1]) opts.slotFloodParticles = Number(argv[++i]);
    else if (a === '--slot-sediment' && argv[i + 1]) opts.slotSedimentSupply = Number(argv[++i]);
    else if (a === '--sdf-source' && argv[i + 1]) opts.sdfErosionSource = argv[++i];
    else if (a === '--erosion-res' && argv[i + 1]) opts.erosionSdfResolution = Number(argv[++i]);
    else if (a === '--wind' && argv[i + 1]) opts.windErosionAmount = Number(argv[++i]);
    else if (a === '--wind-azimuth' && argv[i + 1]) opts.windAzimuth = Number(argv[++i]);
    else if (a === '--wind-shift' && argv[i + 1]) opts.windShiftMode = argv[++i];
    else if (a === '--wind-shift-range' && argv[i + 1]) opts.windShiftRange = Number(argv[++i]);
    else if (a === '--wind-shift-cycles' && argv[i + 1]) opts.windShiftCycles = Number(argv[++i]);
    else if (a === '--wind-turbulence' && argv[i + 1]) opts.windTurbulence = Number(argv[++i]);
    else if (a === '--wind-streamlining' && argv[i + 1]) opts.windStreamlining = Number(argv[++i]);
    else if (a === '--thermal' && argv[i + 1]) opts.thermalErosionAmount = Number(argv[++i]);
    else if (a === '--thermal-repose' && argv[i + 1]) opts.thermalReposeAngle = Number(argv[++i]);
    else if (a === '--cryo' && argv[i + 1]) opts.cryoErosionAmount = Number(argv[++i]);
    else if (a === '--chem-dissolution' && argv[i + 1]) opts.chemDissolutionAmount = Number(argv[++i]);
    else if (a === '--chem' && argv[i + 1]) opts.chemAmount = Number(argv[++i]);
    else if (a === '--iron' && argv[i + 1]) opts.ironContent = Number(argv[++i]);
    else if (a === '--copper' && argv[i + 1]) opts.copperContent = Number(argv[++i]);
    else if (a === '--out' && argv[i + 1]) opts.out = argv[++i];
    else if (a === '--lods') opts.lods = true;
    else if (a === '--help' || a === '-h') {
      console.log('Frontier CLI Baker\nArchetypes:', Object.keys(FORMATION_PRESETS).join(', '));
      process.exit(0);
    }
  }
  return opts;
}

const opts = parseArgs(process.argv);
const outBase = opts.out || `exports/frontier_${opts.archetype}_${opts.seed}.glb`;
fs.mkdirSync(path.dirname(path.resolve(outBase)), { recursive: true });

if (opts.lods) {
  const stem = outBase.replace(/\.(glb|obj)$/i, '');
  const lodTargets = [
    { suffix: 'LOD0', ratio: 0.40 },
    { suffix: 'LOD1', ratio: 0.16 },
    { suffix: 'LOD2', ratio: 0.06 },
  ];
  for (const lod of lodTargets) {
    const res = generateRockFormation({ ...opts, decimation: lod.ratio });
    const file = `${stem}_${lod.suffix}.glb`;
    fs.writeFileSync(file, exportFormationToGLB(res));
    console.log(`[LOD] ${lod.suffix} -> ${file} (${res.stats.triangles} tris, ${res.stats.elapsedMs}ms)`);
  }
} else {
  const res = generateRockFormation(opts);
  if (outBase.toLowerCase().endsWith('.obj')) {
    fs.writeFileSync(outBase, exportFormationToOBJ(res));
  } else {
    fs.writeFileSync(outBase, exportFormationToGLB(res));
  }
  console.log(`[Baked] ${res.stats.archetypeName} (seed ${opts.seed}) -> ${outBase}`);
  console.log(`        ${res.stats.triangles.toLocaleString()} tris | ${res.stats.dimensions.join('m x ')}m | ${res.stats.elapsedMs}ms`);
  if (res.stats.meshToSdf) {
    const conv = res.stats.meshToSdf;
    console.log(`        mesh->SDF: ${conv.gridDims.join('x')} cells | ${(conv.voxelSize * 100).toFixed(1)}cm voxels | ${conv.sourceTriangles.toLocaleString()} source tris | ${conv.elapsedMs}ms`);
  }
  if (res.stats.sdfErosion) {
    const suite = res.stats.sdfErosion;
    const parts = ['chemicalDissolution', 'wind', 'rain', 'slotFlood', 'cryo', 'thermal']
      .filter((key) => suite[key])
      .map((key) => `${key} ${suite[key].erodedM3}m3`);
    console.log(`        SDF erosion (${suite.source}): ${parts.join(' | ')} | ${suite.elapsedMs}ms`);
    if (suite.wind?.directionMode) {
      console.log(`        wind regime: ${suite.wind.directionMode} | direction path ${(suite.wind.directionSamplesDeg || []).join('° -> ')}°`);
      if (suite.wind.directionSectorOffsetsDeg?.length && suite.wind.directionSectorShares?.length) {
        const mix = suite.wind.directionSectorOffsetsDeg
          .map((off, i) => `${off > 0 ? '+' : ''}${off}° ${(100 * suite.wind.directionSectorShares[i]).toFixed(1)}%`)
          .join(' | ');
        console.log(`        wind sector mix (rel. prevailing): ${mix}`);
      }
    }
    if (suite.slotFlood) {
      const sf = suite.slotFlood;
      console.log(`        slot flood: ${sf.floodParticles} flood packets + ${sf.windParticles} channel-wind grains | wall ${sf.wallEvents} impacts | bed ${sf.floorEvents} impacts | pothole eddies ${sf.potholeEvents}`);
    }
  }
  if (res.stats.rainErosion) {
    const re = res.stats.rainErosion;
    console.log(`        rain: ${re.droplets} droplets | eroded ${re.erodedM3} m3 | deposited ${re.depositedM3} m3 | ${re.elapsedMs}ms`);
  }
  if (res.stats.chemicalStaining) {
    const cs = res.stats.chemicalStaining;
    console.log(`        chem: rust on ${(cs.stainedFracFe * 100).toFixed(1)}% of surface | patina ${(cs.stainedFracCu * 100).toFixed(1)}% | ${cs.elapsedMs}ms`);
  }
}
