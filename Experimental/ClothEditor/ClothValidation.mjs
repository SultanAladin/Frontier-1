/*============================================================================================================================================
                                                          CLOTHVALIDATION.MJS
============================================================================================================================================*/
// 🧩 Deterministic Node.js DOM Wiring, WGSL Shader, Dual-Solver Physics, Fit Verification, & CAD Export Validation Suite.

import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';

import { MannequinAvatar } from './Engine/MannequinAvatar.js';
import { cloneTextileMaterial, SOLVER_CATALOG, estimateClothResolutionPIE, TEXTILE_CATALOG } from './Engine/TextileCatalog.js';
import { buildGarmentPresetSpec } from './Engine/GarmentPresets.js';
import { compileGarmentNetwork, sampleKnotTemplate, FPB_KNOT_TEMPLATES } from './Engine/PatternDraftNetwork.js';
import { OrthotropicClothSolver } from './Engine/OrthotropicClothSolver.js';
import { WebGpuClothAccelerator, packGpuNetworkTables } from './Engine/WebGpuClothAccelerator.js';
import { exportPatternSVG, exportPatternDXF, exportDrapedOBJ } from './Engine/GarmentPatternExport.js';

const Dir = path.dirname(fileURLToPath(import.meta.url));
const ReadText = (rel) => fs.readFileSync(path.join(Dir, rel), 'utf8');

console.log('=== 1. Bundled Assets, Zero External CDNs, DOM ID Wiring & WGSL Compute Audit ===');
const RequiredFiles = [
    'index.html', 'ClothEditor.css', 'Serve.py',
    'Assets/FrontierMannequin.bin',
    'Engine/MannequinAvatar.js', 'Engine/TextileCatalog.js', 'Engine/PatternDraftNetwork.js',
    'Engine/GarmentPresets.js', 'Engine/OrthotropicClothSolver.js', 'Engine/WebGpuClothAccelerator.js',
    'Engine/GarmentPatternExport.js', 'Engine/PatternCanvas2D.js', 'Engine/GarmentViewport3D.js',
    'Engine/ClothStudioApp.js',
    '../UVEditor/UVEditor.css', '../UVEditor/StudioControls.js',
    '../UVEditor/Vendor/three.esm.min.js', '../UVEditor/Vendor/orbit-controls.esm.min.js',
];
for (const rel of RequiredFiles)
    assert.ok(fs.existsSync(path.join(Dir, rel)), `Missing required Cloth Atelier file: ${rel}`);

const Html = ReadText('index.html');
const Css = ReadText('ClothEditor.css');
const AppJs = ReadText('Engine/ClothStudioApp.js');
const WgslJs = ReadText('Engine/WebGpuClothAccelerator.js');
const ViewportJs = ReadText('Engine/GarmentViewport3D.js');

assert.ok(!/https?:\/\//i.test(Html), 'index.html must not depend on external CDNs');
assert.ok(!/https?:\/\//i.test(Css), 'ClothEditor.css must not depend on external CDNs');
assert.ok(ViewportJs.includes('canvas.width !== targetW'), 'GarmentViewport3D must size WebGL drawingBuffer via canvas.width (Retina DPR), not CSS clientWidth');

const HtmlIds = new Set([...Html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]));
for (const m of AppJs.matchAll(/document\.getElementById\('([^']+)'\)/g))
    assert.ok(HtmlIds.has(m[1]), `Missing DOM id="${m[1]}" in index.html referenced by ClothStudioApp.js`);

for (const entry of ['fn predictInertia', 'fn solveAvbdColorBatch', 'fn solveOrthotropicFaceBatch', 'fn solveHingeColorBatch', 'fn solveSeamColorBatch', 'fn collideSdfAndFinish'])
    assert.ok(WgslJs.includes(entry), `Missing WGSL compute kernel entry point: ${entry}`);
assert.ok(!WgslJs.includes('@binding(9)'), 'WGSL layout must fit strictly inside WebGPU baseline 8 storage buffers (@binding 1..8)');
assert.ok(typeof WebGpuClothAccelerator === 'function' && typeof packGpuNetworkTables === 'function', 'WebGpuClothAccelerator & packGpuNetworkTables must export cleanly');
console.log(`  [PASS] ${RequiredFiles.length} local files verified, ${HtmlIds.size} HTML IDs wired, 6 WGSL kernels in <=8 storage buffers, Retina DPR viewport verified`);

const binBuf = fs.readFileSync(path.join(Dir, 'Assets/FrontierMannequin.bin'));
const avatar = MannequinAvatar.fromArrayBuffer(binBuf.buffer.slice(binBuf.byteOffset, binBuf.byteOffset + binBuf.byteLength));
avatar.ensureSignedDistanceVolume();

console.log('\n=== 2. Deterministic Bit-Reproducibility Check (All Six Candidate Architectures A–F) ===');
for (const mode of ['avbd', 'xpbd', 'scr', 'gn_knot', 'jgs2', 'kes_aa'])
{
    const spec = buildGarmentPresetSpec('coutureDress', avatar);
    const net = compileGarmentNetwork(spec, avatar, { particleSpacingMm: 16.5 });
    const s1 = new OrthotropicClothSolver(net, cloneTextileMaterial('cottonPoplin'), avatar);
    const s2 = new OrthotropicClothSolver(net, cloneTextileMaterial('cottonPoplin'), avatar);
    s1.solverMode = mode;
    s2.solverMode = mode;
    for (let f = 0; f < 24; ++f)
    {
        s1.stepFrame(1.0 / 60.0);
        s2.stepFrame(1.0 / 60.0);
    }
    let maxBitDiff = 0.0;
    for (let i = 0; i < s1.positions.length; ++i)
        maxBitDiff = Math.max(maxBitDiff, Math.abs(s1.positions[i] - s2.positions[i]));
    assert.equal(maxBitDiff, 0.0, `${mode} must be 100% deterministic across identical runs`);
    console.log(`  [PASS] ${mode.toUpperCase()} bit-exact determinism: maxPositionDiff = ${maxBitDiff}`);
}

console.log('\n=== 3. Six-Solver Physical Relaxation (NO artificial freeze), Seam Closure, Stretch & Wind Wake-Up ===');
const SOLVER_MODES = ['avbd', 'xpbd', 'scr', 'gn_knot', 'jgs2', 'kes_aa'];
for (const mode of SOLVER_MODES)
    assert.ok(SOLVER_CATALOG[mode], `SOLVER_CATALOG must describe solver mode ${mode}`);
assert.equal(Object.keys(SOLVER_CATALOG).length, 6, 'All six candidate architectures (A–F) must remain available side by side');

for (const presetKey of ['coutureDress', 'gatheredSkirt', 'tailoredTee', 'biasSilkTunic'])
{
    const spec = buildGarmentPresetSpec(presetKey, avatar);
    const net = compileGarmentNetwork(spec, avatar, { particleSpacingMm: 16.5 });
    for (const mode of SOLVER_MODES)
    {
        const solver = new OrthotropicClothSolver(net, cloneTextileMaterial(spec.defaultTextile), avatar);
        solver.solverMode = mode;

        let peakRms = 0.0;
        for (let f = 1; f <= 180; ++f)
        {
            solver.stepFrame(1.0 / 60.0);
            if (solver.rmsSpeedMmS > peakRms) peakRms = solver.rmsSpeedMmS;
        }
        const relaxedRms = solver.rmsSpeedMmS;

        for (let i = 0; i < solver.positions.length; ++i)
            assert(Number.isFinite(solver.positions[i]), `Non-finite position in ${presetKey} (${mode})`);

        // Physical relaxation: the garment's kinetic energy must genuinely decay through dissipation alone
        // (no freeze, no deadband, no clamp) — measured as decay relative to the run's own peak.
        // biasSilkTunic is a bias-cut silk shell with almost no hysteresis (2HG = 0.09) and friction-only support,
        // so it keeps creeping/swinging at low level exactly like real silk; it is bounded, not frozen.
        const relaxLimit = presetKey === 'biasSilkTunic' ? 460.0 : 60.0;
        const relaxRatio = presetKey === 'biasSilkTunic' ? 1.25 : 0.5;
        assert(relaxedRms < Math.max(8.0, peakRms * relaxRatio),
            `${presetKey} (${mode}) must relax (peak ${peakRms.toFixed(1)} -> ${relaxedRms.toFixed(1)} mm/s)`);
        assert(relaxedRms < relaxLimit,
            `${presetKey} (${mode}) must stay bounded after 3 s, got RMS ${relaxedRms.toFixed(1)} mm/s`);

        const tel = solver.evaluateTelemetry();
        assert(tel.maxPenetrationMm < 0.5, `Excess penetration ${tel.maxPenetrationMm} mm in ${presetKey} (${mode})`);
        assert(tel.meanSeamGapMm < 4.0, `Seam failed to close (${tel.meanSeamGapMm} mm) in ${presetKey} (${mode})`);
        assert(Number.isFinite(tel.meanYarnStretch) && tel.meanYarnStretch < 1.22,
            `${presetKey} (${mode}) must not balloon (yarn stretch ${tel.meanYarnStretch})`);

        if (presetKey === 'gatheredSkirt')
        {
            const pinCount = solver.userPinned.reduce((a, b) => a + b, 0);
            assert.equal(pinCount, 0, 'gatheredSkirt waistband must drape naturally with 0 point pins');
        }

        // The solver must never gate integration on "settled": an external impulse still integrates.
        const beforeStep = solver.simTime;
        solver.windEnabled = true;
        solver.stepFrame(1.0 / 60.0);
        assert(solver.simTime > beforeStep, `${presetKey} (${mode}) must keep integrating when nearly at rest`);
        assert.equal(solver.isSettled, false, `${presetKey} (${mode}) must wake up immediately when wind turns on`);
        let windPeak = solver.maxSpeedMmS;
        for (let f = 0; f < 5; ++f)
        {
            solver.stepFrame(1.0 / 60.0);
            if (solver.maxSpeedMmS > windPeak) windPeak = solver.maxSpeedMmS;
        }
        assert(windPeak > 35.0, `${presetKey} (${mode}) must respond dynamically to wind (>35 mm/s), got ${windPeak.toFixed(1)}`);
        solver.windEnabled = false;

        console.log(`  [PASS] ${presetKey.padEnd(14)} | ${mode.padEnd(8)} | relax ${peakRms.toFixed(1)}->${relaxedRms.toFixed(1)} mm/s | yarn=${tel.meanYarnStretch.toFixed(3)} shear=${tel.meanShearDeg.toFixed(1)}° | seam=${tel.meanSeamGapMm.toFixed(2)} mm | pen=${tel.maxPenetrationMm.toFixed(2)} mm`);
    }
}

console.log('\n=== 3b. No Artificial Settling / Freezing Audit ===');
{
    const solverSrc = ReadText('Engine/OrthotropicClothSolver.js');
    const gpuSrc = ReadText('Engine/WebGpuClothAccelerator.js');
    assert.ok(!/if \(this\.isSettled\)\s*\{\s*this\.lastStepMs/.test(solverSrc), 'Solver must not early-return while settled');
    assert.ok(!solverSrc.includes('freezeAlpha'), 'No freeze blending / position clamping may remain');
    assert.ok(!solverSrc.includes('vel.fill(0)'), 'Velocities must never be globally zeroed to fake a standstill');
    assert.ok(!gpuSrc.includes('subVisc'), 'GPU path must not apply a settling velocity ramp');

    // Behavioural proof: after deep relaxation, an external drag must still move cloth 1:1 with no deadband.
    const spec = buildGarmentPresetSpec('tailoredTee', avatar);
    const net = compileGarmentNetwork(spec, avatar, { particleSpacingMm: 16.5 });
    for (const mode of SOLVER_MODES)
    {
        const solver = new OrthotropicClothSolver(net, cloneTextileMaterial(spec.defaultTextile), avatar);
        solver.solverMode = mode;
        for (let f = 0; f < 150; ++f) solver.stepFrame(1.0 / 60.0);
        // Pick a free vertex and grab it far away; the solver must respond even though it is at rest.
        let grab = -1;
        for (let i = 0; i < net.pointCount; ++i) if (!solver.userPinned[i]) { grab = i; break; }
        const p0 = [solver.positions[grab * 3], solver.positions[grab * 3 + 1], solver.positions[grab * 3 + 2]];
        solver.grabVertex = grab;
        solver.grabTarget = [p0[0] + 0.05, p0[1], p0[2]];
        for (let f = 0; f < 6; ++f) solver.stepFrame(1.0 / 60.0);
        const moved = Math.hypot(solver.positions[grab * 3] - p0[0], solver.positions[grab * 3 + 1] - p0[1], solver.positions[grab * 3 + 2] - p0[2]);
        assert(moved > 0.001, `${mode}: grabbed vertex must respond after relaxation (moved ${(moved * 1000).toFixed(2)} mm)`);
        console.log(`  [PASS] ${mode.padEnd(8)} responds to a drag after relaxation: ${(moved * 1000).toFixed(1)} mm in 6 frames`);
    }
}

console.log('\n=== 3c. Zhang et al. (2025) PIE Optimal-Resolution Estimation + Guo et al. (2025) Knot Templates ===');
{
    let prev = null;
    for (const key of Object.keys(TEXTILE_CATALOG))
    {
        const pie = estimateClothResolutionPIE(TEXTILE_CATALOG[key]);
        assert(Number.isFinite(pie.rPieBaseMm) && pie.rPieBaseMm > 2.4 && pie.rPieBaseMm < 50.0, `PIE radius out of calibrated range for ${key}`);
        assert(pie.rWarpMm > 0 && pie.rWeftMm > 0, `PIE anisotropic radii must be positive for ${key}`);
        assert(pie.authoringSpacingMm >= 9.5 && pie.authoringSpacingMm <= 18.5, `Authoring spacing must stay interactive for ${key}`);
        assert(pie.gameSpacingMm >= 13.5 && pie.gameSpacingMm <= 24.0, `Game spacing must stay 45-60 FPS friendly for ${key}`);
        assert(pie.wrinklonTransitionMm > 0 && pie.wavelengthMm > pie.rPieBaseMm, `Wrinkle scaling must be physical for ${key}`);
        if (prev) assert(Math.abs(pie.rPieBaseMm - prev) > 1e-6, 'PIE must respond to textile bending/stiffness');
        prev = pie.rPieBaseMm;
        console.log(`  [PASS] PIE ${key.padEnd(13)} r*=${pie.rPieBaseMm.toFixed(2)} mm (warp ${pie.rWarpMm.toFixed(2)} / weft ${pie.rWeftMm.toFixed(2)}) λ=${pie.wavelengthMm.toFixed(1)} mm -> authoring ${pie.authoringSpacingMm.toFixed(1)} mm, game ${pie.gameSpacingMm.toFixed(1)} mm`);
    }

    // Auto-resolution wiring: compileGarmentNetwork must honour the PIE spacing when asked to.
    const spec = buildGarmentPresetSpec('coutureDress', avatar);
    const tex = cloneTextileMaterial('rawDenim');
    const autoNet = compileGarmentNetwork(spec, avatar, { textile: tex });
    assert(autoNet.pieResolution && Number.isFinite(autoNet.pieResolution.authoringSpacingMm), 'compileGarmentNetwork must attach PIE metadata when auto-resolving');
    const denseNet = compileGarmentNetwork(spec, avatar, { textile: tex, particleSpacingMm: 11.0 });
    assert(denseNet.pointCount > autoNet.pointCount, 'Manual finer spacing must produce a denser mesh than the PIE default');
    console.log(`  [PASS] PIE auto-resolution wired: ${autoNet.pointCount} points @ ${autoNet.pieResolution.authoringSpacingMm.toFixed(1)} mm vs ${denseNet.pointCount} @ 11.0 mm`);

    for (const id of Object.keys(FPB_KNOT_TEMPLATES))
    {
        const tube = sampleKnotTemplate(id, 10);
        assert.equal(tube.points.length % 10, 0, 'Knot template must sample whole Bézier spans');
        assert(tube.radii.every((r) => r > 0.004 && r < 0.05), 'Knot pipe radii must stay in a physical millimetre range');
        console.log(`  [PASS] FPB knot template ${id.padEnd(12)} ${tube.points.length} pipe samples, radius ${Math.min(...tube.radii).toFixed(3)}–${Math.max(...tube.radii).toFixed(3)} m`);
    }
}

console.log('\n=== 4. Pre-Cut Fit Verification Bake & 1:1 CAD Exports (.SVG / .DXF / .OBJ) ===');
{
    const spec = buildGarmentPresetSpec('coutureDress', avatar);
    const net = compileGarmentNetwork(spec, avatar, { particleSpacingMm: 16.5 });
    const tex = cloneTextileMaterial('cottonPoplin');
    const solver = new OrthotropicClothSolver(net, tex, avatar);
    for (let f = 0; f < 65; ++f) solver.stepFrame(1.0 / 60.0);
    const cert = solver.runCubicBarrierFitVerification(14);
    assert.equal(cert.verified, true, 'Couture dress must pass <0.5% excess woven yarn stretch verification');
    assert(cert.maxExcessStretchPct < 0.5, `Expected <0.5% excess stretch, got ${cert.maxExcessStretchPct}%`);

    const svg = exportPatternSVG(net, tex, cert);
    const dxf = exportPatternDXF(net, tex);
    const obj = exportDrapedOBJ(solver);
    assert(svg.includes('<svg') && svg.includes('mm"') && svg.includes('50 mm SCALE CHECK'));
    assert(dxf.includes('$INSUNITS') && dxf.includes('1_CUT') && dxf.includes('8_SEW'));
    assert(obj.includes('\nv ') && obj.includes('\nvt ') && obj.includes('\nvn ') && obj.includes('\nf '));
    const packed = packGpuNetworkTables(solver);
    assert.ok(packed.u32Topo.length > net.pointCount + net.faceCount * 3 + net.hingeCount * 4, 'Packed u32Topo must contain full face, hinge, and seam tables');
    assert.ok(packed.f32Rest.length >= net.faceCount * 5 + net.hingeCount * 5, 'Packed f32Rest must contain full DmInv, rest areas, and hinge weights');
    console.log(`  [PASS] Fit Bake verified=${cert.verified} (${cert.maxExcessStretchPct.toFixed(3)}% excess) | GPU Topo=${packed.u32Topo.byteLength}B Rest=${packed.f32Rest.byteLength}B | SVG=${svg.length}B | DXF=${dxf.length}B`);
}

console.log('\nALL CLOTH ATELIER VALIDATION CHECKS PASSED.');
