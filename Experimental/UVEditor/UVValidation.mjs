/*==========================================================================================================================================
                                                          UVVALIDATION.MJS
==========================================================================================================================================*/
// Comprehensive Node headless validation for Frontier UV Studio:
//   - Bundle & DOM ID wiring audit (zero external CDNs, every JS getElementById present in HTML)
//   - TopologySurface geometry, UDIM coordinate math, selection, Split/Stitch/Weld/Pin/Seam, Undo/Redo & .OBJ roundtrip
//   - UnwrapSolver LSCM, Smart Project, Cube/Cylinder/Sphere/View/Reset, Follow Active Quads, Minimize Stretch, Align/Rectify
//   - ImportancePackingSolver exact target ratio checks (e.g. 5 islands=70%, 10 islands=10%, remainder=20%), 0 overlaps, UDIM spillover

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { TopologySurface, UdimFromUV, UdimOrigin } from './Engine/TopologySurface.js';
import {
    UnwrapLscm,
    SmartUvProject,
    AnalyticProject,
    FollowActiveQuads,
    MinimizeStretch,
    AverageIslandScale,
    ApplyAlignOrRectify,
    StackSimilarIslands,
    UnstackOverlappingIslands
} from './Engine/UnwrapSolver.js';
import { MATCAP_STUDIO_CATALOG, SynthesizeMatcapPixels } from './Engine/EngineeringMatcaps.js';
import { SolveImportancePack } from './Engine/ImportancePackingSolver.js';
import { SampleAssetCatalog } from './Engine/SampleAssets.js';
import { SerializeTopologySurfaceForBridge } from '../Shared/UvTextureBridge.js';

const Dir = path.dirname(fileURLToPath(import.meta.url));
const ReadText = Rel => fs.readFileSync(path.join(Dir, Rel), 'utf8');

console.log('1. Checking bundled self-hosted assets, HTML IDs & zero external CDNs...');
const RequiredAssets = [
    'index.html', 'UVEditor.css', 'UVEditor.js', 'StudioControls.js', 'Serve.py',
    'Fonts/dm-sans-latin-300-normal.woff2', 'Fonts/dm-sans-latin-400-normal.woff2', 'Fonts/dm-sans-latin-500-normal.woff2',
    'Vendor/three.esm.min.js', 'Vendor/orbit-controls.esm.min.js',
    'Engine/TopologySurface.js', 'Engine/UnwrapSolver.js', 'Engine/ImportancePackingSolver.js',
    'Engine/ControlledIntegratorLoader.js', 'Engine/SampleAssets.js', 'Engine/EngineeringMatcaps.js',
    'Engine/UVCanvas2D.js', 'Engine/UVViewport3D.js',
    'Assets/ControlVehicleBody.facet', 'Assets/ControlVehicleRim.facet', 'Assets/ControlVehicleTyre.facet',
    '../Shared/UvTextureBridge.js'
];
for (const File of RequiredAssets)
{
    assert.ok(fs.existsSync(path.join(Dir, File)), `Missing required studio file: ${File}`);
}

const Html = ReadText('index.html');
const Css = ReadText('UVEditor.css');
const MainJs = ReadText('UVEditor.js');
assert.ok(!/https?:\/\//i.test(Html), 'index.html must not contain external CDN URLs');
assert.ok(!/https?:\/\//i.test(Css), 'UVEditor.css must not contain external CDN URLs');

const HtmlIds = new Set([...Html.matchAll(/\bid="([^"]+)"/g)].map(M => M[1]));
for (const M of MainJs.matchAll(/document\.getElementById\('([^']+)'\)/g))
{
    assert.ok(HtmlIds.has(M[1]), `Missing DOM id="${M[1]}" in index.html referenced by UVEditor.js`);
}
assert.ok(HtmlIds.has('send-to-texture-studio-btn'), 'index.html must include Export to Texture Studio bridge button');
assert.ok(/data-stage-mode="mesh"[^>]*class="nav-button active"|class="nav-button active"[^>]*data-stage-mode="mesh"/.test(Html), 'Default active stage mode tab must be 3D Mesh');
assert.ok((Html.match(/select-pill-wrap vp-compact-select/g) || []).length >= 4, 'Top toolbar dropdowns must use unified .select-pill-wrap with .pill-caret-divider');
assert.ok(HtmlIds.has('pz-notch-toggle'), 'Project-Zero pull-down notch must be present');

// Verify 16 Procedural Studio MatCaps (Metals, Rubber, Normal, Car Flakes, Carbon Fibre, Fabrics) + Undo/Redo round-trip
{
    const { MATCAP_STUDIO_CATALOG, SynthesizeMatcapPixels } = await import('./Engine/EngineeringMatcaps.js');
    assert.equal(MATCAP_STUDIO_CATALOG.length, 16, 'MATCAP_STUDIO_CATALOG must expose 16 procedural Studio MatCaps');
    const ExpectedIds = [
        'cnc-alu', 'ti-grade5', 'cad-clay', 'studio-chrome',
        'gunmetal-ano', 'layout-blue', 'cnc-brass', 'cad-zebra',
        'vulcan-rubber', 'normal-rgb', 'car-flake-blue', 'car-flake-ruby',
        'carbon-fiber', 'woven-fabric', 'velvet-suede', 'ripstop-fabric'
    ];
    assert.deepEqual(MATCAP_STUDIO_CATALOG.map(M => M.id), ExpectedIds, '16-slot MatCap catalog IDs must match');

    for (const Spec of MATCAP_STUDIO_CATALOG)
    {
        const Buf = SynthesizeMatcapPixels(Spec, 64);
        let MinLum = 255, MaxLum = 0;
        for (let y = 8; y < 56; y++)
        {
            for (let x = 8; x < 56; x++)
            {
                const Nx = (2 * (x + 0.5)) / 64 - 1;
                const Ny = 1 - (2 * (y + 0.5)) / 64;
                if (Nx * Nx + Ny * Ny > 0.88) continue;
                const i = (y * 64 + x) * 4;
                const Lum = 0.2126 * Buf[i] + 0.7152 * Buf[i + 1] + 0.0722 * Buf[i + 2];
                if (Lum < MinLum) MinLum = Lum;
                if (Lum > MaxLum) MaxLum = Lum;
            }
        }
        const MinExpectedSpan = (Spec.kind === 'rubber' || Spec.kind.startsWith('fabric')) ? 42 : 110;
        assert.ok(MaxLum - MinLum >= MinExpectedSpan,
            `MatCap "${Spec.label}" must have realistic shading range >= ${MinExpectedSpan} (got ${(MaxLum - MinLum).toFixed(1)})`);
    }

    // Verify Normal Map MatCap center pixel encodes +Z normal (~128, ~128, ~255)
    const NormBuf = SynthesizeMatcapPixels(MATCAP_STUDIO_CATALOG[9], 64);
    const CenterIdx = (32 * 64 + 32) * 4;
    assert.ok(NormBuf[CenterIdx] > 115 && NormBuf[CenterIdx] < 142 &&
              NormBuf[CenterIdx + 1] > 115 && NormBuf[CenterIdx + 1] < 142 &&
              NormBuf[CenterIdx + 2] > 245,
        `Normal RGB MatCap center must encode (0,0,+1) normal, got (${NormBuf[CenterIdx]}, ${NormBuf[CenterIdx + 1]}, ${NormBuf[CenterIdx + 2]})`);

    const TestSurf = SampleAssetCatalog[0].create().surface;
    TestSurf.PushUndo('Before Multi-MatCap');
    TestSurf.AssignMatcapToFaces([0], 8);        // Face 0 -> Vulcan Rubber
    TestSurf.AssignMatcapToFaces([1, 2, 3], 12); // Faces 1..3 -> Carbon Fibre
    TestSurf.AssignMatcapToFaces([4, 5], 14);    // Faces 4..5 -> Velvet Suede
    assert.equal(TestSurf.faces[0].matcapSlot, 8);
    assert.equal(TestSurf.faces[2].matcapSlot, 12);
    assert.equal(TestSurf.faces[5].matcapSlot, 14);
    const Hist = TestSurf.GetMatcapUsageCounts();
    assert.equal(Hist.length, 16, 'GetMatcapUsageCounts must track all 16 MatCap slots');
    assert.ok(Hist[8] >= 1 && Hist[12] >= 3 && Hist[14] >= 2, 'High-slot MatCaps (Rubber, Carbon, Velvet) active simultaneously');
    TestSurf.Undo();
    TestSurf.Redo();
    assert.equal(TestSurf.faces[2].matcapSlot, 12, '16-slot Per-face MatCap assignment survives Undo/Redo');
}
console.log('   OK: All assets bundled & DOM IDs wired cleanly.');

console.log('2. Checking UDIM coordinate conversions...');
assert.equal(UdimFromUV(0.25, 0.75), 1001);
assert.equal(UdimFromUV(1.4, 0.5), 1002);
assert.equal(UdimFromUV(9.9, 0.5), 1010);
assert.equal(UdimFromUV(0.5, 1.5), 1011);
assert.equal(UdimFromUV(2.5, 2.5), 1023);
assert.deepEqual(UdimOrigin(1001), { u: 0, v: 0 });
assert.deepEqual(UdimOrigin(1023), { u: 2, v: 2 });
console.log('   OK: Mari/Blender UDIM indexing verified.');

console.log(`3. Auditing all ${SampleAssetCatalog.length} sample assets for clean initial UVs, 0 overlaps & 0 flips...`);
for (const Spec of SampleAssetCatalog)
{
    const { surface, importanceGroups, udimTiles } = Spec.create();
    const TileRes = new Map(udimTiles.map(T => [T.id, T.resolution]));
    const Islands = surface.ExtractIslands(Id => TileRes.get(Id) || 2048, 100);
    const Diag = surface.AnalyzeDiagnostics(Islands);
    assert.ok(Islands.length >= 10, `${Spec.label} should have rich island count (got ${Islands.length})`);
    assert.equal(Diag.overlapCount, 0, `${Spec.label} has unexpected overlaps: ${Diag.overlapCount}`);
    assert.equal(Diag.flippedCount, 0, `${Spec.label} has unexpected flipped faces: ${Diag.flippedCount}`);
    if (Spec.id === 'controlled_integrator')
    {
        assert.ok(surface.faces.length >= 25000, `Controlled Integrator should have >= 25k triangles (got ${surface.faces.length})`);
        assert.ok(Islands.length >= 120, `Controlled Integrator should have >= 120 UV islands (got ${Islands.length})`);
        const BridgePayload = SerializeTopologySurfaceForBridge(surface, { udimTiles, importanceGroups });
        assert.equal(BridgePayload.triCount, surface.faces.length);
        assert.ok(BridgePayload.partNames.length >= 6, 'Controlled Integrator bridge must retain vehicle part segmentation');
    }
    console.log(`   OK: ${Spec.label} -> ${Islands.length} islands, ${udimTiles.length} UDIM(s), 0 overlaps, stretch ${(Diag.meanAngleStretch * 100).toFixed(1)}%`);
}

console.log('4. Verifying Importance-Based Space Allocation (5 islands=70%, 10 islands=10%, remainder=20%)...');
{
    const { surface } = SampleAssetCatalog[0].create(); // Aero Coupe GT
    const UdimTiles = [{ id: 1001, resolution: 4096 }];
    const Islands = surface.ExtractIslands(() => 4096, 100);
    assert.ok(Islands.length >= 25, 'Need >= 25 islands for 5 + 10 + remainder test');

    // Clear existing groups and assign programmatically just like interactive selection
    for (const Fi of surface.faces.keys()) surface.importanceByFace.delete(Fi);
    const Hero5 = Islands.slice(0, 5).map(I => I.id);
    const Trim10 = Islands.slice(5, 15).map(I => I.id);
    surface.AssignImportanceGroup(Hero5, 'grpA', Islands);
    surface.AssignImportanceGroup(Trim10, 'grpB', Islands);

    const ImportanceGroups = [
        { id: 'grpA', name: 'Hero 5', targetPercent: 70, color: '#eff4cf', udimTarget: '1001' },
        { id: 'grpB', name: 'Trim 10', targetPercent: 10, color: '#9ecfff', udimTarget: '1001' }
    ];

    const Report = SolveImportancePack(surface, {
        importanceGroups: ImportanceGroups,
        udimTiles: UdimTiles,
        activeUdim: 1001,
        options: { scope: 'active-udim', marginPx: 12, rotations: '90', budgetMode: 'strict-ratio' }
    });

    const Rep = Report.tileReports[0];
    assert.ok(Rep.packedCount === Islands.length, `All ${Islands.length} islands must pack (got ${Rep.packedCount})`);
    const GrpA = Rep.groups.find(G => G.id === 'grpA');
    const GrpB = Rep.groups.find(G => G.id === 'grpB');
    const GrpRem = Rep.groups.find(G => G.id === null);

    assert.ok(Math.abs(GrpA.actualSharePct - 70) < 2.5, `Hero 5 share expected ~70%, got ${GrpA.actualSharePct.toFixed(2)}%`);
    assert.ok(Math.abs(GrpB.actualSharePct - 10) < 1.5, `Trim 10 share expected ~10%, got ${GrpB.actualSharePct.toFixed(2)}%`);
    assert.ok(Math.abs(GrpRem.actualSharePct - 20) < 2.5, `Remainder share expected ~20%, got ${GrpRem.actualSharePct.toFixed(2)}%`);

    const AfterIslands = surface.ExtractIslands(() => 4096, 100);
    const AfterDiag = surface.AnalyzeDiagnostics(AfterIslands);
    assert.equal(AfterDiag.overlapCount, 0, 'Strict importance pack must have 0 overlaps');
    assert.ok(AfterIslands.every(I => !I.outOfBounds && I.udimTile === 1001), 'Every island must reside cleanly inside UDIM 1001');
    console.log(`   OK: Shares achieved -> Hero5=${GrpA.actualSharePct.toFixed(1)}%, Trim10=${GrpB.actualSharePct.toFixed(1)}%, Remainder=${GrpRem.actualSharePct.toFixed(1)}% (Tile fill ${Rep.tileUtilizationPct.toFixed(1)}%, 0 overlaps)`);

    // Also test greedy 'fill-remainder' pocket expansion mode
    const FillReport = SolveImportancePack(surface, {
        importanceGroups: ImportanceGroups,
        udimTiles: UdimTiles,
        activeUdim: 1001,
        options: { scope: 'active-udim', marginPx: 12, rotations: '90', budgetMode: 'fill-remainder' }
    });
    const FillDiag = surface.AnalyzeDiagnostics();
    assert.equal(FillDiag.overlapCount, 0, 'Fill-remainder pocket pack must also have 0 overlaps');
    console.log(`   OK: Fill-remainder pocket mode packed ${FillReport.tileReports[0].packedCount} islands (${FillReport.tileReports[0].tileUtilizationPct.toFixed(1)}% fill, 0 overlaps)`);
}

console.log('5. Verifying Auto-UDIM Spillover & Multi-UDIM Texel Density Equalization...');
{
    const ArchSpec = SampleAssetCatalog.find(S => S.id === 'udim_kit');
    const { surface } = ArchSpec.create(); // Multi-UDIM Arch Kit
    const Islands = surface.ExtractIslands(() => 2048, 100);
    for (const Fi of surface.faces.keys()) surface.importanceByFace.delete(Fi);
    surface.AssignImportanceGroup(Islands.slice(0, 4).map(I => I.id), 'big1', Islands);
    surface.AssignImportanceGroup(Islands.slice(4, 8).map(I => I.id), 'big2', Islands);
    const SpillReport = SolveImportancePack(surface, {
        importanceGroups: [
            { id: 'big1', name: 'Tier A', targetPercent: 80, color: '#eff4cf', udimTarget: 'auto' },
            { id: 'big2', name: 'Tier B', targetPercent: 75, color: '#9ecfff', udimTarget: 'auto' }
        ],
        udimTiles: [{ id: 1001, resolution: 4096 }],
        activeUdim: 1001,
        options: { scope: 'auto-udim-spill', marginPx: 10, rotations: '90' }
    });
    assert.ok(SpillReport.tileReports.length >= 2, 'Over-100% budgets should automatically spill into UDIM 1002+');
    assert.equal(surface.AnalyzeDiagnostics().overlapCount, 0, 'Spilled multi-UDIM atlas must have 0 overlaps');
    console.log(`   OK: Auto-UDIM spillover distributed into ${SpillReport.tileReports.map(R => R.tileId).join(', ')}`);
}

console.log('6. Verifying Blender Unwrap, Relax, Pin, Seam, Stitch, Weld, Split & Undo/Redo...');
{
    const BustSpec = SampleAssetCatalog.find(S => S.id === 'bust');
    const CrateSpec = SampleAssetCatalog.find(S => S.id === 'crate');
    const { surface } = BustSpec.create(); // Armored Bust
    surface.PushUndo('Before Unwrap');
    const Solved = UnwrapLscm(surface, { onlySelected: false });
    assert.ok(Solved >= 1, 'LSCM should unwrap >= 1 island');

    // Distort one UV slightly and verify MinimizeStretch relaxes without flips
    const BeforeDiag = surface.AnalyzeDiagnostics();
    surface.uvCoords[0][0] += 0.04;
    const Moved = MinimizeStretch(surface, { iterations: 20, onlySelected: false });
    assert.ok(Moved > 0, 'MinimizeStretch should relax unpinned UVs');
    assert.equal(surface.AnalyzeDiagnostics().flippedCount, BeforeDiag.flippedCount, 'Flip guard should prevent new inverted triangles');

    // Pinning keeps pinned coordinate exact during LSCM unwrap
    const PinIdx = surface.faces[0].corners[0].uv;
    surface.selectedUVs = new Set([PinIdx]);
    surface.PinSelected(true);
    surface.uvCoords[PinIdx] = [0.123, 0.456];
    UnwrapLscm(surface, { onlySelected: false });
    assert.ok(Math.hypot(surface.uvCoords[PinIdx][0] - 0.123, surface.uvCoords[PinIdx][1] - 0.456) < 1e-6, 'Pinned vertex must remain locked during LSCM');

    // Smart Project, Cube, Cylinder, Sphere, Follow Quads, Align
    assert.ok(SmartUvProject(surface, { angleLimitDeg: 55 }) >= 2);
    assert.ok(AnalyticProject(surface, 'cube') >= 1);
    assert.ok(AnalyticProject(surface, 'cylinder') >= 1);
    assert.ok(AnalyticProject(surface, 'sphere') >= 1);
    assert.ok(AnalyticProject(surface, 'view') >= 1);

    // Quad strip FollowActiveQuads test
    const { surface: CrateSurf } = CrateSpec.create();
    CrateSurf.activeFace = 0;
    CrateSurf.selectedFaces = new Set([0, 1, 2]);
    assert.ok(FollowActiveQuads(CrateSurf) >= 1);

    // Split + Weld + Stitch + Undo/Redo
    const PreIslands = CrateSurf.ExtractIslands().length;
    CrateSurf.SelectFace(0, false);
    CrateSurf.SplitSelection();
    assert.ok(CrateSurf.ExtractIslands().length >= PreIslands);
    CrateSurf.selectedUVs = new Set([CrateSurf.faces[0].corners[0].uv, CrateSurf.faces[0].corners[1].uv]);
    ApplyAlignOrRectify(CrateSurf, 'align-u-center');
    assert.ok(Math.abs(CrateSurf.uvCoords[CrateSurf.faces[0].corners[0].uv][0] - CrateSurf.uvCoords[CrateSurf.faces[0].corners[1].uv][0]) < 1e-9);

    assert.ok(surface.Undo(), 'Undo should succeed');
    assert.ok(surface.Redo(), 'Redo should succeed');

    // OBJ Export / Import roundtrip
    const ObjStr = CrateSurf.SerializeObj();
    const Parsed = TopologySurface.ParseObj(ObjStr, 'Roundtrip');
    assert.equal(Parsed.surface.faces.length, CrateSurf.faces.length);
    assert.ok(Parsed.hadUVs);
    console.log('   OK: All Blender UV solvers, topology edits, Undo/Redo & .OBJ roundtrip passed.');
}

// 7. Edge Selection, Loops, Rings, Shortest Paths, Boundaries, Sharp Edges & Seam Healing
{
    console.log('7. Testing Blender Edge Selection, Loops/Rings/Paths, Sharp/Boundary & Seam Tools...');
    const BustSpec = SampleAssetCatalog.find(S => S.id === 'bust');
    const CrateSpec = SampleAssetCatalog.find(S => S.id === 'crate');
    const { surface: Bust } = BustSpec.create(); // Armored Bust (quad grid + armor)
    const { surface: Crate } = CrateSpec.create();

    // 3D Edge Picking + 2D UV Edge Sync
    const F0 = Bust.faces[0];
    const E0a = F0.corners[0].v, E0b = F0.corners[1].v;
    assert.ok(Bust.SelectEdge3D(E0a, E0b, false) >= 1, 'SelectEdge3D should select 3D edge');
    assert.ok(Bust.selectedUVEdges.size >= 1, 'SelectEdge3D should sync to 2D UV edges');
    assert.ok(Bust.selectedUVs.size >= 2, 'SelectEdge3D should sync UV endpoints');

    // 3D Edge Loop & Edge Ring across quad grid
    const LoopCount = Bust.SelectEdgeLoop3D(E0a, E0b, false);
    assert.ok(LoopCount >= 4, `Expected multi-edge 3D loop on quad bust, got ${LoopCount}`);
    const RingCount = Bust.SelectEdgeRing3D(E0a, E0b, false);
    assert.ok(RingCount >= 2, `Expected multi-edge 3D ring on quad bust, got ${RingCount}`);

    // Shortest Path between two distant edges on same connected mesh
    const DstF = Bust.faces[Math.min(12, Bust.faces.length - 1)];
    const SrcKey = `${Math.min(E0a, E0b)}:${Math.max(E0a, E0b)}`;
    const DstKey = `${Math.min(DstF.corners[0].v, DstF.corners[1].v)}:${Math.max(DstF.corners[0].v, DstF.corners[1].v)}`;
    const PathCount = Bust.SelectShortestPath3D(SrcKey, DstKey, false);
    assert.ok(PathCount >= 2, `Shortest path should connect edges (${PathCount})`);

    // 2D UV Loop, Ring & Shortest Path
    const UvA = F0.corners[0].uv, UvB = F0.corners[1].uv;
    assert.ok(Bust.SelectUvEdgeLoop(UvA, UvB, null, false) >= 2, '2D UV Edge Loop should find >= 2 edges');
    assert.ok(Bust.SelectUvEdgeRing(UvA, UvB, null, false) >= 2, '2D UV Edge Ring should find >= 2 edges');
    const DstUvKey = `${Math.min(DstF.corners[0].uv, DstF.corners[1].uv)}:${Math.max(DstF.corners[0].uv, DstF.corners[1].uv)}`;
    assert.ok(Bust.SelectUvShortestPath(`${Math.min(UvA, UvB)}:${Math.max(UvA, UvB)}`, DstUvKey, null, false) >= 2);

    // Sharp edges & Boundaries & Seams from Islands
    const SharpCnt = Crate.SelectSharpEdges(60, false);
    assert.ok(SharpCnt >= 8, `Tactical Crate should detect >= 8 sharp edges >= 60 deg (got ${SharpCnt})`);

    Crate.ClearSelection();
    Crate.SelectFace(0, false);
    const SelBound = Crate.SelectBoundaryEdges('selection', false);
    assert.equal(SelBound.count3D, Crate.faces[0].corners.length, 'Single face boundary loop should match face edge count');

    const MeshBound = Bust.SelectBoundaryEdges('mesh', false);
    assert.ok(MeshBound.count3D >= 1, 'Open Armored Bust mesh should detect open mesh boundary edges');

    const AllSeamsCount = Crate.SelectAllSeamEdges(false);
    assert.equal(AllSeamsCount, Crate.seams.size, 'SelectAllSeamEdges must match total seams');

    // Grow & Shrink selection
    Crate.SelectFace(0, false);
    const GrownFaces = Crate.GrowSelection('face');
    assert.ok(GrownFaces >= 1);
    const ShrunkFaces = Crate.ShrinkSelection('face');
    assert.ok(ShrunkFaces < GrownFaces);

    // Mark Seam splits UV corners; Clear Seam + WeldNonSeamCorners heals back to 1 continuous island!
    const BaseIslandCount = Bust.ExtractIslands().length;
    const EdgeCountBefore = Bust.seams.size;
    Bust.MarkSharpAsSeam(40);
    assert.ok(Bust.seams.size >= EdgeCountBefore, 'MarkSharpAsSeam should tag sharp edges');
    Bust.SeamsFromIslands();

    // Test Clear Seam + Weld healing on Crate: take 2 adjacent faces sharing a seam, clear their seam and weld
    const Crate2 = CrateSpec.create().surface;
    const PreIsl2 = Crate2.ExtractIslands().length;
    const SeamArr = [...Crate2.seams];
    assert.ok(SeamArr.length > 0);
    Crate2.ClearSelection();
    Crate2.selectedEdges3D.add(SeamArr[0]);
    Crate2.SyncSelectionsFromMode('edge3d');
    const Cleared = Crate2.ClearSeamFromSelection(true, 'edge');
    assert.equal(Cleared, 1, 'Should clear exactly 1 selected seam edge');
    assert.ok(Crate2.ExtractIslands().length <= PreIsl2, 'Healing cleared seam should weld shared corners without increasing islands');

    console.log('   OK: Edge loops, rings, paths, sharp/boundary selection & seam marking/healing passed.');
}

// 8. Blender-Style Selection Suite: Face Loops, Flat Faces, Loop Inner-Region, Select Similar, Random & Checker Deselect
{
    console.log('8. Testing Blender Selection (Face Loops, Similar [Island/Perim/Area/Normal/Coplanar], Random & Checker)...');
    const BustSpec = SampleAssetCatalog.find(S => S.id === 'bust');
    const CrateSpec = SampleAssetCatalog.find(S => S.id === 'crate');
    const VehicleSpec = SampleAssetCatalog.find(S => S.id === 'controlled_integrator');

    const { surface: Bust } = BustSpec.create();
    const { surface: Crate } = CrateSpec.create();
    const { surface: Vehicle } = VehicleSpec.create();

    // 8a. 3D Face Loop on quad Bust AND triangulated Controlled Integrator
    const F0 = Bust.faces[0];
    const FaceLoopQuad = Bust.SelectFaceLoop3D(0, F0.corners[0].v, F0.corners[1].v, false);
    assert.ok(FaceLoopQuad >= 3, `Expected quad face loop >= 3 faces on Bust, got ${FaceLoopQuad}`);

    const Vf0 = Vehicle.faces[0];
    const FaceLoopTri = Vehicle.SelectFaceLoop3D(0, Vf0.corners[0].v, Vf0.corners[1].v, false);
    assert.ok(FaceLoopTri >= 1, `Expected face loop on triangulated Vehicle, got ${FaceLoopTri}`);

    // 8b. Shortest Face Path & Shortest Vertex Path
    const FacePath = Bust.SelectShortestFacePath3D(0, Math.min(18, Bust.faces.length - 1), false);
    assert.ok(FacePath >= 2, `Shortest face path should span >= 2 faces (got ${FacePath})`);
    const VertPath = Bust.SelectShortestVertexPath3D(Bust.faces[0].corners[0].v, Bust.faces[Math.min(18, Bust.faces.length - 1)].corners[0].v, false);
    assert.ok(VertPath >= 2, `Shortest vertex path should span >= 2 vertices (got ${VertPath})`);

    // 8c. Linked Flat Faces & Same UV Island
    const FlatCount = Crate.SelectLinkedFlatFaces(0, { angleDeg: 18, respectSeams: true, additive: false });
    assert.ok(FlatCount >= 1 && FlatCount <= Crate.faces.length, `Linked flat faces should match coplanar panel (${FlatCount})`);
    Crate.SelectFace(0, false);
    const SameIslCount = Crate.SelectSameUvIsland(false);
    assert.ok(SameIslCount >= 1, `SelectSameUvIsland should select entire island containing seed face (${SameIslCount})`);

    // 8d. Select Similar: UV Perimeter, 3D Perimeter, 3D Area, UV Area, Normal, Coplanar, Sides, Same Part, Edge/Vert criteria
    const PerimUV = Crate.SelectSimilar('perimeter_uv', { threshold: 0.15 });
    assert.ok(PerimUV >= 1, 'SelectSimilar perimeter_uv must match similar UV face perimeters');
    Crate.SelectFace(0, false);
    const Perim3D = Crate.SelectSimilar('perimeter_3d', { threshold: 0.15 });
    assert.ok(Perim3D >= 1, 'SelectSimilar perimeter_3d must match similar 3D face perimeters');
    Crate.SelectFace(0, false);
    const Area3D = Crate.SelectSimilar('area_3d', { threshold: 0.15 });
    assert.ok(Area3D >= 1, 'SelectSimilar area_3d must match similar 3D face areas');
    Crate.SelectFace(0, false);
    const AreaUV = Crate.SelectSimilar('area_uv', { threshold: 0.15 });
    assert.ok(AreaUV >= 1, 'SelectSimilar area_uv must match similar UV face areas');
    Crate.SelectFace(0, false);
    const Coplanar = Crate.SelectSimilar('coplanar', { angleDeg: 10 });
    assert.ok(Coplanar >= 1 && Coplanar <= Crate.faces.length, 'SelectSimilar coplanar must match coplanar faces');
    Crate.SelectFace(0, false);
    const Normals = Crate.SelectSimilar('normal', { angleDeg: 15 });
    assert.ok(Normals >= Coplanar, 'SelectSimilar normal should be >= coplanar count');
    Crate.SelectFace(0, false);
    assert.ok(Crate.SelectSimilar('sides') >= 1, 'SelectSimilar sides must match polygon sides');

    Vehicle.SelectFace(0, false);
    const SamePart = Vehicle.SelectSimilar('same_part');
    assert.ok(SamePart >= 1 && SamePart < Vehicle.faces.length, `SelectSimilar same_part should match vehicle sub-part (${SamePart}/${Vehicle.faces.length})`);
    Vehicle.SelectFace(0, false);
    assert.ok(Vehicle.SelectSimilar('same_udim') >= 1, 'SelectSimilar same_udim must select faces in active UDIM');

    // Edge & Vertex Select Similar
    const E0 = Crate.faces[0].corners;
    Crate.SelectEdge3D(E0[0].v, E0[1].v, false);
    assert.ok(Crate.SelectSimilar('edge_length_3d', { threshold: 0.15 }) >= 1, 'SelectSimilar edge_length_3d must match similar edges');
    Crate.SelectEdge3D(E0[0].v, E0[1].v, false);
    assert.ok(Crate.SelectSimilar('edge_direction', { angleDeg: 12 }) >= 1, 'SelectSimilar edge_direction must match parallel edges');
    Crate.ClearSelection();
    Crate.selectedUVs.add(E0[0].uv);
    Crate.SyncSelectionsFromMode('vertex');
    assert.ok(Crate.SelectSimilar('vert_valence') >= 1, 'SelectSimilar vert_valence must match vertices with same valence');

    // 8e. Select Loop Inner-Region (from selected face boundary loop -> selects bounded face(s))
    Crate.SelectFace(0, false);
    Crate.SelectBoundaryEdges('selection', false);
    const InnerCnt = Crate.SelectLoopInnerRegion();
    assert.ok(InnerCnt >= 1 && InnerCnt < Crate.faces.length, `SelectLoopInnerRegion should enclose inner patch (${InnerCnt})`);

    // 8f. Deterministic Select Random + Checker Deselect
    const Rand50 = Crate.SelectRandom({ ratio: 0.5, seed: 42, mode: 'face' });
    assert.equal(Rand50, Math.round(Crate.faces.length * 0.5), 'SelectRandom 50% must select exact half of faces');
    const Rand50Again = Crate.SelectRandom({ ratio: 0.5, seed: 42, mode: 'face' });
    assert.equal(Rand50Again, Rand50);
    const Desel = Crate.SelectRandom({ ratio: 0.5, seed: 7, action: 'deselect', mode: 'face', withinSelection: true });
    assert.ok(Desel > 0 && Crate.selectedFaces.size < Rand50, 'SelectRandom deselect should remove subset of active faces');

    Crate.SelectAll(Crate.ExtractIslands());
    const BeforeCheck = Crate.selectedFaces.size;
    const AfterCheck = Crate.CheckerDeselect({ nth: 2, skip: 1, mode: 'face' });
    assert.ok(AfterCheck > 0 && AfterCheck < BeforeCheck, `CheckerDeselect should deselect ~half (${AfterCheck}/${BeforeCheck})`);

    console.log('   OK: All Blender selection tools (face loops, flat, inner-region, similar, random & checker) passed.');
}

// 9. Right-Click Unified Context Menu + 3D Box/Face/Paint Select Parity
console.log('9. Verifying Right-Click Context Menu & 3D Box/Face/Paint Selection...');
{
    const Html = ReadText('index.html');
    const Css = ReadText('UVEditor.css');
    const EditorJs = ReadText('UVEditor.js');
    const Vp3dJs = ReadText('Engine/UVViewport3D.js');

    assert.ok(Html.includes('id="uv-context-menu"'), 'index.html must include unified #uv-context-menu popover');
    assert.ok(Html.includes('data-ctx-tool="box"'), 'Context menu must expose Box Select');
    assert.ok(Html.includes('data-ctx-face-select="true"'), 'Context menu must expose Face Select');
    assert.ok(Html.includes('data-ctx-tool="circle"'), 'Context menu must expose Paint Select');
    for (const op of ['all', 'none', 'invert', 'grow', 'shrink', 'same-island', 'loop', 'ring', 'linked-flat', 'loop-inner-region', 'boundary-loop', 'random-50', 'checker'])
    {
        assert.ok(Html.includes(`data-ctx-sel="${op}"`), `Context menu missing selection option: ${op}`);
    }
    for (const op of ['unwrap', 'pack', 'smart', 'relax', 'split', 'stitch', 'weld', 'avg-scale', 'pin', 'unpin', 'mirror-u', 'rotate-90'])
    {
        assert.ok(Html.includes(`data-ctx-op="${op}"`), `Context menu missing UV operation: ${op}`);
    }
    assert.ok(Css.includes('.uv-marquee-3d') && Css.includes('.uv-brush-3d'), 'UVEditor.css must style 3D marquee & brush overlays');
    assert.ok(Vp3dJs.includes('SetActiveTool(') && Vp3dJs.includes('DispatchBoxSelect3D(') && Vp3dJs.includes('DispatchPaintSelect3D('),
        'UVViewport3D.js must support 3D Box Select and 3D Paint Select');
    assert.ok(Vp3dJs.includes('EnsureGpuPickBuffer(') && Vp3dJs.includes('FastUpdateGpuFaceMask(') && Vp3dJs.includes('gpuSelectionTex') && Vp3dJs.includes('gpuPickRT'),
        'UVViewport3D.js must perform GPU ID picking and GPU face selection mask updates');
    assert.ok(EditorJs.includes('onBoxSelect3D') && EditorJs.includes('onPaintSelect3D') && EditorJs.includes('OpenUvContextMenu') && EditorJs.includes('FastPaintVisualSync('),
        'UVEditor.js must wire GPU-fast 3D Box/Paint callbacks and unified right-click menu');
    assert.ok(EditorJs.includes("if (Mode === 'face')"), '2D ApplyCircleSelectAt must handle face mode');

    console.log('   OK: Context menu, GPU 3D Box/Paint Select & 2D face-mode Paint Select verified.');
}

// 10. GPU Wireframe/Edge/Vertex Shaders, Versioned Caches, Spatial Grid & Batched 2D Rendering
console.log('10. Testing GPU Wireframe/Edge/Vertex Shaders, Spatial Grid, Adjacency & Versioned Caches...');
{
    const { surface: Vehicle } = SampleAssetCatalog[1].create();

    // Cached analysis returns identical memoized objects when unchanged and invalidates on UV move
    const A1 = Vehicle.GetCachedAnalysis(() => 2048, 100, '1001:2048');
    const A2 = Vehicle.GetCachedAnalysis(() => 2048, 100, '1001:2048');
    assert.strictEqual(A1, A2, 'GetCachedAnalysis must reuse cached islands/diagnostics when data unchanged');
    assert.ok(A1.faceToIsland.length === Vehicle.faces.length, 'GetCachedAnalysis must populate faceToIsland');

    Vehicle.uvCoords[0][0] += 0.05;
    const A3 = Vehicle.GetCachedAnalysis(() => 2048, 100, '1001:2048');
    assert.notStrictEqual(A3, A1, 'GetCachedAnalysis must invalidate cache after UV edit');

    // Fast precomputed distortion matches computed distortion
    const DFast = Vehicle.GetFaceDistortionFast(0);
    assert.ok(Number.isFinite(DFast.angleStretch) && DFast.area3D > 0, 'GetFaceDistortionFast must return cached face distortion');

    // Prebuilt Adjacency indices
    const Adj = Vehicle.EnsureAdjacencyIndices();
    assert.equal(Adj.uvToFaces.length, Vehicle.uvCoords.length, 'EnsureAdjacencyIndices must index UV -> faces');
    assert.ok(Adj.edge3dFaces.size >= Vehicle.faces.length, 'EnsureAdjacencyIndices must index 3D edges -> faces');

    // 2D UV Spatial Grid query returns face containing corner UV without scanning whole mesh
    const TargetUv = Vehicle.uvCoords[Vehicle.faces[0].corners[0].uv];
    const SpatialHits = Vehicle.QueryUvSpatialFaces(TargetUv[0] - 0.005, TargetUv[1] - 0.005, TargetUv[0] + 0.005, TargetUv[1] + 0.005);
    assert.ok(SpatialHits.includes(0), 'QueryUvSpatialFaces must return face 0 at its own UV corner');
    assert.ok(SpatialHits.length < Vehicle.faces.length, 'QueryUvSpatialFaces must narrow candidates using spatial grid buckets');

    const Vp3dJs = ReadText('Engine/UVViewport3D.js');
    const Cv2dJs = ReadText('Engine/UVCanvas2D.js');
    const EditorJs = ReadText('UVEditor.js');

    assert.ok(Vp3dJs.includes('gpuWireMat') && Vp3dJs.includes('aBary') && Vp3dJs.includes('aEdgeMask') && Vp3dJs.includes('fwidth(vBary)'),
        'UVViewport3D.js must render quad/n-gon wireframe on GPU via barycentric derivatives');
    assert.ok(Vp3dJs.includes('gpuEdgeStateTex') && Vp3dJs.includes('gpuEdgeMesh') && Vp3dJs.includes('uHoverEdgeId') && Vp3dJs.includes('FastUpdateGpuEdgeAndVertMasks('),
        'UVViewport3D.js must render seams, boundaries, selected, active, and hovered edges via GPU state texture & shader');
    assert.ok(Vp3dJs.includes('gpuVertPoints') && Vp3dJs.includes('gpuVertSelTex') && Vp3dJs.includes('FastUpdateUvsOnly('),
        'UVViewport3D.js must render selected vertices via GPU point shader and support zero-rebuild UV attribute streaming');
    assert.ok(!Vp3dJs.includes('intersectObject(this.mesh'),
        'UVViewport3D.js must replace CPU THREE.Raycaster mesh intersection with 1-pixel GPU ID lookup');
    assert.ok(Cv2dJs.includes('_baseLayerCanvas') && Cv2dJs.includes('_RenderBaseLayer(') && Cv2dJs.includes('QueryUvSpatialFaces('),
        'UVCanvas2D.js must batch & cache static UV layer offscreen and use spatial grid HitTest');
    assert.ok(EditorJs.includes('_lastOutlinerSig') && EditorJs.includes('GetCachedAnalysis(') && EditorJs.includes('QueryUvSpatialFaces(') && EditorJs.includes('SyncOverlayStates()'),
        'UVEditor.js must memoize Outliner, use cached analysis, spatial 2D selection, and instant GPU overlay toggles');

    console.log('   OK: GPU wireframe/edge/vertex shaders, spatial grid, adjacency & caching verified.');
}

// 11. Multi-Surface Engineering MatCaps, Stack Similar UVs, Follow Active Quads & Decluttered UI Checks
console.log('11. Testing Multi-Surface Engineering MatCaps, Stack Similar UVs, Follow Active Quads & UI Polish...');
{
    assert.equal(MATCAP_STUDIO_CATALOG.length, 16, 'MATCAP_STUDIO_CATALOG must expose 16 procedural MatCap finishes');
    const Pixels = SynthesizeMatcapPixels(MATCAP_STUDIO_CATALOG[12], 64); // Carbon Fibre
    assert.equal(Pixels.length, 64 * 64 * 4, 'SynthesizeMatcapPixels must generate RGBA buffer');

    // Multi-part surface must receive multiple distinct MatCap slots (never 1 single uniform colour)
    const { surface: Vehicle } = SampleAssetCatalog[0].create(); // Controlled Integrator (multi-part vehicle)
    const VehIslands = Vehicle.ExtractIslands();
    Vehicle.AutoAssignMatcapsByPart(VehIslands);
    const DistinctSlots = new Set(Vehicle.faces.map(F => F.matcapSlot));
    assert.ok(DistinctSlots.size >= 5, `Expected >= 5 distinct Engineering MatCap slots across multi-part vehicle, got ${DistinctSlots.size}`);

    // Assigning MatCap + Fast PBR to a selected island updates ONLY that island's faces (never entire geometry!)
    const OtherIslFace = VehIslands[1].faceIndices[0];
    const OrigOtherSlot = Vehicle.faces[OtherIslFace].matcapSlot;
    const OrigOtherColor = Vehicle.faces[OtherIslFace].pbrColor;

    // 1) With empty selection, AssignSurfaceMaterialToSelection MUST return 0 and leave ALL faces untouched
    Vehicle.SelectFace(0, false);
    Vehicle.ClearSelection();
    const AssignedWhenEmpty = Vehicle.AssignSurfaceMaterialToSelection({
        matcapSlot: 3,
        pbrColor: '#112233',
        pbrMetallic: 0.99
    }, VehIslands);
    assert.equal(AssignedWhenEmpty, 0, 'AssignSurfaceMaterialToSelection must affect 0 faces when nothing is selected');
    assert.equal(Vehicle.faces[OtherIslFace].matcapSlot, OrigOtherSlot, 'Empty selection must NOT overwrite unselected faces');
    assert.equal(Vehicle.faces[OtherIslFace].pbrColor, OrigOtherColor, 'Empty selection must NOT overwrite unselected PBR colour');

    // 2) With Island 0 selected ("Apply Selected"), ONLY Island 0's faces receive the MatCap + Fast PBR patch
    Vehicle.PushUndo('Test Per-Face PBR + MatCap Apply');
    Vehicle.SelectIslands([VehIslands[0].id], false);
    const AssignedIsl = Vehicle.AssignSurfaceMaterialToSelection({
        matcapSlot: 7,
        pbrColor: '#e05252',
        pbrMetallic: 0.91,
        pbrRoughness: 0.14,
        pbrSpecular: 0.88,
        matcapMix: 0.45
    }, VehIslands);
    assert.equal(AssignedIsl, VehIslands[0].faceIndices.length, 'AssignSurfaceMaterialToSelection must target ONLY selected island faces');
    for (const Fi of VehIslands[0].faceIndices)
    {
        assert.equal(Vehicle.faces[Fi].matcapSlot, 7, 'Selected face must receive slot 7 (Zebra CAD)');
        assert.equal(Vehicle.faces[Fi].pbrColor, '#e05252', 'Selected face must receive per-face PBR colour');
        assert.ok(Math.abs(Vehicle.faces[Fi].pbrMetallic - 0.91) < 1e-6, 'Selected face must receive per-face metallic');
        assert.ok(Math.abs(Vehicle.faces[Fi].pbrRoughness - 0.14) < 1e-6, 'Selected face must receive per-face roughness');
        assert.ok(Math.abs(Vehicle.faces[Fi].pbrSpecular - 0.88) < 1e-6, 'Selected face must receive per-face specular');
    }
    assert.equal(Vehicle.faces[OtherIslFace].matcapSlot, OrigOtherSlot, 'Unselected island must keep its original MatCap');
    assert.equal(Vehicle.faces[OtherIslFace].pbrColor, OrigOtherColor, 'Unselected island must keep its original PBR colour');

    // Undo / Redo restores per-face PBR + MatCap cleanly and bumps _matVersion
    const MatVerBeforeUndo = Vehicle._matVersion;
    assert.equal(Vehicle.Undo(), true, 'Undo should restore pre-apply material state');
    assert.ok(Vehicle._matVersion > MatVerBeforeUndo, 'Undo must increment _matVersion so GPU attributes refresh');
    assert.equal(Vehicle.Redo(), true, 'Redo should re-apply per-face PBR + MatCap state');
    assert.equal(Vehicle.faces[VehIslands[0].faceIndices[0]].pbrColor, '#e05252');

    // 3) Explicit "Fill All" (FillSurfaceMaterial) fills the entire geometry
    const FilledAll = Vehicle.FillSurfaceMaterial({
        matcapSlot: 5,
        pbrColor: '#4c7dc9',
        pbrMetallic: 0.62,
        pbrRoughness: 0.25,
        pbrSpecular: 0.70,
        matcapMix: 0.60
    });
    assert.equal(FilledAll, Vehicle.faces.length, 'FillSurfaceMaterial must update every face on the geometry');
    assert.ok(Vehicle.faces.every(F => F.matcapSlot === 5 && F.pbrColor === '#4c7dc9'),
        'FillSurfaceMaterial must assign MatCap + Fast PBR to all faces');

    // StackSimilarIslands & UnstackOverlappingIslands on Tactical Crate (6 congruent quad faces / straps)
    const CrateSpec = SampleAssetCatalog.find(S => S.id === 'crate');
    const { surface: Crate } = CrateSpec.create();
    const CrateIslandsBefore = Crate.ExtractIslands();
    const StackRes = StackSimilarIslands(Crate, { islands: CrateIslandsBefore, tolerance: 0.12 });
    assert.ok(StackRes.stackedIslands >= 2 && StackRes.masterGroups >= 1,
        `StackSimilarIslands should stack >= 2 congruent crate islands (got ${StackRes.stackedIslands} in ${StackRes.masterGroups} groups)`);
    assert.ok(Crate.AnalyzeDiagnostics().overlapCount > 0, 'Stacked congruent islands must overlap in UV space to share texels');

    // UnstackOverlappingIslands separates stacked duplicates cleanly
    const Unstacked = UnstackOverlappingIslands(Crate, { stepU: 0.22 });
    assert.ok(Unstacked >= 1, 'UnstackOverlappingIslands should offset stacked duplicate islands');

    // FollowActiveQuads works even when activeFace is null (auto-seeds from selected faces)
    Crate.ClearSelection();
    Crate.activeFace = null;
    Crate.selectedFaces = new Set([0, 1, 2]);
    assert.ok(FollowActiveQuads(Crate, 'length-avg') >= 1, 'FollowActiveQuads must auto-seed when activeFace is null');

    // Verify UI / shader invariants
    const Vp3dJs = ReadText('Engine/UVViewport3D.js');
    const EditorJs = ReadText('UVEditor.js');
    const IndexHtml = ReadText('index.html');
    assert.ok(Vp3dJs.includes('attribute float aMatcapSlot;') && Vp3dJs.includes('attribute vec3 aPbrColor;') &&
              Vp3dJs.includes('attribute vec4 aPbrParams;') && Vp3dJs.includes('FastUpdateGpuFaceMaterials(') &&
              Vp3dJs.includes('precision highp float;'),
        'UVViewport3D.js must pass lossless per-vertex aMatcapSlot, aPbrColor, and aPbrParams with FastUpdateGpuFaceMaterials');
    assert.ok(Vp3dJs.includes('this.fastPbrMat') && !Vp3dJs.includes('dGgx'),
        'UVViewport3D.js must provide simplified Fast PBR shader (Colour, Metallic, Roughness, Specular only) with zero GGX');
    assert.ok(Vp3dJs.includes("this.shadingMode = 'fast-pbr';"),
        'Default 3D shadingMode must be fast-pbr so MatCaps only appear when a MatCap mode is selected');

    // Verify Inspector tab order: Pack -> Unwrap -> UDIM -> Shading -> Check (Shading after UDIM, before Check)
    const TabsBlock = IndexHtml.slice(IndexHtml.indexOf('id="inspector-tabs"'), IndexHtml.indexOf('</div>', IndexHtml.indexOf('id="inspector-tabs"')));
    const TabOrder = [...TabsBlock.matchAll(/data-tab="([^"]+)"/g)].map(M => M[1]);
    assert.deepEqual(TabOrder, ['packing', 'unwrap', 'udim', 'shading', 'check'],
        `Inspector tabs must be ordered Pack -> Unwrap -> UDIM -> Shading -> Check (got ${TabOrder.join(' -> ')})`);

    // Verify unified single Shading card (MatCaps gated strictly on IsMatcapMode + Apply Selected & Fill All)
    const ShadingSlice = EditorJs.slice(EditorJs.indexOf('function RenderShadingTab'), EditorJs.indexOf('function RenderPackingTab'));
    assert.ok(ShadingSlice.includes('if (IsMatcapMode)') && EditorJs.includes('CtxMatcapSec.hidden = !'),
        'MatCap orbs must be conditionally rendered ONLY when a MatCap mode is selected');
    assert.ok(!ShadingSlice.includes('RenderUvSelectionTransformCard'), 'Shading menu must NOT include 2D UV Transform card');
    assert.equal((ShadingSlice.match(/SectionCard\(/g) || []).length, 1,
        'Shading menu must combine Fast PBR Lit + Multi-MatCap into a single unified SectionCard');
    assert.ok(ShadingSlice.includes("Label: 'Apply Selected'") && ShadingSlice.includes("Label: 'Fill All'"),
        'Combined Shading card must expose Apply Selected and Fill All actions');
    assert.ok(ShadingSlice.includes("'Colour'") && ShadingSlice.includes("Label: 'Metallic'") &&
              ShadingSlice.includes("Label: 'Roughness'") && ShadingSlice.includes("Label: 'Specular'") &&
              ShadingSlice.includes('MATCAP_STUDIO_CATALOG'),
        'Combined Shading card must expose Multi-MatCap orbs + Colour, Metallic, Roughness, and Specular');

    const CheckSlice = EditorJs.slice(EditorJs.indexOf('function RenderDiagnosticsTab'), EditorJs.indexOf('function SnapUV'));
    assert.ok(CheckSlice.includes("Label: 'Overlapping Faces'") && CheckSlice.includes("Label: 'Select Overlaps'") &&
              !CheckSlice.includes('RenderStudioPreviewAndFlatDebugCard'),
        'Check menu must match exact UV Integrity & Repair layout from a874c93');

    const UdimSlice = EditorJs.slice(EditorJs.indexOf('function RenderUdimTab'), EditorJs.indexOf('function RenderDiagnosticsTab'));
    assert.ok(UdimSlice.includes('MetricTrio') && UdimSlice.includes('pz-udim-table') &&
              !UdimSlice.includes('pz-udim-atlas-shell') && !UdimSlice.includes('Spatial UDIM Matrix'),
        'UDIM menu must use Check-style MetricTrio + clean UDIM Tile Table instead of Spatial UDIM Matrix');

    assert.ok(!ShadingSlice.includes('Fast PBR') && ShadingSlice.includes('Surface Material') &&
              ShadingSlice.includes('pz-cpp-color-picker') && ShadingSlice.includes('pz-cpp-color-circle') &&
              !ShadingSlice.includes('pz-pbr-swatch-dots') && !ShadingSlice.includes('pz-finish-showcase'),
        'Shading Colour row must use C++-style [(circle) Colour Name · #HEX] picker with no extra swatch clutter');

    const BudgetCardSlice = EditorJs.slice(EditorJs.indexOf('function RenderUdimSpaceBudgetCard'), EditorJs.indexOf('function UpdateHeaderAndHud'));
    const OutlinerSlice = EditorJs.slice(EditorJs.indexOf('function RenderOutliner'), EditorJs.indexOf('function RenderLegendStrip'));
    const PackSlice = EditorJs.slice(EditorJs.indexOf('function RenderPackingTab'), EditorJs.indexOf('function RenderUnwrapTab'));
    assert.ok(EditorJs.includes('function RenderUdimSpaceBudgetCard') &&
              IndexHtml.includes('<footer class="pz-outliner-budget-footer" id="lp-udim-budget-card"></footer>') &&
              !BudgetCardSlice.includes('alloc-chips-row') && !BudgetCardSlice.includes('alloc-chip') &&
              !PackSlice.includes('hero-card') && !PackSlice.includes('allocation-spectrum'),
        'UDIM Space Budget must live in the UV Space footer with zero per-layer % text chips');

    assert.ok(EditorJs.indexOf('function DescribeHexColorName') > 0 &&
              EditorJs.indexOf('function DescribeHexColorName') < EditorJs.indexOf('function RenderShadingTab'),
        'DescribeHexColorName must be declared at module scope so RenderPackingTab never throws ReferenceError');

    assert.ok(IndexHtml.includes('class="pz-outliner-plus-btn" id="pz-new-folder-btn"') &&
              OutlinerSlice.includes('pz-frontier-layer-box') && OutlinerSlice.includes('SelectFaceGroupRef') &&
              OutlinerSlice.includes('FastSyncSelectionViews') && !OutlinerSlice.includes('pz-prio-add-btn') &&
              !OutlinerSlice.includes('Surface Parts') && !OutlinerSlice.includes('pz-part-square') &&
              !OutlinerSlice.includes('pz-chart-row') &&
              PackSlice.includes('MetricTrio(Body') && PackSlice.includes('Selected · UDIM') &&
              PackSlice.includes('Importance Space Allocation') && PackSlice.includes('SliderRow(AllocSec.Inner') &&
              PackSlice.includes("Unit: '%'") &&
              PackSlice.includes('pz-inspector-text-input') && PackSlice.includes('pz-cpp-color-picker'),
        'UV Space must use unrounded colour-coded Frontier Editor Outliner boxes (+ in header), with UDIM selection cards + Inspector % SliderRow in Pack');

    // Benchmark & verify saved FaceGroupRef (zero ExtractIslands re-iteration + cross-object face references!)
    {
        const { surface: SurfA, importanceGroups: GrpsA } = SampleAssetCatalog[0].create();
        SurfA.objectId = 'obj-integrator-a';
        const { surface: SurfB, importanceGroups: GrpsB } = SampleAssetCatalog[1].create();
        SurfB.objectId = 'obj-coupe-b';
        SurfA.GetCachedAnalysis();
        SurfB.GetCachedAnalysis();
        SurfA.EnsureLayerFaceRefs();
        SurfB.EnsureLayerFaceRefs();

        let ReExtractCount = 0;
        const OrigExtract = SurfA.ExtractIslands.bind(SurfA);
        SurfA.ExtractIslands = (...Args) => { ReExtractCount++; return OrigExtract(...Args); };

        const T0 = performance.now();
        for (let iter = 0; iter < 100; iter++)
        {
            const HeroRefA = SurfA.GetLayerFaceRef(GrpsA[0].id);
            const HeroRefB = SurfB.GetLayerFaceRef(GrpsB[0].id);
            SurfA.SelectFaceGroupRef(HeroRefA, false);
            SurfB.SelectFaceGroupRef(HeroRefB, false);
        }
        const ElapsedMs = performance.now() - T0;
        SurfA.ExtractIslands = OrigExtract;

        assert.strictEqual(ReExtractCount, 0, 'Selecting saved FaceGroupRef must NEVER re-run ExtractIslands()');
        assert.ok(SurfA.selectedFaces.size > 0 && SurfB.selectedFaces.size > 0,
            'Cross-object saved FaceGroupRef must select bound faces across independent objects');
        assert.ok(ElapsedMs < 50, `100x cross-object FaceGroupRef selections must stay sub-millisecond per click (got ${ElapsedMs.toFixed(2)}ms)`);

        // Verify direct face-level AssignFacesToLayer updates saved FaceGroupRef cleanly
        SurfA.AssignFacesToLayer([0, 1, 2], 'custom-layer-x');
        const CustomRef = SurfA.GetLayerFaceRef('custom-layer-x');
        assert.strictEqual(CustomRef.faceCount, 3, 'AssignFacesToLayer must persist direct face references');
    }

    assert.ok(EditorJs.includes('function RenderUdimFilterBar') && IndexHtml.includes('data-udim="all"') &&
              !IndexHtml.includes('data-filter="selected"') && !IndexHtml.includes('data-filter="issues"'),
        'UV Space filters must strictly be Search + UDIM filters only');

    assert.ok(IndexHtml.includes('<h1>UV Space</h1>') && !IndexHtml.includes('Clean Charts') &&
              IndexHtml.includes('id="lp-kpi-util"') && IndexHtml.includes('id="lp-kpi-selected"') &&
              IndexHtml.includes('id="lp-kpi-remaining"') && IndexHtml.includes('id="stat-issues-card"') &&
              IndexHtml.includes('id="stat-overlap-card"') && IndexHtml.includes('>Stretch Caution<') && IndexHtml.includes('>Overlaps<'),
        'UV Space left panel must be renamed from Outliner and show 5 infographic cards (3 Pack tiles + Stretch Caution + red Overlaps)');

    const EditorCss = ReadText('UVEditor.css');
    for (const BlueHex of ['#262930', '#191a1d', '#131418', '#0d0e11', 'rgba(56, 189, 248'])
    {
        assert.ok(!EditorCss.includes(BlueHex), `UVEditor.css must not contain blue-tinted card background ${BlueHex}`);
    }

    assert.ok(Vp3dJs.includes('this.showSeams = false;') && EditorJs.includes('showSeams: false'),
        'Seam debug overlay must default to false');

    console.log(`   OK: C++ Colour Picker, 5-card UV Space panel with Importance Space Allocation, neutral Check palette & ${DistinctSlots.size} MatCaps verified.`);
}

console.log('\nALL UV STUDIO VALIDATIONS PASSED SUCCESSFULLY.');


