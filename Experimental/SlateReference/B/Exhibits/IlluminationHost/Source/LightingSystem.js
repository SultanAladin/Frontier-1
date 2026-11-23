//============================================================================================================================================
//                                                            LIGHTINGSYSTEM.JS
//============================================================================================================================================
// 🧩 World-space light records (beam profiles: roadway batwing, low beam, high beam, fog, floodlight) that feed the
//    light grid (LightClusterSpace.js) and the surface cards for multi-bounce indirect GI.

export const LightType = Object.freeze({
    Omni: 0,
    StreetLamp: 1,    // roadway batwing (Type II / Type III spread)
    LowBeam: 2,       // Automotive asymmetric low beam cutoff
    HighBeam: 3,      // Automotive collimated pencil beam
    Fog: 4,           // Low-mounted razor-sharp horizontal cutoff
    Floodlight: 5     // Broad multi-lens flood / roof bar
});

export const RoofFixtureType = Object.freeze({
    SlimLED: 0,
    RallyPod: 1
});

export const AutomotiveMode = Object.freeze({
    Off: 0,
    LowBeam: 1,
    HighBeam: 2,
    Fog: 3
});

export const MaxSceneLights = 32;
// 16 floats per light = 64 bytes
// PositionType: vec4f [px, py, pz, type]
// DirectionReach: vec4f [dx, dy, dz, reach]
// ColourIntensity: vec4f [r, g, b, intensity]
// Params: vec4f [innerCos, outerCos, profileVariant, active]
export const LightRecordFloats = 16;

export class LightingSystem {
    constructor() {
        this.Lights = [];
        this.VehicleLightMode = AutomotiveMode.LowBeam;
        this.RoofFixture = RoofFixtureType.SlimLED;
        this.RoofLightEnabled = false;
        this.BufferData = new Float32Array(MaxSceneLights * LightRecordFloats);
    }

    SetVehicleMode(Mode) {
        this.VehicleLightMode = Mode;
    }

    CycleVehicleMode() {
        this.VehicleLightMode = (this.VehicleLightMode + 1) % 4;
        return this.VehicleLightMode;
    }

    ToggleRoofLight() {
        this.RoofLightEnabled = !this.RoofLightEnabled;
        return this.RoofLightEnabled;
    }

    SetRoofFixture(Type) {
        this.RoofFixture = Type;
    }

    Update(VehicleTransform, Poles = []) {
        this.Lights = [];

        // 1. Street / Pole Lamps from the scene level
        // Street lamp heads are mounted at height ~10.8m, pointing down (-Z)
        for (const Pole of Poles) {
            if (this.Lights.length >= MaxSceneLights - 4) break;
            const Pos = [Pole.X, Pole.Y, Pole.Z || 10.8];
            this.Lights.push({
                Type: LightType.StreetLamp,
                Position: Pos,
                Direction: [0.0, 0.0, -1.0],
                Colour: [1.0, 0.88, 0.65], // Warm high-pressure sodium / warm LED 3000K
                Intensity: 45.0,
                Reach: 32.0,
                Params: [0.70, 0.20, 1.0, 1.0]
            });
        }

        // 2. Automotive Headlights attached to the ControlVehicle
        if (VehicleTransform) {
            // Forward is +X, Left is +Y, Up is +Z in vehicle local space
            const M = VehicleTransform;
            const Fwd = [M[0], M[1], M[2]];
            const Left = [M[4], M[5], M[6]];
            const Up = [M[8], M[9], M[10]];
            const Pos = [M[12], M[13], M[14]];

            const TransformLocal = (Offset) => [
                Pos[0] + Fwd[0] * Offset[0] + Left[0] * Offset[1] + Up[0] * Offset[2],
                Pos[1] + Fwd[1] * Offset[0] + Left[1] * Offset[1] + Up[1] * Offset[2],
                Pos[2] + Fwd[2] * Offset[0] + Left[2] * Offset[1] + Up[2] * Offset[2]
            ];

            // Headlight housings on front bumper: X ≈ 2.2m, Y ≈ ±0.65m, Z ≈ 0.20m
            const HeadlightL = TransformLocal([2.2, 0.65, 0.20]);
            const HeadlightR = TransformLocal([2.2, -0.65, 0.20]);

            if (this.VehicleLightMode === AutomotiveMode.LowBeam) {
                // Low Beam (asymmetric cutoff, ~55m reach)
                const LowBeam = (P) => ({
                    Type: LightType.LowBeam,
                    Position: P,
                    Direction: Fwd,
                    Colour: [0.92, 0.96, 1.0], // 6000K crisp white LED
                    Intensity: 35.0,
                    Reach: 60.0,
                    Params: [0.90, 0.60, 0.0, 1.0]
                });
                this.Lights.push(LowBeam(HeadlightL), LowBeam(HeadlightR));
            } else if (this.VehicleLightMode === AutomotiveMode.HighBeam) {
                // High Beam (long-range collimated beam, ~120m reach)
                const HighBeam = (P) => ({
                    Type: LightType.HighBeam,
                    Position: P,
                    Direction: Fwd,
                    Colour: [0.95, 0.98, 1.0],
                    Intensity: 75.0,
                    Reach: 130.0,
                    Params: [0.98, 0.85, 0.0, 1.0]
                });
                this.Lights.push(HighBeam(HeadlightL), HighBeam(HeadlightR));
            } else if (this.VehicleLightMode === AutomotiveMode.Fog) {
                // Fog Light (wide horizontal cut, low mount: Z ≈ 0.05m)
                const FogL = TransformLocal([2.15, 0.70, 0.05]);
                const FogR = TransformLocal([2.15, -0.70, 0.05]);
                const FogBeam = (P) => ({
                    Type: LightType.Fog,
                    Position: P,
                    Direction: Fwd,
                    Colour: [1.0, 0.90, 0.50], // 3000K selective yellow fog
                    Intensity: 40.0,
                    Reach: 45.0,
                    Params: [0.85, 0.40, 0.0, 1.0]
                });
                this.Lights.push(FogBeam(FogL), FogBeam(FogR));
            }

            // 3. Roof Floodlight Component (Slim LED Lightbar or Rally Pod)
            if (this.RoofLightEnabled) {
                const RoofPos = TransformLocal([0.25, 0.0, 0.92]);
                if (this.RoofFixture === RoofFixtureType.SlimLED) {
                    // Slim multi-lens LED bar: wide forward flood
                    this.Lights.push({
                        Type: LightType.Floodlight,
                        Position: RoofPos,
                        Direction: Fwd,
                        Colour: [0.90, 0.95, 1.0], // 6500K bright LED bar
                        Intensity: 85.0,
                        Reach: 100.0,
                        Params: [Math.cos(30 * Math.PI / 180), Math.cos(45 * Math.PI / 180), 0.0, 1.0]
                    });
                } else {
                    // Dual heavy-duty rally pods: punchy penetrating flood
                    const PodL = TransformLocal([0.25, 0.28, 0.92]);
                    const PodR = TransformLocal([0.25, -0.28, 0.92]);
                    const RallyBeam = (P) => ({
                        Type: LightType.Floodlight,
                        Position: P,
                        Direction: Fwd,
                        Colour: [1.0, 0.98, 0.92],
                        Intensity: 90.0,
                        Reach: 120.0,
                        Params: [Math.cos(22 * Math.PI / 180), Math.cos(38 * Math.PI / 180), 1.0, 1.0]
                    });
                    this.Lights.push(RallyBeam(PodL), RallyBeam(PodR));
                }
            }
        }

        // Pack lights into buffer
        this.BufferData.fill(0);
        for (let i = 0; i < Math.min(this.Lights.length, MaxSceneLights); i++) {
            const L = this.Lights[i];
            const Offset = i * LightRecordFloats;
            this.BufferData[Offset + 0] = L.Position[0];
            this.BufferData[Offset + 1] = L.Position[1];
            this.BufferData[Offset + 2] = L.Position[2];
            this.BufferData[Offset + 3] = L.Type;

            this.BufferData[Offset + 4] = L.Direction[0];
            this.BufferData[Offset + 5] = L.Direction[1];
            this.BufferData[Offset + 6] = L.Direction[2];
            this.BufferData[Offset + 7] = L.Reach;

            this.BufferData[Offset + 8] = L.Colour[0];
            this.BufferData[Offset + 9] = L.Colour[1];
            this.BufferData[Offset + 10] = L.Colour[2];
            this.BufferData[Offset + 11] = L.Intensity;

            this.BufferData[Offset + 12] = L.Params[0];
            this.BufferData[Offset + 13] = L.Params[1];
            this.BufferData[Offset + 14] = L.Params[2];
            this.BufferData[Offset + 15] = L.Params[3];
        }

        return this.BufferData;
    }
}

// Construct polygonal facets for vehicle roof auxiliary fixtures
import { EmitBoxFacets } from "./CourseStructure.js";
import { CourseMaterial } from "./MaterialSpecification.js";
import { ResolveCoMAuthoredZ } from "./VehicleSpecification.js";

export function ConstructRoofFixtureFacets(Type) {
    const Result = [];
    const Rebase = ResolveCoMAuthoredZ();
    const Box = (Centre, Size, Family) => {
        for (const F of EmitBoxFacets(...Size, Family)) {
            Result.push({
                Family,
                Corners: F.Corners.map(P => [
                    P[0] + Centre[0],
                    P[1] + Centre[1],
                    P[2] + Centre[2] - Rebase
                ])
            });
        }
    };

    if (Type === RoofFixtureType.SlimLED) {
        // Slim low-profile multi-lens LED lightbar
        // Mounting brackets on roof edge
        Box([0.25, 0.44, 1.35], [0.10, 0.04, 0.05], CourseMaterial.SteelDark);
        Box([0.25, -0.44, 1.35], [0.10, 0.04, 0.05], CourseMaterial.SteelDark);
        // Black extruded aluminium housing
        Box([0.25, 0.0, 1.38], [0.12, 0.94, 0.07], CourseMaterial.VehiclePlastic);
        // Optical poly-lens emitter face
        Box([0.31, 0.0, 1.38], [0.02, 0.88, 0.05], CourseMaterial.Headlight);
    } else {
        // Dual heavy-duty rally auxiliary light pods
        for (const Y of [0.32, -0.32]) {
            // Steel stanchion mount
            Box([0.25, Y, 1.35], [0.12, 0.08, 0.05], CourseMaterial.SteelDark);
            // Deep cylindrical/box rally housing
            Box([0.25, Y, 1.40], [0.16, 0.20, 0.16], CourseMaterial.VehiclePlastic);
            // Large circular optic lens face
            Box([0.33, Y, 1.40], [0.03, 0.16, 0.14], CourseMaterial.Headlight);
        }
    }
    return Result;
}
