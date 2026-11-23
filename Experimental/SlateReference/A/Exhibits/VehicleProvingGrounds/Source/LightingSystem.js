//============================================================================================================================================
//                                                            LIGHTINGSYSTEM.JS
//============================================================================================================================================
// 🧩 World-space clustered lighting with analytic beam profiles (roadway batwing, low beam, high beam, fog, floodlight)
//    and real-time coupling to surface cards for multi-bounce indirect GI. The profiles are hand-tuned curves shaped
//    like their real counterparts, not measured photometric data.

export const LightType = Object.freeze({
    Omni: 0,
    StreetLamp: 1,    // roadway batwing distribution
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
// Params: vec4f [innerCos, outerCos, iesParam, active]
export const LightRecordFloats = 16;
export const LightBufferBytes = MaxSceneLights * LightRecordFloats * 4; // 2048 bytes

// 16x16x4 spatial grid over the proving grounds [-80..180] in X, [-80..80] in Y, [-2..28] in Z.
// Long-reach beams overlap most of it, so the per-cell cap leaves headroom above the pole count of any one cell.
export const ClusterGridBounds = Object.freeze({
    Minimum: [-80.0, -80.0, -2.0],
    Extent:  [260.0, 160.0, 30.0],
    ResX: 16,
    ResY: 16,
    ResZ: 4,
    CellCount: 1024,
    MaxLightsPerCell: 16
});

// 📝 Cluster assignment buffers (AssignClusters writes both every frame; the header carries the grid back to the
//    shaders, so WGSL never duplicates these numbers): cells hold (3 + CellCount) vec4u — resolution and cap,
//    bitcast minimum, bitcast inverse extent, then one (offset, count) pair per cell — and indices hold one fixed
//    slice of MaxLightsPerCell light slots per cell.
export const ClusterHeaderCells = 3;
export const ClusterCellBytes = (ClusterHeaderCells + ClusterGridBounds.CellCount) * 16;
export const ClusterIndexBytes = ClusterGridBounds.CellCount * ClusterGridBounds.MaxLightsPerCell * 4;

export class LightingSystem {
    constructor() {
        this.Lights = [];
        this.VehicleLightMode = AutomotiveMode.LowBeam;
        this.RoofFixture = RoofFixtureType.SlimLED;
        this.RoofLightEnabled = false;
        this.BufferData = new Float32Array(MaxSceneLights * LightRecordFloats);
        this.ClusterCells = new Uint32Array((ClusterHeaderCells + ClusterGridBounds.CellCount) * 4);
        this.ClusterCellFloats = new Float32Array(this.ClusterCells.buffer);
        this.ClusterIndices = new Uint32Array(ClusterGridBounds.CellCount * ClusterGridBounds.MaxLightsPerCell);
        this.ClusterOverflow = 0;
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

        this.AssignClusters();
        return this.BufferData;
    }

    // 📝 Bins every packed light into the world-space cluster grid. A light overlaps each cell its reach sphere's
    //    box touches — a conservative over-approximation for beams, exact for omnis. Slices are fixed-stride (cell C
    //    owns indices [C·cap, C·cap+cap)); a cell past its cap keeps its first lights (poles pack before vehicle
    //    beams) and counts the rest in ClusterOverflow instead of failing.
    AssignClusters()
    {
        const Grid = ClusterGridBounds;
        const ResX = Grid.ResX, ResY = Grid.ResY, ResZ = Grid.ResZ;
        const Cap = Grid.MaxLightsPerCell;
        const Min = Grid.Minimum;
        const InvX = 1.0 / Grid.Extent[0], InvY = 1.0 / Grid.Extent[1], InvZ = 1.0 / Grid.Extent[2];
        const Cells = this.ClusterCells, Floats = this.ClusterCellFloats, Indices = this.ClusterIndices;
        Cells[0] = ResX; Cells[1] = ResY; Cells[2] = ResZ; Cells[3] = Cap;
        Floats[4] = Min[0]; Floats[5] = Min[1]; Floats[6] = Min[2]; Floats[7] = 0.0;
        Floats[8] = InvX; Floats[9] = InvY; Floats[10] = InvZ; Floats[11] = 0.0;
        Cells.fill(0, ClusterHeaderCells * 4);
        this.ClusterOverflow = 0;
        const ClampAxis = (Centre, Reach, Origin, Inverse, Res) =>
        {
            const Low = Math.floor((Centre - Reach - Origin) * Inverse * Res);
            const High = Math.floor((Centre + Reach - Origin) * Inverse * Res);
            return [Math.max(0, Low), Math.min(Res - 1, High)];
        };
        for (let Light = 0; Light < this.Lights.length; ++Light)
        {
            const Record = this.Lights[Light];
            if (Record.Reach <= 0.0)
                continue;
            const [MinX, MaxX] = ClampAxis(Record.Position[0], Record.Reach, Min[0], InvX, ResX);
            const [MinY, MaxY] = ClampAxis(Record.Position[1], Record.Reach, Min[1], InvY, ResY);
            const [MinZ, MaxZ] = ClampAxis(Record.Position[2], Record.Reach, Min[2], InvZ, ResZ);
            if (MinX > MaxX || MinY > MaxY || MinZ > MaxZ)
                continue;
            for (let Cz = MinZ; Cz <= MaxZ; ++Cz)
                for (let Cy = MinY; Cy <= MaxY; ++Cy)
                    for (let Cx = MinX; Cx <= MaxX; ++Cx)
                    {
                        const Cell = (Cz * ResY + Cy) * ResX + Cx;
                        const Base = (ClusterHeaderCells + Cell) * 4;
                        const Count = Cells[Base + 1];
                        if (Count >= Cap)
                        {
                            this.ClusterOverflow++;
                            continue;
                        }
                        if (Count === 0)
                            Cells[Base] = Cell * Cap;
                        Indices[Cell * Cap + Count] = Light;
                        Cells[Base + 1] = Count + 1;
                    }
        }
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
