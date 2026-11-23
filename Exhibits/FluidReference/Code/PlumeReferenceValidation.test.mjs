/*==========================================================================================================================================
                                           PLUMEREFERENCEVALIDATION.TEST.MJS
==========================================================================================================================================*/
// 🧩 Exercise retained demo gas-source, moving-window and scheduling semantics without executing WGSL.

import test from 'node:test';
import assert from 'node:assert/strict';
import { WorldGridCell, WrapGridCell, FlattenGridCell, RecentreGridWindow }
    from '../../../Experimental/VehicleHmiReference/Source/SpatialGrid.js';
import { PlumeSourceProjection, SourceCapacity, SourceStride, SmokeSpecification }
    from '../../../Experimental/VehicleHmiReference/Source/PlumeSourceProjection.js';
import { ConstructGasParameters, MapPlumeSettings, ProjectPlumeTelemetry }
    from '../../../Experimental/VehicleHmiReference/Source/SmokeSequence.js';
import { PlumeGrades, PlumeSpace } from '../../../Experimental/VehicleHmiReference/Source/PlumeSpace.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                   GRID AND SOURCES
//------------------------------------------------------------------------------------------------------------------------

test('negative world cells wrap positively and recenter only outside the guard', () =>
{
    assert.deepEqual(WorldGridCell([-.01, 2, -2.1], 1), [-1, 2, -3]);
    assert.deepEqual(WrapGridCell([-1, 4, -9], [4, 4, 8]), [3, 0, 7]);
    assert.equal(FlattenGridCell([3, 0, 7], [4, 4, 8]), 115);
    assert.deepEqual(RecentreGridWindow([2.1, 0], 1, [8, 8, 4], [-4, -4]), [-4, -4]);
    assert.deepEqual(RecentreGridWindow([3.1, 0], 1, [8, 8, 4], [-4, -4]), [-1, -4]);
    assert.deepEqual(RecentreGridWindow([-3.1, -.1], 1, [8, 8, 4], [0, 0], true), [-8, -5]);
});

test('slip onset has an entry delay, release grace and immediate contact-loss reset', () =>
{
    const Projection = new PlumeSourceProjection();
    const Onset = Projection.Onsets[0];
    assert.equal(Projection.AdvanceOnset(Onset, 1000, 1, true, .02), false);
    assert.equal(Projection.AdvanceOnset(Onset, 1000, 1, true, .02), false);
    assert.equal(Projection.AdvanceOnset(Onset, 1000, 1, true, .02), true);
    assert.equal(Projection.AdvanceOnset(Onset, 140, 1, true, .04), true);
    assert.equal(Projection.AdvanceOnset(Onset, 0, 1, true, .04), true);
    assert.equal(Projection.AdvanceOnset(Onset, 0, 1, true, .04), false);
    Projection.AdvanceOnset(Onset, 1000, 1, true, .06);
    assert.equal(Projection.AdvanceOnset(Onset, 1000, 1, false, .01), false);
    assert.equal(Onset.RequestSeconds, 0);
    assert.equal(Projection.AdvanceOnset(Onset, 1000, .1, true, .10), false);
});

function ConstructWheel(InContact = true)
{
    return { InContact, SlidingPower: 18000, SlipSpeed: 2, ContactPoint: [0, 0, 0],
             ContactVelocity: [2, 0, 0], HubPosition: [0, 0, .35], Longitudinal: [1, 0, 0],
             SurfaceSpeed: 10, Radius: .35, Burst: false,
             Thermal: { Ignited: false, CombustionPower: 0, SurfaceTemperature: 523.15 } };
}

test('tyre source produces bounded smoke, rearward momentum, fire seats and light', () =>
{
    const Projection = new PlumeSourceProjection();
    const Wheel = ConstructWheel();
    const Telemetry = { Wheels: [Wheel], Velocity: [0, 0, 0], Exhausts: [], AfterfireSeconds: 0 };
    assert.equal(Projection.Project(Telemetry, .03, {}), 0);
    assert.equal(Projection.Project(Telemetry, .03, {}), 2);
    assert.equal(Projection.ActiveTyres, 1);
    assert.equal(Projection.Records.length, SourceCapacity * SourceStride);
    assert.ok(Math.abs(Projection.Records[8] - SmokeSpecification.DensityRate) < 1e-6);
    assert.ok(Math.abs(Projection.Records[4] + 2.5) < 1e-6);
    assert.ok(Math.abs(Projection.Records[SourceStride + 8] - 6.3) < 1e-5);
    const Bounds = Projection.ResolveBounds();
    assert.ok(Bounds.Minimum.every(Number.isFinite) && Bounds.Maximum.every(Number.isFinite));
    Wheel.Thermal.Ignited = true;
    Wheel.Thermal.CombustionPower = 10000;
    assert.equal(Projection.Project(Telemetry, .02, {}), 4);
    assert.equal(Projection.Lights.length, 1);
    assert.equal(Projection.Embers.length, 1);
    assert.ok(Projection.Lights[0].Radiance[0] > 0);
    assert.ok(Projection.Records[2 * SourceStride + 10] > 0);
    assert.ok(Projection.Records[3 * SourceStride + 11] > 0);
});

test('transient sources expire and no unbounded records are produced', () =>
{
    const Projection = new PlumeSourceProjection();
    const Telemetry = { Wheels: [], Velocity: [0, 0, 0], Exhausts: [], AfterfireSeconds: 0 };
    for (let I = 0; I < 8; ++I)
        Projection.QueueBurst({ Position: [I, 0, 0], Radius: .2, Velocity: [0, 0, 1],
                                Smoke: 2, Heat: 3, Flame: 1, Soot: 1, Seconds: .04 });
    assert.equal(Projection.Bursts.length, 4);
    assert.equal(Projection.Project(Telemetry, .05, {}), 4);
    assert.equal(Projection.Project(Telemetry, .05, {}), 0);
    assert.equal(Projection.Bursts.length, 0);
});

//------------------------------------------------------------------------------------------------------------------------
//                                                   ADAPTER AND SCHEDULING
//------------------------------------------------------------------------------------------------------------------------

test('adapter maps supported settings and suppresses shredded-tyre smoke', () =>
{
    const Gas = ConstructGasParameters();
    const Mapped = MapPlumeSettings(Gas, false);
    assert.equal(Mapped.PlumeEnabled, false);
    assert.equal(Mapped.PlumeBuoyancy, Gas.Buoyancy);
    assert.equal(Mapped.PlumeDensity, 1.2 * Gas.Density);
    assert.equal(Mapped.PlumeFlameDecay, -Math.log(1 - Gas.FlameLoss));
    const Vehicle = { Position: [0, 0, 0], LinearVelocity: [0, 0, 0], Orientation: [0, 0, 0, 1],
                      Wheels: [{ Steer: 0, Radius: .35, Spin: 20 }] };
    const Readings = [{ ContactFraction: 1, OnDebris: false, Shredded: true, SlidingPower: 10000 }];
    const Thermal = { Wheels: [{ Ignited: true, CombustionPower: 1000, Surface: 500, Burst: false }] };
    const Projected = ProjectPlumeTelemetry(Vehicle, Readings, Thermal);
    assert.equal(Projected.Wheels[0].SlidingPower, 0);
    assert.equal(Projected.Wheels[0].Thermal.Ignited, false);
    assert.equal(Projected.Wheels[0].InContact, true);
    assert.deepEqual(Projected.Wheels[0].Longitudinal, [1, 0, 0]);
    assert.equal(Projected.Wheels[0].SurfaceSpeed, 7);
});

test('scheduling uploads source records, retires disabled fields and does not execute a GPU', () =>
{
    const Writes = [];
    const Exchange = { Device: { queue: { writeBuffer: (...Arguments) => Writes.push(Arguments) } } };
    const Grade = { ...PlumeGrades[0], Extent: [...PlumeGrades[0].Extent] };
    const Plume = new PlumeSpace(Exchange, null, Grade);
    Plume.UniformRecords = {};
    Plume.SourceRecords = {};
    Plume.CoverageRecords = {};
    const Sources = new PlumeSourceProjection();
    Sources.Count = 1;
    Sources.Records.set([0, 0, 0, .2, 0, 0, 1, 0, 1, 1, 0, 0]);
    const Settings = MapPlumeSettings(ConstructGasParameters());
    Plume.Schedule(.01, .01, [0, 0, 0], Sources, Settings, [0, 0, 1]);
    assert.equal(Plume.Resident, true);
    assert.equal(Plume.StepPending, true);
    assert.equal(Writes.length, 3);
    assert.deepEqual(Plume.VelocityExtent, [32, 32, 10]);
    const Scalars = new Float32Array(Plume.RecordBytes);
    assert.ok(Math.abs(Scalars[19] - .01 - 1 / 60) < 1e-6);
    assert.equal(new Uint32Array(Plume.RecordBytes)[7], 1);
    Plume.Schedule(.01, .01, [0, 0, 0], Sources, { ...Settings, PlumeEnabled: false }, [0, 0, 1]);
    assert.equal(Plume.Resident, false);
    assert.equal(Plume.StepPending, false);
    assert.equal(Writes.length, 3);
});
