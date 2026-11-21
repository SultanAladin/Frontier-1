/*============================================================================================================================================
                                                          CLOTHVALIDATION.MJS
                 Comprehensive Headless Physics, Topology, Solver, Morph, Wind, Grab, and Export Verification Suite
                            Verifies 4 Solvers (AVBD, XPBD, MGPBD, FPB) × 6 Garment Presets + GPU Table Packing
============================================================================================================================================*/

import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';

import { MannequinAvatar } from './Engine/MannequinAvatar.js';
import { TEXTILE_CATALOG, estimateClothResolutionPIE } from './Engine/TextileCatalog.js';
import { compileGarmentNetwork } from './Engine/PatternDraftNetwork.js';
import { GARMENT_PRESET_CATALOG, buildGarmentPresetSpec } from './Engine/GarmentPresets.js';
import { SOLVER_CATALOG, OrthotropicClothSolver } from './Engine/ClothSolvers.js';
import { packGpuNetworkTables } from './Engine/WebGpuClothAccelerator.js';
import { exportPatternSVG, exportPatternDXF, exportDrapedOBJ } from './Engine/GarmentPatternExport.js';

const ROOT = path.dirname(new URL(import.meta.url).pathname);

function loadMannequin()
{
    const binPath = path.join(ROOT, 'Assets', 'FrontierMannequin.bin');
    const raw = fs.readFileSync(binPath);
    const ab = raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength);
    return MannequinAvatar.fromArrayBuffer(ab);
}

function testMannequinAndTextiles(mannequin)
{
    console.log('\n[1/5] Validating Faceless Atelier Mannequin & 6 Physical Textiles...');
    assert.equal(mannequin.pointCount, 10764, 'Expected 10,764 mannequin vertices');
    assert.equal(mannequin.faceIndices.length, 21524 * 3, 'Expected 21,524 triangles');
    assert.ok(mannequin.capCount > 100, 'Expected rebuilt smooth oval head cap');
    assert.ok(mannequin.measurements.bust > 80 && mannequin.measurements.bust < 110);
    assert.ok(mannequin.measurements.waist > 60 && mannequin.measurements.waist < 90);
    assert.ok(mannequin.measurements.hips > 85 && mannequin.measurements.hips < 115);

    const baseWaist = mannequin.measurements.waist;
    mannequin.configureMorphs({ waist: 0.65, hips: 0.45 });
    assert.ok(mannequin.measurements.waist > baseWaist + 2.0, 'Morphs must increase live body waist circumference');
    mannequin.applyBodyPreset('atelier36');

    const textiles = Object.values(TEXTILE_CATALOG);
    assert.equal(textiles.length, 6, 'Expected 6 physical textiles');
    for (const tex of textiles)
    {
        const pie = estimateClothResolutionPIE(tex);
        assert.ok(Number.isFinite(pie.warpOptimalMm) && pie.warpOptimalMm > 0);
        assert.ok(Number.isFinite(pie.authoringSpacingMm) && pie.authoringSpacingMm >= 8.5 && pie.authoringSpacingMm <= 19.0);
        console.log(`  · ${tex.name.padEnd(28)} | ${String(tex.gsm).padStart(3)} GSM | PIE=${pie.authoringSpacingMm.toFixed(2)} mm | rWarp=${pie.warpOptimalMm.toFixed(2)} mm`);
    }
}

function testPatternCompilationAndExports(mannequin)
{
    console.log('\n[2/5] Validating 6 Garment Presets, M:N Seams, Layer Order & SVG/DXF/OBJ Exports...');
    const presets = Object.keys(GARMENT_PRESET_CATALOG);
    assert.equal(presets.length, 6, 'Expected exactly 6 garment presets');

    for (const gKey of presets)
    {
        const tex = TEXTILE_CATALOG[GARMENT_PRESET_CATALOG[gKey].defaultTextile];
        const pie = estimateClothResolutionPIE(tex);
        const spec = buildGarmentPresetSpec(gKey, mannequin);
        const net = compileGarmentNetwork(spec, mannequin, { particleSpacingMm: pie.authoringSpacingMm, textile: tex });

        assert.ok(net.pointCount > 250, `${gKey} too coarse: ${net.pointCount}`);
        assert.ok(net.faceCount > 350, `${gKey} too few tris: ${net.faceCount}`);
        assert.ok(net.seamCount > 15, `${gKey} has insufficient seams: ${net.seamCount}`);
        for (let f = 0; f < net.faceCount; ++f)
        {
            assert.ok(net.faceRestArea[f] > 1e-8 && Number.isFinite(net.faceRestArea[f]), `${gKey} degenerate tri ${f}`);
        }
        assert.equal(net.selfExclOffsets.length, net.pointCount + 1);

        // Enforce per-piece outward 3D winding & consistent hinge orientation across every triangle
        const pos = net.spawnPositions;
        for (let h = 0; h < net.hingeCount; ++h)
        {
            const p0 = net.hingeIndices[h * 4] * 3, p1 = net.hingeIndices[h * 4 + 1] * 3;
            const p2 = net.hingeIndices[h * 4 + 2] * 3, p3 = net.hingeIndices[h * 4 + 3] * 3;
            const ex = pos[p1] - pos[p0], ey = pos[p1 + 1] - pos[p0 + 1], ez = pos[p1 + 2] - pos[p0 + 2];
            const ax = pos[p2] - pos[p0], ay = pos[p2 + 1] - pos[p0 + 1], az = pos[p2 + 2] - pos[p0 + 2];
            const bx = pos[p3] - pos[p0], by = pos[p3 + 1] - pos[p0 + 1], bz = pos[p3 + 2] - pos[p0 + 2];
            const n1x = ay * ez - az * ey, n1y = az * ex - ax * ez, n1z = ax * ey - ay * ex;
            const n2x = ey * bz - ez * by, n2y = ez * bx - ex * bz, n2z = ex * by - ey * bx;
            assert.ok(n1x * n2x + n1y * n2y + n1z * n2z >= -1e-9, `${gKey} inconsistent interior hinge ${h}`);
        }
        for (let p = 0; p < net.pieces.length; ++p)
        {
            const pieceSpec = spec.pieces[p];
            const axisSpec = pieceSpec.outwardAxis || null;
            let inwardCount = 0;
            for (let f = 0; f < net.faceCount; ++f)
            {
                const a = net.faceIndices[f * 3], b = net.faceIndices[f * 3 + 1], c = net.faceIndices[f * 3 + 2];
                if (net.pieceIds[a] !== p) continue;
                const a3 = a * 3, b3 = b * 3, c3 = c * 3;
                const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
                const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
                const nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
                const cx = (pos[a3] + pos[b3] + pos[c3]) / 3, cy = (pos[a3 + 1] + pos[b3 + 1] + pos[c3 + 1]) / 3, cz = (pos[a3 + 2] + pos[b3 + 2] + pos[c3 + 2]) / 3;
                let rx = cx, ry = 0, rz = cz - 0.042;
                if (axisSpec)
                {
                    const dax = axisSpec.p1[0] - axisSpec.p0[0], day = axisSpec.p1[1] - axisSpec.p0[1], daz = axisSpec.p1[2] - axisSpec.p0[2];
                    const t = ((cx - axisSpec.p0[0]) * dax + (cy - axisSpec.p0[1]) * day + (cz - axisSpec.p0[2]) * daz) / (dax * dax + day * day + daz * daz || 1);
                    rx = cx - (axisSpec.p0[0] + t * dax); ry = cy - (axisSpec.p0[1] + t * day); rz = cz - (axisSpec.p0[2] + t * daz);
                }
                if (nx * rx + ny * ry + nz * rz < 0) inwardCount++;
            }
            assert.equal(inwardCount, 0, `${gKey} piece ${net.pieces[p].name} has ${inwardCount} inward faces`);
        }

        const svg = exportPatternSVG(net, tex, null);
        const dxf = exportPatternDXF(net, tex);
        const dummySolver = { network: net, positions: net.spawnPositions, normals: new Float32Array(net.pointCount * 3) };
        const obj = exportDrapedOBJ(dummySolver);
        assert.ok(svg.includes('<svg') && svg.includes('50 mm SCALE CHECK'));
        assert.ok(dxf.includes('1_CUT') && dxf.includes('8_SEW') && dxf.includes('EOF'));
        assert.ok(obj.includes('vt ') && obj.includes('f '));

        const layers = new Set(net.pieces.map((p) => p.layerOrder));
        console.log(`  · ${(spec.shortLabel || spec.name).padEnd(24)} | ${net.pieces.length} panels | layers=[${[...layers].join(',')}] | ${net.pointCount} pts / ${net.faceCount} Δ / ${net.seamCount} seams`);
    }
}

function testSolverMatrix(mannequin)
{
    console.log('\n[3/5] Running Full 4-Solver × 6-Garment Matrix (24 Real Drape Simulations, 90 frames each)...');
    const solverKeys = Object.keys(SOLVER_CATALOG);
    const garmentKeys = Object.keys(GARMENT_PRESET_CATALOG);
    assert.deepEqual(solverKeys, ['avbd', 'xpbd', 'mgpbd', 'fpb']);

    const results = [];
    for (const gKey of garmentKeys)
    {
        const tex = TEXTILE_CATALOG[GARMENT_PRESET_CATALOG[gKey].defaultTextile];
        const spec = buildGarmentPresetSpec(gKey, mannequin);
        // Use standardized simulation density (17mm) so 24-cell full matrix runs cleanly and consistently
        const net = compileGarmentNetwork(spec, mannequin, { particleSpacingMm: 17.0, textile: tex });

        for (const sKey of solverKeys)
        {
            const meta = SOLVER_CATALOG[sKey];
            const solver = new OrthotropicClothSolver(net, mannequin, tex, {
                solverMode: sKey,
                substeps: meta.defaultSubsteps,
                iterations: meta.defaultIterations,
            });

            const tStart = performance.now();
            for (let f = 0; f < 90; ++f)
                solver.stepFrame(1 / 60);
            const wallMs = performance.now() - tStart;
            const m = solver.metrics;

            // Strict finite & physical stability invariants
            for (let i = 0; i < solver.positions.length; ++i)
                assert.ok(Number.isFinite(solver.positions[i]), `Non-finite pos in ${gKey}/${sKey}`);

            let minY = 1e9, maxY = -1e9;
            for (let i = 0; i < net.pointCount; ++i)
            {
                const y = solver.positions[i * 3 + 1];
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }

            assert.ok(minY > 0.25 && maxY < 1.85, `${gKey}/${sKey} dropped or out of bounds: [${minY.toFixed(3)}, ${maxY.toFixed(3)}]`);
            assert.ok(m.fitDriftPct < 9.5, `${gKey}/${sKey} area-weighted strain drift too high: ${m.fitDriftPct.toFixed(2)}%`);
            assert.ok(m.seamResidualMm < 8.5, `${gKey}/${sKey} seam gapped: ${m.seamResidualMm.toFixed(2)} mm`);
            assert.ok(m.mannequinPenetrationPct < 1.5, `${gKey}/${sKey} penetration too high: ${m.mannequinPenetrationPct.toFixed(2)}%`);
            assert.ok(m.meanFoldAngleDeg > 1.2, `${gKey}/${sKey} failed to form cloth folds: ${m.meanFoldAngleDeg.toFixed(2)}°`);

            results.push({ gKey, sKey, m, wallMs, minY });
            console.log(
                `  ✓ ${gKey.padEnd(18)} × ${sKey.padEnd(5)} | ${(wallMs / 90).toFixed(2).padStart(5)} ms/fr | drift=${m.fitDriftPct.toFixed(2).padStart(4)}% | maxS=${m.maxFaceStretch.toFixed(2)} | seam=${m.seamResidualMm.toFixed(2).padStart(4)}mm | pen=${m.mannequinPenetrationPct.toFixed(2)}% | fold=${m.meanFoldAngleDeg.toFixed(1).padStart(4)}° | minY=${minY.toFixed(3)}m`
            );
        }
    }
    return results;
}

function testDynamicFeatures(mannequin)
{
    console.log('\n[4/5] Testing Hot Solver Switching, Wind Drag, Grab Vertex, Morph Re-collision & Steam Relax...');
    const tex = TEXTILE_CATALOG.mulberrySilk;
    const spec = buildGarmentPresetSpec('biasSilkTunic', mannequin);
    const net = compileGarmentNetwork(spec, mannequin, { particleSpacingMm: 17.0, textile: tex });
    const solver = new OrthotropicClothSolver(net, mannequin, tex, { solverMode: 'avbd' });

    for (let f = 0; f < 30; ++f) solver.stepFrame(1 / 60);
    // Hot-switch AVBD -> MGPBD -> FPB -> XPBD without losing draped state
    for (const nextKey of ['mgpbd', 'fpb', 'xpbd', 'avbd'])
    {
        const meta = SOLVER_CATALOG[nextKey];
        solver.switchSolverHot(nextKey, meta.defaultSubsteps, meta.defaultIterations);
        for (let f = 0; f < 15; ++f) solver.stepFrame(1 / 60);
        assert.ok(Number.isFinite(solver.metrics.maxStrainPct));
    }

    // Wind test (positive tangential drag must push garment downwind)
    let meanX0 = 0;
    for (let i = 0; i < net.pointCount; ++i) meanX0 += solver.positions[i * 3];
    meanX0 /= net.pointCount;
    solver.windEnabled = true;
    solver.windSpeed = 5.5;
    solver.windAngleDeg = 90; // +X direction
    for (let f = 0; f < 40; ++f) solver.stepFrame(1 / 60);
    let meanX1 = 0;
    for (let i = 0; i < net.pointCount; ++i) meanX1 += solver.positions[i * 3];
    meanX1 /= net.pointCount;
    assert.ok(meanX1 > meanX0 + 0.005, `Wind +X did not displace garment (+X: ${meanX0.toFixed(4)} -> ${meanX1.toFixed(4)})`);
    solver.windEnabled = false;

    // Interactive grab test
    let hemIdx = 0, lowestY = 1e9;
    for (let i = 0; i < net.pointCount; ++i)
    {
        if (solver.positions[i * 3 + 1] < lowestY) { lowestY = solver.positions[i * 3 + 1]; hemIdx = i; }
    }
    const grabStartY = solver.positions[hemIdx * 3 + 1];
    solver.grabVertex = hemIdx;
    solver.grabTarget = [solver.positions[hemIdx * 3] + 0.14, grabStartY + 0.14, solver.positions[hemIdx * 3 + 2]];
    for (let f = 0; f < 25; ++f) solver.stepFrame(1 / 60);
    assert.ok(solver.positions[hemIdx * 3 + 1] > grabStartY + 0.06, 'Grab target did not lift hem vertex');
    solver.grabVertex = -1;

    // Steam relax
    solver.steamRelax();
    for (let f = 0; f < 20; ++f) solver.stepFrame(1 / 60);
    assert.ok(solver.metrics.fitDriftPct < 8.0, `High strain drift after steam relax: ${solver.metrics.fitDriftPct}%`);

    // GPU Table Packing verification
    const gpuTables = packGpuNetworkTables(solver);
    assert.equal(gpuTables.u32Topo[18], net.faceCount);
    assert.equal(gpuTables.u32Topo[19], net.hingeCount);
    assert.ok(gpuTables.f32Rest.length >= net.faceCount * 4);
    console.log(`  ✓ Hot-switch, wind (+${((meanX1 - meanX0) * 1000).toFixed(1)} mm), grab (+${((solver.positions[hemIdx * 3 + 1] - grabStartY) * 1000).toFixed(1)} mm), steam relax & WebGPU table packing passed.`);
}

function testMorphCollisionSafety(mannequin)
{
    console.log('\n[5/5] Testing Anthropometric Morph Expansion Against Draped Garment...');
    const tex = TEXTILE_CATALOG.cottonPoplin;
    const spec = buildGarmentPresetSpec('coutureDress', mannequin);
    const net = compileGarmentNetwork(spec, mannequin, { particleSpacingMm: 17.0, textile: tex });
    const solver = new OrthotropicClothSolver(net, mannequin, tex, { solverMode: 'avbd' });

    for (let f = 0; f < 60; ++f) solver.stepFrame(1 / 60);
    mannequin.configureMorphs({ bust: 0.65, hips: 0.65 });
    for (let f = 0; f < 30; ++f) solver.stepFrame(1 / 60);
    assert.ok(solver.metrics.mannequinPenetrationPct < 2.0, `Expanded morph penetration too high: ${solver.metrics.mannequinPenetrationPct}%`);
    console.log(`  ✓ Expanded body morph penetration = ${solver.metrics.mannequinPenetrationPct.toFixed(2)}% (limit < 2.0%).`);
}

function main()
{
    const t0 = performance.now();
    const mannequin = loadMannequin();
    testMannequinAndTextiles(mannequin);
    testPatternCompilationAndExports(mannequin);
    testSolverMatrix(mannequin);
    testDynamicFeatures(mannequin);
    testMorphCollisionSafety(mannequin);
    console.log(`\nALL CLOTH ATELIER VALIDATIONS PASSED in ${((performance.now() - t0) / 1000).toFixed(2)}s!`);
}

main();
