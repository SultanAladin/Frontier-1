/*============================================================================================================================================
                                                    CLOTHSOLVERBENCH.MJS
============================================================================================================================================*/
// Independent A-F bench (not part of ClothValidation.mjs): same mesh / textile / mannequin SDF for every
// solver, 180 calm-air frames at 60 FPS, plus a mid-run solver-switch audit. Used by
// Documentation/Plans/ClothSolverVerificationReport.md -- every number in that report comes from here.
//
//   node ClothSolverBench.mjs            (takes ~2 minutes)
//
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { MannequinAvatar } from './Engine/MannequinAvatar.js';
import { cloneTextileMaterial } from './Engine/TextileCatalog.js';
import { buildGarmentPresetSpec } from './Engine/GarmentPresets.js';
import { compileGarmentNetwork } from './Engine/PatternDraftNetwork.js';
import { OrthotropicClothSolver } from './Engine/OrthotropicClothSolver.js';

const Dir = path.dirname(fileURLToPath(import.meta.url));
const binBuf = fs.readFileSync(path.join(Dir, 'Assets/FrontierMannequin.bin'));
const avatar = MannequinAvatar.fromArrayBuffer(binBuf.buffer.slice(binBuf.byteOffset, binBuf.byteOffset + binBuf.byteLength));
avatar.ensureSignedDistanceVolume();

const MODES = ['avbd', 'xpbd', 'scr', 'gn_knot', 'jgs2', 'kes_aa'];

function gapAndVolume(solver)
{
    const hit = { phi: 0, dist: 0, qx: 0, qy: 0, qz: 0, nx: 0, ny: 1, nz: 0 };
    let sum = 0, cnt = 0;
    const mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
    for (let i = 0; i < solver.network.pointCount; ++i)
    {
        const i3 = i * 3;
        for (let a = 0; a < 3; ++a)
        {
            const v = solver.positions[i3 + a];
            if (v < mn[a]) mn[a] = v;
            if (v > mx[a]) mx[a] = v;
        }
        if (avatar.sampleSignedDistance(solver.positions[i3], solver.positions[i3 + 1], solver.positions[i3 + 2], 0, hit))
        {
            sum += Math.abs(hit.phi);
            cnt++;
        }
    }
    return { gapMm: (sum / Math.max(1, cnt)) * 1000.0, volM3: (mx[0] - mn[0]) * (mx[1] - mn[1]) * (mx[2] - mn[2]) };
}

for (const preset of ['coutureDress', 'tailoredTee'])
{
    const spec = buildGarmentPresetSpec(preset, avatar);
    const net = compileGarmentNetwork(spec, avatar, { particleSpacingMm: 16.5 });
    console.log(`\n## ${preset} (${net.pointCount} pts / ${net.faceCount} tris, ${spec.defaultTextile})`);
    for (const mode of MODES)
    {
        const solver = new OrthotropicClothSolver(net, cloneTextileMaterial(spec.defaultTextile), avatar);
        solver.solverMode = mode;
        const spawn = gapAndVolume(solver);
        const t0 = performance.now();
        for (let f = 0; f < 180; ++f) solver.stepFrame(1.0 / 60.0);
        const ms = (performance.now() - t0) / 180.0;
        const g = gapAndVolume(solver);
        const tel = solver.evaluateTelemetry();
        let nan = 0;
        for (const v of solver.positions) if (!Number.isFinite(v)) nan++;
        console.log(`${mode.padEnd(8)} ms=${ms.toFixed(1)} gap ${spawn.gapMm.toFixed(1)}->${g.gapMm.toFixed(1)} mm vol ${spawn.volM3.toFixed(3)}->${g.volM3.toFixed(3)} m3 rms=${solver.rmsSpeedMmS.toFixed(1)} mm/s yarn=${tel.meanYarnStretch.toFixed(3)} shear=${tel.meanShearDeg.toFixed(1)} deg seam=${tel.meanSeamGapMm.toFixed(2)} mm pen=${tel.maxPenetrationMm.toFixed(2)} mm NaN=${nan}`);
    }
}

console.log('\n## Mid-run switch: avbd x70 frames -> target x5 frames (App path: resetSolverMemory + wakeUp)');
{
    const spec = buildGarmentPresetSpec('coutureDress', avatar);
    const net = compileGarmentNetwork(spec, avatar, { particleSpacingMm: 16.5 });
    for (const mode of MODES)
    {
        const solver = new OrthotropicClothSolver(net, cloneTextileMaterial(spec.defaultTextile), avatar);
        solver.solverMode = 'avbd';
        for (let f = 0; f < 70; ++f) solver.stepFrame(1.0 / 60.0);
        const before = Float32Array.from(solver.positions);
        const gapIn = gapAndVolume(solver).gapMm;
        solver.solverMode = mode;
        solver.resetSolverMemory();
        solver.wakeUp();
        for (let f = 0; f < 5; ++f) solver.stepFrame(1.0 / 60.0);
        let maxMoved = 0, meanMoved = 0, nan = 0;
        for (let i = 0; i < net.pointCount; ++i)
        {
            const d = Math.hypot(solver.positions[i * 3] - before[i * 3], solver.positions[i * 3 + 1] - before[i * 3 + 1], solver.positions[i * 3 + 2] - before[i * 3 + 2]);
            meanMoved += d;
            if (d > maxMoved) maxMoved = d;
        }
        for (const v of solver.positions) if (!Number.isFinite(v)) nan++;
        console.log(`-> ${mode.padEnd(8)} maxMoved=${(maxMoved * 1000).toFixed(1)} mm meanMoved=${((meanMoved / net.pointCount) * 1000).toFixed(1)} mm gap ${gapIn.toFixed(1)}->${gapAndVolume(solver).gapMm.toFixed(1)} mm rms=${solver.rmsSpeedMmS.toFixed(1)} mm/s NaN=${nan}`);
    }
}
