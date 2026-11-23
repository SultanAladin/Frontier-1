import assert from 'node:assert/strict';
let Started = false, SawSources = false, SawSteps = false;
export default {
    BeforeFrame(Host, Frame) {
        if (!Started) {
            Started = true;
            assert.match(Host.Bodies.Backend, /Jolt.*WebAssembly/);
            Host.FixedStep = 1/60;
            Host.Settings.LineLock = true;
            Host.Settings.TestPlume = 1;
            Host.ScriptedInput = { Throttle: 1, Brake: 1, Steer: 0, Handbrake: false };
            Host.Settings.HmiScale = 2;
            Host.AssignCameraMode('HMI');
            Host.Cabin.Appearance.View = 3;
        }
        SawSources ||= Host.Smoke.Sources.Count > 0;
        SawSteps ||= Host.Smoke.StepsTaken > 0;
        if (Frame > 28) { Host.Settings.TestPlume = 0; Host.AssignCameraMode('Cockpit'); }
    },
    Report(Host) {
        assert.ok(Started && SawSources && SawSteps);
        assert.match(Host.Bodies.Backend, /Jolt/);
        assert.ok(Host.Smoke.MarchGroups && Host.Smoke.Resident);
        console.log(`Slate integration: Jolt active; plume sources/transport/march exercised (${Host.Smoke.StepsTaken} steps); scaled HMI focus and cockpit cameras exercised`);
    }
};
