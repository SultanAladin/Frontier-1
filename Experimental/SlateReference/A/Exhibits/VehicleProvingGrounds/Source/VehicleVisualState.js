// Wheel damage state, volumetric tyre deformation and cabin articulation.
import { ConstructAxisQuaternion, MultiplyQuaternion, RotateVector, ConjugateQuaternion } from './TransformProjection.js';

// Five cabin vec4s (20 floats), four wheel damage vec4s (16 floats), four volumetric deformation vec4s (16 floats) = 52 floats = 208 bytes.
export const VehicleRecordBytes = 208;

export class VehicleVisualState {
    EncodeRecord(Header, Wheels = [], VehicleOrientation = null) {
        const Data = new Float32Array(VehicleRecordBytes / 4);
        const Cockpit = Header.Cockpit || { Anchor: [0, 0, 0], Scale: 1, Offset: [0, 0, 0] };
        Data.set([Header.SteeringAngle || 0, Header.SeeThroughGlass ? 1 : 0, 0, Header.Lamp || 0]);
        Data.set(Header.Column || [0, 0, 0], 4);
        Data.set(Header.Axis || [1, 0, 0], 8);
        Data.set([...Cockpit.Anchor, Cockpit.Scale], 12);
        Data.set([...(Header.Tablet?.Centre || [0, 0, 0]), Header.Tablet?.Scale || 1], 16);

        // 📝 Wheel damage state (offsets 20..35):
        Wheels.forEach((Wheel, Index) => {
            Data[20 + Index * 4] = Wheel?.Shredded ? 1 : 0;
        });

        // 📝 Volumetric tyre physics & pressure deformation (offsets 36..51):
        // Each wheel: [ContactAngle, Deflection, GaugePressure, LateralDistortion]
        // Runs on GPU vertex stage matching Chaos Flesh volumetric / pressure-dilated continuum deformation.
        const Orientation = VehicleOrientation || [0, 0, 0, 1];
        Wheels.forEach((Wheel, Index) => {
            if (!Wheel || Wheel.Shredded) return;

            const Deflection = Math.max(0, Wheel.TyreDeflection || 0);
            const Pressure = Math.max(0, Wheel.Pressure != null ? Wheel.Pressure : 220000);

            // Compute local contact vector from wheel occupant's orientation:
            const Steered = MultiplyQuaternion(Orientation, ConstructAxisQuaternion([0, 0, 1], Wheel.Steer || 0));
            const Spun = MultiplyQuaternion(Steered, ConstructAxisQuaternion([0, 1, 0], Wheel.SpinAngle || 0));
            const DownWorld = Wheel.GroundNormal ? Wheel.GroundNormal.map(V => -V) : [0, 0, -1];
            const LocalDown = RotateVector(ConjugateQuaternion(Spun), DownWorld);
            const ContactAngle = Math.atan2(LocalDown[2], LocalDown[0]);

            // Lateral distortion from cornering slip / lateral tire force:
            const LateralForce = Wheel.LateralForce || 0;
            const LateralDistortion = Math.max(-0.04, Math.min(0.04, LateralForce / 150000.0));

            const Offset = 36 + Index * 4;
            Data[Offset + 0] = ContactAngle;
            Data[Offset + 1] = Deflection;
            Data[Offset + 2] = Pressure;
            Data[Offset + 3] = LateralDistortion;
        });

        return Data;
    }
}
