//============================================================================================================================================
//                                                          FRAGMENTDISTANCE.WGSL
//============================================================================================================================================
// 🧩 A fragment's signed distance volume, computed on the device the frame it is born — one workgroup per fragment.

// 📝 A fragment is a closed convex solid, so its distance has a closed form on both sides: inside, the largest face
//    plane distance (exact for a convex solid); outside, the nearest of its triangles (exact). A 12³ volume
//    is 1728 voxels against at most 48 triangles — about eighty thousand point-triangle tests, a few microseconds,
//    so a break that throws forty pieces puts all forty into the scene distance field the same frame.

struct FragmentJob
{
    TexelOrigin : vec3u,   // [texel] the pool slot in the occupant distance space
    Counts      : u32,     // [-]     triangles | planes << 16
    Minimum     : vec3f,   // [m]     local corner of texel (0, 0, 0)
    VoxelExtent : f32,     // [m]
}

@group(0) @binding(0) var<storage, read> Jobs     : array<FragmentJob>;
@group(0) @binding(1) var<storage, read> Geometry : array<vec4f>;   // per job: MaxTriangles·3 corners, then MaxPlanes planes
@group(0) @binding(2) var Target                  : texture_storage_3d<rgba16float, write>;

const Edge         : u32 = 12u;
const MaxTriangles : u32 = 48u;
const MaxPlanes    : u32 = 40u;
const Stride       : u32 = MaxTriangles * 3u + MaxPlanes;

var<workgroup> Corners : array<vec3f, 144>;
var<workgroup> Planes  : array<vec4f, 40>;

// 📐 Ericson, Real-Time Collision Detection §5.1.5: the closest point of triangle ABC to P, by Voronoi region.
fn MeasureTriangleDistanceSquared(P: vec3f, A: vec3f, B: vec3f, C: vec3f) -> f32
{
    let AB = B - A;
    let AC = C - A;
    let AP = P - A;
    let D1 = dot(AB, AP);
    let D2 = dot(AC, AP);
    if (D1 <= 0.0 && D2 <= 0.0) { return dot(AP, AP); }
    let BP = P - B;
    let D3 = dot(AB, BP);
    let D4 = dot(AC, BP);
    if (D3 >= 0.0 && D4 <= D3) { return dot(BP, BP); }
    let VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0 && D1 >= 0.0 && D3 <= 0.0)
    {
        let V = D1 / (D1 - D3);
        let Q = P - (A + V * AB);
        return dot(Q, Q);
    }
    let CP = P - C;
    let D5 = dot(AB, CP);
    let D6 = dot(AC, CP);
    if (D6 >= 0.0 && D5 <= D6) { return dot(CP, CP); }
    let VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0 && D2 >= 0.0 && D6 <= 0.0)
    {
        let W = D2 / (D2 - D6);
        let Q = P - (A + W * AC);
        return dot(Q, Q);
    }
    let VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
    {
        let W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
        let Q = P - (B + W * (C - B));
        return dot(Q, Q);
    }
    let Denominator = 1.0 / (VA + VB + VC);
    let V = VB * Denominator;
    let W = VC * Denominator;
    let Q = P - (A + AB * V + AC * W);
    return dot(Q, Q);
}

@compute @workgroup_size(64)
fn ComputeFragmentDistance(@builtin(workgroup_id) Group: vec3u, @builtin(local_invocation_index) Lane: u32)
{
    let Job = Jobs[Group.x];
    let TriangleCount = min(Job.Counts & 0xFFFFu, MaxTriangles);
    let PlaneCount = min(Job.Counts >> 16u, MaxPlanes);
    let Base = Group.x * Stride;
    for (var Index = Lane; Index < TriangleCount * 3u; Index += 64u)
    {
        Corners[Index] = Geometry[Base + Index].xyz;
    }
    for (var Index = Lane; Index < PlaneCount; Index += 64u)
    {
        Planes[Index] = Geometry[Base + MaxTriangles * 3u + Index];
    }
    workgroupBarrier();

    for (var Voxel = Lane; Voxel < Edge * Edge * Edge; Voxel += 64u)
    {
        let Cell = vec3u(Voxel % Edge, (Voxel / Edge) % Edge, Voxel / (Edge * Edge));
        let Point = Job.Minimum + (vec3f(Cell) + 0.5) * Job.VoxelExtent;
        var Inside = -1.0e9;
        for (var Index = 0u; Index < PlaneCount; Index++)
        {
            Inside = max(Inside, dot(Planes[Index].xyz, Point) - Planes[Index].w);
        }
        var Signed = Inside;
        if (Inside > 0.0)
        {
            var Nearest = 1.0e18;
            for (var Triangle = 0u; Triangle < TriangleCount; Triangle++)
            {
                Nearest = min(Nearest, MeasureTriangleDistanceSquared(Point, Corners[Triangle * 3u], Corners[Triangle * 3u + 1u], Corners[Triangle * 3u + 2u]));
            }
            Signed = sqrt(Nearest);
        }
        textureStore(Target, vec3i(Job.TexelOrigin + Cell), vec4f(Signed, 0.0, 0.0, 1.0));
    }
}
