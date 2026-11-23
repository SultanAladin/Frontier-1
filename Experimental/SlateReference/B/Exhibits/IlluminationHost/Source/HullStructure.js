//============================================================================================================================================
//                                                             HULLSTRUCTURE.JS
//============================================================================================================================================
// 🧩 The car's collision hull from its own facets: points on the underside, the top and the flanks, where the body actually is.

// 📝 The body used to meet the ground and the obstacle boxes as seventeen spheres of 0.32–0.45 m — a rounded box with
//    gaps between the beads, so a crate could nose between two of them and the car rested on air above a kerb. Here
//    the hull is the body itself, sampled: vertical rays on a 0.3 × 0.26 m grid give the underside and the top surface
//    at each column, horizontal rays at four heights give the flanks, the nose and the tail. Points closer than
//    MergeSpacing collapse into one. Each point is a small sphere (PointRadius) the solver projects out of the course
//    and the boxes — and where one slides along the road or a wall it scrapes, and the sparks start there.

import { CourseMaterial } from "./MaterialSpecification.js";

const GridSpacing  = [0.30, 0.26];   // [m] vertical-ray grid along x and y
const FlankHeights = 4;              // [-] horizontal-ray heights between underside and roof
const MergeSpacing = 0.14;           // [m]
export const HullPointRadius = 0.035;   // [m]

// 📝 The outer shell only: cabin furniture (families from CabinLeather on) sits inside it, except the door mirrors.
function ClassifyShell(Facet)
{
    return Facet.Family < CourseMaterial.CabinLeather || Facet.Family === CourseMaterial.BodyCoat;
}

// 📐 Möller–Trumbore: travel along Direction to the facet, or −1.
function IntersectFacet(Origin, Direction, Facet)
{
    const [A, B, C] = Facet.Corners;
    const E1 = [B[0] - A[0], B[1] - A[1], B[2] - A[2]], E2 = [C[0] - A[0], C[1] - A[1], C[2] - A[2]];
    const P = [Direction[1] * E2[2] - Direction[2] * E2[1], Direction[2] * E2[0] - Direction[0] * E2[2], Direction[0] * E2[1] - Direction[1] * E2[0]];
    const Determinant = E1[0] * P[0] + E1[1] * P[1] + E1[2] * P[2];
    if (Math.abs(Determinant) < 1.0e-10)
        return -1.0;
    const Inverse = 1.0 / Determinant;
    const T = [Origin[0] - A[0], Origin[1] - A[1], Origin[2] - A[2]];
    const U = (T[0] * P[0] + T[1] * P[1] + T[2] * P[2]) * Inverse;
    if (U < 0.0 || U > 1.0)
        return -1.0;
    const Q = [T[1] * E1[2] - T[2] * E1[1], T[2] * E1[0] - T[0] * E1[2], T[0] * E1[1] - T[1] * E1[0]];
    const V = (Direction[0] * Q[0] + Direction[1] * Q[1] + Direction[2] * Q[2]) * Inverse;
    if (V < 0.0 || U + V > 1.0)
        return -1.0;
    const Travel = (E2[0] * Q[0] + E2[1] * Q[1] + E2[2] * Q[2]) * Inverse;
    return Travel > 0.0 ? Travel : -1.0;
}

function CastExtremes(Facets, Origin, Direction)
{
    let Near = Infinity, Far = -Infinity;
    for (const Facet of Facets)
    {
        const Travel = IntersectFacet(Origin, Direction, Facet);
        if (Travel > 0.0)
        {
            Near = Math.min(Near, Travel);
            Far = Math.max(Far, Travel);
        }
    }
    return Number.isFinite(Near) ? { Near, Far } : null;
}

// 📝 Facets in the car-body frame (the body set, cabin included); returns [x, y, z] points, body frame.
export function ConstructHullPoints(Facets)
{
    const Shell = Facets.filter(ClassifyShell);
    const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
    for (const Facet of Shell)
        for (const Corner of Facet.Corners)
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                Minimum[Axis] = Math.min(Minimum[Axis], Corner[Axis]);
                Maximum[Axis] = Math.max(Maximum[Axis], Corner[Axis]);
            }
    const Points = [];
    const Inset = 0.004;
    // ① underside and top: vertical rays from above
    for (let X = Minimum[0] + 0.05; X <= Maximum[0] - 0.05 + 1.0e-6; X += GridSpacing[0])
        for (let Y = Minimum[1] + 0.05; Y <= Maximum[1] - 0.05 + 1.0e-6; Y += GridSpacing[1])
        {
            const Origin = [X, Y, Maximum[2] + 1.0];
            const Hit = CastExtremes(Shell, Origin, [0.0, 0.0, -1.0]);
            if (!Hit)
                continue;
            Points.push([X, Y, Origin[2] - Hit.Near - Inset]);
            Points.push([X, Y, Origin[2] - Hit.Far + Inset]);
        }
    // ② flanks: horizontal rays across y at several heights, and along x for the nose and the tail
    for (let Level = 0; Level < FlankHeights; ++Level)
    {
        const Z = Minimum[2] + (Maximum[2] - Minimum[2]) * (0.08 + 0.84 * Level / (FlankHeights - 1));
        for (let X = Minimum[0] + 0.05; X <= Maximum[0] - 0.05 + 1.0e-6; X += GridSpacing[0])
        {
            const Origin = [X, Maximum[1] + 1.0, Z];
            const Hit = CastExtremes(Shell, Origin, [0.0, -1.0, 0.0]);
            if (!Hit)
                continue;
            Points.push([X, Origin[1] - Hit.Near - Inset, Z]);
            Points.push([X, Origin[1] - Hit.Far + Inset, Z]);
        }
        for (let Y = Minimum[1] + 0.1; Y <= Maximum[1] - 0.1 + 1.0e-6; Y += GridSpacing[1])
        {
            const Origin = [Maximum[0] + 1.0, Y, Z];
            const Hit = CastExtremes(Shell, Origin, [-1.0, 0.0, 0.0]);
            if (!Hit)
                continue;
            Points.push([Origin[0] - Hit.Near - Inset, Y, Z]);
            Points.push([Origin[0] - Hit.Far + Inset, Y, Z]);
        }
    }
    // ③ the shell's own vertices — creases and edges a grid can step over (the 0.1 m ridge ahead of the front arches) —
    //    lead the list, so in the merge an extreme vertex wins over the ray samples beside it
    const Vertices = [];
    for (const Facet of Shell)
        for (const Corner of Facet.Corners)
            Vertices.push(Corner.slice());
    // ④ merge near neighbours
    const Merged = [];
    for (const Point of [...Vertices, ...Points])
    {
        if (Merged.some((Other) => Math.hypot(Other[0] - Point[0], Other[1] - Point[1], Other[2] - Point[2]) < MergeSpacing))
            continue;
        Merged.push(Point);
    }
    return Merged;
}
