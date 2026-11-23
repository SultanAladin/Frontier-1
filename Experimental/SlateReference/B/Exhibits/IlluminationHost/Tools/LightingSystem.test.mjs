import test from "node:test";
import assert from "node:assert/strict";
import {
    LightingSystem,
    LightType,
    RoofFixtureType,
    AutomotiveMode,
    MaxSceneLights,
    LightRecordFloats,
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
