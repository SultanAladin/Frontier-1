import assert from 'node:assert/strict';
let LastAngles;
export default {
    BeforeFrame(Host,Frame) {
        if(Frame===0) {
            Host.AssignCameraMode('Orbit'); Host.FixedStep=1/60;
            Host.Orbit.Rotate(200,50); Host.Orbit.Zoom(-100);
            LastAngles=[Host.Orbit.Yaw,Host.Orbit.Pitch,Host.Orbit.Distance];
        }
        assert.equal(Host.Settings.CameraMode,'Orbit');
        assert.deepEqual([Host.Orbit.Yaw,Host.Orbit.Pitch,Host.Orbit.Distance],LastAngles);
        if(Frame===10 || Frame===20) {
            Host.Orbit.Rotate(400,-25);Host.Orbit.Zoom(80);
            LastAngles=[Host.Orbit.Yaw,Host.Orbit.Pitch,Host.Orbit.Distance];
        }
    },
    Report(Host) {
        assert.equal(Host.Settings.CameraMode,'Orbit');
        assert.deepEqual([Host.Orbit.Yaw,Host.Orbit.Pitch,Host.Orbit.Distance],LastAngles);
        assert.ok(Host.CameraEye.every(Number.isFinite));
        console.log('independent Orbit camera: rotate, zoom and persistent angles exercised');
    }
};
