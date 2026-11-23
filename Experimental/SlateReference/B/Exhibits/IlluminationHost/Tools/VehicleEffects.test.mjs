import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { SweepSpark, BounceSpark, BoxPlanes } from '../Source/SparkCollision.js';
import { SparkSequence, ResolveSparkRadiance } from '../Source/SparkSequence.js';
import { TyreMarkDepot } from '../Source/TyreMarkDepot.js';
import { VehicleVisualState, VehicleRecordBytes } from '../Source/VehicleVisualState.js';
import { DriverInputIntegrator, ResolveCountersteer } from '../Source/DriverInputIntegrator.js';
import { CourseConstants } from '../Source/CourseStructure.js';
globalThis.GPUBufferUsage = { VERTEX: 1, COPY_DST: 2 };
globalThis.GPUTextureUsage = { TEXTURE_BINDING: 1, COPY_DST: 2 };
function Exchange() {
    return { ConstructRecords: () => ({}), ConstructTexture: () => ({ createView: () => ({}) }),
        ConstructSampler: () => ({}), Device: { queue: { writeBuffer() {}, writeTexture() {} } } };
}
const Wall = { Minimum: [0, -1, 0], Maximum: [0.01, 1, 3] };
test('swept sparks hit a thin wall, ground, ramp and convex debris without endpoint overlap', () => {
    const Hit = SweepSpark([-2,0,1], [2,0,1], [Wall]);
    assert.ok(Hit && Hit.Time > 0.49 && Hit.Time < 0.51);
    assert.deepEqual(Hit.Normal, [-1,0,0]);
    assert.equal(SweepSpark([-2,2,1], [2,2,1], [Wall]), null);
    const Bounce = BounceSpark([50,3,0], Hit.Normal);
    assert.ok(Bounce[0] < 0 && Bounce[1] < 3);
    assert.ok(SweepSpark([0,0,1], [0,0,-1]).Time < 0.5);
    const Body = { Position: [0,0,1.5], Radius: 2, WorldPlanes: new Float32Array(BoxPlanes(Wall)) };
    assert.ok(SweepSpark([-2,0,1], [2,0,1], [], [Body]));
    const C = CourseConstants, X = C.RampNearX + C.RampRunX * 0.5;
    const Ramp = SweepSpark([X,C.RampCentreY,C.RampRise + 1], [X,C.RampCentreY,0]);
    assert.ok(Ramp && Ramp.Normal[0] < 0 && Ramp.Normal[2] > 0);
});
test('spark integration bounces, cools monotonically, retires particles and clears GPU draw count', () => {
    const Sparks = new SparkSequence(Exchange(), null);
    Sparks.Particles = [{ Position: [-1,0,1], Velocity: [100,0,0], Kelvin: 3000, Age: 0, Impacts: 0, Size: 1 }];
    Sparks.Advance(0.02, [Wall]);
    assert.ok(Sparks.Particles[0].Position[0] < 0);
    assert.ok(Sparks.Particles[0].Velocity[0] < 0);
    assert.ok(Sparks.Particles[0].Kelvin < 3000);
    for (let I = 0; I < 300; ++I) Sparks.Advance(1/120, [Wall]);
    assert.equal(Sparks.Live, 0);
    Sparks.Clear(); assert.equal(Sparks.Metrics.Live, 0);
    const Hot = ResolveSparkRadiance(3000), Warm = ResolveSparkRadiance(1800), Cool = ResolveSparkRadiance(1000);
    assert.ok(Hot[0] > Warm[0] && Warm[0] > Cool[0]);
    assert.ok(Hot[2] / Hot[0] > Warm[2] / Warm[0]);
    assert.ok(Hot[1] / Hot[0] > Warm[1] / Warm[0]);
});
function Reading(X, Shredded = false) {
    return { ContactFraction: 1, OnDebris: false, SlidingPower: 30000, Seconds: 1/60, Shredded,
        ContactPoint: [X,0,0], ContactVelocity: [10,0,0], TreadVelocity: [10,0,0] };
}
const MarkSettings = { Enabled: true, Strength: 1 };
test('shredded wheels write rim channels only; transitions reset trails; debris and disabled marks lay nothing', () => {
    const Depot = new TyreMarkDepot(Exchange());
    Depot.Advance([Reading(0,true)], MarkSettings);
    let Rubber = 0, Rim = 0;
    for (const Tile of Depot.Tiles.values()) for (let I = 0; I < Tile.Bytes.length; I += 4) {
        Rubber += Tile.Bytes[I]; Rim += Tile.Bytes[I+2];
    }
    assert.equal(Rubber, 0); assert.ok(Rim > 0);
    const Calls = [], Stamp = Depot.StampSegment.bind(Depot);
    Depot.StampSegment = (...Args) => { Calls.push(Args); Stamp(...Args); };
    Depot.Advance([Reading(2,false)], MarkSettings);
    assert.deepEqual(Calls[0][0], Calls[0][1]);
    assert.equal(Calls[0][4], undefined);
    Calls.length = 0;
    Depot.Advance([{ ...Reading(3,true), OnDebris: true }], MarkSettings);
    Depot.Advance([Reading(4,true)], { ...MarkSettings, Enabled: false });
    assert.equal(Calls.length, 0);
    Depot.Clear(); assert.equal(Depot.Tiles.size, 0);
    assert.deepEqual(Depot.PreviousKind, [null,null,null,null]);
});
test('vehicle record preserves cabin articulation, wheel damage and volumetric deformation; every raster path shares it', () => {
    const Data = new VehicleVisualState().EncodeRecord({ SteeringAngle: 0.3, SeeThroughGlass: true }, [{ Shredded: true }, { TyreDeflection: 0.02, Pressure: 220000 }]);
    assert.equal(Data.byteLength, VehicleRecordBytes);
    assert.ok(Math.abs(Data[0] - 0.3) < 1e-6);
    assert.equal(Data[20], 1); assert.equal(Data[24], 0);
    // Volumetric deformation encoded at index 36..51:
    assert.ok(Math.abs(Data[40] - (-Math.PI * 0.5)) < 1e-5); // Local down contact angle
    assert.ok(Math.abs(Data[41] - 0.02) < 1e-6); // Deflection
    assert.equal(Data[42], 220000); // Pressure
    for (const Name of ['SurfaceRaster', 'ShadowRaster', 'ForwardRaster']) {
        const Text = fs.readFileSync(new URL(`../Shader/${Name}.wgsl`, import.meta.url), 'utf8');
        assert.ok(Text.includes('ResolveWheelVertex'));
    }
});
test('countersteer is symmetric, bounded, optional and yields to manual input; reverse/airborne restore manual control', () => {
    const F = { Speed: 25, LateralSpeed: 6, YawRate: 0.3, Contacts: 4 }, S = { Enabled: true, Strength: 1 };
    const Left = ResolveCountersteer(F, 0, S);
    const Right = ResolveCountersteer({ ...F, LateralSpeed: -6, YawRate: -0.3 }, 0, S);
    assert.ok(Left > 0 && Left <= 0.55); assert.equal(Left, -Right);
    assert.equal(ResolveCountersteer(F, 1, S), 0);
    assert.equal(ResolveCountersteer(F, 0, { Enabled: false }), 0);
    assert.equal(ResolveCountersteer({ ...F, Speed: -15 }, 0, S), 0);
    assert.equal(ResolveCountersteer({ ...F, Contacts: 0 }, 0, S), 0);
    const Driver = new DriverInputIntegrator();
    let Previous = 0;
    for (let I = 0; I < 100; ++I) {
        const Command = Driver.Advance(new Set(), 1/120, F, S);
        assert.ok(Math.abs(Command.Steer - Previous) <= 1.5 / 120 + 1e-6);
        Previous = Command.Steer;
    }
    assert.equal(Driver.Advance(new Set(), 1/120, F, { Enabled: false }).Steer, 0);
    Driver.Reset(); assert.equal(Driver.Correction, 0);
});
test('assisted left/right slide recovery stays grounded, finite and symmetric on the flat pad', async () => {
    const { VehicleSolver } = await import('../Source/VehicleSolver.js');
    const Results = [];
    for (const Sign of [-1,1]) {
        const Vehicle = new VehicleSolver([]), Driver = new DriverInputIntegrator();
        Vehicle.Respawn([-100,-34,0.53], 0);
        const Idle = { Throttle: 0, Brake: 0, Steer: 0, Handbrake: false };
        for (let I = 0; I < 240; ++I) Vehicle.Advance(Idle, 1/120);
        Vehicle.LinearVelocity = [25, Sign * 6, 0];
        Vehicle.AngularVelocity = [0,0,Sign * 0.2];
        for (let I = 0; I < 360; ++I) {
            Vehicle.Advance(Driver.Advance(new Set(), 1/120, Vehicle.QuerySteeringFeedback(),
                { Enabled: true, Strength: 0.45 }), 1/120);
            assert.ok(Vehicle.Position.every(Number.isFinite));
            assert.ok(Vehicle.Position[2] < 0.6);
        }
        assert.equal(Vehicle.QuerySteeringFeedback().Contacts, 4);
        assert.ok(Math.abs(Vehicle.QuerySteeringFeedback().LateralSpeed) < 0.1);
        Results.push(Vehicle.Position[1] + 34);
    }
    assert.ok(Math.abs(Results[0] + Results[1]) < 0.02);
});
