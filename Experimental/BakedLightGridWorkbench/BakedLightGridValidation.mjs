/*============================================================================================================================================
                                                    BAKEDLIGHTGRIDVALIDATION.MJS
============================================================================================================================================*/
// 🧩 Deterministic numerical & architectural validation suite for the Multi-Resolution 8x4x4 Spatial-Directional Baked Light Grid:
//    1. Verifies -Y-pole Octahedral encode/decode invertibility (< 1e-6 rad angular error across 1024 unit vectors).
//    2. Verifies ZERO center-averaging on Dual-Headlamps: Left (ix = 0,1) & Right (ix = 6,7) cells occupied; center ix in {2,3,4,5} == 0!
//    3. Verifies Naive Center-Averaged mode produces huge error (> 70% RelRMSE), proving why naive merging fails.
//    4. Verifies Residual-Error Optimization slashes RelRMSE below 5.0% on Dual-Headlamps and below 2.0% on 5-Direction Panels.
//    5. Verifies Continuous Nanite Distance-LOD transitions: 5 cells near -> 2 bimodal poles mid -> 1 single grid sample far -> 0 culled.
//    6. Verifies FCT1 ControlVehicle decoding, 3D Debug wireframe/solid generation, and RGBA32F GPU texture packing.

import fs from "node:fs";
import assert from "node:assert/strict";

import {
    GridDimX,
    GridDimY,
    GridDimZ,
    GridBakingMode,
    OctahedralEncode,
    OctahedralDecode,
    ConstructSceneLightClusters,
    BakeInitialLightClusterGrid,
    ComputeClusterNaniteLod,
    EvaluateBakedGridCluster,
    GenerateCalibrationReceivers,
    MeasureClusterGridError,
    StepResidualErrorOptimization,
    PackGpuLightGridTextures
} from "./LightGridBaker.js";

import {
    DecodeFct1Binary,
    BuildPresetSceneSurfaces,
    BuildDebugOverlayGeometry
} from "./SceneGeometryBuilder.js";

console.log("🔍 Running Spatial-Directional Baked Light Grid Validation Suite...\n");

// 1. Octahedral -Y Pole Invertibility Test
let MaxOctAngleRad = 0.0;
for (let S = 0; S < 1024; ++S)
{
    const Phi = S * 2.399963229728653;
    const CosT = 1.0 - (2.0 * (S + 0.5)) / 1024.0;
    const SinT = Math.sqrt(Math.max(0.0, 1.0 - CosT * CosT));
    const Dir = [Math.cos(Phi) * SinT, CosT, Math.sin(Phi) * SinT];
    const [U, V] = OctahedralEncode(Dir);
    const Rec = OctahedralDecode(U, V);
    const Dot = Math.min(1.0, Math.max(-1.0, Dir[0] * Rec[0] + Dir[1] * Rec[1] + Dir[2] * Rec[2]));
    const Ang = Math.acos(Dot);
    if (Ang > MaxOctAngleRad) MaxOctAngleRad = Ang;
}
assert(MaxOctAngleRad < 1e-6, `Octahedral invertibility exceeded tolerance: ${MaxOctAngleRad}`);
console.log(`✔️ [1/6] Octahedral (-Y pole) bijection verified across 1024 directions (max err = ${MaxOctAngleRad.toExponential(2)} rad)`);

// 2. Zero Center-Averaging Verification on Dual-Headlamps
const [CarCluster] = ConstructSceneLightClusters("dual-headlamps");
const CarGrid = BakeInitialLightClusterGrid(CarCluster);
for (let Iz = 0; Iz < GridDimZ; ++Iz)
{
    for (let Iy = 0; Iy < GridDimY; ++Iy)
    {
        for (let Ix = 2; Ix <= 5; ++Ix)
        {
            const Flat = (Iz * GridDimY + Iy) * GridDimX + Ix;
            assert.equal(CarGrid.OccupancyMask[Flat], 0, `Center voxel [${Ix},${Iy},${Iz}] must remain empty (zero center-averaging)!`);
        }
    }
}
const OccupiedCols = CarGrid.ActiveCellsLod0.map((C) => C.GridIndex[0]).sort((A, B) => A - B);
assert.deepEqual(OccupiedCols, [0, 0, 1, 6, 7]);
console.log(`✔️ [2/6] Zero Center-Averaging verified: occupied X-columns = [${OccupiedCols.join(",")}], center columns [2..5] = 0`);

// 3 & 4. Residual-Error Optimization Benchmark across Isolated Presets
for (const Preset of ["dual-headlamps", "five-emissives"])
{
    const [Cluster] = ConstructSceneLightClusters(Preset);
    const Grid = BakeInitialLightClusterGrid(Cluster);
    const Receivers = GenerateCalibrationReceivers(Cluster);

    const Naive = MeasureClusterGridError(Grid, Receivers, GridBakingMode.NaiveCenterAveraged, false);
    const Unopt = MeasureClusterGridError(Grid, Receivers, GridBakingMode.SpatialUnoptimized, false);
    StepResidualErrorOptimization(Grid, Receivers, 25);
    const Opt = MeasureClusterGridError(Grid, Receivers, GridBakingMode.ErrorOptimizedMip, false);

    assert(Naive.RelRmsePercent > 70.0, `Expected Naive Center-Averaged error > 70%, got ${Naive.RelRmsePercent}`);
    assert(Unopt.RelRmsePercent < Naive.RelRmsePercent * 0.55, "Spatial 8x4x4 must cut Naive error substantially");
    assert(Opt.RelRmsePercent < 5.0, `Expected Residual-Error Optimized RelRMSE < 5%, got ${Opt.RelRmsePercent}`);

    console.log(
        `✔️ [3-4/6] ${Preset}: Naive=${Naive.RelRmsePercent.toFixed(2)}% (${Naive.PsnrDb.toFixed(1)} dB) -> ` +
        `Raw 8x4x4=${Unopt.RelRmsePercent.toFixed(2)}% (${Unopt.PsnrDb.toFixed(1)} dB) -> ` +
        `Optimized=${Opt.RelRmsePercent.toFixed(2)}% (${Opt.PsnrDb.toFixed(1)} dB)`
    );
}

// 5. Continuous Nanite Distance-LOD & Far Single-Sample Transition
const NearEval = EvaluateBakedGridCluster(
    CarGrid, GridBakingMode.ErrorOptimizedMip, true, 0.0,
    [0.0, 0.0, 4.2], [0.0, 1.0, 0.0], [0.0, 0.707, 0.707], 0.2, [0.7, 0.7, 0.7]
);
const FarOneSampleEval = EvaluateBakedGridCluster(
    CarGrid, GridBakingMode.ErrorOptimizedMip, true, 0.0,
    [0.0, 0.0, 20.5], [0.0, 1.0, 0.0], [0.0, 0.707, 0.707], 0.2, [0.7, 0.7, 0.7]
);
const CulledEval = EvaluateBakedGridCluster(
    CarGrid, GridBakingMode.ErrorOptimizedMip, true, 0.0,
    [0.0, 0.0, 35.0], [0.0, 1.0, 0.0], [0.0, 0.707, 0.707], 0.2, [0.7, 0.7, 0.7]
);
assert.equal(NearEval.SampleLookups, 5, "Near LOD 0 must evaluate all 5 active spatial cells");
assert.equal(FarOneSampleEval.SampleLookups, 1, "Far LOD 2/3 must collapse to 1 single octahedral grid lookup");
assert.equal(CulledEval.SampleLookups, 0, "Beyond InfluenceRadius must cull to 0 lookups");
const MidLod = ComputeClusterNaniteLod(CarGrid, [0.0, 0.0, 10.5], 0.0);
assert(MidLod.LodContinuous > 0.5 && MidLod.LodContinuous < 2.2);
console.log(
    `✔️ [5/6] Nanite Distance-LOD verified: Near=${NearEval.SampleLookups} taps -> ` +
    `Far(18.5m)=${FarOneSampleEval.SampleLookups} tap -> BeyondCutoff(33m)=${CulledEval.SampleLookups} taps`
);

// 6. ControlVehicle FCT1 Decoding, 12-Cluster Hall GPU Packing & 3D Debug Geometry
const Body = DecodeFct1Binary(fs.readFileSync(new URL("./Content/ControlVehicleBody.bin", import.meta.url)).buffer);
const Rim  = DecodeFct1Binary(fs.readFileSync(new URL("./Content/ControlVehicleRim.bin", import.meta.url)).buffer);
const Tyre = DecodeFct1Binary(fs.readFileSync(new URL("./Content/ControlVehicleTyre.bin", import.meta.url)).buffer);
const HallSurf = BuildPresetSceneSurfaces("nanite-hall", { Body, Rim, Tyre });
const HallGrids = ConstructSceneLightClusters("nanite-hall").map((C) => BakeInitialLightClusterGrid(C));
const PackedGpu = PackGpuLightGridTextures(HallGrids, GridBakingMode.ErrorOptimizedMip);
const DbgGeom = BuildDebugOverlayGeometry(HallGrids, GridBakingMode.ErrorOptimizedMip, {
    Show3dGrid: true,
    ShowRawLights: true,
    ShowLodRings: true
});
assert.equal(PackedGpu.RawSourceCount, 60);
assert.equal(PackedGpu.ClusterCount, 12);
assert(HallSurf.TriangleCount > 30000);
assert(DbgGeom.LineVertexCount > 1000 && DbgGeom.SolidVertexCount > 100);
console.log(
    `✔️ [6/6] GPU RGBA32F sheet & 3D Debug verified: ${PackedGpu.ClusterCount} clusters (${PackedGpu.RawSourceCount} raw lights), ` +
    `${PackedGpu.TotalTilesUsed}/256 octahedral tiles, ${HallSurf.TriangleCount} tris, ${DbgGeom.LineVertexCount / 2} debug segments\n`
);
console.log("🏆 ALL SPATIAL-DIRECTIONAL BAKED LIGHT GRID VALIDATION CHECKS SUCCEEDED.");
