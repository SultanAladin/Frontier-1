import { test } from 'node:test';
import assert from 'node:assert/strict';
import { JoltWorld } from '../Source/JoltWorld.js';
import { ConstructHullPlanes } from '../Source/RigidBodyWorld.js';
import { VehicleSolver } from '../Source/VehicleSolver.js';
function Box(Position = [0,0,2], Size = 0.25, Mass = 10) {
    const Points = [];
    for (const X of [-Size,Size]) for (const Y of [-Size,Size]) for (const Z of [-Size,Size]) Points.push([X,Y,Z]);
    return { Position, Orientation: [0,0,0,1], Points, Planes: ConstructHullPlanes(Points),
        Mass, Inertia: [1,1,1], Restitution: 0, Friction: 0.7 };
}
test('real Jolt WASM bodies fall, settle, sleep, reset and leave no bodies after cleanup', async () => {
    const World = await JoltWorld.Create();
    try {
        assert.match(World.Backend, /Jolt.*WebAssembly/);
        const Cube = World.AddBody(Box([-100,-34,2]));
        for (let I = 0; I < 600; ++I) World.Advance(1/120);
        assert.ok(Math.abs(Cube.Position[2] - 0.25) < 0.025, Cube.Position.join(','));
        assert.equal(Cube.Sleeping, true);
        World.PlaceBody(Cube, [-100,-34,3], [0,0,0,1], false);
        for (let I = 0; I < 30; ++I) World.Advance(1/120);
        assert.ok(Cube.Position[2] < 3 && Cube.Position[2] > 2);
        World.RemoveBody(Cube); assert.equal(World.Bodies.length, 0);
    } finally { World.Dispose(); }
});
test('Jolt static colliders prevent fast loose-body wall tunnelling and can be rebuilt', async () => {
    const World = await JoltWorld.Create();
    try {
        World.AssignObstacles([{ Minimum: [-99,-35,0], Maximum: [-98.9,-33,3] }]);
        const Cube = World.AddBody({ ...Box([-100,-34,1],0.1), Linear: [70,0,0] });
        for (let I = 0; I < 30; ++I) World.Advance(1/120);
        assert.ok(Cube.Position[0] < -98.9, Cube.Position.join(','));
        World.AssignObstacles([]); assert.equal(World.StaticBodies.length, 0);
    } finally { World.Dispose(); }
});
test('Jolt loose-body collisions exchange impulses with the custom-tyre chassis proxy', async () => {
    const World = await JoltWorld.Create();
    try {
        const Vehicle = new VehicleSolver([]);
        Vehicle.Respawn([-100,-34,0.53],0);
        const Cube = World.AddBody({ ...Box([-96.8,-34,1],0.2,200), Linear: [-15,0,0] });
        const Prior = Vehicle.LinearVelocity[0];
        for (let I = 0; I < 12; ++I) World.Advance(1/120, Vehicle);
        assert.ok(Cube.Linear[0] > -15);
        assert.ok(Vehicle.LinearVelocity[0] < Prior);
        assert.ok(Vehicle.LinearVelocity.every(Number.isFinite));
    } finally { World.Dispose(); }
});
test('real cone hulls settle and tyre-support impulses wake Jolt bodies', async () => {
    const { ResolveConeHullPoints, ConeConstants } = await import('../Source/CourseStructure.js');
    const World = await JoltWorld.Create();
    try {
        const Points = ResolveConeHullPoints().map(([X,Y,Z]) => [X,Y,Z-ConeConstants.CentreHeight]);
        const Cone = World.AddBody({ ...Box([-100,-34,1]), Points, Planes: ConstructHullPlanes(Points), Mass: ConeConstants.Mass });
        for (let I=0; I<600; ++I) World.Advance(1/120);
        assert.ok(Cone.Position[2] > 0.05 && Cone.Position[2] < 0.3);
        assert.equal(Cone.Sleeping,true);
        const Slab = World.AddBody({ ...Box([-98,-34,0.25]), Sleeping: true });
        const Vehicle = new VehicleSolver([]); Vehicle.Respawn([-100,-34,0.53],0);
        World.Settings.Vehicle = false; // isolate the tyre-support bridge from chassis contact
        World.RefreshSupport(Vehicle);
        const Support = World.QuerySupport(-98,-34,1);
        assert.equal(Support.Body,Slab); assert.ok(Math.abs(Support.Height-0.5) < 1e-5);
        Vehicle.Wheels[0].SupportBody = Slab;
        Vehicle.Wheels[0].SupportImpulse = [30,0,-20];
        Vehicle.Wheels[0].SupportPoint = [-98,-34,0.5];
        World.Advance(1/120,Vehicle);
        assert.ok(Slab.Linear[0] > 0.1); assert.equal(Slab.Sleeping,false);
        assert.deepEqual(Vehicle.Wheels[0].SupportImpulse,[0,0,0]);
    } finally { World.Dispose(); }
});
