/*==========================================================================================================================================
                                                      SDFSCULPTVALIDATION.MJS
==========================================================================================================================================*/
// Automated end-to-end validation suite for Frontier Experimental Remastered — SDF Sculpt Studio.
// Run via: node Experimental/SdfSculptEditor/SdfSculptValidation.mjs

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
    SHAPE_TYPES,
    BOOL_OPS,
    EncodeFloat16,
    DecodeFloat16,
    EvaluatePrimitiveLocal,
    EvaluateCsgNode,
    PackCsgUniformArray,
    ExpandSymmetryTargets,
    FALLOFF_PRESETS,
    EvaluateFalloff
} from './Engine/SdfMath.js';
import { SdfVolume } from './Engine/SdfVolume.js';
import {
    SCULPT_BRUSHES,
    KITBASH_LIBRARY,
    MakeCsgNode,
    BuildStarterScene
} from './Engine/SdfPresets.js';
import {
    ExtractSurfaceNetsMesh,
    EncodeSdfSpaceBinary,
    DecodeSdfSpaceBinary,
    ExportMeshObj,
    ExportMeshBinaryStl,
    SerializeSceneToml
} from './Engine/SdfMesherAndCodec.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
let Passed = 0;

function Test(Name, Fn)
{
    Fn();
    Passed++;
    console.log(`  [PASS] ${Name}`);
}

console.log('=== Frontier SDF Sculpt Studio Validation Suite ===');

Test('1. IEEE-754 Float16 encode/decode round-trip precision', () =>
{
    const Samples = [0.0, -0.0, 1.0, -1.0, 0.04166, -0.725, 2.0, -2.0, 0.0005];
    for (const Val of Samples)
    {
        const H = EncodeFloat16(Val);
        const Back = DecodeFloat16(H);
        assert.ok(Math.abs(Back - Val) < 1e-3, `Float16 mismatch for ${Val}: got ${Back}`);
    }
});

Test('2. All 12 analytical SDF primitives & 5 boolean operators evaluate cleanly', () =>
{
    assert.equal(SHAPE_TYPES.length, 12);
    assert.equal(BOOL_OPS.length, 5);
    for (const [, ShapeId] of SHAPE_TYPES)
    {
        const Inside = EvaluatePrimitiveLocal(ShapeId, 0, 0, 0, 0.5, 0.5, 0.5);
        const Far = EvaluatePrimitiveLocal(ShapeId, 3.0, 0, 0, 0.5, 0.5, 0.5);
        assert.ok(Number.isFinite(Inside) && Number.isFinite(Far), `Non-finite SDF for shape ${ShapeId}`);
        assert.ok(Far > Inside, `Far distance should exceed origin distance for shape ${ShapeId}`);
    }

    const Node = MakeCsgNode({
        shape: 1, op: 0, position: [0.3, 0.1, -0.2], rotation: [15, 35, -10],
        size: [0.4, 0.3, 0.25], round: 0.04, mirrorX: true, radialCount: 4
    });
    const D = EvaluateCsgNode(Node, 0.3, 0.1, -0.2);
    assert.ok(D < 0, `Expected point inside CSG node to have negative distance, got ${D}`);

    const Packed = PackCsgUniformArray([Node]);
    assert.equal(Packed.Count, 1);
    assert.equal(Packed.Buffer.length, 24 * 5 * 4);
});

Test('3. Falloff curves & mirror/radial symmetry expansion', () =>
{
    for (const Key of Object.keys(FALLOFF_PRESETS))
    {
        const Curve = { Mode: Key, Custom: false, Points: FALLOFF_PRESETS[Key].Points };
        assert.ok(EvaluateFalloff(0.0, Curve) > 0.9);
        assert.ok(EvaluateFalloff(1.0, Curve) <= 0.01);
    }
    const Targets = ExpandSymmetryTargets([0.4, 0.2, 0.3], [0, 1, 0], [1, 0, 0], { x: true, y: false, z: true, radial: 1 });
    assert.equal(Targets.length, 4);
    const RadTargets = ExpandSymmetryTargets([0.5, 0.1, 0.0], [0, 1, 0], [1, 0, 0], { x: false, y: false, z: false, radial: 6 });
    assert.equal(RadTargets.length, 6);
});

Test('4. Starter scenes, RGBA16F dirty sub-brick packer, and hybrid CPU raycast', () =>
{
    const Vol = new SdfVolume(64);
    for (const Key of ['sculpt-bust', 'mech-pod', 'gyroid-relic', 'rock-monolith'])
    {
        const Scene = BuildStarterScene(Key, Vol);
        assert.ok(Scene.csgNodes.length >= 2, `Expected CSG nodes in starter scene ${Key}`);
        const Brick = Vol.consumeDirtySubBrick();
        assert.ok(Brick && Brick.data instanceof Uint16Array);
        assert.equal(Brick.data.length, Brick.width * Brick.height * Brick.depth * 4);
        assert.equal(Vol.consumeDirtySubBrick(), null, 'Dirty sub-brick should clear after consume');

        const Hit = Vol.raycastHybrid([0, 0.1, 3.2], [0, 0, -1], Scene.csgNodes, true);
        assert.ok(Hit.hit, `Expected raycast hit on starter scene ${Key}`);
    }
});

Test('5. All 10 volumetric sculpting brushes + Undo/Redo + CSG bake + Eikonal solver', () =>
{
    assert.equal(SCULPT_BRUSHES.length, 10);
    assert.equal(KITBASH_LIBRARY.length, 18);

    const Vol = new SdfVolume(48);
    Vol.seedClayPrimitive(0, [0.75, 0.75, 0.75], [0.6, 0.3, 0.2], 0);
    Vol.pushHistorySnapshot('Seed Sphere', []);

    const BaseSettings = {
        radius: 0.28,
        strength: 0.75,
        smoothness: 0.35,
        spacing: 0.20,
        lazyRadius: 0.0,
        paintWithSculpt: true,
        brushColor: [0.8, 0.4, 0.2],
        stampShape: 1,
        stampOp: 0,
        falloffCurve: { Mode: 'smooth', Custom: false, Points: FALLOFF_PRESETS.smooth.Points },
        symmetry: { x: true, y: false, z: false, radial: 1 }
    };

    for (const Brush of SCULPT_BRUSHES)
    {
        const Hit = Vol.raycastHybrid([0.22, 0.12, 2.5], [0, 0, -1], [], true);
        assert.ok(Hit.hit);
        Vol.beginStroke(Brush.id, Hit.pos, Hit.normal, BaseSettings);
        Vol.continueStroke([Hit.pos[0] + 0.08, Hit.pos[1] + 0.06, Hit.pos[2] + 0.04], Hit.normal, BaseSettings);
        Vol.endStroke();
        Vol.pushHistorySnapshot(Brush.short, []);
    }

    assert.ok(Vol.canUndo());
    const PrevIdx = Vol.historyIndex - 2;
    Vol.restoreHistoryAt(PrevIdx);
    assert.ok(Vol.canRedo());

    // Bake CSG cutter & union
    const Cutter = MakeCsgNode({ shape: 0, op: 1, position: [0, 0.65, 0.45], size: [0.35, 0.35, 0.35], blend: 0.05 });
    Vol.bakeCsgNodes([Cutter], false);
    Vol.normalizeEikonal(2);

    const Tel = Vol.computeTelemetry();
    assert.ok(Tel.occupiedVoxels > 500, 'Expected occupied interior voxels after sculpting');
    assert.ok(Tel.meanEikonal > 0.6 && Tel.meanEikonal < 1.4, `Unexpected Eikonal gradient ${Tel.meanEikonal}`);
});

Test('6. Dual Surface Nets watertight mesh extraction + OBJ & Binary STL codecs', () =>
{
    const Vol = new SdfVolume(48);
    Vol.seedClayPrimitive(0, [0.8, 0.8, 0.8], [0.5, 0.35, 0.25], 0);
    const ExtraNode = MakeCsgNode({ shape: 4, op: 0, position: [0, 0, 0], size: [0.85, 0.16, 0.85], blend: 0.08 });

    const Mesh = ExtractSurfaceNetsMesh(Vol, [ExtraNode], { resolution: 48 });
    assert.ok(Mesh.vertexCount > 400, `Expected >400 vertices, got ${Mesh.vertexCount}`);
    assert.ok(Mesh.triangleCount > 800, `Expected >800 triangles, got ${Mesh.triangleCount}`);
    assert.ok(Mesh.volumeLiters > 500, `Expected positive volume in liters, got ${Mesh.volumeLiters}`);

    const Obj = ExportMeshObj(Mesh, 'ValidationSculpt');
    assert.ok(Obj.includes('\nv ') && Obj.includes('\nvn ') && Obj.includes('\nf '));

    const Stl = ExportMeshBinaryStl(Mesh, 'ValidationSculpt');
    assert.equal(Stl.byteLength, 84 + Mesh.triangleCount * 50);
});

Test('7. Frontier SDF1 (.sdfspace) 68-byte binary archive round-trip & .toml spec', () =>
{
    const Src = new SdfVolume(48);
    Src.seedClayPrimitive(1, [0.65, 0.5, 0.45], [0.4, 0.5, 0.6], 0.08);
    const Bytes = EncodeSdfSpaceBinary(Src, [], false);
    assert.equal(Bytes.byteLength, 68 + 48 * 48 * 48 * 2);
    assert.equal(String.fromCharCode(Bytes[0], Bytes[1], Bytes[2], Bytes[3]), 'SDF1');

    const Dst = new SdfVolume(32);
    const Meta = DecodeSdfSpaceBinary(Bytes, Dst);
    assert.equal(Meta.nx, 48);
    assert.equal(Dst.nx, 48);

    const D0 = Src.sampleDistance(0.15, -0.1, 0.2);
    const D1 = Dst.sampleDistance(0.15, -0.1, 0.2);
    assert.ok(Math.abs(D0 - D1) < 2e-3, `SDF1 round-trip drift too large: ${D0} vs ${D1}`);

    const Toml = SerializeSceneToml({
        name: 'Test Scene',
        volumeRes: Dst.nx,
        csgNodes: [MakeCsgNode({ name: 'Test Sphere' })],
        symmetry: { x: true, y: false, z: false, radial: 1 },
        material: { preset: 'terracotta', roughness: 0.6, metallic: 0.02, subsurface: 0.35, cavity: 0.5 },
        stage: { lens: 'pbr', sunAzimuth: 38, sunElevation: 46 }
    }, Dst.computeTelemetry());
    assert.ok(Toml.includes('[workspace]') && Toml.includes('[volume]') && Toml.includes('[[csg_node]]'));
});

Test('8. DOM ID parity, self-hosted fonts/licenses, and zero external CDN links', () =>
{
    const Html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
    const Css = fs.readFileSync(path.join(__dirname, 'SdfSculptEditor.css'), 'utf8');
    const Js = fs.readFileSync(path.join(__dirname, 'SdfSculptEditor.js'), 'utf8');

    // Every getElementById('...') in SdfSculptEditor.js must exist in index.html
    const IdRegex = /getElementById\('([^']+)'\)/g;
    let Match;
    const MissingIds = [];
    while ((Match = IdRegex.exec(Js)) !== null)
    {
        const Id = Match[1];
        if (!Html.includes(`id="${Id}"`)) MissingIds.push(Id);
    }
    assert.deepEqual(MissingIds, [], `Missing HTML element IDs referenced by JS: ${MissingIds.join(', ')}`);

    // No external CDN URLs in HTML or CSS (excluding W3C SVG XML namespace)
    const StripW3cNs = S => S.replace(/http:\/\/www\.w3\.org\/2000\/svg/g, '');
    assert.ok(!/https?:\/\//i.test(StripW3cNs(Html)), 'index.html must not contain external http/https CDN links');
    assert.ok(!/https?:\/\//i.test(StripW3cNs(Css)), 'SdfSculptEditor.css must not contain external http/https CDN links');

    // Verify self-hosted fonts & licenses exist
    const RequiredFonts = [
        'dm-sans-latin-300-normal.woff2',
        'dm-sans-latin-400-normal.woff2',
        'dm-sans-latin-500-normal.woff2',
        'fa-solid-900.woff2',
        'OFL.txt',
        'LICENSE-Font-Awesome.txt'
    ];
    for (const F of RequiredFonts)
    {
        const Full = path.join(__dirname, 'Fonts', F);
        assert.ok(fs.existsSync(Full) && fs.statSync(Full).size > 100, `Missing or empty font/license: ${F}`);
    }
});

Test('9. Out-of-bounds Elastic Auto-Expand, Soft Cushion Cap closure & grazing raymarch safety', () =>
{
    const Vol = new SdfVolume(64);
    Vol.seedClayPrimitive(0, [0.65, 0.65, 0.65], [0.68, 0.42, 0.31], 0);
    Vol.pushHistorySnapshot('Initial 4m', []);
    assert.equal(Vol.span, 4.0);

    const Settings = {
        radius: 0.26,
        strength: 0.75,
        smoothness: 0.34,
        spacing: 0.18,
        paintWithSculpt: true,
        brushColor: [0.8, 0.5, 0.3],
        falloffCurve: { Mode: 'smooth', Custom: false, Points: FALLOFF_PRESETS.smooth.Points },
        symmetry: { x: false, y: false, z: false, radial: 1 }
    };

    // (a) Off-silhouette floating clay dab: verify grazing rays hit it cleanly without Lipschitz overstep slicing
    Vol.beginStroke('clay', [1.35, 0.25, 0.0], [0, 0, 1], Settings);
    Vol.endStroke();
    const GrazingHit = Vol.raycastHybrid([1.35, 0.36, 3.0], [0, 0, -1], [], true);
    assert.ok(GrazingHit.hit, 'Expected grazing ray to hit off-surface clay dab without overstepping');

    // (b) Sculpting past +2.0m auto-expands domain bounds and preserves both center & out-of-bounds clay
    Vol.beginStroke('clay', [1.85, 0.0, 0.0], [1, 0, 0], Settings);
    Vol.continueStroke([2.45, 0.0, 0.0], [1, 0, 0], Settings);
    Vol.endStroke();
    Vol.pushHistorySnapshot('Out of bounds stroke', []);

    assert.ok(Vol.max[0] > 2.6, `Expected auto-expanded max[0] > 2.6, got ${Vol.max[0]}`);
    assert.ok(Vol.sampleDistance(0, 0.05, 0) < 0, 'Center clay must remain intact after auto-expand');
    assert.ok(Vol.sampleDistance(2.40, 0, 0) < 0, 'Out-of-bounds clay at x=2.40m must be captured inside expanded domain');

    // Undo restores previous 4.0m bounds
    Vol.undo();
    assert.equal(Vol.span, 4.0);
    Vol.redo();
    assert.ok(Vol.span > 4.5);

    // (c) Soft Cushion Cap guarantees outer voxel shell stays strictly positive (d > 0 -> zero flat cutoff)
    Vol.boundsMode = 'soft';
    const WallX = Vol.max[0] - 0.02;
    Vol.beginStroke('clay', [WallX, 0, 0], [1, 0, 0], Settings);
    Vol.endStroke();
    const N = Vol.nx;
    for (let Z = 0; Z < N; ++Z)
    {
        for (let Y = 0; Y < N; ++Y)
        {
            assert.ok(Vol.distances[(Z * N + Y) * N + (N - 1)] > 0, 'Outer +X boundary texel must remain > 0 (closed manifold)');
        }
    }

    // (d) Compact / Fit Bounds re-centers around actual sculpted clay
    const CompactedSpan = Vol.compactBoundsToClay(3.2, 0.45);
    assert.ok(CompactedSpan >= 3.2 && CompactedSpan <= Vol.maxAutoSpan);
});

console.log(`\nAll ${Passed}/${Passed} validation suites passed.`);
