// Wheel damage state, 3D FEM / Shell / XPBD carcass nodal displacement field, volumetric tyre deformation, and cabin articulation.
import { ConstructAxisQuaternion, MultiplyQuaternion, RotateVector, ConjugateQuaternion } from './TransformProjection.js';
import { NodesPerWheel } from './TyreContinuumElasticity.js';

// Header & analytical parameters:
// - 5 cabin vec4s (20 floats)
// - 4 wheel state vec4s (16 floats): [Shredded, CarcassActive, TyreModelId, Reserved]
// - 4 volumetric deformation vec4s (16 floats): [ContactAngle, Deflection, GaugePressure, LateralDistortion]
// - 4 * 160 = 640 carcass nodal displacement vec4s (2560 floats): [ux, uy, uz, radialStrain] in wheel-local space
// Total = 2612 floats = 10,448 bytes.
export const VehicleHeaderFloats   = 52;
export const VehicleCarcassFloats  = 4 * NodesPerWheel * 4; // 2560 floats
export const VehicleRecordFloats   = VehicleHeaderFloats + VehicleCarcassFloats; // 2612 floats
export const VehicleRecordBytes    = VehicleRecordFloats * 4; // 10,448 bytes

export class VehicleVisualState {
    constructor() {
        this.Data = new Float32Array(VehicleRecordFloats);
    }

    EncodeRecord(Header, Wheels = [], VehicleOrientation = null) {
        const Data = this.Data;
        Data.fill(0);

        const Cockpit = Header.Cockpit || { Anchor: [0, 0, 0], Scale: 1, Offset: [0, 0, 0] };
        Data[0] = Header.SteeringAngle || 0;
        Data[1] = Header.SeeThroughGlass ? 1 : 0;
        Data[2] = 0;
        Data[3] = Header.Lamp || 0;
        if (Header.Column) Data.set(Header.Column, 4);
        if (Header.Axis) Data.set(Header.Axis, 8);
        else Data[8] = 1;
        Data[12] = Cockpit.Anchor[0] || 0;
        Data[13] = Cockpit.Anchor[1] || 0;
        Data[14] = Cockpit.Anchor[2] || 0;
        Data[15] = Cockpit.Scale ?? 1;
        const TabletCentre = Header.Tablet?.Centre || [0, 0, 0];
        Data[16] = TabletCentre[0] || 0;
        Data[17] = TabletCentre[1] || 0;
        Data[18] = TabletCentre[2] || 0;
        Data[19] = Header.Tablet?.Scale || 1;

        const Orientation = VehicleOrientation || [0, 0, 0, 1];
        for (let Index = 0; Index < 4; ++Index) {
            const Wheel = Wheels[Index];
            const WheelOffset = 20 + Index * 4;
            Data[WheelOffset + 0] = Wheel?.Shredded ? 1 : 0;

            if (!Wheel || Wheel.Shredded) continue;

            const Deflection = Math.max(0, Wheel.TyreDeflection || 0);
            const Pressure = Math.max(0, Wheel.Pressure != null ? Wheel.Pressure : 220000);

            const Steered = MultiplyQuaternion(Orientation, ConstructAxisQuaternion([0, 0, 1], Wheel.Steer || 0));
            const Spun = MultiplyQuaternion(Steered, ConstructAxisQuaternion([0, 1, 0], Wheel.SpinAngle || 0));
            const DownWorld = Wheel.GroundNormal ? [-Wheel.GroundNormal[0], -Wheel.GroundNormal[1], -Wheel.GroundNormal[2]] : [0, 0, -1];
            const LocalDown = RotateVector(ConjugateQuaternion(Spun), DownWorld);
            const ContactAngle = Math.atan2(LocalDown[2], LocalDown[0]);

            const LateralForce = Wheel.LateralForce || 0;
            const LateralDistortion = Math.max(-0.04, Math.min(0.04, LateralForce / 150000.0));

            const DeformOffset = 36 + Index * 4;
            Data[DeformOffset + 0] = ContactAngle;
            Data[DeformOffset + 1] = Deflection;
            Data[DeformOffset + 2] = Pressure;
            Data[DeformOffset + 3] = LateralDistortion;

            // Encode 3D FEM / Shell / XPBD carcass nodal displacement vectors in wheel-local space
            const Carcass = Wheel.Carcass;
            if (Carcass && Carcass.LocalPositions && Carcass.RestLocal && Carcass.NodeCount === NodesPerWheel) {
                Data[WheelOffset + 1] = 1.0;
                const LocalPos = Carcass.LocalPositions;
                const RestPos  = Carcass.RestLocal;
                const BaseOut  = VehicleHeaderFloats + Index * NodesPerWheel * 4;
                for (let n = 0; n < NodesPerWheel; ++n) {
                    const n3 = n * 3;
                    const o4 = BaseOut + n * 4;
                    Data[o4 + 0] = LocalPos[n3 + 0] - RestPos[n3 + 0];
                    Data[o4 + 1] = LocalPos[n3 + 1] - RestPos[n3 + 1];
                    Data[o4 + 2] = LocalPos[n3 + 2] - RestPos[n3 + 2];
                    Data[o4 + 3] = 0.0;
                }
            }
        }

        return Data;
    }
}
