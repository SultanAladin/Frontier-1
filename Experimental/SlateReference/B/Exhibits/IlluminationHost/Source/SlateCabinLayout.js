// Ported from unassignedinbox/Slate@3c987f33c71c2477bd73ca29c69d5bd0ee484d1a
// Estimated model-space fixtures ported from Frontier arena/4f07c82a-frontier,
// Projects/Project-Drive/Source/DriveCabinLayout.h. +X forward, +Y driver/left, +Z up.
// No authored interior sockets exist; keep placement in one file, not in the renderer.
import { ResolveCoMAuthoredZ } from "./VehicleSpecification.js";
import { EmitBoxFacets } from "./CourseStructure.js";
import { CourseMaterial } from "./MaterialSpecification.js";
import { ConstructIdentityTransform } from "./TransformProjection.js";

export const CabinLayout = Object.freeze({
    Driver: [-0.20, 0.42, 1.12], Passenger: [-0.20, -0.42, 1.12], Centre: [-0.65, 0, 1.20],
    Wheel: [0.68, 0.42, 0.82], WheelTilt: 0.22, WheelRadius: 0.19,
    Tablet: [0.98, 0, 0.62], TabletTilt: 0.42, TabletWidth: 0.30, TabletHeight: 0.1875
});

export function ConstructCabinTransform(Centre, Tilt, Rotation = 0)
{
    const C = Math.cos(Rotation), S = Math.sin(Rotation), T = Math.sin(Tilt), U = Math.cos(Tilt);
    // Columns: screen right (−Y), screen up, screen toward viewer (−X).
    const M = ConstructIdentityTransform();
    M.set([S * T, -C, S * U, 0], 0);
    M.set([C * T, S, C * U, 0], 4);
    M.set([-U, 0, T, 0], 8);
    M.set([Centre[0], Centre[1], Centre[2] - ResolveCoMAuthoredZ(), 1], 12);
    return M;
}

export function ConstructCabinFacets()
{
    const Result = [];
    const Box = (Centre, Size, Family = CourseMaterial.VehiclePlastic) => {
        for (const F of EmitBoxFacets(...Size, Family))
            Result.push({ Family, Corners: F.Corners.map(P => [P[0] + Centre[0], P[1] + Centre[1],
                P[2] + Centre[2] - ResolveCoMAuthoredZ()]) });
    };
    Box([1.24, 0, 0.52], [0.20, 1.98, 0.17]); // dash top below the cowl, not across the windscreen
    Box([0.30, 0, 0.05], [1.28, 0.28, 0.30]); // centre tunnel
    Box([-0.50, 0.50, 0.03], [0.65, 0.49, 0.15]);
    Box([-0.50, -0.50, 0.03], [0.65, 0.49, 0.15]);
    Box([-0.83, 0.50, 0.16], [0.14, 0.52, 0.69]);
    Box([-0.83, -0.50, 0.16], [0.14, 0.52, 0.69]);
    Box([0.0, 0, -0.05], [2.1, 1.9, 0.07]);
    return Result;
}
