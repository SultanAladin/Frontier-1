// SURFACESCATTER.JS
// 🧩 Scatters particles over the ControlVehicle's surface (area-weighted, seeded) and measures its side and plan
//    silhouette along its length (the Profile texture the airflow and streak looks steer around).
// 📝 Car axes (VehicleAssembly): +X forward, +Y up, Z across. Positions are in the car Root's space.

import { THREE, Family } from '../Vehicle/VehicleFacets.js';

/** Small seeded generator (mulberry32) so captures are repeatable. */
export function Seeded(Seed = 1)
{
    let State = Seed >>> 0;
    return () =>
    {
        State = (State + 0x6d2b79f5) >>> 0;
        let Mixed = Math.imul(State ^ (State >>> 15), 1 | State);
        Mixed = (Mixed + Math.imul(Mixed ^ (Mixed >>> 7), 61 | Mixed)) ^ Mixed;
        return ((Mixed ^ (Mixed >>> 14)) >>> 0) / 4294967296;
    };
}

// Share of particles per surface family: paint and glass carry the shape; rubber is dark and mostly hidden.
const FamilyWeight = { [Family.BodyCoat]: 1, [Family.Glass]: 0.9, [Family.Plastic]: 0.7, [Family.Tyre]: 0.35, [Family.Hub]: 0.8,
    [Family.Brake]: 0.4, [Family.Trim]: 1, [Family.Headlight]: 2.5, [Family.Taillight]: 2.5, [Family.Default]: 0.6 };

/**
 * Collects every triangle of the car in Root space with its family, then draws Count area-weighted points.
 * @returns {{ Positions: Float32Array, Normals: Float32Array, Families: Float32Array, Min: THREE.Vector3, Max: THREE.Vector3 }}
 */
export function ScatterSurface(Vehicle, Count, Seed = 7)
{
    const Random = Seeded(Seed), FamilyOf = new Map(Object.entries(Vehicle.Finish).map(([Key, Material]) => [Material, +Key]));
    Vehicle.Root.updateMatrixWorld(true);
    const Inverse = new THREE.Matrix4().copy(Vehicle.Root.matrixWorld).invert();
    const Corners = [], Tags = [], Areas = [];
    const A = new THREE.Vector3(), B = new THREE.Vector3(), C = new THREE.Vector3(), Edge1 = new THREE.Vector3(), Edge2 = new THREE.Vector3();
    Vehicle.Root.traverse((Mesh) =>
    {
        if (!Mesh.isMesh) return;
        const Geometry = Mesh.geometry, Spots = Geometry.attributes.position, Index = Geometry.index;
        const Matrix = new THREE.Matrix4().multiplyMatrices(Inverse, Mesh.matrixWorld);
        const Materials = Array.isArray(Mesh.material) ? Mesh.material : [Mesh.material];
        const Groups = Geometry.groups.length ? Geometry.groups : [{ start: 0, count: Index ? Index.count : Spots.count, materialIndex: 0 }];
        for (const Group of Groups)
        {
            const Tag = FamilyOf.get(Materials[Group.materialIndex]) ?? Family.Default, Weight = FamilyWeight[Tag] ?? 0.6;
            for (let Corner = Group.start; Corner < Group.start + Group.count; Corner += 3)
            {
                const [I0, I1, I2] = Index ? [Index.getX(Corner), Index.getX(Corner + 1), Index.getX(Corner + 2)] : [Corner, Corner + 1, Corner + 2];
                A.fromBufferAttribute(Spots, I0).applyMatrix4(Matrix);
                B.fromBufferAttribute(Spots, I1).applyMatrix4(Matrix);
                C.fromBufferAttribute(Spots, I2).applyMatrix4(Matrix);
                const Area = Edge1.subVectors(B, A).cross(Edge2.subVectors(C, A)).length() * 0.5;
                if (Area < 1e-9) continue;
                Corners.push(A.x, A.y, A.z, B.x, B.y, B.z, C.x, C.y, C.z);
                Tags.push(Tag);
                Areas.push(Area * Weight);
            }
        }
    });
    const Running = new Float64Array(Areas.length);
    let Total = 0;
    Areas.forEach((Area, Slot) => { Total += Area; Running[Slot] = Total; });

    const Positions = new Float32Array(Count * 3), Normals = new Float32Array(Count * 3), Families = new Float32Array(Count);
    const Min = new THREE.Vector3(Infinity, Infinity, Infinity), Max = new THREE.Vector3(-Infinity, -Infinity, -Infinity);
    for (let Point = 0; Point < Count; Point++)
    {
        const Pick = Random() * Total;
        let Low = 0, High = Running.length - 1;
        while (Low < High) { const Middle = (Low + High) >> 1; if (Running[Middle] < Pick) Low = Middle + 1; else High = Middle; }
        const Base = Low * 9;
        let U = Random(), V = Random();
        if (U + V > 1) { U = 1 - U; V = 1 - V; }
        A.fromArray(Corners, Base); B.fromArray(Corners, Base + 3); C.fromArray(Corners, Base + 6);
        Edge1.subVectors(B, A); Edge2.subVectors(C, A);
        const Spot = A.clone().addScaledVector(Edge1, U).addScaledVector(Edge2, V);
        const Facing = new THREE.Vector3().crossVectors(Edge1, Edge2).normalize();
        Spot.toArray(Positions, Point * 3); Facing.toArray(Normals, Point * 3);
        Families[Point] = Tags[Low];
        Min.min(Spot); Max.max(Spot);
    }
    return { Positions, Normals, Families, Min, Max };
}

/**
 * The car's silhouette along X as a 1-D float texture (256 texels from Min.x − 3 m to Max.x + 7 m):
 * R = roof height at x, G = half-width at x, B and A = the same "felt" by passing air: they rise 0.6 m ahead of the
 * nose and decay over 2 m behind the tail (the wake), so streamlines lift before the car and settle after it.
 */
export function SilhouetteProfile(Scatter)
{
    const Texels = 256, From = Scatter.Min.x - 3, Span = Scatter.Max.x + 7 - From;
    const Top = new Float32Array(Texels), Half = new Float32Array(Texels);
    for (let Point = 0; Point < Scatter.Families.length; Point++)
    {
        const X = Scatter.Positions[Point * 3], Y = Scatter.Positions[Point * 3 + 1], Z = Math.abs(Scatter.Positions[Point * 3 + 2]);
        const Slot = Math.min(Texels - 1, Math.max(0, Math.floor((X - From) / Span * Texels)));
        Top[Slot] = Math.max(Top[Slot], Y); Half[Slot] = Math.max(Half[Slot], Z);
    }
    const Pixels = new Float32Array(Texels * 4), Step = Span / Texels;
    for (let Slot = 0; Slot < Texels; Slot++)
    {
        let FeltTop = 0, FeltHalf = 0;
        for (let Other = 0; Other < Texels; Other++)
        {
            const Gap = (Slot - Other) * Step;                         // > 0: the car part is behind this point (we are ahead of it)
            const Reach = Math.exp(-Math.abs(Gap) / (Gap > 0 ? 0.6 : 2.0));
            FeltTop = Math.max(FeltTop, Top[Other] * Reach); FeltHalf = Math.max(FeltHalf, Half[Other] * Reach);
        }
        Pixels.set([Top[Slot], Half[Slot], FeltTop, FeltHalf], Slot * 4);
    }
    const Texture = new THREE.DataTexture(Pixels, Texels, 1, THREE.RGBAFormat, THREE.FloatType);
    Texture.magFilter = THREE.NearestFilter; Texture.minFilter = THREE.NearestFilter;   // float linear filtering is optional in WebGL2 Texture.needsUpdate = true;
    return { Texture, From, Span };
}
