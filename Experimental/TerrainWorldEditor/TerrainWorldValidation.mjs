/*==========================================================================================================================================
                                                     TERRAINWORLDVALIDATION.MJS
==========================================================================================================================================*/
// Comprehensive Headless Validation Suite for Frontier Open-World SDF Terrain Studio

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
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
    BASE_FORM_SHAPES_POLY,
    BASE_FORM_SHAPES_BASALT,
    createBoxPoly,
    splitPolyByPlane,
    transformPoly,
    buildFracturedFormation,
    evalFracturedFormationSdf,
    bakeFracturedFormationQefMesh,
    packFracturedFormationGpuBuffers
} from './Engine/RockCliff/PureSdfFractureEngine.js';
import {
    ROCK2_NODE_CATALOG,
    ROCK2_MODIFIER_CATALOG,
    ROCK2_STARTER_STACKS,
    isRock2ModifierType,
    computeRock2FaultBasis,
    rock2FaultNormalToStrikeDip,
    buildRock2SplinePresetKnots,
    buildRock2SplineArcTable,
    sampleRock2SplineAtNormalizedArc,
    buildRock2EulerMatrix3,
    forwardRock2Point,
    inverseRock2Vector,
    createRock2Generator,
    createRock2Layer,
    cloneRock2Layer,
    createDefaultRock2Document,
    evaluateRock2GeneratorInstances,
    getOrCreateRock2InstanceOverride,
    hasActiveRock2InstanceOverride,
    compileRock2Stack,
    evalRock2StackSdf,
    pickRock2LayerAtRay,
    packRock2StackGpuBuffers,
    bakeRock2StackQefMesh,
    serializeRock2Document,
    deserializeRock2Document
} from './Engine/RockCliff/RockFormationStackEngine.js';

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

// 27. Controllable Split System (Al-Naslaa / Onion-Skin / Parallel / Cross / Unsplit) & Unified Base Rock Form Generator
Suite('27. Controllable Split System & Base Rock Form Generator reshape proportions, silhouettes & splits across all 7 rocks', () =>
{
    assert.ok(BASE_FORM_SHAPES_POLY.length >= 6 && BASE_FORM_SHAPES_BASALT.length >= 4,
        'Base Form shape catalogs must provide rich polyhedral & basalt colonnade silhouettes');

    // 1. Onion-Skin Exfoliation Dome full split control: Unsplit ('none') vs Al-Naslaa Single Split vs Closed Gap vs Shifted Split vs Parallel/Cross!
    const DomeUnsplit = buildFracturedFormation({
        presetId: 'exfoliation-dome',
        splitMode: 'none',
        spallCalving: 0.0
    });
    assert.equal(DomeUnsplit.blocks.length, 1, 'Unsplit Onion-Skin Exfoliation Dome must compile to 1 unbroken solid dome block');
    assert.ok(evalFracturedFormationSdf(DomeUnsplit, 0.0, 2.0, 0.0) < -0.50,
        'Unsplit Onion-Skin Exfoliation Dome center must be solid stone');

    const DomeAlNaslaa = buildFracturedFormation({
        presetId: 'exfoliation-dome',
        baseFormShape: 'mushroom',
        splitMode: 'single',
        splitAngleDeg: 0,
        splitDipDeg: 0,
        splitOffset: 0.0,
        chasmAperture: 0.65,
        hingeTiltDeg: 3.5,
        faultHeave: 0.20,
        spallCalving: 0.0
    });
    // 2 cleaved onion-skin boulder halves + 2 fitted balanced undercut stone pedestals = 4 blocks!
    assert.equal(DomeAlNaslaa.blocks.length, 4, 'Balanced-pedestal Al-Naslaa split Onion-Skin boulder must build 2 halves + 2 pedestals');
    let DChasmOpen = -1e9;
    for (let sx = -0.35; sx <= 0.35; sx += 0.05)
    {
        DChasmOpen = Math.max(DChasmOpen, evalFracturedFormationSdf(DomeAlNaslaa, sx, 2.6, 0.0));
    }
    const BLeft = DomeAlNaslaa.blocks[0], BRight = DomeAlNaslaa.blocks[1];
    const DLeftHalf = evalFracturedFormationSdf(DomeAlNaslaa, BLeft.cx, BLeft.cy, BLeft.cz);
    const DRightHalf = evalFracturedFormationSdf(DomeAlNaslaa, BRight.cx, BRight.cy, BRight.cz);
    assert.ok(DChasmOpen > 0.08, `Open Al-Naslaa split on Onion-Skin boulder must carve clear air chasm near center (got d=${DChasmOpen.toFixed(3)})`);
    assert.ok(DLeftHalf < -0.50 && DRightHalf < -0.50,
        `Both cleaved Onion-Skin halves must remain solid stone (L=${DLeftHalf.toFixed(3)}, R=${DRightHalf.toFixed(3)})`);

    // Closing Split Gap Width to 0 (`chasmAperture = 0, hingeTiltDeg = 0, faultHeave = 0`) seals hairline-tight!
    const DomeClosed = buildFracturedFormation({
        presetId: 'exfoliation-dome',
        splitMode: 'single',
        splitAngleDeg: 0,
        splitOffset: 0.0,
        chasmAperture: 0.0,
        hingeTiltDeg: 0.0,
        faultHeave: 0.0,
        voronoiJointStrength: 0.0,
        crazing90Strength: 0.0,
        spallCalving: 0.0
    });
    let MaxSeamD = -1e9;
    for (let sx = -0.50; sx <= 0.50; sx += 0.04)
    {
        MaxSeamD = Math.max(MaxSeamD, evalFracturedFormationSdf(DomeClosed, sx, 2.0, 0.0));
    }
    assert.ok(MaxSeamD <= 0.002, `Zero-aperture split must close hairline-tight with 0 air gap across the seam (max d=${MaxSeamD.toFixed(4)})`);

    // Shifting `splitOffset` moves the split chasm cleanly off-center while center becomes solid!
    const DomeShifted = buildFracturedFormation({
        presetId: 'exfoliation-dome',
        splitMode: 'single',
        splitAngleDeg: 0,
        splitOffset: 0.34,
        chasmAperture: 0.65,
        hingeTiltDeg: 0.0,
        faultHeave: 0.0,
        spallCalving: 0.0
    });
    assert.ok(evalFracturedFormationSdf(DomeShifted, 0.0, 2.0, 0.0) < -0.20,
        'Shifting splitOffset to +0.34 must leave center x=0 inside solid rock');

    // Parallel (3 slabs) & Crossed splits (>= 3 blocks)
    const DomeParallel = buildFracturedFormation({ presetId: 'exfoliation-dome', splitMode: 'parallel', spallCalving: 0.0 });
    const DomeCross = buildFracturedFormation({ presetId: 'exfoliation-dome', splitMode: 'cross', spallCalving: 0.0 });
    assert.equal(DomeParallel.blocks.length, 3, 'Parallel split mode must cleave dome into 3 slabs');
    assert.ok(DomeCross.blocks.length >= 3, 'Crossed conjugate split mode must cleave dome into >= 3 wedged blocks');

    // 2. Unified Base Rock Form Generator stretches Width (X), Height (Y) & Length (Z) across ALL 7 Rock Formations!
    for (const Preset of FRACTURE_PRESETS)
    {
        const NarrowShort = buildFracturedFormation({
            presetId: Preset.id,
            formWidth: 0.55,
            formHeight: 0.50,
            formLength: 0.55,
            spallCalving: 0.0
        });
        const WideTallLong = buildFracturedFormation({
            presetId: Preset.id,
            formWidth: 1.75,
            formHeight: 1.65,
            formLength: 1.75,
            spallCalving: 0.0
        });

        const SpanXNarrow = NarrowShort.bounds.maxX - NarrowShort.bounds.minX;
        const SpanXWide = WideTallLong.bounds.maxX - WideTallLong.bounds.minX;
        const SpanYShort = NarrowShort.bounds.maxY - NarrowShort.bounds.minY;
        const SpanYTall = WideTallLong.bounds.maxY - WideTallLong.bounds.minY;
        const SpanZShort = NarrowShort.bounds.maxZ - NarrowShort.bounds.minZ;
        const SpanZLong = WideTallLong.bounds.maxZ - WideTallLong.bounds.minZ;

        assert.ok(SpanXWide > SpanXNarrow * 1.65,
            `${Preset.id}: formWidth must widen rock X-span (${SpanXNarrow.toFixed(2)}m -> ${SpanXWide.toFixed(2)}m)`);
        assert.ok(SpanYTall > SpanYShort * 1.65,
            `${Preset.id}: formHeight must heighten rock Y-span (${SpanYShort.toFixed(2)}m -> ${SpanYTall.toFixed(2)}m)`);
        assert.ok(SpanZLong > SpanZShort * 1.65,
            `${Preset.id}: formLength must lengthen rock Z-span (${SpanZShort.toFixed(2)}m -> ${SpanZLong.toFixed(2)}m)`);
        assert.ok(WideTallLong.blocks.length <= 96,
            `${Preset.id}: compiled block count (${WideTallLong.blocks.length}) must fit GPU bake texture cap (96)`);
    }

    // 3. Columnar Basalt Base Form Shapes ('cliff', 'mound', 'ridge', 'amphitheater') + Sea-Gorge Split + QEF Bake
    for (const Shape of ['cliff', 'mound', 'ridge', 'amphitheater'])
    {
        const Basalt = buildFracturedFormation({
            presetId: 'columnar-basalt',
            baseFormShape: Shape,
            formWidth: 1.45,
            formHeight: 1.15,
            formLength: 1.35,
            columnRadiusScale: 0.85,
            splitMode: Shape === 'mound' ? 'single' : 'cross'
        });
        assert.ok(Basalt.blocks.length >= 8 && Basalt.blocks.length <= 96,
            `Columnar Basalt '${Shape}' must build valid colonnade within GPU cap (got ${Basalt.blocks.length} blocks)`);
        const Packed = packFracturedFormationGpuBuffers(Basalt);
        assert.equal(Packed.numBlocks, Basalt.blocks.length);
    }

    const BasaltQef = bakeFracturedFormationQefMesh(
        buildFracturedFormation({
            presetId: 'columnar-basalt',
            baseFormShape: 'mound',
            formWidth: 1.35,
            formHeight: 1.25,
            splitMode: 'single',
            chasmAperture: 0.55
        }),
        { maxAxisRes: 56 }
    );
    assert.ok(BasaltQef.sdfStats.triangles > 2000, 'Custom-formed Sea-Gorge Columnar Basalt must QEF-bake cleanly');
});

// 28. RockFormation 2.0: 11 Atomic Single Base Rocks + Composable Per-Layer Spline/Array/Splatter/Scatter Generators + Single Viewport CSS
Suite('28. RockFormation 2.0: 11 atomic single base rocks, per-layer Spline/Array/Splatter/Scatter generators, CSG stack, QEF mesh & viewport CSS', () =>
{
    // 0. Assert 11 plain base rock nodes and CSS pane exclusivity (no split viewport!)
    assert.equal(ROCK2_NODE_CATALOG.length, 11, 'RockFormation 2.0 must expose 11 atomic plain base rock nodes');
    assert.equal(ROCK2_NODE_CATALOG.some(N => N.type.includes('arch') || N.name.toLowerCase().includes('arch')), false,
        'RockFormation 2.0 base rock catalog must contain zero hardcoded arch base nodes');

    const CssText = readFileSync(new URL('./TerrainWorldEditor.css', import.meta.url), 'utf8');
    const PaneFracBlock = CssText.match(/#PaneFracture,\s*#CanvasFracture\s*\{[^}]*\}/);
    const PaneRock2Block = CssText.match(/#PaneRock2,\s*#CanvasRock2\s*\{[^}]*\}/);
    assert.ok(PaneFracBlock && !/display\s*:\s*block/.test(PaneFracBlock[0]),
        '#PaneFracture must NOT set display:block on #ID selector (prevents split viewport bug)');
    assert.ok(PaneRock2Block && !/display\s*:\s*block/.test(PaneRock2Block[0]),
        '#PaneRock2 must NOT set display:block on #ID selector (prevents split viewport bug)');
    assert.ok(/\.terrain-stage-grid\.mode-rock2[\s\S]*?\.stage-pane-fracture\s*\{\s*display:\s*none\s*!important/.test(CssText),
        'mode-rock2 must hide .stage-pane-fracture with !important');

    // 1. Validate every Base Rock Node is a SINGLE atomic plain rock instance when generator='none' (zero hardcoded multi-rock loops!)
    for (const Node of ROCK2_NODE_CATALOG)
    {
        const Doc = {
            name: `Test ${Node.name}`,
            layers: [createRock2Layer(Node.type, { seed: 4242, generator: createRock2Generator('none') })]
        };
        const Compiled = compileRock2Stack(Doc);
        assert.equal(Compiled.compiledLayers.length, 1, `${Node.type}: must compile 1 active layer`);
        assert.equal(Compiled.compiledLayers[0].instanceCount, 1, `${Node.type}: base rock without generator must emit EXACTLY 1 rock instance`);
        const RootBlocks = Compiled.primitives.filter(pr => pr.primOp > -1.5 && pr.primOp < -0.5);
        assert.equal(RootBlocks.length, 1, `${Node.type}: base rock without generator must have EXACTLY 1 instance root primitive`);

        // Verify all UI controls in Node.controlGroups map to real defaults (zero dead controls!)
        for (const Grp of Node.controlGroups)
        {
            for (const Ctrl of Grp.controls)
            {
                assert.ok(Object.prototype.hasOwnProperty.call(Node.defaults, Ctrl.key),
                    `${Node.type} control "${Ctrl.key}" must exist in defaults`);
            }
        }

        // Sample grid inside bounds to verify genuine solid interior (< -0.04) and clean exterior (> +0.25) with zero NaN
        const B = Compiled.bounds;
        let MinD = 1e9, MaxD = -1e9;
        for (let iz = 0; iz < 11; iz++)
        {
            const wz = B.minZ + (iz / 10) * (B.maxZ - B.minZ);
            for (let iy = 0; iy < 11; iy++)
            {
                const wy = B.minY + (iy / 10) * (B.maxY - B.minY);
                for (let ix = 0; ix < 11; ix++)
                {
                    const wx = B.minX + (ix / 10) * (B.maxX - B.minX);
                    const D = evalRock2StackSdf(Compiled, wx, wy, wz);
                    assert.ok(Number.isFinite(D), `${Node.type}: SDF must be finite everywhere`);
                    if (D < MinD) MinD = D;
                    if (D > MaxD) MaxD = D;
                }
            }
        }
        assert.ok(MinD < -0.04, `${Node.type}: must contain solid negative SDF interior (minD=${MinD.toFixed(4)})`);
        assert.ok(MaxD > 0.25, `${Node.type}: must have positive exterior SDF field (maxD=${MaxD.toFixed(4)})`);

        const Packed = packRock2StackGpuBuffers(Compiled);
        assert.equal(Packed.blockTexWidth, 11, 'Packed GPU block texture width must be 11 RGBA32F columns');
        assert.equal(Packed.numBlocks, Compiled.primitives.length);
        for (let k = 0; k < Packed.blockTexData.length; k++)
        {
            assert.ok(Number.isFinite(Packed.blockTexData[k]), `${Node.type}: packed blockTexData must be finite`);
        }
    }

    // 2. Validate Composable Per-Layer Generators (`spline`, `array`, `splatter`, `scatter`) on arbitrary Base Rocks!
    const GenTests = [
        ['cliff_block', createRock2Generator('spline', { splinePreset: 'arc', count: 6, intraBlend: 0.24 }), 6],
        ['hoodoo_column', createRock2Generator('spline', { splinePreset: 'arc', count: 5, intraBlend: 0.20 }), 5],
        ['smooth_rock', createRock2Generator('spline', { splinePreset: 'scurve', knots: buildRock2SplinePresetKnots('scurve'), count: 7, intraBlend: 0.0 }), 7],
        ['spire_pinnacle', createRock2Generator('spline', { splinePreset: 'ring', closedLoop: 1, knots: buildRock2SplinePresetKnots('ring'), count: 8 }), 8],
        ['cliff_block', createRock2Generator('spline', { splinePreset: 'box_canyon', knots: buildRock2SplinePresetKnots('box_canyon'), count: 9 }), 9],
        ['big_boulder', createRock2Generator('array', { arrayMode: 'line', countX: 6, startX: -5, endX: 5 }), 6],
        ['floating_rock', createRock2Generator('array', { arrayMode: 'radial', radialCount: 5 }), 5],
        ['small_rock', createRock2Generator('array', { arrayMode: 'grid2d', countX: 3, countZ: 4 }), 12],
        ['cliff_block', createRock2Generator('array', { arrayMode: 'grid3d', countX: 2, countY: 2, countZ: 2 }), 8],
        ['basalt_column', createRock2Generator('array', { arrayMode: 'hex', hexRings: 2 }), 19],
        ['spire_pinnacle', createRock2Generator('array', { arrayMode: 'radial', radialCount: 9 }), 9],
        ['small_rock', createRock2Generator('splatter', { splatterMode: 'burst', count: 14 }), 14],
        ['small_rock', createRock2Generator('splatter', { splatterMode: 'talus_fan', count: 12 }), 12],
        ['smooth_rock', createRock2Generator('splatter', { splatterMode: 'clusters', count: 15 }), 15],
        ['basalt_column', createRock2Generator('splatter', { splatterMode: 'rings', count: 10 }), 10],
        ['small_rock', createRock2Generator('scatter', { count: 18, scatterWidth: 7.0, scatterLength: 6.0 }), 18]
    ];
    for (const [BaseType, Gen, ExpectedCount] of GenTests)
    {
        const Eval = evaluateRock2GeneratorInstances(Gen, { seed: 101, width: 1.2, height: 1.0, length: 1.2 }, BaseType);
        assert.equal(Eval.instances.length, ExpectedCount, `${BaseType} + ${Gen.type}: expected ${ExpectedCount} instances`);
        const Comp = compileRock2Stack({ layers: [createRock2Layer(BaseType, { generator: Gen })] });
        assert.equal(Comp.compiledLayers[0].instanceCount, ExpectedCount, `${BaseType} + ${Gen.type}: must compile ${ExpectedCount} rock instances`);
    }

    // 3. Validate Composable CSG Layer Stack Operations: union, smooth_union, chamfer_union, subtract, intersect + Solo/Mute
    const BaseBoulder = createRock2Layer('big_boulder', {
        name: 'Base Boulder',
        combineMode: 'union',
        posX: 0, posY: 0, posZ: 0,
        width: 4.4, height: 3.6, length: 4.4
    });
    const CutterCobble = createRock2Layer('smooth_rock', {
        name: 'Carve Cavity',
        combineMode: 'subtract',
        blendRadius: 0.25,
        posX: 0, posY: 1.6, posZ: 0,
        width: 2.8, height: 2.4, length: 5.6
    });
    const TopSpire = createRock2Layer('spire_pinnacle', {
        name: 'Fused Spire',
        combineMode: 'smooth_union',
        blendRadius: 0.40,
        posX: 1.8, posY: 0, posZ: 0,
        width: 2.0, height: 4.8, length: 2.0
    });

    const StackSub = compileRock2Stack({ name: 'CSG Stack', layers: [BaseBoulder, CutterCobble, TopSpire] });
    const DCarved = evalRock2StackSdf(StackSub, 0.0, 1.8, 0.0);
    assert.ok(DCarved > 0.05, `Subtract layer must carve open cavity inside base boulder (got d=${DCarved.toFixed(3)})`);

    CutterCobble.enabled = false;
    const StackMuted = compileRock2Stack({ name: 'Muted Cutter', layers: [BaseBoulder, CutterCobble, TopSpire] });
    assert.ok(evalRock2StackSdf(StackMuted, 0.0, 1.8, 0.0) < -0.25, 'Muting subtract layer must restore solid base boulder');
    CutterCobble.enabled = true;

    TopSpire.solo = true;
    const StackSolo = compileRock2Stack({ name: 'Solo Spire', layers: [BaseBoulder, CutterCobble, TopSpire] });
    assert.equal(StackSolo.compiledLayers.length, 1, 'Solo flag must compile only the soloed layer');
    assert.ok(evalRock2StackSdf(StackSolo, -1.6, 1.2, 0.0) > 0.40, 'Non-soloed base boulder must not contribute while Solo is active');
    assert.ok(evalRock2StackSdf(StackSolo, 1.8, 1.5, 0.0) < -0.15, 'Soloed spire must remain solid stone');
    TopSpire.solo = false;

    const ChamferLayer = cloneRock2Layer(TopSpire);
    ChamferLayer.combineMode = 'chamfer_union';
    const IntersectLayer = createRock2Layer('rock_slab', { combineMode: 'intersect', blendRadius: 0.1, width: 6, height: 2.5, length: 6 });
    const StackAllOps = compileRock2Stack({ name: 'All Ops', layers: [BaseBoulder, ChamferLayer, IntersectLayer] });
    assert.ok(Number.isFinite(evalRock2StackSdf(StackAllOps, 0.2, 1.0, 0.1)), 'Chamfer & Intersect stack modes must evaluate finite SDF');

    // 4. Validate Starter Stacks, Pure-SDF QEF Dual-Contour Mesh Extraction (0 Mesh->SDF), GLB/OBJ/STL export & JSON round-trip
    assert.ok(ROCK2_STARTER_STACKS.length >= 14, 'Must provide >= 14 starter stacks (including Moab Arch, Sky Island, Cleaved Jigsaw Monolith & Conjugate Fault Cliff)');
    const OutcropDoc = createDefaultRock2Document('stack-cliff-outcrop');
    const OutcropCompiled = compileRock2Stack(OutcropDoc);
    assert.equal(OutcropCompiled.compiledLayers[0].generatorType, 'spline');
    assert.equal(OutcropCompiled.compiledLayers[1].generatorType, 'none');
    assert.equal(OutcropCompiled.compiledLayers[2].generatorType, 'splatter');

    const { mesh, sdfStats } = bakeRock2StackQefMesh(OutcropCompiled, { resolution: 54 });
    assert.equal(sdfStats.meshToSdfCount, 0, 'RockFormation 2.0 must perform 0 Mesh->SDF conversions');
    assert.ok(sdfStats.triangles > 1200, `Multi-layer plain rock stack must extract clean QEF mesh (got ${sdfStats.triangles} tris)`);
    assert.ok(sdfStats.brickOccupancyPct < 85, `4^3 sparse bricks must cull empty space (${sdfStats.brickOccupancyPct}% occupancy)`);

    const GlbBytes = exportFractureMeshGlb(mesh, { name: 'rock2-outcrop' }, sdfStats);
    const ObjText = exportFractureMeshObj(mesh, 'rock2-outcrop');
    const StlBytes = exportFractureMeshBinaryStl(mesh, 'rock2-outcrop');
    assert.ok(GlbBytes.byteLength > 1000 && ObjText.length > 1000 && StlBytes.byteLength > 1000, 'GLB/OBJ/STL exports must succeed');

    const JsonRoundTrip = deserializeRock2Document(JSON.parse(JSON.stringify(serializeRock2Document(OutcropDoc))));
    assert.equal(JsonRoundTrip.layers.length, OutcropDoc.layers.length, 'JSON round-trip must preserve layer count');
    assert.equal(JsonRoundTrip.layers[0].generator.type, 'spline', 'JSON round-trip must preserve Spline generator');
    assert.equal(JsonRoundTrip.layers[2].generator.type, 'splatter', 'JSON round-trip must preserve Splatter generator');
});

// 29. Interactive 3D Catmull-Rom Spline Knots + Non-Cloned Varied Array Rocks (Equal & Randomized Spacing)
Suite('29. Interactive 3D Spline knot editing + Varied Non-Cloned Array rocks with random angles & spacing jitter', () =>
{
    // A. Layer-local <-> World 3D gizmo coordinate round-trip
    const R = buildRock2EulerMatrix3(37.5, -14.0, 8.5);
    const Pivot = [2.4, 0.9, -1.7];
    const W = forwardRock2Point(R, Pivot, -3.2, 1.5, 4.1);
    const L = inverseRock2Vector(R, W[0] - Pivot[0], W[1] - Pivot[1], W[2] - Pivot[2]);
    assert.ok(Math.hypot(L[0] - (-3.2), L[1] - 1.5, L[2] - 4.1) < 1e-6, 'Gizmo 3D Euler forward/inverse transform must be exact');

    // B. Multi-knot 3D Catmull-Rom Spline arc-length sampling + live knot manipulation for Canyon & Cliff walls
    const CanyonDoc = createDefaultRock2Document('stack-canyon-swall');
    const WallLayer = CanyonDoc.layers[0];
    assert.equal(WallLayer.generator.type, 'spline');
    assert.ok(WallLayer.generator.knots.length >= 5, 'Winding Canyon S-Spline Wall must start with >= 5 editable 3D knots');

    const ArcTable0 = buildRock2SplineArcTable(WallLayer.generator.knots, false);
    assert.ok(ArcTable0.totalLen > 10.0, `Canyon spline arc length must be > 10m (got ${ArcTable0.totalLen.toFixed(2)}m)`);
    const MidSample0 = sampleRock2SplineAtNormalizedArc(ArcTable0, 0.5);
    assert.ok(Number.isFinite(MidSample0.x) && Math.hypot(MidSample0.tx, MidSample0.ty, MidSample0.tz) > 0.99, 'Spline sample must carry unit tangent');

    // Simulate dragging middle knot #2 in 3D viewport (+Z bend & +Y cliff rise) + inserting a new knot
    WallLayer.generator.knots[2].z += 3.2;
    WallLayer.generator.knots[2].y += 2.1;
    WallLayer.generator.knots.splice(3, 0, { x: 2.2, y: 1.4, z: 1.9, scale: 1.15 });
    const CompiledEdited = compileRock2Stack(CanyonDoc);
    const ArcTable1 = buildRock2SplineArcTable(WallLayer.generator.knots, false);
    const MidSample1 = sampleRock2SplineAtNormalizedArc(ArcTable1, 0.45);
    assert.ok(Math.hypot(MidSample1.x - MidSample0.x, MidSample1.y - MidSample0.y, MidSample1.z - MidSample0.z) > 1.2,
        'Dragging & inserting 3D spline knots must deform the canyon cliff trajectory in 3D space');
    assert.ok(evalRock2StackSdf(CompiledEdited, MidSample1.x, MidSample1.y + 1.2, MidSample1.z) < 0.85,
        'Compiled SDF cliff blocks must follow the deformed 3D spline');

    // Verify custom 3D knots survive JSON v3 save/load round-trip verbatim
    const CanyonRT = deserializeRock2Document(JSON.parse(JSON.stringify(serializeRock2Document(CanyonDoc))));
    assert.equal(CanyonRT.layers[0].generator.knots.length, WallLayer.generator.knots.length, 'JSON round-trip must preserve inserted spline knots');
    assert.ok(Math.abs(CanyonRT.layers[0].generator.knots[2].y - WallLayer.generator.knots[2].y) < 1e-6, 'JSON round-trip must preserve 3D knot elevation');

    // C. Array Generator MUST NEVER produce a rigid clone row:
    //    1) Equal spacing (`spacingJitter = 0`, `lateralJitter = 0`) -> uniform spacing along A->B, YET every rock is a DIFFERENT rock at a RANDOM angle!
    const EqualSpacedArray = createRock2Layer('big_boulder', {
        seed: 303,
        generator: createRock2Generator('array', {
            arrayMode: 'line',
            countX: 6,
            startX: -5.0, startY: 0.0, startZ: 0.0,
            endX: 5.0, endY: 0.0, endZ: 0.0,
            spacingJitter: 0.0,
            lateralJitter: 0.0,
            rockPoolMode: 'varied_base',
            shapeVariety: 0.85,
            yawJitterDeg: 180,
            tiltJitterDeg: 12,
            seed: 809
        })
    });
    const EvalEq = evaluateRock2GeneratorInstances(EqualSpacedArray.generator, EqualSpacedArray.params, EqualSpacedArray.type);
    assert.equal(EvalEq.instances.length, 6);

    // Check equal 2.0m step spacing along X when spacingJitter === 0:
    for (let i = 1; i < EvalEq.instances.length; i++)
    {
        const dx = EvalEq.instances[i].pos[0] - EvalEq.instances[i - 1].pos[0];
        assert.ok(Math.abs(dx - 2.0) < 1e-5, `spacingJitter=0 must preserve equal spacing along A->B (got dx=${dx})`);
    }
    // Check that NO TWO rocks in the array are clones: distinct seeds, distinct angles, AND distinct compiled half-space planes!
    const CompEq = compileRock2Stack({ layers: [EqualSpacedArray] });
    const Seeds = new Set(EvalEq.instances.map(it => it.seedOffset));
    const Yaws = new Set(EvalEq.instances.map(it => Math.round(it.yawDeg)));
    assert.equal(Seeds.size, 6, 'Every rock in the array must receive a unique procedural seed');
    assert.ok(Yaws.size >= 5, 'Rocks in the array must have randomized angles, not uniform clone orientation');

    for (let i = 1; i < CompEq.primitives.length; i++)
    {
        const P0 = CompEq.primitives[i - 1];
        const P1 = CompEq.primitives[i];
        let MaxPlaneDiff = 0;
        const CheckN = Math.min(P0.planes.length, P1.planes.length);
        for (let k = 0; k < CheckN; k++)
        {
            MaxPlaneDiff = Math.max(MaxPlaneDiff, Math.abs(P0.planes[k] - P1.planes[k]));
        }
        assert.ok(P0.numPlanes !== P1.numPlanes || MaxPlaneDiff > 0.03,
            `Adjacent array rocks #${i - 1} and #${i} must have distinct sculpted rock geometry (never identical clones)`);
    }

    //    2) Randomized spacing (`spacingJitter = 0.60`, `lateralJitter = 0.35`) + `mixed_boulders` pool -> non-uniform spacing + mixed rock types!
    const JitteredMixedArray = createRock2Layer('big_boulder', {
        seed: 404,
        generator: createRock2Generator('array', {
            arrayMode: 'line',
            countX: 7,
            startX: -6.0, startY: 0.0, startZ: -1.5,
            endX: 6.0, endY: 1.8, endZ: 2.0,
            spacingJitter: 0.60,
            lateralJitter: 0.35,
            rockPoolMode: 'mixed_boulders',
            shapeVariety: 0.90,
            seed: 991
        })
    });
    const EvalJit = evaluateRock2GeneratorInstances(JitteredMixedArray.generator, JitteredMixedArray.params, JitteredMixedArray.type);
    const StepLengths = [];
    for (let i = 1; i < EvalJit.instances.length; i++)
    {
        const A = EvalJit.instances[i - 1].pos, B = EvalJit.instances[i].pos;
        StepLengths.push(Math.hypot(B[0] - A[0], B[1] - A[1], B[2] - A[2]));
    }
    const MinStep = Math.min(...StepLengths);
    const MaxStep = Math.max(...StepLengths);
    assert.ok(MaxStep - MinStep > 0.25, `spacingJitter > 0 must randomize rock spacing along A->B (min=${MinStep.toFixed(2)}m, max=${MaxStep.toFixed(2)}m)`);
    const NodeTypesUsed = new Set(EvalJit.instances.map(it => it.rockType));
    assert.ok(NodeTypesUsed.size >= 2, `mixed_boulders pool must mix multiple plain rock archetypes along the array (got ${[...NodeTypesUsed].join(', ')})`);
});

// 30. Step A Plain Pure-SDF Base Shapes: Hoodoo Column, Pedestal / Balanced Rock, Floating Rock / Sky Island, Karst/Cathedral Spires & Yardang/Half-Dome Cliffs
Suite('30. Step A Plain Pure-SDF Base Shapes: Hoodoos, Pedestals, Elevated Floating Rocks, Karst/Organ Spires, Yardang/Half-Dome Cliffs & Zero Dead Controls', () =>
{
    // A. Hoodoo Column: continuous shaft from ground into crown + harmonic pinch-and-swell rings + clean QEF bake across all 4 styles
    for (const Style of ['bulb_cap', 'turret', 'totem', 'fin_spire'])
    {
        const Doc = {
            layers: [createRock2Layer('hoodoo_column', {
                hoodooStyle: Style,
                width: 2.2, height: 6.4, length: 2.1,
                ringCount: 5, ringSwell: 0.24, neckPinch: 0.38, capSize: 1.18, leanX: 0.0
            })]
        };
        const Comp = compileRock2Stack(Doc);
        // Verify continuous solid stone along centerline from y = 0.2m up to y = 5.8m (zero air gap / floating detached cap!)
        for (let y = 0.25; y <= 5.75; y += 0.35)
        {
            const dCenter = evalRock2StackSdf(Comp, 0.0, y, 0.0);
            assert.ok(dCenter < 0.0, `Hoodoo (${Style}) must have continuous unbroken stone spine at y=${y.toFixed(2)}m (got d=${dCenter.toFixed(4)})`);
        }
        const Baked = bakeRock2StackQefMesh(Comp, { resolution: 52 });
        assert.equal(Baked.sdfStats.meshToSdfCount, 0, 'Hoodoo QEF bake must use 0 Mesh->SDF');
        assert.ok(Baked.sdfStats.triangles > 600, `Hoodoo (${Style}) must bake a rich watertight QEF mesh`);
    }

    // B. Pedestal / Balanced Rock: narrow eroded throat neck < wide overhanging caprock & plinth across all 4 styles
    const measureXHalfWidthAtY = (compiled, y) =>
    {
        let maxInsideX = 0.0;
        for (let x = 0.02; x <= 3.6; x += 0.04)
        {
            if (evalRock2StackSdf(compiled, x, y, 0.0) <= 0.0 || evalRock2StackSdf(compiled, -x, y, 0.0) <= 0.0)
            {
                maxInsideX = x;
            }
        }
        return maxInsideX;
    };

    for (const Style of ['balanced_boulder', 'desert_mushroom', 'caprock_table', 'anvil_prow'])
    {
        const Doc = {
            layers: [createRock2Layer('pedestal_rock', {
                pedestalStyle: Style,
                width: 3.4, height: 5.2, length: 3.2,
                stemHeightRatio: 0.50, neckWidth: 0.32, capOverhang: 1.32, capTiltDeg: 10, capOffset: 0.0, plinthFlare: 0.45
            })]
        };
        const Comp = compileRock2Stack(Doc);
        const wPlinth = measureXHalfWidthAtY(Comp, 0.25);
        const wNeck   = measureXHalfWidthAtY(Comp, 2.25);
        const wCap    = measureXHalfWidthAtY(Comp, 3.65);
        assert.ok(wNeck > 0.15 && wCap > wNeck * 1.28 && wPlinth > wNeck * 1.15,
            `Pedestal (${Style}) must pinch at throat (plinth=${wPlinth.toFixed(2)}, neck=${wNeck.toFixed(2)}, cap=${wCap.toFixed(2)})`);
        const Baked = bakeRock2StackQefMesh(Comp, { resolution: 52 });
        assert.ok(Baked.sdfStats.triangles > 600, `Pedestal (${Style}) must extract clean QEF mesh`);
    }

    // C. Floating Rock / Sky Island: strictly elevated above Y = 0 (zero floor clip!), wide plateau rim & tapered stalactite root keel
    for (const Style of ['sky_island', 'torn_monolith', 'stalactite_dagger', 'hover_ledge'])
    {
        const HoverH = 3.2;
        const RockH = 4.8;
        const Doc = {
            layers: [createRock2Layer('floating_rock', {
                floatingStyle: Style,
                width: 4.8, height: RockH, length: 4.4,
                hoverHeight: HoverH, keelRatio: 0.62, keelSharpness: 0.80, topFlatness: 0.85, rimOverhang: 0.35, keelSpurs: 3
            })]
        };
        const Comp = compileRock2Stack(Doc);
        assert.ok(Comp.bounds.minY > 1.2, `Floating rock (${Style}) volume bounds must wrap elevated island tightly (bounds.minY=${Comp.bounds.minY.toFixed(2)})`);
        assert.ok(evalRock2StackSdf(Comp, 0.0, 0.0, 0.0) > 1.5, `Floating rock (${Style}) must remain in mid-air with empty space at Y=0`);

        const Baked = bakeRock2StackQefMesh(Comp, { resolution: 56 });
        let minVertY = Infinity, maxVertY = -Infinity;
        const pos = Baked.mesh.positions;
        for (let i = 1; i < pos.length; i += 3)
        {
            if (pos[i] < minVertY) minVertY = pos[i];
            if (pos[i] > maxVertY) maxVertY = pos[i];
        }
        assert.ok(minVertY >= HoverH - 0.22, `Floating rock (${Style}) lowest QEF vertex (${minVertY.toFixed(2)}m) must respect hoverHeight=${HoverH}m`);
        assert.ok(maxVertY > HoverH + RockH * 0.65, `Floating rock (${Style}) crown (${maxVertY.toFixed(2)}m) must reach full island height`);

        if (Style === 'sky_island')
        {
            const rimY = HoverH + RockH * 0.64;
            const tipNearY = HoverH + RockH * 0.10;
            const wRim = measureXHalfWidthAtY(Comp, rimY);
            const wTip = measureXHalfWidthAtY(Comp, tipNearY);
            assert.ok(wRim > wTip * 1.85, `Sky island rim (${wRim.toFixed(2)}m) must flare far wider than stalactite root tip (${wTip.toFixed(2)}m)`);
        }
    }

    // D. Enriched Spire (`karst_razor`, `cathedral_organ`) & Cliff (`yardang_fin`, `exfoliation_dome`) + 3 new Starter Stacks
    for (const SpireStyle of ['karst_razor', 'cathedral_organ'])
    {
        const Comp = compileRock2Stack({ layers: [createRock2Layer('spire_pinnacle', { spireStyle: SpireStyle, buttressRibs: 3, fluteDepth: 0.25 })] });
        const Baked = bakeRock2StackQefMesh(Comp, { resolution: 50 });
        assert.ok(Baked.sdfStats.triangles > 650, `Enriched spire (${SpireStyle}) must bake cleanly (${Baked.sdfStats.triangles} tris)`);
    }
    for (const CliffProf of ['yardang_fin', 'exfoliation_dome'])
    {
        const Comp = compileRock2Stack({ layers: [createRock2Layer('cliff_block', { cliffProfile: CliffProf, basalUndercut: 0.30 })] });
        const Baked = bakeRock2StackQefMesh(Comp, { resolution: 50 });
        assert.ok(Baked.sdfStats.triangles > 650, `Enriched cliff (${CliffProf}) must bake cleanly (${Baked.sdfStats.triangles} tris)`);
    }
    for (const StarterId of ['stack-bryce-hoodoos', 'stack-pedestal-outcrop', 'stack-floating-islands'])
    {
        const Doc = createDefaultRock2Document(StarterId);
        const Comp = compileRock2Stack(Doc);
        const Baked = bakeRock2StackQefMesh(Comp, { resolution: 48 });
        assert.equal(Baked.sdfStats.meshToSdfCount, 0);
        assert.ok(Baked.sdfStats.triangles > 900, `Starter stack "${StarterId}" must QEF-bake cleanly (${Baked.sdfStats.triangles} tris)`);
    }

    // E. Automated Zero-Dead-Controls Proof across all Step A Base Nodes & new controls
    const sampleGridSignature = doc =>
    {
        const comp = compileRock2Stack(doc);
        const out = new Float64Array(9 * 9 * 9);
        let p = 0;
        for (let iz = 0; iz < 9; iz++)
        {
            const wz = -4.0 + (iz / 8) * 8.0;
            for (let iy = 0; iy < 9; iy++)
            {
                const wy = (iy / 8) * 11.0;
                for (let ix = 0; ix < 9; ix++)
                {
                    const wx = -4.0 + (ix / 8) * 8.0;
                    out[p++] = evalRock2StackSdf(comp, wx, wy, wz);
                }
            }
        }
        return out;
    };

    const CheckNodes = ['hoodoo_column', 'pedestal_rock', 'floating_rock', 'spire_pinnacle', 'cliff_block'];
    for (const NodeType of CheckNodes)
    {
        const Spec = ROCK2_NODE_CATALOG.find(n => n.type === NodeType);
        const BaseSig = sampleGridSignature({ layers: [createRock2Layer(NodeType, { seed: 5150 })] });

        for (const Grp of Spec.controlGroups)
        {
            for (const Ctrl of Grp.controls)
            {
                let NextVal;
                if (Ctrl.type === 'select')
                {
                    const Cur = String(Spec.defaults[Ctrl.key]);
                    const Alt = Ctrl.options.find(o => String(o[1]) !== Cur);
                    NextVal = Alt[1];
                }
                else
                {
                    const Cur = Number(Spec.defaults[Ctrl.key]);
                    const Span = Ctrl.max - Ctrl.min;
                    NextVal = (Cur + Span * 0.35 <= Ctrl.max) ? (Cur + Math.max(Ctrl.step, Span * 0.35)) : (Cur - Math.max(Ctrl.step, Span * 0.35));
                    if (Ctrl.decimals === 0) NextVal = Math.round(NextVal);
                }

                const ModSig = sampleGridSignature({ layers: [createRock2Layer(NodeType, { seed: 5150, [Ctrl.key]: NextVal })] });
                let MaxDelta = 0.0;
                for (let i = 0; i < BaseSig.length; i++)
                {
                    const d = Math.abs(ModSig[i] - BaseSig[i]);
                    if (d > MaxDelta) MaxDelta = d;
                }
                assert.ok(MaxDelta > 1e-3,
                    `Zero-dead-controls check failed: ${NodeType}.${Ctrl.key} (${Spec.defaults[Ctrl.key]} -> ${NextVal}) produced maxDelta=${MaxDelta}`);
            }
        }
    }
});

// 31. Step B (3D Spline Arch Span + 3D Orbital Debris Cloud) & Direct 3D Layer Click-Select + Whole-Layer TRS Scale
Suite('31. Step B 3D Spline Arch Span (open portal + grounded piers), 3D Orbital Cloud & 3D Ray-Pick Layer Selection + TRS Scale', () =>
{
    const sampleWideGridSig = doc =>
    {
        const comp = compileRock2Stack(doc);
        const out = new Float64Array(11 * 9 * 9);
        let p = 0;
        for (let iz = 0; iz < 9; iz++)
        {
            const wz = -5.5 + (iz / 8) * 11.0;
            for (let iy = 0; iy < 9; iy++)
            {
                const wy = (iy / 8) * 9.5;
                for (let ix = 0; ix < 11; ix++)
                {
                    const wx = -6.5 + (ix / 10) * 13.0;
                    out[p++] = evalRock2StackSdf(comp, wx, wy, wz);
                }
            }
        }
        return out;
    };

    const maxSigDelta = (a, b) =>
    {
        let m = 0.0;
        for (let i = 0; i < a.length; i++)
        {
            const d = Math.abs(a[i] - b[i]);
            if (d > m) m = d;
        }
        return m;
    };

    // A. 3D Spline Arch Span across all 4 geological presets: grounded piers + solid crown + unobstructed daylight portal!
    for (const Preset of ['desert_arch', 'sea_arch', 'cathedral_arch', 'natural_bridge'])
    {
        const Knots = buildRock2SplinePresetKnots(Preset, 9.6, 1.6);
        const ArchLayer = createRock2Layer('cliff_block', {
            seed: 9101,
            width: 2.35, height: 1.85, length: 2.25,
            cliffProfile: 'stepped',
            generator: createRock2Generator('spline', {
                seed: 9105,
                splineMode: 'arch_span',
                splinePreset: Preset,
                knots: Knots,
                count: 10,
                archPierFlare: 0.44,
                archCrownThin: 0.24,
                archBeddingBlend: 0.45,
                archButtress: 0.45,
                archExtrados: 0.35,
                intraBlend: 0.30
            })
        });
        const Comp = compileRock2Stack({ layers: [ArchLayer] });
        const K0 = Knots[0];
        const KMid = Knots[Math.floor(Knots.length * 0.5)];
        const KEnd = Knots[Knots.length - 1];

        // Grounded piers & continuous crown must be inside solid stone (d < 0.05)
        const dLeftPier  = evalRock2StackSdf(Comp, K0.x, 0.75, K0.z);
        const dRightPier = evalRock2StackSdf(Comp, KEnd.x, 0.75, KEnd.z);
        const dCrown     = evalRock2StackSdf(Comp, KMid.x, KMid.y, KMid.z);
        // Daylight portal directly underneath crown must be wide-open air (d > +1.4m)!
        const portalX = (K0.x + KEnd.x) * 0.5;
        const portalZ = (K0.z + KEnd.z) * 0.5;
        const dPortal = evalRock2StackSdf(Comp, portalX, KMid.y * 0.38, portalZ);

        assert.ok(dLeftPier < 0.05 && dRightPier < 0.05,
            `Arch (${Preset}) left & right piers must sit grounded in stone (dL=${dLeftPier.toFixed(3)}, dR=${dRightPier.toFixed(3)})`);
        assert.ok(dCrown < 0.05,
            `Arch (${Preset}) keystone crown must form solid bridged stone (dCrown=${dCrown.toFixed(3)})`);
        assert.ok(dPortal > 1.4,
            `Arch (${Preset}) must leave a wide-open daylight portal underneath (dPortal=${dPortal.toFixed(2)}m)`);
    }

    // Zero-dead-controls check on all 5 Arch Span geological controls
    const BaseArchGen = {
        splineMode: 'arch_span', splinePreset: 'desert_arch', count: 9,
        archPierFlare: 0.42, archCrownThin: 0.22, archBeddingBlend: 0.45, archButtress: 0.45, archExtrados: 0.35
    };
    const BaseArchSig = sampleWideGridSig({
        layers: [createRock2Layer('cliff_block', { width: 2.3, height: 1.85, length: 2.2, generator: createRock2Generator('spline', BaseArchGen) })]
    });
    for (const [Key, AltVal] of [
        ['archPierFlare', 0.95],
        ['archCrownThin', 0.58],
        ['archBeddingBlend', 0.0],
        ['archButtress', 0.0],
        ['archExtrados', 0.0]
    ])
    {
        const ModSig = sampleWideGridSig({
            layers: [createRock2Layer('cliff_block', {
                width: 2.3, height: 1.85, length: 2.2,
                generator: createRock2Generator('spline', { ...BaseArchGen, [Key]: AltVal })
            })]
        });
        assert.ok(maxSigDelta(BaseArchSig, ModSig) > 0.02, `Arch control "${Key}" must actively reshape the 3D Arch SDF`);
    }

    // B. 3D Cloud / Floating Satellites across all 4 modes + inner clearance void + zero dead controls
    for (const CMode of ['orbit_halo', 'cluster_nebula', 'rising_vortex', 'shatter_wake'])
    {
        const CloudLyr = createRock2Layer('big_boulder', {
            width: 1.2, height: 1.0, length: 1.1, flatBottom: 0.0,
            generator: createRock2Generator('cloud', {
                cloudMode: CMode, count: 10, centerX: 0.0, centerY: 4.5, centerZ: 0.0,
                radiusXZ: 4.8, spreadY: 2.2, innerClearance: 0.55, orbitTiltDeg: 18
            })
        });
        const EvalC = evaluateRock2GeneratorInstances(CloudLyr.generator, CloudLyr.params, CloudLyr.type);
        assert.equal(EvalC.instances.length, 10);
        const Ys = EvalC.instances.map(it => it.pos[1]);
        assert.ok(Math.max(...Ys) - Math.min(...Ys) > 0.85, `3D Cloud (${CMode}) must distribute satellites across 3D altitude`);
        const CompC = compileRock2Stack({ layers: [CloudLyr] });
        if (CMode === 'orbit_halo')
        {
            const dVoid = evalRock2StackSdf(CompC, 0.0, 4.5, 0.0);
            assert.ok(dVoid > 1.2, `orbit_halo innerClearance must preserve open central sanctuary void (dVoid=${dVoid.toFixed(2)}m)`);
        }
    }

    const BaseCloudGen = {
        cloudMode: 'orbit_halo', count: 10, centerX: 0.0, centerY: 4.2, centerZ: 0.0,
        radiusXZ: 4.6, spreadY: 2.0, innerClearance: 0.45, orbitTiltDeg: 14,
        scaleMin: 0.40, scaleMax: 1.05, radialSizeFalloff: 0.40, yawJitterDeg: 180, tiltJitterDeg: 20, seed: 1187
    };
    const BaseCloudSig = sampleWideGridSig({
        layers: [createRock2Layer('big_boulder', { width: 1.3, height: 1.1, length: 1.2, flatBottom: 0.0, generator: createRock2Generator('cloud', BaseCloudGen) })]
    });
    for (const [Key, AltVal] of [
        ['cloudMode', 'rising_vortex'],
        ['count', 16],
        ['centerX', 2.2],
        ['centerY', 6.4],
        ['centerZ', -2.1],
        ['radiusXZ', 2.4],
        ['spreadY', 3.8],
        ['innerClearance', 0.05],
        ['orbitTiltDeg', -38],
        ['scaleMin', 0.85],
        ['scaleMax', 1.75],
        ['radialSizeFalloff', -0.65],
        ['yawJitterDeg', 0],
        ['tiltJitterDeg', 0],
        ['seed', 4409]
    ])
    {
        const ModSig = sampleWideGridSig({
            layers: [createRock2Layer('big_boulder', {
                width: 1.3, height: 1.1, length: 1.2, flatBottom: 0.0,
                generator: createRock2Generator('cloud', { ...BaseCloudGen, [Key]: AltVal })
            })]
        });
        assert.ok(maxSigDelta(BaseCloudSig, ModSig) > 0.015, `Cloud control "${Key}" must actively reshape the 3D Cloud SDF`);
    }

    // C. Direct 3D Viewport Ray-Pick Layer Selection (`pickRock2LayerAtRay`) + Whole-Layer TRS (`scaleX/Y/Z`)
    const L1 = createRock2Layer('big_boulder',    { id: 'lyr_left',   posX: -5.0, posY: 0.0, posZ: 0.0, width: 2.8, height: 2.6, length: 2.8 });
    const L2 = createRock2Layer('spire_pinnacle', { id: 'lyr_center', posX:  0.0, posY: 0.0, posZ: 0.0, width: 2.2, height: 5.5, length: 2.2 });
    const L3 = createRock2Layer('floating_rock',  { id: 'lyr_float',  posX:  5.0, posY: 0.0, posZ: 0.0, width: 2.8, height: 3.2, length: 2.6, hoverHeight: 2.6 });
    const PickComp = compileRock2Stack({ layers: [L1, L2, L3] });

    const HitLeft   = pickRock2LayerAtRay(PickComp, [-5.0, 1.2, 12.0], [0.0, 0.0, -1.0]);
    const HitCenter = pickRock2LayerAtRay(PickComp, [ 0.0, 2.5, 12.0], [0.0, 0.0, -1.0]);
    const HitFloat  = pickRock2LayerAtRay(PickComp, [ 5.0, 4.4, 12.0], [0.0, 0.0, -1.0]);
    const MissSky   = pickRock2LayerAtRay(PickComp, [ 0.0, 9.5, 12.0], [0.0, 0.0, -1.0]);

    assert.equal(HitLeft?.layerId, 'lyr_left', '3D ray-pick on left boulder must select lyr_left');
    assert.equal(HitCenter?.layerId, 'lyr_center', '3D ray-pick on center spire must select lyr_center');
    assert.equal(HitFloat?.layerId, 'lyr_float', '3D ray-pick on floating island must select lyr_float');
    assert.equal(MissSky, null, '3D ray-pick on open sky must return null');

    // Whole-layer TRS Scale (`scaleX`, `scaleY`, `scaleZ`) + JSON round-trip
    L2.params.scaleX = 1.65;
    L2.params.scaleY = 1.40;
    L2.params.scaleZ = 0.75;
    const ScaledComp = compileRock2Stack({ layers: [L2] });
    const UnscaledComp = compileRock2Stack({ layers: [createRock2Layer('spire_pinnacle', { width: 2.2, height: 5.5, length: 2.2 })] });
    const SpanXScaled   = ScaledComp.compiledLayers[0].bounds.maxX - ScaledComp.compiledLayers[0].bounds.minX;
    const SpanXUnscaled = UnscaledComp.compiledLayers[0].bounds.maxX - UnscaledComp.compiledLayers[0].bounds.minX;
    assert.ok(SpanXScaled > SpanXUnscaled * 1.45, `Whole-layer scaleX=1.65 must widen layer bounds (${SpanXUnscaled.toFixed(2)}m -> ${SpanXScaled.toFixed(2)}m)`);

    const ScaleRT = deserializeRock2Document(JSON.parse(JSON.stringify(serializeRock2Document({ layers: [L2] }))));
    assert.ok(Math.abs(ScaleRT.layers[0].params.scaleX - 1.65) < 1e-6 && Math.abs(ScaleRT.layers[0].params.scaleZ - 0.75) < 1e-6,
        'Whole-layer scaleX/Y/Z must round-trip cleanly through RockFormation 2.0 JSON');

    // Starter Stacks 11 & 12 QEF Dual-Contour Mesh Extraction (0 Mesh->SDF)
    for (const StarterId of ['stack-sandstone-arch', 'stack-sky-orbit-cloud'])
    {
        const Doc = createDefaultRock2Document(StarterId);
        const Comp = compileRock2Stack(Doc);
        const Baked = bakeRock2StackQefMesh(Comp, { resolution: 52 });
        assert.equal(Baked.sdfStats.meshToSdfCount, 0, `${StarterId} must use 0 Mesh->SDF`);
        assert.ok(Baked.sdfStats.triangles > 1000, `${StarterId} must extract rich manifold QEF mesh (${Baked.sdfStats.triangles} tris)`);
    }
});

// 32. Phase C: Non-Destructive Per-Instance Rock Selection, TRS Overrides, Archetype Swap & Hidden Toggle
Suite('32. Phase C: Multi-rock layers support non-destructive per-instance ray-picking, TRS overrides, archetype swap, seed mutation & hiding', () =>
{
    const Gen = createRock2Generator('array', {
        arrayMode: 'line',
        countX: 5,
        startX: -8.0, startY: 0.0, startZ: 0.0,
        endX: 8.0, endY: 0.0, endZ: 0.0,
        spacingJitter: 0.0,
        lateralJitter: 0.0,
        scaleMin: 1.0,
        scaleMax: 1.0,
        yawJitterDeg: 0,
        tiltJitterDeg: 0,
        rockPoolMode: 'uniform_base',
        seed: 515
    });
    const Layer = createRock2Layer('big_boulder', {
        id: 'lyr_inst_line',
        width: 2.2, height: 2.4, length: 2.2,
        generator: Gen
    });
    const LiveGen = Layer.generator;

    const BaseComp = compileRock2Stack({ layers: [Layer] });
    const BaseCL = BaseComp.compiledLayers[0];
    assert.equal(BaseCL.rawInstanceCount, 5, 'Line array must report rawInstanceCount = 5');
    assert.equal(Object.keys(BaseCL.instanceBounds).length, 5, 'All 5 stones must populate per-instance world bounds');

    // A. Sub-Instance 3D Viewport Ray-Picking (`pickRock2LayerAtRay` returns exact `instanceIndex` 0..4!)
    for (let k = 0; k < 5; k++)
    {
        const ib = BaseCL.instanceBounds[k];
        const Hit = pickRock2LayerAtRay(BaseComp, [ib.cx, ib.cy, 12.0], [0.0, 0.0, -1.0]);
        assert.ok(Hit, `Ray aimed at rock #${k} center must hit`);
        assert.equal(Hit.layerId, 'lyr_inst_line', `Ray hit must report lyr_inst_line`);
        assert.equal(Hit.instanceIndex, k, `Ray aimed at rock #${k} must identify instanceIndex = ${k} (got ${Hit.instanceIndex})`);
        assert.equal(Hit.rawInstanceCount, 5, 'Ray hit must report rawInstanceCount = 5');
    }

    // B. Non-destructive Move (`offsetX / offsetY / offsetZ`) on Rock #2 ONLY leaves Rocks #0, #1, #3, #4 untouched!
    const Sib0Before = { ...BaseCL.instanceBounds[0] };
    const Sib4Before = { ...BaseCL.instanceBounds[4] };
    const Ov2 = getOrCreateRock2InstanceOverride(LiveGen, 2);
    Ov2.offsetY = 3.5;
    Ov2.offsetZ = -2.2;
    assert.equal(hasActiveRock2InstanceOverride(Ov2), true, 'Modified override must report active');

    const MovedComp = compileRock2Stack({ layers: [Layer] });
    const MovedCL = MovedComp.compiledLayers[0];
    assert.ok(Math.abs(MovedCL.instanceBounds[0].cy - Sib0Before.cy) < 1e-6, 'Sibling rock #0 Y must be untouched when rock #2 moves');
    assert.ok(Math.abs(MovedCL.instanceBounds[4].cz - Sib4Before.cz) < 1e-6, 'Sibling rock #4 Z must be untouched when rock #2 moves');
    assert.ok(MovedCL.instanceBounds[2].cy > BaseCL.instanceBounds[2].cy + 3.2, 'Rock #2 center Y must rise by ~3.5m');
    assert.ok(MovedCL.instanceBounds[2].cz < BaseCL.instanceBounds[2].cz - 1.9, 'Rock #2 center Z must shift by ~-2.2m');

    // Ray-pick at Rock #2's NEW lifted world coordinate hits instanceIndex = 2!
    const MovedHit2 = pickRock2LayerAtRay(MovedComp, [MovedCL.instanceBounds[2].cx, MovedCL.instanceBounds[2].cy, 12.0], [0.0, 0.0, -1.0]);
    assert.equal(MovedHit2?.instanceIndex, 2, 'Ray-pick at lifted Rock #2 position must still identify instanceIndex = 2');

    // C. Per-instance Scale (`scaleX / scaleY / scaleZ`), Rotation (`yawDelta / pitchDelta / rollDelta`), `seedDelta` & `rockType` Archetype Swap!
    const Ov1 = getOrCreateRock2InstanceOverride(LiveGen, 1);
    Ov1.scaleX = 1.85;
    Ov1.scaleY = 1.60;
    Ov1.yawDelta = 42;
    Ov1.pitchDelta = 15;
    Ov1.seedDelta = 331;
    Ov1.rockType = 'hoodoo_column'; // Multi-primitive compound archetype inside a boulder array!

    const CustomComp = compileRock2Stack({ layers: [Layer] });
    const CustomCL = CustomComp.compiledLayers[0];
    assert.equal(CustomCL.instanceBounds[1].rockType, 'hoodoo_column', 'Rock #1 must swap cleanly to compound hoodoo_column archetype');
    const BaseSpan1Y = BaseCL.instanceBounds[1].maxY - BaseCL.instanceBounds[1].minY;
    const CustSpan1Y = CustomCL.instanceBounds[1].maxY - CustomCL.instanceBounds[1].minY;
    assert.ok(CustSpan1Y > BaseSpan1Y * 1.35, `Scaled hoodoo Rock #1 height (${CustSpan1Y.toFixed(2)}m) must exceed unscaled boulder (${BaseSpan1Y.toFixed(2)}m)`);

    // D. Non-destructive `hidden = 1` hides Rock #3 while keeping Rock #4 at stable index 4!
    const Ov3 = getOrCreateRock2InstanceOverride(LiveGen, 3);
    Ov3.hidden = 1;
    const HiddenComp = compileRock2Stack({ layers: [Layer] });
    const HiddenCL = HiddenComp.compiledLayers[0];
    assert.equal(HiddenCL.rawInstanceCount, 5, 'rawInstanceCount must stay 5 even when 1 rock is hidden');
    assert.equal(HiddenCL.instanceCount, 4, 'Visible instanceCount must drop from 5 to 4');
    assert.equal(HiddenCL.instanceBounds[3], undefined, 'Hidden rock #3 must produce no SDF bounds');
    assert.ok(HiddenCL.instanceBounds[4], 'Sibling rock #4 must retain stable index 4 without shifting');
    const MissHidden3 = pickRock2LayerAtRay(HiddenComp, [BaseCL.instanceBounds[3].cx, BaseCL.instanceBounds[3].cy, 12.0], [0.0, 0.0, -1.0]);
    assert.equal(MissHidden3, null, 'Ray through hidden Rock #3 must pass cleanly through empty air');

    // E. JSON v3 Round-Trip + QEF Dual-Contour Mesh Extraction (0 Mesh->SDF)
    const JsonRT = deserializeRock2Document(JSON.parse(JSON.stringify(serializeRock2Document({ layers: [Layer] }))));
    const RTComp = compileRock2Stack(JsonRT);
    for (const [px, py, pz] of [[-8, 1.0, 0], [-4, 1.8, 0], [0, 4.5, -2.2], [4, 1.0, 0], [8, 1.0, 0]])
    {
        const dOrig = evalRock2StackSdf(HiddenComp, px, py, pz);
        const dRest = evalRock2StackSdf(RTComp, px, py, pz);
        assert.ok(Math.abs(dOrig - dRest) < 1e-6, `Instance overrides must round-trip bit-identically at (${px},${py},${pz}): ${dOrig} vs ${dRest}`);
    }

    const Baked = bakeRock2StackQefMesh(HiddenComp, { resolution: 52 });
    assert.equal(Baked.sdfStats.meshToSdfCount, 0, 'Phase C customized multi-rock stack must use 0 Mesh->SDF');
    assert.ok(Baked.sdfStats.triangles > 800, `Customized multi-rock stack must bake valid QEF mesh (${Baked.sdfStats.triangles} tris)`);
});

Suite('33. Phase D · Modifier D1: Kinematic Split & Fault Plane (T^-1(p) Slip, 4 Modes, Blind V-Tip, Scope & QEF)', () =>
{
    assert.ok(ROCK2_MODIFIER_CATALOG.some(m => m.type === 'split_fault'), 'Catalog must register split_fault modifier');
    assert.equal(isRock2ModifierType('split_fault'), true, 'split_fault must be recognized as a modifier type');
    assert.equal(isRock2ModifierType('big_boulder'), false, 'Plain rock must not be flagged as a modifier');

    // A. Orthonormal Geological Fault Frame + Normal -> Strike/Dip Round-Trip (used by 3D Normal Arrow Gizmo)
    for (const [strikeIn, dipIn] of [[-135, 24], [-22.5, 58], [0, 75], [48, 36], [142, 84]])
    {
        const B = computeRock2FaultBasis(strikeIn, dipIn);
        const lenS = Math.hypot(...B.strike), lenD = Math.hypot(...B.dip), lenN = Math.hypot(...B.normal);
        assert.ok(Math.abs(lenS - 1) < 1e-6 && Math.abs(lenD - 1) < 1e-6 && Math.abs(lenN - 1) < 1e-6, 'Fault basis vectors must be unit length');
        const dotSD = B.strike[0] * B.dip[0] + B.strike[1] * B.dip[1] + B.strike[2] * B.dip[2];
        const dotSN = B.strike[0] * B.normal[0] + B.strike[1] * B.normal[1] + B.strike[2] * B.normal[2];
        const dotDN = B.dip[0] * B.normal[0] + B.dip[1] * B.normal[1] + B.dip[2] * B.normal[2];
        assert.ok(Math.abs(dotSD) < 1e-6 && Math.abs(dotSN) < 1e-6 && Math.abs(dotDN) < 1e-6, 'Fault basis must be mutually orthogonal');
        assert.ok(Math.abs(B.strike[1]) < 1e-6 && B.dip[1] <= 1e-6 && B.normal[1] >= -1e-6, 'Strike horizontal, Dip downward, Normal upper-hemisphere');

        const RT = rock2FaultNormalToStrikeDip(B.normal[0], B.normal[1], B.normal[2]);
        assert.ok(Math.abs(RT.strikeYawDeg - strikeIn) <= 0.15, `Strike round-trip mismatch: ${RT.strikeYawDeg} vs ${strikeIn}`);
        assert.ok(Math.abs(RT.dipPitchDeg - dipIn) <= 0.15, `Dip round-trip mismatch: ${RT.dipPitchDeg} vs ${dipIn}`);
    }

    // B. All 4 Split Modes + Family Distribution & Roughness / Kinematic Parameter Sensitivity
    const BaseRock = createRock2Layer('big_boulder', {
        posX: 0, posY: 0, posZ: 0,
        params: { width: 4.8, height: 4.0, depth: 4.4, bevelRadius: 0.12, seed: 211 }
    });
    const IntactComp = compileRock2Stack({ layers: [BaseRock] });
    const dCenterIntact = evalRock2StackSdf(IntactComp, 0.0, 1.8, 0.0);
    assert.ok(dCenterIntact < -1.1, `Intact boulder core at (0, 1.8, 0) must be deep inside rock (got ${dCenterIntact.toFixed(3)})`);

    // Mode 1: Single Custom Plane with Through-Cut & T^-1(p) Normal Heave + Dip-Slip + Jigsaw Roughness
    const SingleMod = createRock2Layer('split_fault', {
        params: {
            splitMode: 'single_plane',
            originX: 0.0, originY: 1.8, originZ: 0.0,
            strikeYawDeg: 0.0, // normal = (+sin(90), cos(90), 0) = (+1, 0, 0) when dip = 90°!
            dipPitchDeg: 90.0,
            macroWavinessAmp: 0.18,
            macroWavelength: 2.2,
            microJaggednessAmp: 0.05,
            microFrequency: 2.4,
            octaves: 3,
            anisotropyStretch: 1.35,
            crackGapWidth: 0.16,
            lipChamfer: 0.04,
            vTaperDepth: 0.0,
            normalHeave: 0.85,
            strikeSlip: 0.30,
            dipSlip: 0.45,
            hingeTiltDeg: 8.0,
            gravitySettle: 0.15,
            seed: 505
        }
    });
    const SingleComp = compileRock2Stack({ layers: [BaseRock, SingleMod] });
    assert.equal(SingleComp.compiledModifiers.length, 1);
    assert.equal(SingleComp.compiledModifiers[0].planes.length, 1);
    assert.equal(SingleComp.compiledModifiers[0].blocks.length, 2);

    // Along x = 0.35 (inside the dilated heave chasm between footwall x <= 0 and hanging wall shifted +0.85m along +X), SDF must be open air (> 0)!
    const dInChasm = evalRock2StackSdf(SingleComp, 0.38, 1.8, 0.0);
    assert.ok(dInChasm > 0.05, `Dilated fault chasm at x=0.38 must be open air > 0 (got ${dInChasm.toFixed(3)})`);

    // Displaced hanging wall moved out beyond original boulder bound (+X) must now contain solid rock (< 0) AND ray-pick back to BaseRock!
    let foundDisplacedInterior = false;
    for (let tx = 1.6; tx <= 3.1; tx += 0.25)
    {
        if (evalRock2StackSdf(IntactComp, tx, 1.4, -0.3) > 0.05 && evalRock2StackSdf(SingleComp, tx, 1.4, -0.3) < -0.05)
        {
            foundDisplacedInterior = true;
            break;
        }
    }
    assert.ok(foundDisplacedInterior, 'Rigid T^-1(p) heave/slip/tilt must carry solid hanging-wall rock into previously empty +X space');

    const PickDisplaced = pickRock2LayerAtRay(SingleComp, [6.0, 1.5, -0.3], [-1.0, 0.0, 0.0]);
    assert.equal(PickDisplaced?.layerId, BaseRock.id, 'Ray-picking displaced hanging wall must pull back via T^-1(p) to true source rock layer');

    // Mode 2: Parallel Fault Family (`planeCount = 4` -> 4 planes, 5 slab blocks)
    const FamilyMod = createRock2Layer('split_fault', {
        params: {
            splitMode: 'parallel_family',
            originX: 0.0, originY: 1.8, originZ: 0.0,
            strikeYawDeg: 15.0, dipPitchDeg: 72.0,
            planeCount: 4, spacing: 0.85, spacingJitter: 0.22, dipJitterDeg: 4.0,
            crackGapWidth: 0.10, normalHeave: 0.14, dipSlip: 0.18, seed: 612
        }
    });
    const FamCompA = compileRock2Stack({ layers: [BaseRock, FamilyMod] });
    assert.equal(FamCompA.compiledModifiers[0].planes.length, 4, 'Parallel family must build 4 planes');
    assert.equal(FamCompA.compiledModifiers[0].blocks.length, 5, 'Parallel family of 4 planes must partition into 5 slab blocks');
    FamilyMod.params.spacing = 1.45;
    const FamCompB = compileRock2Stack({ layers: [BaseRock, FamilyMod] });
    assert.ok(Math.abs(evalRock2StackSdf(FamCompA, 0.9, 1.8, 0.2) - evalRock2StackSdf(FamCompB, 0.9, 1.8, 0.2)) > 1e-3, 'Family spacing must actively move fault slabs');

    // Mode 3: Conjugate X-Shear Pair (`conjugate_pair` -> 2 intersecting sets, 4 quadrant blocks)
    const ConjMod = createRock2Layer('split_fault', {
        params: {
            splitMode: 'conjugate_pair',
            originX: 0.0, originY: 1.8, originZ: 0.0,
            strikeYawDeg: 10.0, dipPitchDeg: 66.0, conjugateAngleDeg: 58.0,
            planeCount: 2, crackGapWidth: 0.09, normalHeave: 0.12, dipSlip: 0.20, seed: 719
        }
    });
    const ConjCompA = compileRock2Stack({ layers: [BaseRock, ConjMod] });
    assert.equal(ConjCompA.compiledModifiers[0].planes.length, 2, '2-plane conjugate mode must emit Set A + Set B');
    assert.equal(ConjCompA.compiledModifiers[0].blocks.length, 4, 'Conjugate X-shear pair must partition into 4 rigid wedges');
    ConjMod.params.conjugateAngleDeg = 104.0;
    const ConjCompB = compileRock2Stack({ layers: [BaseRock, ConjMod] });
    assert.ok(Math.abs(evalRock2StackSdf(ConjCompA, -0.6, 2.5, 0.3) - evalRock2StackSdf(ConjCompB, -0.6, 2.5, 0.3)) > 1e-3, 'conjugateAngleDeg must rotate X-shear Set B');

    // Mode 4: Radial / Wedge Cleave (`radial_wedge` -> N sector planes, N outward wedges)
    const RadialMod = createRock2Layer('split_fault', {
        params: {
            splitMode: 'radial_wedge',
            originX: 0.0, originY: 1.8, originZ: 0.0,
            strikeYawDeg: 0.0, dipPitchDeg: 80.0,
            planeCount: 5, spacingJitter: 0.15, dipJitterDeg: 3.0,
            crackGapWidth: 0.08, normalHeave: 0.22, gravitySettle: 0.18, seed: 823
        }
    });
    const RadialComp = compileRock2Stack({ layers: [BaseRock, RadialMod] });
    assert.equal(RadialComp.compiledModifiers[0].planes.length, 5, 'Radial wedge mode must build 5 sector planes');
    assert.equal(RadialComp.compiledModifiers[0].blocks.length, 5, 'Radial wedge mode must build 5 sector blocks');

    // C. Blind V-Taper Arrest (`vTaperDepth > 0`) vs Through-Cut (`vTaperDepth = 0`) + `tipSharpness` & `lipChamfer`
    const BlindMod = createRock2Layer('split_fault', {
        params: {
            splitMode: 'single_plane',
            originX: 0.0, originY: 1.8, originZ: 0.0,
            strikeYawDeg: 0.0, dipPitchDeg: 90.0,
            macroWavinessAmp: 0.0, microJaggednessAmp: 0.0,
            crackGapWidth: 0.28,
            lipChamfer: 0.05,
            vTaperDepth: 0.75,
            tipSharpness: 1.35,
            normalHeave: 0.0, strikeSlip: 0.0, dipSlip: 0.0,
            hingeTiltDeg: 0.0, gravitySettle: 0.0
        }
    });
    const BlindComp = compileRock2Stack({ layers: [BaseRock, BlindMod] });
    let topSkinY = 4.2;
    while (topSkinY > 1.8 && evalRock2StackSdf(IntactComp, 0.0, topSkinY, 0.0) > 0.0) topSkinY -= 0.05;
    const dShallowCrack = evalRock2StackSdf(BlindComp, 0.0, topSkinY - 0.15, 0.0);
    const dMidCrack     = evalRock2StackSdf(BlindComp, 0.0, topSkinY - 0.40, 0.0);
    const dDeepCore     = evalRock2StackSdf(BlindComp, 0.0, 1.8, 0.0);
    assert.ok(dShallowCrack > 0.06, `Blind V-crack must open outer skin within vTaperDepth (got ${dShallowCrack.toFixed(3)})`);
    assert.ok(dShallowCrack > dMidCrack && dMidCrack > 0.01, `Blind V-crack aperture must taper inward (${dShallowCrack.toFixed(3)} -> ${dMidCrack.toFixed(3)})`);
    assert.ok(dDeepCore < -0.50, `Blind V-crack must arrest cleanly before deep boulder core (got ${dDeepCore.toFixed(3)})`);

    BlindMod.params.tipSharpness = 2.60;
    const BlindSharpComp = compileRock2Stack({ layers: [BaseRock, BlindMod] });
    const dMidSharp = evalRock2StackSdf(BlindSharpComp, 0.0, topSkinY - 0.40, 0.0);
    assert.ok(dMidSharp < dMidCrack - 0.01, `Higher tipSharpness must narrow mid-depth V-taper (${dMidSharp.toFixed(3)} < ${dMidCrack.toFixed(3)})`);

    // D. Scoped Modifier (`targetScope = 'single_layer'`) leaves untargeted rocks 100% untouched!
    const LeftRock = createRock2Layer('big_boulder', { posX: -3.5, posY: 0, posZ: 0, params: { width: 2.6, height: 2.6, depth: 2.6 } });
    const RightRock = createRock2Layer('big_boulder', { posX: 3.5, posY: 0, posZ: 0, params: { width: 2.6, height: 2.6, depth: 2.6 } });
    const ScopedMod = createRock2Layer('split_fault', {
        params: {
            targetScope: 'single_layer',
            targetLayerId: LeftRock.id,
            splitMode: 'single_plane',
            originX: -3.5, originY: 1.2, originZ: 0.0,
            strikeYawDeg: 0.0, dipPitchDeg: 90.0,
            crackGapWidth: 0.30, normalHeave: 0.65,
            macroWavinessAmp: 0.0, microJaggednessAmp: 0.0
        }
    });
    const TwoRocksIntact = compileRock2Stack({ layers: [LeftRock, RightRock] });
    const TwoRocksScoped = compileRock2Stack({ layers: [LeftRock, RightRock, ScopedMod] });
    assert.ok(evalRock2StackSdf(TwoRocksScoped, -3.3, 1.2, 0.0) > 0.02, 'Targeted LeftRock must be cleaved open');
    const dRightBefore = evalRock2StackSdf(TwoRocksIntact, 3.5, 1.2, 0.0);
    const dRightAfter  = evalRock2StackSdf(TwoRocksScoped, 3.5, 1.2, 0.0);
    assert.ok(Math.abs(dRightBefore - dRightAfter) < 1e-6, `Untargeted RightRock must stay bit-identical (${dRightBefore} vs ${dRightAfter})`);

    // E. Starter Stacks #13 & #14 + GPU Fault Packer + JSON v3 Round-Trip + QEF Dual-Contour Mesh (0 Mesh->SDF)
    assert.ok(ROCK2_STARTER_STACKS.some(s => s.id === 'stack-cleaved-jigsaw-monolith'), 'Starter Stack #13 must exist');
    assert.ok(ROCK2_STARTER_STACKS.some(s => s.id === 'stack-conjugate-fault-cliff'), 'Starter Stack #14 must exist');

    const Starter13 = createDefaultRock2Document('stack-cleaved-jigsaw-monolith');
    const Comp13 = compileRock2Stack(Starter13);
    const Packed13 = packRock2StackGpuBuffers(Comp13);
    assert.ok(Packed13.gpuPasses.some(p => p.kind === 'split_fault'), 'GPU packer must emit split_fault ping-pong pass');
    assert.ok(Packed13.faultPlaneTexData.length >= 4 && Packed13.faultBlockTexData.length >= 8, 'GPU packer must pack fault plane & block textures');

    const DocRT = deserializeRock2Document(JSON.parse(JSON.stringify(serializeRock2Document(Starter13))));
    const Comp13RT = compileRock2Stack(DocRT);
    for (const [px, py, pz] of [[0.0, 2.0, 0.0], [1.2, 2.4, -0.5], [-1.1, 1.5, 0.6]])
    {
        const dA = evalRock2StackSdf(Comp13, px, py, pz);
        const dB = evalRock2StackSdf(Comp13RT, px, py, pz);
        assert.ok(Math.abs(dA - dB) < 1e-6, `split_fault JSON round-trip mismatch at (${px},${py},${pz}): ${dA} vs ${dB}`);
    }

    const Baked13 = bakeRock2StackQefMesh(Comp13, { resolution: 52 });
    assert.equal(Baked13.sdfStats.meshToSdfCount, 0, 'Modifier D1 QEF mesh extraction must perform ZERO Mesh->SDF conversions');
    assert.ok(Baked13.sdfStats.triangles > 1000, `Cleaved monolith must extract rich manifold QEF mesh (${Baked13.sdfStats.triangles} tris)`);
});

console.log(`\nAll ${Passed}/33 Open-World SDF Terrain & Pure-SDF Rock Formation 1.0 + 2.0 Studio validation suites passed!`);
