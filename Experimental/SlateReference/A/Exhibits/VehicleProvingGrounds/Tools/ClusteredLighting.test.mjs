import test from "node:test";
import assert from "node:assert/strict";
import {
    LightingSystem,
    LightType,
    RoofFixtureType,
    AutomotiveMode,
    MaxSceneLights,
    LightRecordFloats,
    ClusterGridBounds,
    ClusterHeaderCells,
    ConstructRoofFixtureFacets
} from "../Source/LightingSystem.js";
import { ResolveLevelPlacements } from "../Source/LevelStructure.js";
import { ConstructIdentityTransform } from "../Source/TransformProjection.js";

test("LightingSystem manages automotive light modes and roof fixtures correctly", () => {
    const sys = new LightingSystem();
    assert.equal(sys.VehicleLightMode, AutomotiveMode.LowBeam);
    assert.equal(sys.RoofLightEnabled, false);
    assert.equal(sys.RoofFixture, RoofFixtureType.SlimLED);

    // Cycle modes
    assert.equal(sys.CycleVehicleMode(), AutomotiveMode.HighBeam);
    assert.equal(sys.CycleVehicleMode(), AutomotiveMode.Fog);
    assert.equal(sys.CycleVehicleMode(), AutomotiveMode.Off);
    assert.equal(sys.CycleVehicleMode(), AutomotiveMode.LowBeam);

    // Toggle roof light
    assert.equal(sys.ToggleRoofLight(), true);
    assert.equal(sys.RoofLightEnabled, true);
    assert.equal(sys.ToggleRoofLight(), false);
    assert.equal(sys.RoofLightEnabled, false);

    // Switch roof fixture
    sys.SetRoofFixture(RoofFixtureType.RallyPod);
    assert.equal(sys.RoofFixture, RoofFixtureType.RallyPod);
    sys.SetRoofFixture(RoofFixtureType.SlimLED);
    assert.equal(sys.RoofFixture, RoofFixtureType.SlimLED);
});

test("LightingSystem packs street lamps and automotive headlights without buffer overflow", () => {
    const sys = new LightingSystem();
    const placements = ResolveLevelPlacements();
    const poles = placements.filter(p => p.Shape === "PoleHead").map(p => ({
        X: p.Position[0],
        Y: p.Position[1],
        Z: p.Position[2]
    }));
    assert.equal(poles.length, 20);

    const vehicleTransform = ConstructIdentityTransform();
    const buffer = sys.Update(vehicleTransform, poles);

    assert.equal(buffer.length, MaxSceneLights * LightRecordFloats);
    assert.ok(sys.Lights.length <= MaxSceneLights);

    // Street lamps check
    const streetLamps = sys.Lights.filter(l => l.Type === LightType.StreetLamp);
    assert.ok(streetLamps.length > 0);
    assert.equal(streetLamps[0].Direction[2], -1.0); // Downward photometric orientation

    // Headlights check (Low beam default)
    const lowBeams = sys.Lights.filter(l => l.Type === LightType.LowBeam);
    assert.equal(lowBeams.length, 2);

    // Switch to High beam
    sys.SetVehicleMode(AutomotiveMode.HighBeam);
    sys.Update(vehicleTransform, poles);
    const highBeams = sys.Lights.filter(l => l.Type === LightType.HighBeam);
    assert.equal(highBeams.length, 2);

    // Enable roof light
    sys.ToggleRoofLight();
    sys.SetRoofFixture(RoofFixtureType.SlimLED);
    sys.Update(vehicleTransform, poles);
    const slimFloods = sys.Lights.filter(l => l.Type === LightType.Floodlight);
    assert.equal(slimFloods.length, 1);

    // Switch to Rally pods
    sys.SetRoofFixture(RoofFixtureType.RallyPod);
    sys.Update(vehicleTransform, poles);
    const rallyFloods = sys.Lights.filter(l => l.Type === LightType.Floodlight);
    assert.equal(rallyFloods.length, 2);
});

test("Roof fixture polygon generation produces valid geometry for SlimLED and RallyPod", () => {
    const slim = ConstructRoofFixtureFacets(RoofFixtureType.SlimLED);
    const rally = ConstructRoofFixtureFacets(RoofFixtureType.RallyPod);

    assert.ok(slim.length > 0);
    assert.ok(rally.length > 0);

    for (const facet of [...slim, ...rally]) {
        assert.equal(facet.Corners.length, 3);
        assert.equal(typeof facet.Family, "number");
        for (const pt of facet.Corners) {
            assert.equal(pt.length, 3);
            for (const coord of pt) {
                assert.ok(!Number.isNaN(coord));
                assert.ok(Number.isFinite(coord));
            }
        }
    }
});

// 📝 The cluster assignment the shaders read: header layout, cell math and overflow behaviour. These run in Node
//    against the same LightingSystem.Update the frame loop calls; they check the numbers, not the rendered result.
function ResolveCellOf(Position) {
    const Grid = ClusterGridBounds;
    const Cx = Math.min(Grid.ResX - 1, Math.max(0, Math.floor((Position[0] - Grid.Minimum[0]) / Grid.Extent[0] * Grid.ResX)));
    const Cy = Math.min(Grid.ResY - 1, Math.max(0, Math.floor((Position[1] - Grid.Minimum[1]) / Grid.Extent[1] * Grid.ResY)));
    const Cz = Math.min(Grid.ResZ - 1, Math.max(0, Math.floor((Position[2] - Grid.Minimum[2]) / Grid.Extent[2] * Grid.ResZ)));
    return (Cz * Grid.ResY + Cy) * Grid.ResX + Cx;
}

function ReadCellLights(System, Cell) {
    const Base = (ClusterHeaderCells + Cell) * 4;
    const Offset = System.ClusterCells[Base];
    const Count = System.ClusterCells[Base + 1];
    return Array.from(System.ClusterIndices.slice(Offset, Offset + Count));
}

test("AssignClusters writes a header the shader's ResolveClusterRange can read back", () => {
    const System = new LightingSystem();
    System.Update(ConstructIdentityTransform(), []);
    const Grid = ClusterGridBounds;
    assert.equal(System.ClusterCells[0], Grid.ResX);
    assert.equal(System.ClusterCells[1], Grid.ResY);
    assert.equal(System.ClusterCells[2], Grid.ResZ);
    assert.equal(System.ClusterCells[3], Grid.MaxLightsPerCell);
    assert.deepEqual(Array.from(System.ClusterCellFloats.slice(4, 7)), Grid.Minimum);
    const Inverse = [1.0 / Grid.Extent[0], 1.0 / Grid.Extent[1], 1.0 / Grid.Extent[2]];
    for (let Axis = 0; Axis < 3; ++Axis)
        assert.ok(Math.abs(System.ClusterCellFloats[8 + Axis] - Inverse[Axis]) / Inverse[Axis] < 1.0e-6);
});

test("AssignClusters bins a street lamp into its overlapped cells and no others", () => {
    const System = new LightingSystem();
    const placements = ResolveLevelPlacements();
    const poles = placements.filter(p => p.Shape === "PoleHead").map(p => ({ X: p.Position[0], Y: p.Position[1], Z: p.Position[2] }));
    System.Update(ConstructIdentityTransform(), poles);
    // Poles pack first, so light 0 is the first pole lamp.
    const Lamp = System.Lights[0];
    assert.equal(Lamp.Type, LightType.StreetLamp);
    const OwnCell = ResolveCellOf(Lamp.Position);
    assert.ok(ReadCellLights(System, OwnCell).includes(0));
    // Every listed cell must overlap the lamp's reach box; every overlapped cell must list it.
    const Grid = ClusterGridBounds;
    for (let Cell = 0; Cell < Grid.CellCount; ++Cell) {
        const Cx = Cell % Grid.ResX, Cy = Math.floor(Cell / Grid.ResX) % Grid.ResY, Cz = Math.floor(Cell / (Grid.ResX * Grid.ResY));
        const CellMin = [Grid.Minimum[0] + Cx / Grid.ResX * Grid.Extent[0], Grid.Minimum[1] + Cy / Grid.ResY * Grid.Extent[1],
                         Grid.Minimum[2] + Cz / Grid.ResZ * Grid.Extent[2]];
        const CellMax = [Grid.Minimum[0] + (Cx + 1) / Grid.ResX * Grid.Extent[0], Grid.Minimum[1] + (Cy + 1) / Grid.ResY * Grid.Extent[1],
                         Grid.Minimum[2] + (Cz + 1) / Grid.ResZ * Grid.Extent[2]];
        const Overlaps = Lamp.Position[0] + Lamp.Reach >= CellMin[0] && Lamp.Position[0] - Lamp.Reach <= CellMax[0]
                      && Lamp.Position[1] + Lamp.Reach >= CellMin[1] && Lamp.Position[1] - Lamp.Reach <= CellMax[1]
                      && Lamp.Position[2] + Lamp.Reach >= CellMin[2] && Lamp.Position[2] - Lamp.Reach <= CellMax[2];
        assert.equal(ReadCellLights(System, Cell).includes(0), Overlaps);
    }
});

test("AssignClusters spreads a long-reach headlight beam across most of the grid", () => {
    const System = new LightingSystem();
    System.Update(ConstructIdentityTransform(), []);
    const Beam = System.Lights.findIndex(l => l.Type === LightType.LowBeam);
    assert.ok(Beam >= 0);
    let Covered = 0;
    for (let Cell = 0; Cell < ClusterGridBounds.CellCount; ++Cell)
        if (ReadCellLights(System, Cell).includes(Beam))
            Covered++;
    assert.ok(Covered > 200);
});

test("AssignClusters caps a crowded cell and counts the overflow instead of failing", () => {
    const System = new LightingSystem();
    System.Lights = [];
    for (let Light = 0; Light < ClusterGridBounds.MaxLightsPerCell + 4; ++Light)
        System.Lights.push({ Type: LightType.Omni, Position: [0.0, 0.0, 5.0], Direction: [0.0, 0.0, -1.0],
                             Colour: [1.0, 1.0, 1.0], Intensity: 1.0, Reach: 60.0, Params: [0, 0, 0, 1] });
    System.AssignClusters();
    assert.ok(System.ClusterOverflow > 0);
    const Base = (ClusterHeaderCells + ResolveCellOf([0.0, 0.0, 5.0])) * 4;
    assert.equal(System.ClusterCells[Base + 1], ClusterGridBounds.MaxLightsPerCell);
    for (const Index of System.ClusterIndices)
        assert.ok(Index < System.Lights.length);
});

test("AssignClusters is deterministic across frames", () => {
    const System = new LightingSystem();
    const Transform = ConstructIdentityTransform();
    System.Update(Transform, []);
    const First = System.ClusterCells.slice();
    System.Update(Transform, []);
    assert.deepEqual(System.ClusterCells, First);
});
