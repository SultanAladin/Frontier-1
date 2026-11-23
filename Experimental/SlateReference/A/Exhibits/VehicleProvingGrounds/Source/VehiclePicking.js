// CPU picking against the same vehicle facets and occupant poses as the Garage renderer.
import { InvertTransform, TransformPoint, TransformDirection, SubtractVector, CrossVector, DotVector, NormalizeVector } from './TransformProjection.js';
export function IntersectVehicleFacet(Origin, Direction, Corners) {
    const [A,B,C] = Corners, E1 = SubtractVector(B,A), E2 = SubtractVector(C,A);
    const P = CrossVector(Direction,E2), D = DotVector(E1,P);
    if (Math.abs(D) < 1e-9) return Infinity;
    const T = SubtractVector(Origin,A), U = DotVector(T,P) / D;
    if (U < 0 || U > 1) return Infinity;
    const Q = CrossVector(T,E1), V = DotVector(Direction,Q) / D;
    if (V < 0 || U+V > 1) return Infinity;
    const Distance = DotVector(E2,Q) / D;
    return Distance > 0 ? Distance : Infinity;
}
export function PickVehicle(Scene, InverseView, U, V) {
    if (!InverseView || U < 0 || U > 1 || V < 0 || V > 1) return null;
    const From = TransformPoint(InverseView, [U*2-1, 1-V*2, 0]);
    const To = TransformPoint(InverseView, [U*2-1, 1-V*2, 1]);
    const Ray = NormalizeVector(SubtractVector(To,From));
    let Best = null;
    for (let Index = 0; Index < 5; ++Index) {
        const Occupant = Scene.Occupants[Index], Inverse = InvertTransform(Occupant.Transform);
        const Origin = TransformPoint(Inverse, From), Direction = TransformDirection(Inverse, Ray);
        for (const Facet of Scene.FacetSets[Occupant.FacetSet].Facets) {
            const Distance = IntersectVehicleFacet(Origin, Direction, Facet.Corners);
            if (Number.isFinite(Distance) && (!Best || Distance < Best.Distance)) Best = { Occupant: Index, Family: Facet.Family, Distance };
        }
    }
    return Best;
}
