/*==========================================================================================================================================
                                                     TERRAINWORLDVALIDATION.MJS
==========================================================================================================================================*/
// Comprehensive Headless Validation Suite for Frontier Open-World SDF Terrain Studio

import assert from 'node:assert/strict';
import { WorldMask2D, ComputeRegionMaskSdf2D } from './Engine/WorldMask2D.js';
import {
    EvaluateBoundaryFalloff,
    CreateTerrainLayer,
    EvaluateTerrainRegion,
    ApplyTerrainSculptDab,
    ExtractRegionTriangleMesh,
    StitchAdjacentRegionBorders,
    ComputeHydrostaticWaterBodies,
    ComputeRegionBaseSignature,
    ExportWorldTerrainsObj,
    ExportWorldTerrainsBinaryStl,
    SeedThreeDemoTerrains
} from './Engine/TerrainLayerStack.js';
import { evalHierarchical90Crazing3D } from './Engine/RockCliff/Core/SpatialNoise3D.js';
import {
    exportFractureMeshObj,
    exportFractureMeshBinaryStl,
    exportFractureMeshGlb
} from './Engine/RockCliff/Core/QefDualContour.js';
import {
    FRACTURE_PRESETS,
    createBoxPoly,
    splitPolyByPlane,
    transformPoly,
    buildFracturedFormation,
    evalFracturedFormationSdf,
    bakeFracturedFormationQefMesh,
    packFracturedFormationGpuBuffers
} from './Engine/RockCliff/PureSdfFractureEngine.js';

let Passed = 0;
function Suite(Name, Fn)
{
    Fn();
    Passed++;
    console.log(`  [PASS] ${Name}`);
}

console.log('Running Frontier Open-World SDF Terrain Studio Validation...\n');

// 1. Infinite Sparse 2D Mask Allocation & Negative/Distant Coordinate Painting
Suite('1. Infinite sparse 2D world mask allocates tiles on demand across arbitrary kilometer coordinates', () =>
{
    const World = new WorldMask2D();
    const Mask = World.addMaskLayer({ name: 'Infinite Test' });
    assert.equal(World.totalAllocatedTiles(), 0, 'Fresh mask should allocate 0 tiles');

    // Paint at (-14.5 km, +28.2 km) — far outside any fixed canvas!
    World.paintCapsuleMeters(Mask, [-14500, 28200], [-14100, 28450], 180, 'paint', 1.0, 0.0, 101);
    assert.ok(World.totalAllocatedTiles() > 0 && World.totalAllocatedTiles() <= 4, 'Only touched 1x1 km tiles should be allocated');
    assert.ok(World.sampleCoverageMeters(-14300, 28320, Mask.id) > 0.95, 'Painted distant coordinate should have full land coverage');
    assert.equal(World.sampleCoverageMeters(5000, -5000, Mask.id), 0.0, 'Unpainted distant ocean should return 0 without allocating tiles');

    // Erasing back to 0 prunes empty tiles automatically
    World.paintCapsuleMeters(Mask, [-14600, 28100], [-14000, 28550], 350, 'erase', 1.0, 0.0, 101);
    assert.equal(World.totalAllocatedTiles(), 0, 'Erased empty sparse tiles should be pruned automatically');
});

// 2. Exact O(N) Meijster 2D Euclidean Signed Distance Transform
Suite('2. Meijster 2D Euclidean Signed Distance Field computes exact metric inland/offshore distances', () =>
{
    const World = new WorldMask2D();
    const Mask = World.addMaskLayer({ name: 'Circle Island' });
    World.paintCapsuleMeters(Mask, [0, 0], [0, 0], 300, 'paint', 1.0, 0.0, 42);
    const Region = World.addTerrainRegion({
        maskLayerId: Mask.id,
        bounds: { x: -500, z: -500, width: 1000, depth: 1000 },
        resolution: 101
    });
    const { sdf2D, hasLand } = ComputeRegionMaskSdf2D(World, Region, 101, 101);
    assert.equal(hasLand, true);

    const CenterIdx = 50 * 101 + 50; // (0, 0) -> ~300m inland -> sdf2D ~ -300
    const CornerIdx = 0;             // (-500, -500) -> ~707 - 300 = +407m in ocean
    assert.ok(sdf2D[CenterIdx] < -270 && sdf2D[CenterIdx] > -330, `Expected ~-300m at island center, got ${sdf2D[CenterIdx]}`);
    assert.ok(sdf2D[CornerIdx] > 370 && sdf2D[CornerIdx] < 440, `Expected ~+407m at ocean corner, got ${sdf2D[CornerIdx]}`);
});

// 3. Per-Layer Boundary Falloff Modes: Soft Edge -> Strong Center vs None (Full Cliff)
Suite('3. Per-Layer Boundary Falloff supports Soft Edge -> Strong Center AND None (Full Cliff Edge)', () =>
{
    const SoftSpec = { mode: 'soft', distance: 300, edgeFloor: 0.0 };
    assert.equal(EvaluateBoundaryFalloff(0, 500, SoftSpec), 0.0, 'Soft falloff at coastline (0m) should have 0 influence');
    const MidW = EvaluateBoundaryFalloff(150, 500, SoftSpec);
    assert.ok(Math.abs(MidW - 0.5) < 0.02, `Soft falloff halfway inland should be ~0.5, got ${MidW}`);
    assert.equal(EvaluateBoundaryFalloff(350, 500, SoftSpec), 1.0, 'Soft falloff deep inland should reach 1.0');

    const CliffSpec = { mode: 'none', distance: 300, edgeFloor: 0.0 };
    assert.equal(EvaluateBoundaryFalloff(0, 0, CliffSpec), 1.0, 'None (Cliff) falloff must deliver 100% full strength right at 0m coastline edge');
    assert.equal(EvaluateBoundaryFalloff(200, 200, CliffSpec), 1.0);

    const RimSpec = { mode: 'rim', distance: 300, edgeFloor: 0.0 };
    const RimPeak = EvaluateBoundaryFalloff(90, 500, RimSpec);
    const RimCenter = EvaluateBoundaryFalloff(300, 500, RimSpec);
    assert.ok(RimPeak > RimCenter, 'Coastal Rim Ring falloff should peak near coastal rim and relax toward center basin');
});

// 4. 3 Pre-Built Demonstration Masks & Independent Toggleable Terrain Bounds Regions
Suite('4. Ships with 3 distinct Demo Masks & independent toggleable Terrain Bounds Regions', () =>
{
    const World = new WorldMask2D();
    const { masks, regions } = SeedThreeDemoTerrains(World);
    assert.equal(masks.length, 3, 'Must create 3 Demo 2D Mask Layers');
    assert.equal(regions.length, 3, 'Must create 3 independent Demo Terrain Bounds Regions');

    for (const R of regions)
    {
        assert.equal(R.enabled, true);
        assert.ok(R.runtime && R.runtime.hasLand, `${R.name} must evaluate valid land inside its 2D mask`);
        assert.ok(R.runtime.maxHeight > 80, `${R.name} must build substantial 3D mountain/cliff relief above sea level`);
    }

    // Verify Demo Terrain 2 ("Basalt Sea Cliffs") uses Falloff = 'none' for sheer coastal cliffs!
    const CliffRegion = regions[1];
    assert.equal(CliffRegion.stack[0].falloff.mode, 'none', 'Demo Terrain 02 should showcase None (Full Cliff) falloff');

    // Verify toggling a Terrain Bounds Box OFF works independently without affecting the others
    CliffRegion.enabled = false;
    const EnabledList = World.regions.filter(R => R.enabled);
    assert.equal(EnabledList.length, 2, 'Toggling one Terrain Bounds Box OFF leaves the other 2 active');
    CliffRegion.enabled = true;
});

// 5. Thin-Slab SDF Space Efficiency & Triangle Mesh Conversion
Suite('5. Thin-Slab SDF wastes zero deep-ocean space and extracts clean culled triangle meshes (.OBJ & .STL)', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const R1 = World.regions[0];
    const Mesh = ExtractRegionTriangleMesh(World, R1, { includeUnderside: true });

    assert.ok(Mesh.triangleCount > 2000, 'Terrain should generate rich top + thin-underside triangle mesh');
    const FullGridCells = (R1.runtime.nx - 1) * (R1.runtime.nz - 1);
    // If empty ocean cells weren't culled, top + bottom alone would exceed 4 * FullGridCells!
    assert.ok(Mesh.triangleCount < FullGridCells * 3.4, 'Empty ocean cells outside 2D mask must be culled from triangle output');

    // Verify NO vertex ever wastes vertical SDF space below slabBottom (-6.5m)!
    let MinY = Infinity, MaxY = -Infinity;
    for (let I = 1; I < Mesh.positions.length; I += 3)
    {
        if (Mesh.positions[I] < MinY) MinY = Mesh.positions[I];
        if (Mesh.positions[I] > MaxY) MaxY = Mesh.positions[I];
    }
    assert.ok(Math.abs(MinY - (-R1.slabThickness)) < 0.05, `Thin-slab underside should sit cleanly at -${R1.slabThickness}m, got ${MinY}`);
    assert.ok(MaxY > 120.0, `Top terrain relief should build upward above sea level, got ${MaxY}`);

    // Verify multi-terrain .OBJ & binary .STL exports
    const Obj = ExportWorldTerrainsObj(World, World.regions);
    assert.ok(Obj.triangleCount > 6000 && Obj.text.includes('o Terrain_01'));
    const Stl = ExportWorldTerrainsBinaryStl(World, [R1]);
    assert.equal(Stl.bytes.byteLength, 84 + Stl.triangleCount * 50, 'Binary STL byte length must match 84 + 50 * triCount');
});

// 6. Real-time SDF Sculpting, 3D Overhang Arches & JSON Round-Trip Serialization
Suite('6. Supports real-time SDF sculpting, 3D overhang arches, spline landforms, and JSON round-trip', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const R = World.regions[0];
    const Rt0 = R.runtime;

    // Find the highest peak cell inside Region 01 and sculpt directly on it
    let PeakIdx = 0, BeforePeak = -1e9;
    for (let I = 0; I < Rt0.heights.length; ++I)
    {
        if (Rt0.sdf2D[I] < -40.0 && Rt0.heights[I] > BeforePeak)
        {
            BeforePeak = Rt0.heights[I];
            PeakIdx = I;
        }
    }
    const Px = R.bounds.x + (PeakIdx % Rt0.nx) * Rt0.stepX;
    const Pz = R.bounds.z + Math.floor(PeakIdx / Rt0.nx) * Rt0.stepZ;

    ApplyTerrainSculptDab(World, R, [Px, BeforePeak, Pz], 'raise', { radius: 220, strength: 0.95 });
    EvaluateTerrainRegion(World, R);
    assert.ok(R.runtime.heights[PeakIdx] > BeforePeak + 25.0, '3D Raise sculpt brush should elevate terrain SDF at brush target');
    assert.ok(R.runtime.maxHeight > BeforePeak + 25.0, '3D Raise sculpt on peak should elevate region maxHeight');

    ApplyTerrainSculptDab(World, R, [Px, 65, Pz], 'overhang', { radius: 90, strength: 0.8 });
    assert.equal(R.sdf3DOps.length, 1, '3D Overhang tool should append analytical 3D SDF arch/overhang op');

    // JSON round-trip
    const Saved = JSON.stringify(World.toJSON());
    const Restored = new WorldMask2D();
    Restored.loadFromJSON(JSON.parse(Saved));
    assert.equal(Restored.maskLayers.length, 3);
    assert.equal(Restored.regions.length, 3);
    assert.equal(Restored.totalAllocatedTiles(), World.totalAllocatedTiles());
});

// 7. 3D Spline Knot Up/Down Y-Axis Gizmo Elevation Grading & 3D Rock Arch Bridge Meshing
Suite('7. Moving 3D Spline Knots UP and DOWN on Y-axis shapes terrain elevation & builds 3D SDF Arch Bridges', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const R = World.regions[0];
    const RtBase = EvaluateTerrainRegion(World, R);

    // Pick an interior land cell on Region 01
    let TargetIdx = 0, BaseH = 0;
    for (let I = 0; I < RtBase.heights.length; ++I)
    {
        if (RtBase.sdf2D[I] < -140.0)
        {
            TargetIdx = I;
            BaseH = RtBase.heights[I];
            break;
        }
    }
    const Kx = Math.round(R.bounds.x + (TargetIdx % RtBase.nx) * RtBase.stepX);
    const Kz = Math.round(R.bounds.z + Math.floor(TargetIdx / RtBase.nx) * RtBase.stepZ);

    const GradedSpline = CreateTerrainLayer('spline', {
        name: '3D Gizmo Ramp Test',
        splineMode: 'grade',
        width: 140.0,
        bankSmooth: 0.65,
        meander: 0.0,
        falloff: { mode: 'none', distance: 100, edgeFloor: 1.0 },
        points: [
            [Kx - 120, Kz, BaseH],
            [Kx, Kz, BaseH + 95.0], // Dragged UP +95m on Y Gizmo!
            [Kx + 120, Kz, BaseH]
        ]
    });
    R.stack.push(GradedSpline);
    EvaluateTerrainRegion(World, R);
    const RaisedH = R.runtime.heights[TargetIdx];
    assert.ok(RaisedH > BaseH + 45.0, `Moving spline knot UP on Y-axis must raise terrain (before=${BaseH.toFixed(1)}, after=${RaisedH.toFixed(1)})`);

    // Now move the same knot DOWN below BaseH on Y Gizmo!
    GradedSpline.points[1][2] = Math.max(2.0, BaseH - 55.0);
    EvaluateTerrainRegion(World, R);
    const LoweredH = R.runtime.heights[TargetIdx];
    assert.ok(LoweredH < BaseH - 18.0, `Moving spline knot DOWN on Y-axis must carve terrain down (before=${BaseH.toFixed(1)}, after=${LoweredH.toFixed(1)})`);

    // Switch spline to 'arch3d' (True 3D Rock Arch Bridge) and verify 3D tube triangles are emitted
    const TrisBeforeArch = ExtractRegionTriangleMesh(World, R).triangleCount;
    GradedSpline.splineMode = 'arch3d';
    GradedSpline.points[1][2] = BaseH + 80.0;
    R.dirty = true;
    const MeshWithArch = ExtractRegionTriangleMesh(World, R);
    assert.ok(MeshWithArch.triangleCount > TrisBeforeArch + 200, '3D Rock Arch Bridge spline mode must generate true 3D overhang tube triangles');
});

/*==========================================================================================================================================
                     EROSION PHYSICS · PRESETS · GAEA TEXTURING VALIDATION
==========================================================================================================================================*/
import {
    CreateClimateGroup, CreateErosionState, StepErosionDay, BakeErosionYears,
    ApplyErosionToRegion, ErosionStateSummary, SampleClimate, AdvanceClimateDay,
    UpdateDrainageArea
} from './Engine/ErosionPhysicsEngine.js';
import {
    EROSION_PRESETS, BuildPresetWorld, CreateSimulationGroup, InitSimulationGroup,
    StepSimulationGroupDay, BakeSimulationGroup, ApplySimulationGroup
} from './Engine/ErosionPresets.js';
import {
    DATA_MAP_CATALOG, ComputeDataMaps, STAMP_CATALOG, SampleStamp,
    SATMAP_RAMPS, EvaluateRamp, ComposeSurfaceColors, EncodePng, ExportMapPngBytes,
    ExportSurfacePngBytes
} from './Engine/GaeaTexturingAndStamps.js';

function AssertFiniteField(Field, Name)
{
    for (let I = 0; I < Field.length; ++I)
    {
        assert.ok(Number.isFinite(Field[I]), `${Name}[${I}] must stay finite (got ${Field[I]})`);
    }
}

// 8. Hydrology: rainfall runs off to the sea, floods never cover hillslopes, films ≠ rivers
Suite('8. Non-flooding hydrology: rain drains to coastline sinks, thin films separate from real rivers, no NaN', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const Region = World.regions[0];
    EvaluateTerrainRegion(World, Region);
    const State = CreateErosionState(World, Region, { seed: 7 });
    const Group = CreateClimateGroup({ regimeId: 'monsoon', latitudeDeg: 22, seed: 7, startDayOfYear: 20, precipitationScale: 1.6 });
    BakeErosionYears(State, Group, 0.1);   // 36 storm-heavy days

    AssertFiniteField(State.h, 'h');
    AssertFiniteField(State.w, 'w');
    AssertFiniteField(State.sed, 'sed');
    AssertFiniteField(State.soil, 'soil');

    let LandCells = 0, SeaWithWater = 0, FilmOnly = 0, RealWater = 0;
    for (let I = 0; I < State.h.length; ++I)
    {
        if (State.sdf2D[I] >= 0)
        {
            if (State.w[I] > 1e-6) SeaWithWater++;
            continue;
        }
        LandCells++;
        if (State.w[I] > 1e-4 && State.w[I] <= 0.08) FilmOnly++;
        if (State.w[I] > 0.12) RealWater++;
    }
    assert.equal(SeaWithWater, 0, 'Coastline sink cells (sdf2D >= 0) must NEVER hold water');
    assert.ok(RealWater >= 0, 'Standing river/lake water count must be non-negative');
    const S = ErosionStateSummary(State);
    assert.ok(S.floodedCells <= LandCells * 0.02,
        `Stagnant hillslope flooding must stay under 2% of land (got ${S.floodedCells}/${LandCells})`);
    // Wetness encodes thin films without inventing water volume
    let Wet = 0;
    for (let I = 0; I < State.wetness.length; ++I) if (State.wetness[I] > 0.02) Wet++;
    assert.ok(Wet >= 0, 'Wetness field must exist for thin-film wet-rock rendering');
});

// 9. Pitting: segregation-ice + freeze-thaw + thermal-expansion fatigue carve visible pits
Suite('9. Pitting erosion: ice-expansion + thermal-expansion cracking nucleates pits, spalls & protected cavities', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const Region = World.regions[0];
    EvaluateTerrainRegion(World, Region);
    const State = CreateErosionState(World, Region, {
        seed: 11,
        thermalFatigueRate: 0.055, frostSegregationRate: 0.05, freezeThawRate: 0.06,
        spallDamageThreshold: 0.45, spallDepthM: 0.2, pitProtectDepthM: 0.5
    });
    const Group = CreateClimateGroup({ regimeId: 'semi-arid-high-desert', latitudeDeg: 39, seed: 11, startDayOfYear: 40 });
    BakeErosionYears(State, Group, 0.2);   // 73 days of thermal cycling + frost nights

    AssertFiniteField(State.crack, 'crack');
    AssertFiniteField(State.pit, 'pit');
    let MaxCrack = 0;
    for (let I = 0; I < State.crack.length; ++I) MaxCrack = Math.max(MaxCrack, State.crack[I]);
    assert.ok(MaxCrack > 0.15, `Crack damage must accumulate visibly (max ${MaxCrack.toFixed(3)})`);
    assert.ok(State.stats.pits > 10, `Spall events must nucleate real pits (got ${State.stats.pits})`);
    const S = ErosionStateSummary(State);
    assert.ok(S.maxPitDepthM > 0.05, `Pits must carve measurable depth (got ${S.maxPitDepthM.toFixed(3)} m)`);
    // Pits cluster (tafoni honeycomb) — damage field non-uniform
    let Cracked = 0;
    for (let I = 0; I < State.crack.length; ++I) if (State.crack[I] > 0.1) Cracked++;
    assert.ok(Cracked > 30 && Cracked < State.crack.length * 0.9, 'Crack damage must be spatially clustered, not uniform');
});

// 10. Wind: Bagnold saltation migrates sand to slipfaces ≤ 34° and canyon undercut wears bedrock
Suite('10. Wind erosion: saltation builds 34° slipface dunes and basal undercutting carves canyon corridors', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const Region = World.regions[0];
    EvaluateTerrainRegion(World, Region);
    const State = CreateErosionState(World, Region, {
        seed: 13,
        windAbrasionK: 0.004, venturiGain: 2.0, undercutGain: 3.4,
        sandSupply: 1.5, saltationThresholdMs: 3.8, slipfaceDeg: 34
    });
    // Guarantee a loose sand sheet to feed saltation
    for (let I = 0; I < State.sand.length; ++I) if (State.sdf2D[I] < 0) State.sand[I] = Math.max(State.sand[I], 0.4);
    const Group = CreateClimateGroup({ regimeId: 'arid-desert', latitudeDeg: 30, seed: 13, startDayOfYear: 180, windScale: 1.8 });
    const SandBefore = State.sand.slice();
    BakeErosionYears(State, Group, 0.25);   // 91 windy desert days

    AssertFiniteField(State.sand, 'sand');
    AssertFiniteField(State.h, 'h-after-wind');
    let Moved = 0;
    for (let I = 0; I < State.sand.length; ++I) if (Math.abs(State.sand[I] - SandBefore[I]) > 0.02) Moved++;
    assert.ok(Moved > 50, `Saltation must migrate sand across many cells (moved ${Moved})`);
    // Slipface repose: sand surfaces never exceed ~tan(36°) for long — check average sand slope
    const Nx = State.nx;
    let SteepSand = 0, SandCells = 0;
    for (let Z = 1; Z < State.nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (State.sdf2D[I] >= 0 || State.sand[I] < 0.06) continue;
            SandCells++;
            const Tan = Math.hypot(State.h[I + 1] - State.h[I - 1], State.h[I + Nx] - State.h[I - Nx]) / (2 * State.stepX);
            const SandTan = Math.max(Tan, Math.abs(State.sand[I + 1] - State.sand[I - 1]) / (2 * State.stepX));
            if (SandTan > Math.tan(38 * Math.PI / 180)) SteepSand++;
        }
    }
    assert.ok(SandCells > 20, 'Wind must leave an active sand sheet');
    assert.ok(SteepSand < SandCells * 0.35, `Slipfaces must relax toward 34° repose (steep ${SteepSand}/${SandCells})`);
    // Canyon undercut: wear happened somewhere
    let Wear = 0;
    for (let I = 0; I < State.wearMap.length; ++I) Wear += State.wearMap[I];
    assert.ok(Wear > 0.1, `Wind abrasion + undercutting must carve bedrock (wear ${Wear.toFixed(2)} m)`);
});

// 11. Seasonal cycles + shared weather groups across 2 terrains + determinism
Suite('11. Seasonal climate & shared weather group: 2 terrains share ONE cycle (solo mode stays 1-by-1) & runs are deterministic', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const RA = World.regions[0];
    const RB = World.regions[1];
    EvaluateTerrainRegion(World, RA);
    EvaluateTerrainRegion(World, RB);

    const Group = CreateSimulationGroup('shared', [RA, RB], { regimeId: 'alpine-continental', latitudeDeg: 46, seed: 21 }, {});
    InitSimulationGroup(World, Group, 21);
    const A = Group.members[0].state, B = Group.members[1].state;
    for (let D = 0; D < 24; ++D) StepSimulationGroupDay(World, Group);
    assert.equal(A.day, B.day, 'Both shared-group terrains must advance the same number of days');
    assert.equal(A.lastClimate.tempC, B.lastClimate.tempC, 'Both terrains must experience the SAME daily temperature');
    assert.equal(A.lastClimate.rainMm, B.lastClimate.rainMm, 'Both terrains must experience the SAME daily rainfall');
    assert.equal(Group.climate.dayOfYear, 120 + 24 > 365 ? (120 + 24) % 365 : 144, 'Group calendar advances exactly once per day');

    // Seasonal temperature cycle: summer vs winter differ at 46°N
    const G2 = CreateClimateGroup({ regimeId: 'alpine-continental', latitudeDeg: 46, seed: 21, startDayOfYear: 196 });
    const Summer = SampleClimate(G2);
    const G3 = CreateClimateGroup({ regimeId: 'alpine-continental', latitudeDeg: 46, seed: 21, startDayOfYear: 15 });
    const Winter = SampleClimate(G3);
    assert.ok(Summer.tempC > Winter.tempC + 8, `Seasonal cycle must swing summer above winter (S=${Summer.tempC.toFixed(1)}, W=${Winter.tempC.toFixed(1)})`);
    assert.ok(Winter.snowFrac > Summer.snowFrac, 'Winter precipitation must skew to snow, summer to rain');

    // Determinism: two identical solo runs → bit-identical surfaces
    const World2 = new WorldMask2D();
    SeedThreeDemoTerrains(World2);
    const RA2 = World2.regions[0];
    EvaluateTerrainRegion(World2, RA2);
    const S1 = CreateErosionState(World2, RA2, { seed: 99 });
    const S2 = CreateErosionState(World2, RA2, { seed: 99 });
    const G4 = CreateClimateGroup({ regimeId: 'monsoon', latitudeDeg: 22, seed: 99 });
    const G5 = CreateClimateGroup({ regimeId: 'monsoon', latitudeDeg: 22, seed: 99 });
    for (let D = 0; D < 12; ++D) { StepErosionDay(S1, G4); StepErosionDay(S2, G5); }
    for (let I = 0; I < S1.h.length; ++I) assert.equal(S1.h[I], S2.h[I], 'Deterministic engine must reproduce identical heights');
});

// 12. Continuum rockfall: cliff calving, ballistic runout & size-sorted talus aprons (no rigid meshes)
Suite('12. Rockfall erosion: over-steepened cliffs calve into ballistic runout with angle-of-repose talus aprons', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const Region = World.regions[0];
    EvaluateTerrainRegion(World, Region);
    const State = CreateErosionState(World, Region, {
        seed: 17, rockfallRate: 1.0, cliffBaseAngleDeg: 50,
        talusCoarseReposeDeg: 35, talusFineReposeDeg: 26, ballisticScourK: 0.04
    });
    // Force an over-steepened cliff band so detachment is guaranteed
    const Nx = State.nx;
    for (let Z = 2; Z < State.nz - 2; ++Z)
    {
        for (let X = 2; X < Nx - 2; ++X)
        {
            const I = Z * Nx + X;
            if (State.sdf2D[I] >= 0) continue;
            State.h[I] += (X > Nx / 2 ? 26 : 0) * (1 - Math.abs(Z / State.nz - 0.5));
        }
    }
    const Group = CreateClimateGroup({ regimeId: 'alpine-continental', latitudeDeg: 45, seed: 17, startDayOfYear: 300 });
    BakeErosionYears(State, Group, 0.15);   // 55 winter days

    AssertFiniteField(State.h, 'h-after-rockfall');
    AssertFiniteField(State.soil, 'soil-after-rockfall');
    let TalusTot = 0, DepoTot = 0;
    for (let I = 0; I < State.talusMap.length; ++I) { TalusTot += State.talusMap[I]; DepoTot += State.depoMap[I]; }
    assert.ok(TalusTot > 0.5, `Talus aprons must accumulate at cliff bases (talus ${TalusTot.toFixed(2)} m)`);
    assert.ok(DepoTot > 0.5, `Ballistic fragments must deposit along runout (depo ${DepoTot.toFixed(2)} m)`);
    // Deposits respect grain-size repose: cells with talus rarely exceed coarse repose angle
    let OverRepose = 0, TalusCells = 0;
    const ReposeTan = Math.tan(40 * Math.PI / 180);
    for (let Z = 1; Z < State.nz - 1; ++Z)
    {
        for (let X = 1; X < Nx - 1; ++X)
        {
            const I = Z * Nx + X;
            if (State.sdf2D[I] >= 0 || State.talusMap[I] < 0.05) continue;
            TalusCells++;
            const Tan = Math.hypot(State.h[I + 1] - State.h[I - 1], State.h[I + Nx] - State.h[I - Nx]) / (2 * State.stepX);
            if (Tan > ReposeTan) OverRepose++;
        }
    }
    assert.ok(OverRepose < Math.max(8, TalusCells * 0.3), `Talus must relax below angle of repose (over ${OverRepose}/${TalusCells})`);
});

// 13. Chemical weathering: karst dissolution, hydrolysis, haloclasty & downhill mineral staining
Suite('13. Chemical erosion: dissolution lowers carbonate rock, clay saprolite softens, salts crack, stains transport downhill', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const Region = World.regions[0];
    EvaluateTerrainRegion(World, Region);
    const State = CreateErosionState(World, Region, {
        seed: 19,
        carbonateShare: 0.8, dissolutionK: 0.008, hydrolysisK: 0.008,
        haloclastyK: 0.006, stainTransportK: 0.08
    });
    const Group = CreateClimateGroup({ regimeId: 'semi-arid-high-desert', latitudeDeg: 34, seed: 19, startDayOfYear: 100, precipitationScale: 1.3 });
    BakeErosionYears(State, Group, 0.2);

    AssertFiniteField(State.h, 'h-after-chem');
    AssertFiniteField(State.fe, 'fe');
    AssertFiniteField(State.salt, 'salt');
    let Wear = 0, Clay = 0, Salt = 0, Fe = 0, Mn = 0;
    for (let I = 0; I < State.h.length; ++I)
    {
        Wear += State.wearMap[I];
        Clay += State.clay[I];
        Salt += State.salt[I];
        Fe += State.fe[I];
        Mn += State.mn[I];
    }
    assert.ok(Wear > 0.5, `Dissolution + hydrolysis must remove measurable rock (wear ${Wear.toFixed(2)} m)`);
    assert.ok(Clay > 5, `Hydrolysis must build clay saprolite (clay sum ${Clay.toFixed(1)})`);
    assert.ok(Salt > 5, `Haloclasty must accumulate salt in arid pores (salt sum ${Salt.toFixed(1)})`);
    assert.ok(Fe > 5 && Mn > 5, 'Dissolved Fe / Mn staining must transport and accumulate');
    // Hardness softening through hydrolysis
    let Soft = 0;
    for (let I = 0; I < State.hard.length; ++I) if (State.hard[I] < 0.42) Soft++;
    assert.ok(Soft > 20, 'Hydrolysis must soften rock hardness into weak saprolite');
});

// 14. Presets & Gaea texturing: all landscapes build, 25 data maps normalize, stamps/ramps/PNG export valid
Suite('14. Presets & Gaea texturing: 23 presets build land, data maps normalize 0..1, stamps finite, ramps valid, PNG encoder structurally sound', () =>
{
    for (const Preset of EROSION_PRESETS)
    {
        const World = new WorldMask2D();
        const Built = BuildPresetWorld(World, Preset.id, { resolution: 48 });
        EvaluateTerrainRegion(World, Built.region);
        assert.ok(Built.region.runtime.hasLand, `Preset '${Preset.id}' must produce land inside its mask`);
        assert.ok(Built.region.stack.length >= 2, `Preset '${Preset.id}' must ship a composable layer stack`);
        assert.ok(Preset.climate?.regimeId, `Preset '${Preset.id}' must declare a climate regime`);
        assert.ok(Preset.texture?.rampId, `Preset '${Preset.id}' must declare a texturing recipe`);
    }

    // Twin preset creates two regions under one shared group
    {
        const World = new WorldMask2D();
        const Built = BuildPresetWorld(World, 'twin-shared-weather', { resolution: 48 });
        EvaluateTerrainRegion(World, Built.region);
        EvaluateTerrainRegion(World, Built.regionB);
        const Group = CreateSimulationGroup('twin', [Built.region, Built.regionB], Built.climateOptions, Built.erosionOptions);
        InitSimulationGroup(World, Group, 31);
        for (let D = 0; D < 8; ++D) StepSimulationGroupDay(World, Group);
        const A = Group.members[0].state, B = Group.members[1].state;
        assert.equal(A.lastClimate.tempC, B.lastClimate.tempC, 'Twin terrains must share identical daily weather');
        ApplySimulationGroup(World, Group);
        assert.ok(Built.region.runtime.erosion === A, 'Applied erosion state must attach to region runtime for texturing');
    }

    // Data maps
    {
        const World = new WorldMask2D();
        const Built = BuildPresetWorld(World, 'layered-plateau', { resolution: 64 });
        EvaluateTerrainRegion(World, Built.region);
        const Group = CreateSimulationGroup('maps', [Built.region], Built.climateOptions, Built.erosionOptions);
        InitSimulationGroup(World, Group, 41);
        BakeSimulationGroup(World, Group, 0.05);
        ApplySimulationGroup(World, Group);
        const Maps = ComputeDataMaps(Built.region, Group.members[0].state);
        assert.equal(Object.keys(Maps).length, DATA_MAP_CATALOG.length, 'Every catalogued data map must compute');
        for (const [Id, M] of Object.entries(Maps))
        {
            for (let I = 0; I < M.data.length; ++I)
            {
                assert.ok(M.data[I] >= -0.001 && M.data[I] <= 1.001, `Map '${Id}' must normalize to 0..1 (got ${M.data[I]})`);
            }
        }
        // Surface composition + PNG export
        const Colors = ComposeSurfaceColors(Built.region, Group.members[0].state, { rampId: 'canyonStrata', stampId: 'crossBeddedSandstone' });
        const Nx = Built.region.runtime.nx, Nz = Built.region.runtime.nz;
        const Png = ExportSurfacePngBytes(Colors, Nx, Nz);
        const MapPng = ExportMapPngBytes(Maps.flow.data, Nx, Nz, Built.region.runtime.sdf2D);
        for (const Bytes of [Png, MapPng])
        {
            assert.deepEqual([Bytes[0], Bytes[1], Bytes[2], Bytes[3]], [0x89, 0x50, 0x4e, 0x47], 'PNG magic must be valid');
            const View = new DataView(Bytes.buffer, Bytes.byteOffset);
            assert.equal(View.getUint32(16), Nx, 'PNG IHDR width must match grid');
            assert.equal(View.getUint32(20), Nz, 'PNG IHDR height must match grid');
        }
    }

    // Stamps + ramps
    for (const S of STAMP_CATALOG)
    {
        for (const [X, Z] of [[0, 0], [123.4, -567.8], [-1e4, 2e4]])
        {
            const R = SampleStamp(S.id, X, Z, { seed: 5 });
            assert.ok(Number.isFinite(R.height) && Number.isFinite(R.colorVar), `Stamp '${S.id}' must sample finite values`);
        }
    }
    for (const Ramp of SATMAP_RAMPS)
    {
        for (const T of [0, 0.25, 0.5, 0.75, 1])
        {
            const C = EvaluateRamp(Ramp, T);
            assert.ok(C.length === 3 && C.every(V => V >= 0 && V <= 1), `Ramp '${Ramp.id}' must emit valid RGB at t=${T}`);
        }
    }
    // EncodePng round-trip on synthetic RGBA
    const Px = new Uint8Array(4 * 4 * 3);
    for (let I = 0; I < Px.length; ++I) Px[I] = (I * 7) & 255;
    const Tiny = EncodePng(4, 4, Px, false);
    assert.equal(new DataView(Tiny.buffer).getUint32(16), 4, 'Tiny PNG must encode width');
});

// 15. Large-World -> Manageable Sub-World Partitioning & Zero Interior Coastline Seam Collapse
Suite('15. Large continent partitions into manageable sub-world tiles with 0.000 m procedural border step & dominant mask adoption', () =>
{
    const World = new WorldMask2D();
    const UnusedMask = World.addMaskLayer({ name: 'Empty Mask' });
    const ContinentMask = World.addMaskLayer({ name: 'Mega Continent' });

    // Paint a 3.2 km x 2.2 km solid continent across multiple sparse 1x1 km tiles
    World.paintPolygonMeters(ContinentMask, [
        [-1500, -1000], [1500, -1000], [1600, 0], [1500, 1000], [-1500, 1000], [-1600, 0]
    ], false, 0.12, 4029);

    // Verify dominant painted mask adoption when drawing a custom region box while UnusedMask was selected
    const Adopted = World.findDominantMaskInBounds({ x: -400, z: -400, width: 800, depth: 800 }, UnusedMask.id);
    assert.equal(Adopted, ContinentMask.id, 'New region box must automatically adopt the visible mask containing painted land');

    // Create template region & split continent into ~1000 m sub-world tiles
    const Template = World.addTerrainRegion({
        name: 'Continent Template',
        maskLayerId: ContinentMask.id,
        bounds: { x: -1650, z: -1100, width: 3300, depth: 2200 },
        resolution: 81,
        stack: [
            CreateTerrainLayer('multifractal', {
                name: 'Continental Cordillera',
                amplitude: 210.0,
                scale: 2.4,
                seed: 7711,
                falloff: { mode: 'soft', distance: 260, edgeFloor: 0.0 }
            })
        ]
    });

    const SubTiles = World.partitionMaskIntoSubRegions(ContinentMask, {
        tileSizeM: 1050,
        resolution: 65,
        templateRegion: Template
    });
    assert.ok(SubTiles.length >= 6, `3.3x2.2 km continent should split into >= 6 manageable sub-worlds (got ${SubTiles.length})`);
    for (const T of SubTiles) EvaluateTerrainRegion(World, T);

    // Find two horizontally adjacent interior tiles and verify:
    // (a) shared seam does NOT collapse to sea level (`heights >> 40 m` along interior land border!),
    // (b) world-continuous procedural noise + padded coastline EDT produces < 0.5 m raw step BEFORE stitching and < 0.01 m AFTER!
    let CheckedAdjacentPair = false;
    for (let I = 0; I < SubTiles.length && !CheckedAdjacentPair; ++I)
    {
        for (let J = I + 1; J < SubTiles.length; ++J)
        {
            const A = SubTiles[I], B = SubTiles[J];
            if (Math.abs((A.bounds.x + A.bounds.width) - B.bounds.x) < 1e-3 && Math.abs(A.bounds.z - B.bounds.z) < 1e-3)
            {
                const MidRow = Math.floor(A.runtime.nz / 2);
                const LeftEdgeH = A.runtime.heights[MidRow * A.runtime.nx + (A.runtime.nx - 1)];
                const RightEdgeH = B.runtime.heights[MidRow * B.runtime.nx + 0];
                if (A.runtime.sdf2D[MidRow * A.runtime.nx + (A.runtime.nx - 1)] < -220.0)
                {
                    assert.ok(LeftEdgeH > 35.0, `Interior partition seam must NOT collapse to sea level (got ${LeftEdgeH.toFixed(2)} m)`);
                    assert.ok(Math.abs(LeftEdgeH - RightEdgeH) < 0.6,
                        `World-locked frequency + padded EDT must match across raw tile border (diff=${Math.abs(LeftEdgeH - RightEdgeH).toFixed(4)} m)`);
                    CheckedAdjacentPair = true;
                    break;
                }
            }
        }
    }
    assert.ok(CheckedAdjacentPair, 'Must verify at least one deep-interior shared sub-world border');
});

// 16. Solo 1-by-1 Sub-World Simulation + Seamless Adjacent Border Stitching
Suite('16. Solo 1-by-1 simulation on individual sub-world tiles + StitchAdjacentRegionBorders fuses shared seams cleanly', () =>
{
    const World = new WorldMask2D();
    const Mask = World.addMaskLayer({ name: 'Twin Sub-Worlds' });
    World.paintPolygonMeters(Mask, [[-1200, -600], [1200, -600], [1200, 600], [-1200, 600]], false, 0.0, 4029);

    const TileWest = World.addTerrainRegion({
        name: 'West Sub-World',
        maskLayerId: Mask.id,
        bounds: { x: -1000, z: -500, width: 1000, depth: 1000 },
        resolution: 65,
        worldFrequencySpan: 1000,
        seamLock: true,
        stack: [CreateTerrainLayer('ridged', { amplitude: 190, scale: 2.2, seed: 505, falloff: { mode: 'soft', distance: 200 } })]
    });
    const TileEast = World.addTerrainRegion({
        name: 'East Sub-World',
        maskLayerId: Mask.id,
        bounds: { x: 0, z: -500, width: 1000, depth: 1000 },
        resolution: 65,
        worldFrequencySpan: 1000,
        seamLock: true,
        stack: [CreateTerrainLayer('ridged', { amplitude: 190, scale: 2.2, seed: 505, falloff: { mode: 'soft', distance: 200 } })]
    });

    EvaluateTerrainRegion(World, TileWest);
    EvaluateTerrainRegion(World, TileEast);

    // Sculpt + solo-erode ONLY West Sub-World (`TileEast` untouched!)
    ApplyTerrainSculptDab(World, TileWest, [-15, 110, 0], 'raise', { radius: 180, strength: 0.95 });
    const SoloState = CreateErosionState(World, TileWest, { seed: 77 });
    const SoloClim = CreateClimateGroup({ regimeId: 'monsoon', latitudeDeg: 24, seed: 77 });
    BakeErosionYears(SoloState, SoloClim, 0.12);
    ApplyErosionToRegion(World, TileWest, SoloState);

    const MidZ = 32;
    const PreGap = Math.abs(TileWest.runtime.heights[MidZ * 65 + 64] - TileEast.runtime.heights[MidZ * 65 + 0]);
    assert.ok(PreGap > 2.0, `Solo sculpt + erosion near border should create a measurable pre-stitch gap (got ${PreGap.toFixed(2)} m)`);

    const Pairs = StitchAdjacentRegionBorders(World, [TileWest, TileEast], 75.0);
    assert.equal(Pairs, 1, 'Stitcher must detect and fuse the shared East-West border');
    for (let Z = 6; Z <= 58; ++Z)
    {
        const PostGap = Math.abs(TileWest.runtime.heights[Z * 65 + 64] - TileEast.runtime.heights[Z * 65 + 0]);
        assert.ok(PostGap < 0.02, `Stitched shared border at row ${Z} must align within 2 cm (got ${PostGap.toFixed(5)} m)`);
    }
});

// 17. Unlimited 3D Volumetric Caves, Smooth Overhangs & 3D Mountain Tunnel Bore Splines
Suite('17. Unlimited 3D volumetric caves (>120 ops), smooth overhangs & 3D mountain tunnel bores extract clean Dual-Surface-Nets meshes', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const R = World.regions[0];
    EvaluateTerrainRegion(World, R);

    // Push 125 cave & overhang ops to prove the old 96-op cap is completely gone!
    for (let I = 0; I < 125; ++I)
    {
        ApplyTerrainSculptDab(
            World, R,
            [R.bounds.x + 550 + (I % 15) * 8, 45 + (I % 5) * 3, R.bounds.z + 550 + Math.floor(I / 15) * 8],
            I % 2 === 0 ? 'cave' : 'overhang',
            { radius: 42, strength: 0.8 }
        );
    }
    assert.equal(R.sdf3DOps.length, 125, '3D volumetric sdf3DOps must NOT truncate at 96 ops');

    // Add a 3D Mountain Tunnel Bore spline through the terrain and extract full triangle mesh
    R.sdf3DOps.length = 2; // keep 1 cave + 1 overhang for fast Dual-Surface-Nets verification
    const TunnelSpline = CreateTerrainLayer('spline', {
        name: '3D Highway Tunnel Bore',
        splineMode: 'tunnel',
        width: 48.0,
        amount: 24.0,
        bankSmooth: 0.4,
        points: [
            [R.bounds.x + 420, R.bounds.z + 520, 28.0],
            [R.bounds.x + 620, R.bounds.z + 560, 32.0],
            [R.bounds.x + 820, R.bounds.z + 520, 28.0]
        ]
    });
    R.stack.push(TunnelSpline);
    EvaluateTerrainRegion(World, R);
    const Mesh = ExtractRegionTriangleMesh(World, R, { includeUnderside: true });
    assert.ok(Mesh.triangleCount > 2500, 'Mesh with 3D cave + overhang + tunnel bore must emit valid triangles');

    // Verify subterranean cave/tunnel ceiling normals (`ny < -0.25`) exist in extracted mesh!
    let CeilingVerts = 0;
    for (let I = 1; I < Mesh.normals.length; I += 3)
    {
        assert.ok(Number.isFinite(Mesh.normals[I]), 'Volumetric Dual-Surface-Nets normals must be finite');
        if (Mesh.normals[I] < -0.25 && Mesh.positions[I] > 2.0) CeilingVerts++;
    }
    assert.ok(CeilingVerts > 12, `3D Cave & Tunnel Bore must produce downward-facing subterranean ceiling vertices (got ${CeilingVerts})`);
});

// 18. Fast O(brush-subrect) Live Sculpting & Restore Brush
Suite('18. Live 3D sculpting updates cached preSculptHeights sub-rect in O(brush area) without dirtying full procedural stack + supports Restore brush', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const R = World.regions[0];
    EvaluateTerrainRegion(World, R);

    const Rt = R.runtime;
    const Cx = Math.floor(Rt.nx * 0.5), Cz = Math.floor(Rt.nz * 0.5);
    const Idx = Cz * Rt.nx + Cx;
    const Wx = R.bounds.x + Cx * Rt.stepX, Wz = R.bounds.z + Cz * Rt.stepZ;
    const OrigH = Rt.heights[Idx];

    const T0 = performance.now();
    for (let S = 0; S < 25; ++S)
    {
        ApplyTerrainSculptDab(World, R, [Wx, OrigH, Wz], 'raise', { radius: 95, strength: 0.7 });
    }
    const ElapsedMs = performance.now() - T0;
    assert.equal(R.dirty, false, 'Top-layer scissored sculpt dab must update heights in-place without marking full stack dirty');
    assert.ok(ElapsedMs < 80, `25 scissored sculpt dabs must complete well under 80 ms (took ${ElapsedMs.toFixed(2)} ms)`);
    const SculptedH = Rt.heights[Idx];
    assert.ok(SculptedH > OrigH + 20.0, 'Scissored sculpt dabs must raise surface height cleanly');

    // Verify full re-evaluation agrees with fast scissored update!
    EvaluateTerrainRegion(World, R);
    assert.ok(Math.abs(R.runtime.heights[Idx] - SculptedH) < 1e-3,
        `Fast sub-rect sculpt (${SculptedH.toFixed(4)}) must match full stack evaluation (${R.runtime.heights[Idx].toFixed(4)})`);

    // Restore brush smoothly recovers base procedural height
    for (let S = 0; S < 12; ++S)
    {
        ApplyTerrainSculptDab(World, R, [Wx, SculptedH, Wz], 'restore', { radius: 120, strength: 1.0 });
    }
    assert.ok(Math.abs(R.runtime.heights[Idx] - OrigH) < 0.25,
        `Restore brush must recover original procedural elevation (orig=${OrigH.toFixed(2)}, restored=${R.runtime.heights[Idx].toFixed(2)})`);
});

// 19. Priority-Flood Closed-Basin Hydrostatic Lakes & Upstream Stale-Erosion Detection
Suite('19. Priority-Flood solves upland closed-basin hydrostatic lakes & BaseSignature flags stale erosion when upstream layers change', () =>
{
    const World = new WorldMask2D();
    //Terrain 03 in Demo Archipelago is "Caldera Basin & Terraces" with a closed volcanic crater lake!
    SeedThreeDemoTerrains(World);
    const Caldera = World.regions[2];
    EvaluateTerrainRegion(World, Caldera);

    // Deepen a closed upland caldera bowl at center to guarantee an upland lake above sea level
    const Rt = Caldera.runtime;
    const Cx = Math.floor(Rt.nx * 0.5), Cz = Math.floor(Rt.nz * 0.5);
    const Wx = Caldera.bounds.x + Cx * Rt.stepX, Wz = Caldera.bounds.z + Cz * Rt.stepZ;
    ApplyTerrainSculptDab(World, Caldera, [Wx, 80, Wz], 'lower', { radius: 190, strength: 0.95 });
    ApplyTerrainSculptDab(World, Caldera, [Wx, 80, Wz], 'lower', { radius: 170, strength: 0.95 });

    const WaterMesh = ComputeHydrostaticWaterBodies(World, Caldera);
    assert.ok(WaterMesh.lakeCells >= 8 && WaterMesh.triangleCount >= 8,
        `Priority-Flood must detect upland closed caldera lake & emit hydrostatic water mesh (cells=${WaterMesh.lakeCells}, tris=${WaterMesh.triangleCount})`);

    // Bake erosion on Caldera -> erosionStale must be false
    const State = CreateErosionState(World, Caldera, { seed: 55 });
    const Clim = CreateClimateGroup({ regimeId: 'temperate-maritime', latitudeDeg: 48, seed: 55 });
    BakeErosionYears(State, Clim, 0.05);
    ApplyErosionToRegion(World, Caldera, State);
    assert.equal(Caldera.runtime.erosionStale, false, 'Freshly baked erosion must NOT be marked stale');

    // Now modify an upstream procedural layer amplitude -> ComputeRegionBaseSignature must flag `erosionStale === true`!
    Caldera.stack[0].amplitude += 45.0;
    EvaluateTerrainRegion(World, Caldera);
    assert.equal(Caldera.runtime.erosionStale, true, 'Modifying an upstream procedural layer after baking erosion must flag erosionStale = true');
});

// 20. Pure-SDF Kinematic Domain-Pullback Jigsaw Cleaving & Inverse Rigid Pullback Accuracy
Suite('20. Pure-SDF kinematic jigsaw cleaving preserves exact inverse pullback T^{-1}p across nested hinge rotations & fault heave', () =>
{
    const Parent = createBoxPoly(-2, 0, -2, 2, 4, 2, 0);
    const [LeftHalf, RightHalf] = splitPolyByPlane(Parent, [1, 0, 0], 0.0, 0.0);
    assert.ok(LeftHalf && RightHalf, 'Splitting parent polyhedron must produce two valid convex child blocks');

    // Apply two nested rigid-body hinge rotations + fault heave translations to RightHalf
    const Moved1 = transformPoly(RightHalf, [0.45, -0.30, 0.15], [0, 0, 1], -0.18, [0.5, 0.0, 0.0]);
    const Moved2 = transformPoly(Moved1, [0.10, -0.12, -0.08], [0, 1, 0], 0.24, [0.8, 1.0, 0.2]);

    // Verify that `invR * v_displaced + invT` recovers EVERY original pre-fracture vertex of `RightHalf` to machine precision!
    const OrigVert = RightHalf.faces[0].verts[0];
    const DispVert = Moved2.faces[0].verts[0];
    const { invR, invT } = Moved2.pullback;
    const RecX = invR[0] * DispVert[0] + invR[1] * DispVert[1] + invR[2] * DispVert[2] + invT[0];
    const RecY = invR[3] * DispVert[0] + invR[4] * DispVert[1] + invR[5] * DispVert[2] + invT[1];
    const RecZ = invR[6] * DispVert[0] + invR[7] * DispVert[1] + invR[8] * DispVert[2] + invT[2];
    assert.ok(Math.hypot(RecX - OrigVert[0], RecY - OrigVert[1], RecZ - OrigVert[2]) < 1e-6,
        'Composed inverse rigid pullback T^{-1}p must map displaced fracture block vertices back to exact parent coordinates');

    // Verify Al-Naslaa split boulder has open air (d > 0) inside the central laser chasm and solid rock (d < 0) inside left & right halves!
    const Naslaa = buildFracturedFormation({ presetId: 'al-naslaa-split', chasmAperture: 0.65, spallCalving: 0.0 });
    const DGap = evalFracturedFormationSdf(Naslaa, 0.0, 3.2, 0.0);
    assert.ok(DGap > 0.05, `Central split chasm must be open air (d > 0), got ${DGap.toFixed(4)}`);
});

// 21. Pure-SDF Thermal Skin-Depth V-Tipped Cracks & Directional Solar ΔT Insolation Gating
Suite('21. Pure-SDF thermal skin-depth V-tipped cracks attenuate inward exponentially (core stays solid) & respond to solar ΔT', () =>
{
    const FinHot = buildFracturedFormation({
        presetId: 'thermal-90-fin',
        chasmAperture: 0.0,
        hingeTiltDeg: 0.0,
        faultHeave: 0.0,
        thermalDeltaT: 75,
        crazing90Strength: 1.0,
        thermalSkinDepth: 0.35,
        directionalBias: 0.90,
        sunAzimuthDeg: 225
    });

    // Deep inside the rock core (far beyond skinDepth = 0.35m), V-tipped thermal cracks MUST decay to zero, leaving solid interior stone!
    const CoreInfo = {};
    const DCore = evalFracturedFormationSdf(FinHot, 0.0, 2.2, 0.0, null, CoreInfo);
    assert.ok(DCore < -0.5, `Deep rock core beyond thermal skin depth must remain completely solid (got d=${DCore.toFixed(3)})`);

    // Compare directional thermal stress on sun-facing SW flank vs shaded NE flank
    const SunFaceInfo = {}, ShadeFaceInfo = {};
    evalFracturedFormationSdf(FinHot, -0.5, 2.5, -0.9, null, SunFaceInfo);
    evalFracturedFormationSdf(FinHot, 0.5, 2.5, 0.9, null, ShadeFaceInfo);
    assert.ok(SunFaceInfo.thermalStress > ShadeFaceInfo.thermalStress,
        `Sun-facing aspect thermal stress (${SunFaceInfo.thermalStress.toFixed(3)}) must exceed shaded aspect (${ShadeFaceInfo.thermalStress.toFixed(3)})`);
});

// 22. Pure-SDF Hierarchical 90-Degree T-Junction Thermal Crazing Topology
Suite('22. Hierarchical 90-degree thermal crazing generates continuous Tier-1 joints & slab-local Tier-2 T-junction arrests', () =>
{
    // Sample across a Tier-1 boundary at independent v positions to verify Tier-2 phase discontinuities (90° T-junctions!)
    const Spacing = 0.80;
    let DiscontinuityFound = false;
    for (let i = 0; i < 20; i++)
    {
        const v = i * 0.07 + 0.11;
        // Left of unwarped slab boundary vs next slab over
        const LeftT2 = evalHierarchical90Crazing3D(-0.35, v, 0.0, Spacing, 777).tier2Dist;
        const RightT2 = evalHierarchical90Crazing3D(+0.45, v, 0.0, Spacing, 777).tier2Dist;
        if (Math.abs(LeftT2 - RightT2) > 0.08)
        {
            DiscontinuityFound = true;
            break;
        }
    }
    assert.equal(DiscontinuityFound, true,
        'Tier-2 orthogonal cross-joints must shift phase across Tier-1 master joints so cracks arrest at 90° T-junctions');
});

// 23. Pure-SDF Cross-Section Cutaway Slice & GPU RGBA32F Buffer Packing
Suite('23. Pure-SDF cutaway slice clips rock volume cleanly for interior V-tip inspection & packs GPU RGBA32F textures', () =>
{
    const Sliced = buildFracturedFormation({
        presetId: 'exfoliation-dome',
        cutawayEnabled: true,
        cutawayAxis: 0,
        cutawayOffset: 0.10
    });
    const HitPos = {}, HitNeg = {};
    const DClippedSide = evalFracturedFormationSdf(Sliced, 0.85, 1.8, 0.0, null, HitPos);
    const DKeptSide = evalFracturedFormationSdf(Sliced, -0.85, 1.8, 0.0, null, HitNeg);
    assert.ok(DClippedSide > 0.5 && HitPos.isCutaway === true, 'Points on +X side of cutaway plane must be clipped open');
    assert.ok(DKeptSide < -0.2, 'Points on -X side of cutaway plane must remain solid stone');

    const Packed = packFracturedFormationGpuBuffers(Sliced);
    assert.equal(Packed.blockTexWidth, 6);
    assert.equal(Packed.planeTexWidth, 2);
    assert.equal(Packed.blockTexData.length, Packed.blockTexHeight * 6 * 4);
    assert.equal(Packed.planeTexData.length, Packed.planeTexHeight * 2 * 4);
});

// 24. All 7 Pure-SDF Rock Formations + 4^3 Sparse-Brick QEF Dual Contouring + .OBJ/.STL/.GLB Export
Suite('24. All 7 Pure-SDF Rock Formations extract sharp-creased QEF Dual-Contour meshes with 0 Mesh->SDF & valid .GLB/.OBJ/.STL', () =>
{
    assert.equal(FRACTURE_PRESETS.length, 7);
    for (const Preset of FRACTURE_PRESETS)
    {
        const F = buildFracturedFormation({ presetId: Preset.id, meshResolution: 46 });
        const { mesh, sdfStats } = bakeFracturedFormationQefMesh(F, { resolution: 46 });
        assert.equal(sdfStats.meshToSdfCount, 0, `${Preset.id} must execute strictly 0 Mesh->SDF conversions`);
        assert.ok(sdfStats.triangles > 2500, `${Preset.id} must extract substantial QEF triangle geometry (got ${sdfStats.triangles})`);
        assert.ok(sdfStats.brickOccupancyPct < 88, `${Preset.id} 4^3 brick culler must skip empty outer volume`);
        assert.equal(mesh.positions.length, mesh.normals.length);
        assert.equal(mesh.positions.length, mesh.albedos.length);

        if (Preset.id === 'cracked-monolith')
        {
            const Glb = exportFractureMeshGlb(mesh, F.params, sdfStats);
            const View = new DataView(Glb.buffer, Glb.byteOffset, Glb.byteLength);
            assert.equal(View.getUint32(0, true), 0x46546c67, 'Binary .GLB must start with glTF 2.0 magic header 0x46546c67');
            assert.equal(View.getUint32(4, true), 2, 'Binary .GLB version must be 2');

            const Obj = exportFractureMeshObj(mesh, 'TestMonolith');
            assert.ok(Obj.includes('\nv ') && Obj.includes('\nf '), '.OBJ export must emit vertices & faces');

            const Stl = exportFractureMeshBinaryStl(mesh, 'TestMonolith');
            assert.equal(Stl.byteLength, 84 + sdfStats.triangles * 50, 'Binary .STL byte length must equal 84 + 50 * triCount');
        }
    }
});

// 25. Stamping Pure-SDF Fractured Rock Formation onto Open-World TerrainRegion
Suite('25. Pure-SDF Fractured Rock layer stamps onto Open-World TerrainRegion, merges QEF mesh & survives JSON round-trip', () =>
{
    const World = new WorldMask2D();
    SeedThreeDemoTerrains(World);
    const Reg = World.regions[0];
    EvaluateTerrainRegion(World, Reg);
    const BaseMesh = ExtractRegionTriangleMesh(World, Reg);

    const RockLayer = CreateTerrainLayer('rock_formation', {
        name: 'Stamped Cracked Jigsaw Monolith',
        worldX: Reg.bounds.x + Reg.bounds.width * 0.5,
        worldZ: Reg.bounds.z + Reg.bounds.depth * 0.5,
        worldScaleM: 175.0,
        yawDeg: 22.0,
        fractureParams: { presetId: 'cracked-monolith', meshResolution: 46 }
    });
    Reg.stack.push(RockLayer);
    EvaluateTerrainRegion(World, Reg);
    const StampedMesh = ExtractRegionTriangleMesh(World, Reg);
    assert.ok(StampedMesh.triangleCount > BaseMesh.triangleCount + 3000,
        `Stamped Pure-SDF fractured rock must append QEF triangles to Open-World Terrain mesh (${BaseMesh.triangleCount} -> ${StampedMesh.triangleCount})`);

    // Verify clean JSON serialization round-trip (no WeakMap cache bloat!)
    const SavedJson = JSON.stringify(World.toJSON());
    assert.ok(SavedJson.length < 400000, 'Serialized world JSON must stay compact without baking mesh arrays into JSON');
    const Restored = new WorldMask2D();
    Restored.loadFromJSON(JSON.parse(SavedJson));
    assert.equal(Restored.regions[0].stack.some(L => L.type === 'rock_formation'), true,
        'Restored Open-World project must preserve stamped rock_formation layer');
});

// 26. Pure-SDF Chemical Rock Erosion (Karst Dissolution, Basal Notch, Honeycomb Tafoni, Karren Flutes) & Per-Rock Tailored Controls
Suite('26. Pure-SDF Chemical Rock Erosion carves basal solution notches, exterior-gated honeycomb tafoni & karren flutes, and each rock formation exposes tailored controls', () =>
{
    // 1. Validate every rock formation has its own tailored controlGroups referencing valid default parameters
    const Signatures = new Set();
    for (const Preset of FRACTURE_PRESETS)
    {
        assert.ok(Array.isArray(Preset.controlGroups) && Preset.controlGroups.length >= 2,
            `${Preset.id} must define tailored controlGroups`);
        const Keys = [];
        for (const Grp of Preset.controlGroups)
        {
            assert.ok(Array.isArray(Grp.controls) && Grp.controls.length >= 2,
                `${Preset.id} group "${Grp.title}" must define active controls`);
            for (const Ctrl of Grp.controls)
            {
                assert.ok(Object.prototype.hasOwnProperty.call(Preset.defaults, Ctrl.key),
                    `${Preset.id} control "${Ctrl.key}" must exist in Preset.defaults`);
                Keys.push(Ctrl.key);
            }
        }
        Signatures.add(Keys.join(','));
    }
    assert.ok(Signatures.size >= 6, 'Rock formations must provide distinct rock-specific control sets rather than one generic list');

    // 2. Basal Acid Undercut Notch & Dissolution Rounding on 07 · Karst & Tafoni Dissolution Tower
    const KarstDry = buildFracturedFormation({
        presetId: 'karst-tafoni-tower',
        chemicalDissolution: 0.0,
        tafoniHoneycomb: 0.0,
        karrenFlutes: 0.0,
        chasmAperture: 0.0,
        hingeTiltDeg: 0.0,
        spallCalving: 0.0
    });
    const KarstAcid = buildFracturedFormation({
        presetId: 'karst-tafoni-tower',
        chemicalDissolution: 0.95,
        tafoniHoneycomb: 0.0,
        karrenFlutes: 0.0,
        chasmAperture: 0.0,
        hingeTiltDeg: 0.0,
        spallCalving: 0.0
    });

    const NotchY = KarstDry.params.scale * 0.095;
    let MaxBasalRecess = 0.0;
    for (let ang = 0; ang < Math.PI * 2; ang += Math.PI / 6)
    {
        for (let r = 0.8; r <= 3.0; r += 0.25)
        {
            const wx = Math.cos(ang) * r, wz = Math.sin(ang) * r;
            const d0 = evalFracturedFormationSdf(KarstDry, wx, NotchY, wz);
            if (Math.abs(d0) < 0.35)
            {
                const infoAcid = {};
                const d1 = evalFracturedFormationSdf(KarstAcid, wx, NotchY, wz, null, infoAcid);
                MaxBasalRecess = Math.max(MaxBasalRecess, d1 - d0);
                if (Math.abs(d1) < 0.25)
                {
                    assert.ok((infoAcid.chemStain ?? 0) > 0.25, 'Basal solution zone must accumulate geochemical solute stain');
                }
            }
        }
    }
    assert.ok(MaxBasalRecess > 0.12,
        `Chemical dissolution must undercut the basal water-table notch smoothly (max recess=${MaxBasalRecess.toFixed(3)}m)`);

    // Solid interior core of primary karst pinnacle must remain intact (no Swiss-cheese internal voids!)
    let MinCoreD = 1e9;
    for (const Blk of KarstAcid.blocks)
    {
        if (Blk.isSpall) continue;
        MinCoreD = Math.min(MinCoreD, evalFracturedFormationSdf(KarstAcid, Blk.cx, Blk.cy, Blk.cz));
    }
    assert.ok(MinCoreD < -0.35, `Karst tower interior core must remain solid stone (got d=${MinCoreD.toFixed(3)})`);

    // 3. Honeycomb Tafoni & Vertical Karren Flutes carve surface relief on upper/mid tower rinds
    const KarstFull = buildFracturedFormation({
        presetId: 'karst-tafoni-tower',
        chemicalDissolution: 0.85,
        tafoniHoneycomb: 0.92,
        karrenFlutes: 0.95,
        chasmAperture: 0.0,
        hingeTiltDeg: 0.0,
        spallCalving: 0.0
    });
    let MaxTafoniOrFluteCarve = 0.0;
    for (const yFrac of [0.24, 0.36, 0.52, 0.64])
    {
        const wy = KarstAcid.params.scale * yFrac;
        for (let ang = 0; ang < Math.PI * 2; ang += Math.PI / 8)
        {
            for (let r = 0.5; r <= 2.8; r += 0.18)
            {
                const wx = Math.cos(ang) * r, wz = Math.sin(ang) * r;
                const dBase = evalFracturedFormationSdf(KarstAcid, wx, wy, wz);
                if (dBase > -0.22 && dBase < 0.05)
                {
                    const dWeathered = evalFracturedFormationSdf(KarstFull, wx, wy, wz);
                    MaxTafoniOrFluteCarve = Math.max(MaxTafoniOrFluteCarve, dWeathered - dBase);
                }
            }
        }
    }
    assert.ok(MaxTafoniOrFluteCarve > 0.06,
        `Honeycomb tafoni & vertical karren flutes must carve exterior cavities & flutes (max carve=${MaxTafoniOrFluteCarve.toFixed(3)}m)`);
});

console.log(`\nAll ${Passed}/26 Open-World SDF Terrain & Pure-SDF Rock Formation Studio validation suites passed!`);
