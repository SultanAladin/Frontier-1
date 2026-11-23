import { test } from 'node:test';
import assert from 'node:assert/strict';
import { VehicleOrbitCamera } from '../Source/VehicleOrbitCamera.js';
import { CameraRigs } from '../Source/CabinStructure.js';
test('independent orbit preserves user angles across repeated frames and vehicle translation', () => {
    assert.ok(CameraRigs.Orbit.OrbitVehicle);
    const Camera = new VehicleOrbitCamera();
    Camera.Rotate(650,40);
    const First = Camera.Resolve([0,0,0.53]), Yaw = Camera.Yaw, Pitch = Camera.Pitch;
    for (let I=0;I<500;I++) assert.deepEqual(Camera.Resolve([0,0,0.53]),First);
    const Moved = Camera.Resolve([10,20,0.53]);
    assert.equal(Camera.Yaw,Yaw);assert.equal(Camera.Pitch,Pitch);
    for (let I=0;I<3;I++) assert.ok(Math.abs(Moved.Eye[I]-Moved.Target[I]-(First.Eye[I]-First.Target[I]))<1e-8);
});
test('orbit permits repeated full yaw rotations while zoom and pitch remain safe', () => {
    const Camera = new VehicleOrbitCamera();
    Camera.Rotate(1e6,1e6); for(let I=0;I<5;I++)Camera.Zoom(1e6);
    assert.ok(Camera.Yaw>=0 && Camera.Yaw<2*Math.PI);
    assert.equal(Camera.Pitch,1.5);assert.equal(Camera.Distance,40);
    Camera.Rotate(-1e6,-1e6); for(let I=0;I<5;I++)Camera.Zoom(-1e6);
    assert.equal(Camera.Pitch,-0.15);assert.equal(Camera.Distance,4.5);
    const View=Camera.Resolve([0,0,0.53],()=>2);
    assert.ok(View.Eye[2]>=2.2);assert.ok(View.Eye.every(Number.isFinite));
    Camera.Reset();assert.equal(Camera.Distance,8);assert.equal(Camera.Pitch,0.3);
});
