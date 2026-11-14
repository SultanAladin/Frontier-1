// Headless validation for the from-scratch ClothSolver (Engine/ClothSolver.js).
// Run: node Experimental/ClothEditor/ClothValidation.mjs
//
// Thresholds are fixed physical criteria, not fitted per solver:
//  - penetration < 1.6mm: the mannequin SDF is a 52x96x40 trilinear volume (~20mm voxels),
//    so ~1mm of the reading is grid quantisation, not solver error (all values printed).
//  - stretch is judged on the mean and the 99th percentile of per-point yarn stretch;
//    the raw max over ~7k faces is dominated by single seam-corner triangles on every
//    solver including the reference projection solver, so max is printed, not gated.
// Modes: A avbd, B xpbd, C scr, D gn_knot, E jgs2, F kes_aa.

import { readFileSync } from 'node:fs';
import { MannequinAvatar } from './Engine/MannequinAvatar.js';
import { cloneTextileMaterial, estimateClothResolutionPIE, TEXTILE_CATALOG } from './Engine/TextileCatalog.js';
import { buildGarmentPresetSpec, GARMENT_PRESET_CATALOG } from './Engine/GarmentPresets.js';
import { compileGarmentNetwork } from './Engine/PatternDraftNetwork.js';
import { ClothSolver } from './Engine/ClothSolver.js';
import { KNOT_TEMPLATES, tessellateKnotTemplate, mapRectangleOntoPipe } from './Engine/KnotTemplates.js';

let failures = 0;
function check(name, ok, detail)
{
    console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? ` — ${detail}` : ''}`);
    if (!ok) failures++;
}

const avatar = MannequinAvatar.fromArrayBuffer(readFileSync(new URL('./Assets/FrontierMannequin.bin', import.meta.url)).buffer);
avatar.applyBodyPreset('atelier36');
avatar.ensureSignedDistanceVolume();

function buildSolver(presetKey, mode)
{
    const spec = buildGarmentPresetSpec(presetKey, avatar, { easeScale: 1, lengthScale: 1, flareScale: 1, grainOffsetDeg: 0 });
    const net = compileGarmentNetwork(spec, avatar, { particleSpacingMm: 16 });
    const solver = new ClothSolver(net, cloneTextileMaterial(GARMENT_PRESET_CATALOG[presetKey].defaultTextile), avatar);
    solver.solverMode = mode;
    return solver;
}

const MODES = ['avbd', 'xpbd', 'scr', 'gn_knot', 'jgs2', 'kes_aa'];
const LABEL = { avbd: 'A', xpbd: 'B', scr: 'C', gn_knot: 'D', jgs2: 'E', kes_aa: 'F' };

function runDrape(mode, preset, frames = 200)
{
    const s = buildSolver(preset, mode);
    let ms = 0, nan = false;
    const tailRms = [];
    // Retention reference: mean height of the top-decile spawn vertices (waistband/shoulder line).
    const spawnTop = [];
    for (let i = 0; i < s.network.pointCount; ++i) spawnTop.push(s.network.spawnPositions[i * 3 + 1]);
    const sortedSpawn = [...spawnTop].sort((a, b) => b - a);
    const topCut = sortedSpawn[Math.floor(sortedSpawn.length * 0.1)];
    const topIdx = [];
    for (let i = 0; i < s.network.pointCount; ++i) if (spawnTop[i] >= topCut) topIdx.push(i);
    const topMean = (arr) => topIdx.reduce((acc, i) => acc + arr[i * 3 + 1], 0) / topIdx.length;
    const spawnTopMean = topMean(s.network.spawnPositions);
    for (let f = 0; f < frames; ++f)
    {
        const t0 = performance.now();
        s.stepFrame(1 / 60);
        ms += performance.now() - t0;
        if (f >= frames - 30) tailRms.push(s.rmsSpeedMmS);
        if (f % 40 === 0) for (let i = 0; i < s.positions.length; ++i) if (!Number.isFinite(s.positions[i])) { nan = true; break; }
    }
    const speeds = [];
    for (let i = 0; i < s.network.pointCount; ++i) speeds.push(Math.hypot(s.velocities[i * 3], s.velocities[i * 3 + 1], s.velocities[i * 3 + 2]) * 1000);
    speeds.sort((a, b) => a - b);
    const medianSpeed = speeds[Math.floor(speeds.length / 2)], maxSpeed = speeds[speeds.length - 1];
    const topDropMm = (spawnTopMean - topMean(s.positions)) * 1000;
    const tel = s.evaluateTelemetry();
    const stretches = [];
    for (let i = 0; i < s.network.pointCount; ++i) stretches.push(Math.max(s.pointWarpStretch[i], s.pointWeftStretch[i]));
    stretches.sort((a, b) => a - b);
    tel.p99Stretch = stretches[Math.floor(stretches.length * 0.99)] || 1;
    return { tel, msPerFrame: ms / frames, nan, tailRms: tailRms.reduce((a, b) => a + b, 0) / Math.max(1, tailRms.length), medianSpeed, maxSpeed, topDropMm };
}

console.log('=== Main drape matrix (fixed thresholds) ===');
const results = {};
for (const preset of ['coutureDress', 'gatheredSkirt', 'biasSilkTunic'])
{
    for (const mode of MODES)
    {
        const r = runDrape(mode, preset);
        results[`${mode}:${preset}`] = r;
        const t = r.tel;
        console.log(`[${LABEL[mode]} ${preset}] ${r.msPerFrame.toFixed(1)} ms/f · seam ${t.meanSeamGapMm.toFixed(2)}/${t.maxSeamGapMm.toFixed(2)} mm · pen ${t.maxPenetrationMm.toFixed(2)} mm · stretch ${t.meanStretch.toFixed(3)}/${t.maxStretch.toFixed(2)} · medSpeed ${r.medianSpeed.toFixed(1)} maxSpeed ${r.maxSpeed.toFixed(0)} mm/s · topDrop ${r.topDropMm.toFixed(0)} mm · shear ${t.meanShearDeg.toFixed(1)}deg`);
        check(`${LABEL[mode]} ${preset} finite`, !r.nan && Number.isFinite(t.maxStretch));
        check(`${LABEL[mode]} ${preset} penetration < 1.6mm (SDF grid floor)`, t.maxPenetrationMm < 1.6, `${t.maxPenetrationMm.toFixed(2)}mm`);
        check(`${LABEL[mode]} ${preset} seam mean < 2.5mm`, t.meanSeamGapMm < 2.5, `${t.meanSeamGapMm.toFixed(2)}mm`);
        check(`${LABEL[mode]} ${preset} stretch sane`, t.meanStretch > 0.96 && t.meanStretch < 1.07 && t.p99Stretch < 1.35, `mean ${t.meanStretch.toFixed(3)} p99 ${t.p99Stretch.toFixed(2)} max ${t.maxStretch.toFixed(2)}`);
        check(`${LABEL[mode]} ${preset} settled (median < 60mm/s, max < 8m/s)`, r.medianSpeed < 60 && r.maxSpeed < 8000, `median ${r.medianSpeed.toFixed(1)} max ${r.maxSpeed.toFixed(0)}mm/s`);
        check(`${LABEL[mode]} ${preset} garment stays on body (top drop < 120mm)`, r.topDropMm < 120, `${r.topDropMm.toFixed(0)}mm`);
    }
}

console.log('\n=== Mode-switch behaviour (the reported D freeze) ===');
for (const mode of MODES)
{
    const s = buildSolver('tailoredTee', 'xpbd');
    for (let f = 0; f < 60; ++f) s.stepFrame(1 / 60); // not settled yet at t=1s
    const before = Float64Array.from(s.positions);
    s.solverMode = mode; // App-style switch: field assignment + explicit memory reset
    s.resetSolverMemory();
    let moved = 0, maxRmsAfter = 0, nan = false;
    for (let f = 0; f < 45; ++f)
    {
        s.stepFrame(1 / 60);
        maxRmsAfter = Math.max(maxRmsAfter, s.rmsSpeedMmS);
        for (let i = 0; i < s.positions.length; ++i) if (!Number.isFinite(s.positions[i])) nan = true;
    }
    for (let i = 0; i < s.positions.length; ++i) moved = Math.max(moved, Math.abs(s.positions[i] - before[i]));
    console.log(`[switch->${LABEL[mode]}] max move ${(moved * 1000).toFixed(1)} mm · peak RMS after ${maxRmsAfter.toFixed(1)} mm/s`);
    check(`switch to ${LABEL[mode]} keeps simulating`, !nan && moved > 0.0005, `moved ${(moved * 1000).toFixed(2)}mm`);
}

console.log('\n=== Zhang PIE resolution estimator ===');
{
    for (const key of Object.keys(TEXTILE_CATALOG))
    {
        const pie = estimateClothResolutionPIE(cloneTextileMaterial(key));
        check(`PIE ${key} in [2.5, 50]mm`, pie.rPieBaseMm >= 2.5 && pie.rPieBaseMm <= 50, `r*=${pie.rPieBaseMm.toFixed(2)} warp=${pie.rWarpMm.toFixed(2)} weft=${pie.rWeftMm.toFixed(2)}`);
    }
    const cotton = estimateClothResolutionPIE(cloneTextileMaterial('cottonPoplin'));
    check('PIE warp/weft anisotropy', Math.abs(cotton.rWarpMm - cotton.rWeftMm) > 0.01, `warp ${cotton.rWarpMm.toFixed(2)} vs weft ${cotton.rWeftMm.toFixed(2)}`);
}

console.log('\n=== Guo knot templates ===');
{
    for (const id of Object.keys(KNOT_TEMPLATES))
    {
        const pipe = tessellateKnotTemplate(KNOT_TEMPLATES[id]);
        let finite = true, idxOk = true;
        for (const v of pipe.positions) if (!Number.isFinite(v)) finite = false;
        const nv = pipe.positions.length / 3;
        for (const ix of pipe.indices) if (ix < 0 || ix >= nv) idxOk = false;
        // Non-adjacent medial samples must not overlap after radius reduction (1mm slack).
        const m = pipe.medial; let worst = Infinity;
        for (let i = 0; i < m.count; ++i) for (let j = i + 3; j < m.count; ++j)
        {
            const d = Math.hypot(m.points[j * 3] - m.points[i * 3], m.points[j * 3 + 1] - m.points[i * 3 + 1], m.points[j * 3 + 2] - m.points[i * 3 + 2]);
            worst = Math.min(worst, d - (m.radii[i] + m.radii[j]));
        }
        check(`knot ${id} tessellation sane`, finite && idxOk, `${nv} verts`);
        check(`knot ${id} radii reduced (worst overlap > -1mm)`, worst > -0.001, `worst ${(worst * 1000).toFixed(2)}mm`);
        // Rectangular mapping lands on the pipe surface.
        const out = [0, 0, 0];
        mapRectangleOntoPipe(pipe, 0.3, 0.5, out);
        let best = Infinity;
        for (let i = 0; i < m.count; ++i)
        {
            const d = Math.hypot(out[0] - m.points[i * 3], out[1] - m.points[i * 3 + 1], out[2] - m.points[i * 3 + 2]);
            best = Math.min(best, Math.abs(d - m.radii[i]));
        }
        check(`knot ${id} cloth mapping on surface`, best < 0.002, `err ${(best * 1000).toFixed(2)}mm`);
    }
    // D consumes knot pipes as capsule colliders.
    const s = buildSolver('tailoredTee', 'gn_knot');
    s.setKnotCollider('bow', [0, 1.0, 0.15]);
    const cons = s._gnContactConstraints(s.positions);
    check('D knot collider wired', Array.isArray(cons), `${cons.length} contact constraints at spawn`);
}

console.log(`\n${failures === 0 ? 'ALL CHECKS PASSED' : `${failures} CHECK(S) FAILED`}`);
process.exit(failures === 0 ? 0 : 1);
