import { test } from 'node:test';
import assert from 'node:assert/strict';
import { VehicleVisualState, VehicleRecordBytes } from '../Source/VehicleVisualState.js';
import { VehicleSolver } from '../Source/VehicleSolver.js';

test('volumetric tyre deformation encodes physical contact angle, deflection, and pressure', () => {
    const Vehicle = new VehicleSolver([]);
    Vehicle.Respawn([-100, -34, 0.53], 0);
    // Settle onto suspension and tyres
    for (let i = 0; i < 60; i++) {
        Vehicle.Advance({ Throttle: 0, Brake: 0, Steer: 0, Handbrake: false }, 1/120);
    }

    const Visual = new VehicleVisualState();
    const Data = Visual.EncodeRecord({ SteeringAngle: 0 }, Vehicle.Wheels, Vehicle.Orientation);

    assert.equal(Data.byteLength, VehicleRecordBytes);
    assert.equal(VehicleRecordBytes, 208);

    for (let w = 0; w < 4; w++) {
        const Offset = 36 + w * 4;
        const ContactAngle = Data[Offset + 0];
        const Deflection = Data[Offset + 1];
        const Pressure = Data[Offset + 2];
        const LateralDistortion = Data[Offset + 3];

        // Settle contact angle should point approximately down in wheel local frame
        assert.ok(Math.abs(ContactAngle - (-Math.PI * 0.5)) < 0.05);
        // Deflection should be positive static compression (~14 mm)
        assert.ok(Deflection > 0.010 && Deflection < 0.025);
        // Pressure should be around standard nominal (220 kPa)
        assert.ok(Pressure >= 200000 && Pressure <= 250000);
        // Standing still -> zero lateral distortion
        assert.ok(Math.abs(LateralDistortion) < 1e-4);
    }
});

test('volumetric tyre deformation responds to inflation pressure and deflation/flat tyres', () => {
    const Visual = new VehicleVisualState();

    // Nominal tyre vs flat tyre
    const NominalWheel = { TyreDeflection: 0.014, Pressure: 220000, Steer: 0, SpinAngle: 0 };
    const FlatWheel = { TyreDeflection: 0.060, Pressure: 30000, Steer: 0, SpinAngle: 0 };
    const OverinflatedWheel = { TyreDeflection: 0.008, Pressure: 350000, Steer: 0, SpinAngle: 0 };

    const Data = Visual.EncodeRecord({ SteeringAngle: 0 }, [NominalWheel, FlatWheel, OverinflatedWheel, null]);

    // Wheel 0 (Nominal):
    assert.ok(Math.abs(Data[36 + 1] - 0.014) < 1e-6);
    assert.equal(Data[36 + 2], 220000);

    // Wheel 1 (Flat):
    assert.ok(Math.abs(Data[40 + 1] - 0.060) < 1e-6);
    assert.equal(Data[40 + 2], 30000);

    // Wheel 2 (Overinflated):
    assert.ok(Math.abs(Data[44 + 1] - 0.008) < 1e-6);
    assert.equal(Data[44 + 2], 350000);

    // Wheel 3 (Null/missing):
    assert.equal(Data[48 + 1], 0);
});

test('volumetric tyre deformation handles cornering lateral distortion and steer rotation', () => {
    const Visual = new VehicleVisualState();
    const CorneringWheel = {
        TyreDeflection: 0.018,
        Pressure: 220000,
        Steer: 0.2,
        SpinAngle: 1.5,
        LateralForce: 4500
    };

    const Data = Visual.EncodeRecord({ SteeringAngle: 0.2 }, [CorneringWheel]);
    const ContactAngle = Data[36 + 0];
    const Deflection = Data[36 + 1];
    const Pressure = Data[36 + 2];
    const LateralDistortion = Data[36 + 3];

    assert.ok(Number.isFinite(ContactAngle));
    assert.ok(Math.abs(Deflection - 0.018) < 1e-6);
    assert.equal(Pressure, 220000);
    assert.ok(LateralDistortion > 0.01 && LateralDistortion < 0.04);
});
