import { test } from 'node:test';
import assert from 'node:assert/strict';
import { VehicleVisualState, VehicleRecordBytes, VehicleHeaderFloats } from '../Source/VehicleVisualState.js';
import { VehicleSolver } from '../Source/VehicleSolver.js';
import { SolidContinuumWheel, ShellContinuumWheel, NodesPerWheel, MeridionalCount } from '../Source/TyreContinuumElasticity.js';
import { CarcassWheel } from '../Source/TyreCarcassProjection.js';

test('VehicleSolver starts with 3D FEM ("solid") tyre model by default and encodes 32x5 nodal displacement field', () => {
    const Vehicle = new VehicleSolver([]);
    assert.equal(Vehicle.TyreModel, 'solid');
    Vehicle.Respawn([-40, 0, 0.53], 0);

    for (let i = 0; i < 60; i++) {
        Vehicle.Advance({ Throttle: 0, Brake: 0, Steer: 0, Handbrake: false }, 1 / 120);
    }

    const Visual = new VehicleVisualState();
    const Data = Visual.EncodeRecord({ SteeringAngle: 0 }, Vehicle.Wheels, Vehicle.Orientation);

    assert.equal(Data.byteLength, VehicleRecordBytes);
    assert.equal(VehicleRecordBytes, 10448);

    for (let w = 0; w < 4; w++) {
        const WheelOffset = 20 + w * 4;
        assert.equal(Data[WheelOffset + 0], 0); // not shredded
        assert.equal(Data[WheelOffset + 1], 1); // 3D FEM carcass active

        const Offset = 36 + w * 4;
        const ContactAngle = Data[Offset + 0];
        const Deflection = Data[Offset + 1];
        const Pressure = Data[Offset + 2];
        const LateralDistortion = Data[Offset + 3];

        assert.ok(Math.abs(ContactAngle - (-Math.PI * 0.5)) < 0.05);
        assert.ok(Deflection > 0.010 && Deflection < 0.025);
        assert.ok(Pressure >= 200000 && Pressure <= 250000);
        assert.ok(Math.abs(LateralDistortion) < 1e-4);

        // Verify non-zero 3D FEM nodal displacements in the contact zone
        const BaseOut = VehicleHeaderFloats + w * NodesPerWheel * 4;
        let MaxDisp = 0;
        for (let n = 0; n < NodesPerWheel; n++) {
            const ux = Data[BaseOut + n * 4 + 0];
            const uy = Data[BaseOut + n * 4 + 1];
            const uz = Data[BaseOut + n * 4 + 2];
            const mag = Math.hypot(ux, uy, uz);
            if (mag > MaxDisp) MaxDisp = mag;
        }
        assert.ok(MaxDisp > 0.005, `Expected 3D FEM contact displacement > 5mm on wheel ${w}, got ${MaxDisp}`);
    }
});

test('3D Hexahedral Neo-Hookean FEM (SolidContinuumWheel) produces radial contact flattening and Poisson sidewall bulge', () => {
    const Solid = new SolidContinuumWheel();
    for (let i = 0; i < 8; i++) {
        Solid.Step({
            Center: [0, 0, 0.49],
            Orientation: [0, 0, 0, 1],
            GroundNormal: [0, 0, 1],
            Deflection: 0.024,
            Pressure: 220000,
            LateralForce: 3200
        }, 1 / 60, 2);
    }

    assert.ok(Solid.MeanJacobian > 0.7 && Solid.MeanJacobian < 1.3);
    assert.ok(Solid.MeanStress > 0);
    assert.ok(Solid.ContactNodes > 0);

    // Bottom sector is theta = -pi/2 -> sector i = 24 (24 / 32 * 2*pi = 1.5*pi = -pi/2)
    const BottomSector = 24;
    const CrownIdx = (BottomSector * MeridionalCount + 2) * 3;
    const InnerSideIdx = (BottomSector * MeridionalCount + 1) * 3;
    const OuterSideIdx = (BottomSector * MeridionalCount + 3) * 3;

    // Crown at bottom (Z < 0) should flatten upward (uz > 0 => Z less negative than rest)
    const CrownUz = Solid.LocalPositions[CrownIdx + 2] - Solid.RestLocal[CrownIdx + 2];
    assert.ok(CrownUz > 0.010, `Expected upward radial flattening at bottom crown, got ${CrownUz}`);

    // Sidewalls (m=1 at -Y, m=3 at +Y) should bulge outward in opposite lateral directions (OuterUy - InnerUy > 0)
    const InnerUy = Solid.LocalPositions[InnerSideIdx + 1] - Solid.RestLocal[InnerSideIdx + 1];
    const OuterUy = Solid.LocalPositions[OuterSideIdx + 1] - Solid.RestLocal[OuterSideIdx + 1];
    assert.ok(OuterUy - InnerUy > 0.004, `Expected outward Poisson sidewall bulge, got ${OuterUy - InnerUy}`);
});

test('VehicleSolver switches cleanly between 3D FEM ("solid"), 2D Shell ("shell"), XPBD ("xpbd"), and Spring ("spring")', () => {
    const Vehicle = new VehicleSolver([]);
    const Visual = new VehicleVisualState();

    for (const Model of ['solid', 'shell', 'xpbd', 'spring']) {
        Vehicle.AssignTyreModel(Model);
        assert.equal(Vehicle.TyreModel, Model);
        Vehicle.Advance({ Throttle: 0.2, Brake: 0, Steer: 0.1, Handbrake: false }, 1 / 60);
        const Data = Visual.EncodeRecord({ SteeringAngle: 0.1 }, Vehicle.Wheels, Vehicle.Orientation);
        const ExpectedCarcassFlag = Model === 'spring' ? 0 : 1;
        assert.equal(Data[20 + 1], ExpectedCarcassFlag);
    }
});

test('volumetric tyre deformation responds to inflation pressure and deflation/flat tyres', () => {
    const Visual = new VehicleVisualState();

    const NominalWheel = { TyreDeflection: 0.014, Pressure: 220000, Steer: 0, SpinAngle: 0 };
    const FlatWheel = { TyreDeflection: 0.060, Pressure: 30000, Steer: 0, SpinAngle: 0 };
    const OverinflatedWheel = { TyreDeflection: 0.008, Pressure: 350000, Steer: 0, SpinAngle: 0 };

    const Data = Visual.EncodeRecord({ SteeringAngle: 0 }, [NominalWheel, FlatWheel, OverinflatedWheel, null]);

    assert.ok(Math.abs(Data[36 + 1] - 0.014) < 1e-6);
    assert.equal(Data[36 + 2], 220000);
    assert.ok(Math.abs(Data[40 + 1] - 0.060) < 1e-6);
    assert.equal(Data[40 + 2], 30000);
    assert.ok(Math.abs(Data[44 + 1] - 0.008) < 1e-6);
    assert.equal(Data[44 + 2], 350000);
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
