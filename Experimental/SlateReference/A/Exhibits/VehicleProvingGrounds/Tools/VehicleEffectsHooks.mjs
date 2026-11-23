// Exercise rigid shredded-wheel draws, marks and all Garage modes.
// HOOKS=Tools/VehicleEffectsHooks.mjs node Tools/HeadlessHarness.mjs . 36 '?capture=1'
import assert from 'node:assert/strict';
let Started = false, SawSpark = false;
export default {
    BeforeFrame(Host, Frame) {
        if (!Started) {
            Started = true;
            Host.Vehicle.ShredTyre(0);
            Host.Cabin.Appearance.Apply({ Colour: '#9eb8df', Matte: true, DarkWheels: true });
            Host.Effects.DerezVehicle(Host.Vehicle);
            Host.FixedStep = 1/60;
        }
        Host.Marks.Advance([{ ContactFraction: 1, OnDebris: false, Shredded: Frame % 12 < 6,
            SlidingPower: 30000, Seconds: 1/60, ContactPoint: [Frame * 0.05, 0, 0],
            ContactVelocity: [10,0,0], TreadVelocity: [10,0,0] }], { Enabled: true, Strength: 1 });
        Host.Cabin.Appearance.View = Math.floor(Frame / 6) % 4;
        const P = Host.Vehicle.Position;
        Host.Sparks.Emit([{ Point: [P[0],P[1],0.05], Velocity: [15,0,1], Normal: [0,0,1], Strength: 1 }], 1/60);
        SawSpark ||= Host.Sparks.Live > 0;
        // The harness has no real cabin Canvas UI. Explicitly record the preview to exercise its shaders/draws.
        const Encoder = Host.Exchange.Device.createCommandEncoder();
        Host.Cabin.RecordPreview(Encoder, Host.Scene, Host.VertexRecords, Host.Vehicle.QueryBodyTransform());
        Host.Exchange.Device.queue.submit([Encoder.finish()]);
    },
    Report(Host) {
        assert.ok(Started && SawSpark);
        assert.equal(Host.Vehicle.Wheels[0].Shredded, true);
        assert.ok(Host.Cabin.Metrics.PreviewPasses >= 3);
        console.log('effects integration: sparks, shredded wheel and Garage modes exercised');
    }
};
