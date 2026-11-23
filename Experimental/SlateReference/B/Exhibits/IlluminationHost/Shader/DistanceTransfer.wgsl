//============================================================================================================================================
//                                                          DISTANCETRANSFER.WGSL
//============================================================================================================================================
// 🧩 Transfers one facet set into its signed distance volume — closest-facet distance, sign from back-facing ray hits.

// 📝 The per-occupant volume is the analogue of a mesh distance field: built once, in the facet set's own frame, and
//    only ever SAMPLED by the union pass that assembles the one scene distance field. No tracer reads it directly.
//
//    ① unsigned distance: closest point on any facet, by a pruned traversal of the bounding structure;
//    ② sign: RayCount rays in a spherical Fibonacci set, randomly rotated per voxel; the voxel is inside when more
//       than a quarter of all rays first strike a back face. That is the heuristic that survives open and
//       non-manifold input — the ControlVehicle body has 124 open and 76 non-manifold edges — where parity cannot.

struct TransferRecord
{
    VolumeMinimum : vec3f,   // [m]     local corner of texel (0,0,0)
    VoxelExtent   : f32,     // [m]
    TexelOrigin   : vec3u,   // [texel] placement inside the volume space
    NodeCount     : u32,     // [-]
    TexelExtent   : vec3u,   // [texel] resolution
    FacetCount    : u32,     // [-]
    SlabOrigin    : u32,     // [texel] first z slice of this submission
    SlabExtent    : u32,     // [texel] z slices in this submission
    RayCount      : u32,     // [-]
    Pad           : u32,
}

struct BoundingNode
{
    Minimum     : vec3f,
    LeftOrFirst : u32,
    Maximum     : vec3f,
    Count       : u32,
}

@group(0) @binding(0) var<uniform> Transfer           : TransferRecord;
@group(0) @binding(1) var<storage, read> Nodes        : array<BoundingNode>;
@group(0) @binding(2) var<storage, read> FacetCorners : array<vec4f>;
@group(0) @binding(3) var VolumeSpace                 : texture_storage_3d<rgba16float, write>;

const TraversalDepth : u32 = 40u;

// 📐 Closest point on a triangle by Voronoi-region classification (Ericson, Real-Time Collision Detection §5.1.5).
fn ProjectClosestOnFacet(Point: vec3f, A: vec3f, B: vec3f, C: vec3f) -> vec3f
{
    let AB = B - A;
    let AC = C - A;
    let AP = Point - A;
    let D1 = dot(AB, AP);
    let D2 = dot(AC, AP);
    if (D1 <= 0.0 && D2 <= 0.0) { return A; }
    let BP = Point - B;
    let D3 = dot(AB, BP);
    let D4 = dot(AC, BP);
    if (D3 >= 0.0 && D4 <= D3) { return B; }
    let VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0 && D1 >= 0.0 && D3 <= 0.0) { return A + AB * (D1 / (D1 - D3)); }
    let CP = Point - C;
    let D5 = dot(AB, CP);
    let D6 = dot(AC, CP);
    if (D6 >= 0.0 && D5 <= D6) { return C; }
    let VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0 && D2 >= 0.0 && D6 <= 0.0) { return A + AC * (D2 / (D2 - D6)); }
    let VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
    {
        return B + (C - B) * ((D4 - D3) / ((D4 - D3) + (D5 - D6)));
    }
    let Reciprocal = 1.0 / (VA + VB + VC);
    return A + AB * (VB * Reciprocal) + AC * (VC * Reciprocal);
}

fn ResolveBoundsDistanceSquared(Point: vec3f, Minimum: vec3f, Maximum: vec3f) -> f32
{
    let Outside = max(max(Minimum - Point, Point - Maximum), vec3f(0.0));
    return dot(Outside, Outside);
}

// 📐 Slab test; returns the entry distance or a large value when the ray misses the box before Limit.
fn IntersectBounds(Origin: vec3f, Reciprocal: vec3f, Minimum: vec3f, Maximum: vec3f, Limit: f32) -> f32
{
    let Near = (Minimum - Origin) * Reciprocal;
    let Far  = (Maximum - Origin) * Reciprocal;
    let Entry = max(max(min(Near.x, Far.x), min(Near.y, Far.y)), max(min(Near.z, Far.z), 0.0));
    let Exit  = min(min(max(Near.x, Far.x), max(Near.y, Far.y)), min(max(Near.z, Far.z), Limit));
    return select(1.0e30, Entry, Entry <= Exit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 CLOSEST-FACET QUERY
//------------------------------------------------------------------------------------------------------------------------

fn ResolveClosestDistanceSquared(Point: vec3f) -> f32
{
    var Best = 1.0e30;
    var Stack: array<u32, 40>;
    var Depth = 1u;
    Stack[0] = 0u;
    while (Depth > 0u)
    {
        Depth--;
        let Node = Nodes[Stack[Depth]];
        if (ResolveBoundsDistanceSquared(Point, Node.Minimum, Node.Maximum) >= Best)
        {
            continue;
        }
        if (Node.Count > 0u)
        {
            for (var Facet = Node.LeftOrFirst; Facet < Node.LeftOrFirst + Node.Count; Facet++)
            {
                let A = FacetCorners[Facet * 3u].xyz;
                let B = FacetCorners[Facet * 3u + 1u].xyz;
                let C = FacetCorners[Facet * 3u + 2u].xyz;
                let Offset = Point - ProjectClosestOnFacet(Point, A, B, C);
                Best = min(Best, dot(Offset, Offset));
            }
            continue;
        }
        let Left  = Nodes[Node.LeftOrFirst];
        let Right = Nodes[Node.LeftOrFirst + 1u];
        let LeftDistance  = ResolveBoundsDistanceSquared(Point, Left.Minimum, Left.Maximum);
        let RightDistance = ResolveBoundsDistanceSquared(Point, Right.Minimum, Right.Maximum);
        if (Depth + 2u > TraversalDepth)
        {
            continue;
        }
        // 📝 The nearer child is pushed last so it is visited first and tightens Best before the farther one.
        if (LeftDistance < RightDistance)
        {
            Stack[Depth] = Node.LeftOrFirst + 1u;
            Stack[Depth + 1u] = Node.LeftOrFirst;
        }
        else
        {
            Stack[Depth] = Node.LeftOrFirst;
            Stack[Depth + 1u] = Node.LeftOrFirst + 1u;
        }
        Depth += 2u;
    }
    return Best;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                FIRST-HIT FACING QUERY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Returns +1 when the first hit is a front face, −1 for a back face, 0 for a miss.
fn ResolveFirstHitFacing(Origin: vec3f, Direction: vec3f) -> f32
{
    let Reciprocal = 1.0 / select(Direction, vec3f(1.0e-9), abs(Direction) < vec3f(1.0e-9));
    var Nearest = 1.0e30;
    var Facing  = 0.0;
    var Stack: array<u32, 40>;
    var Depth = 1u;
    Stack[0] = 0u;
    while (Depth > 0u)
    {
        Depth--;
        let Node = Nodes[Stack[Depth]];
        if (IntersectBounds(Origin, Reciprocal, Node.Minimum, Node.Maximum, Nearest) >= Nearest)
        {
            continue;
        }
        if (Node.Count > 0u)
        {
            for (var Facet = Node.LeftOrFirst; Facet < Node.LeftOrFirst + Node.Count; Facet++)
            {
                let A  = FacetCorners[Facet * 3u].xyz;
                let E1 = FacetCorners[Facet * 3u + 1u].xyz - A;
                let E2 = FacetCorners[Facet * 3u + 2u].xyz - A;
                // 📐 Möller–Trumbore, two-sided; det = −dot(direction, e₁×e₂), positive for a front-face hit.
                let P = cross(Direction, E2);
                let Determinant = dot(E1, P);
                if (abs(Determinant) < 1.0e-12)
                {
                    continue;
                }
                let Inverse = 1.0 / Determinant;
                let T = Origin - A;
                let U = dot(T, P) * Inverse;
                if (U < 0.0 || U > 1.0)
                {
                    continue;
                }
                let Q = cross(T, E1);
                let V = dot(Direction, Q) * Inverse;
                if (V < 0.0 || U + V > 1.0)
                {
                    continue;
                }
                let Travel = dot(E2, Q) * Inverse;
                if (Travel > 1.0e-5 && Travel < Nearest)
                {
                    Nearest = Travel;
                    Facing  = select(-1.0, 1.0, Determinant > 0.0);
                }
            }
            continue;
        }
        if (Depth + 2u > TraversalDepth)
        {
            continue;
        }
        let LeftEntry  = IntersectBounds(Origin, Reciprocal, Nodes[Node.LeftOrFirst].Minimum, Nodes[Node.LeftOrFirst].Maximum, Nearest);
        let RightEntry = IntersectBounds(Origin, Reciprocal, Nodes[Node.LeftOrFirst + 1u].Minimum, Nodes[Node.LeftOrFirst + 1u].Maximum, Nearest);
        if (LeftEntry < RightEntry)
        {
            Stack[Depth] = Node.LeftOrFirst + 1u;
            Stack[Depth + 1u] = Node.LeftOrFirst;
        }
        else
        {
            Stack[Depth] = Node.LeftOrFirst;
            Stack[Depth + 1u] = Node.LeftOrFirst + 1u;
        }
        Depth += 2u;
    }
    return Facing;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSFER ENTRY
//------------------------------------------------------------------------------------------------------------------------

fn PermuteTransfer(Input: u32) -> u32
{
    let Advanced = Input * 747796405u + 2891336453u;
    let Word = ((Advanced >> ((Advanced >> 28u) + 4u)) ^ Advanced) * 277803737u;
    return (Word >> 22u) ^ Word;
}

// 📐 Spherical Fibonacci point i of n: z = 1 − (2i + 1)/n, φ = 2π·i·(1 − 1/golden), uniform over the sphere.
fn ProjectSphericalFibonacci(Index: u32, Count: u32) -> vec3f
{
    let z = 1.0 - (2.0 * f32(Index) + 1.0) / f32(Count);
    let Radius = sqrt(max(0.0, 1.0 - z * z));
    let φ = 2.399963229728653 * f32(Index);
    return vec3f(Radius * cos(φ), Radius * sin(φ), z);
}

@compute @workgroup_size(4, 4, 4)
fn TransferDistance(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Voxel = vec3u(Invocation.x, Invocation.y, Invocation.z + Transfer.SlabOrigin);
    if (Voxel.x >= Transfer.TexelExtent.x || Voxel.y >= Transfer.TexelExtent.y || Voxel.z >= Transfer.TexelExtent.z
        || Invocation.z >= Transfer.SlabExtent)
    {
        return;
    }
    let Point = Transfer.VolumeMinimum + (vec3f(Voxel) + 0.5) * Transfer.VoxelExtent;
    let Unsigned = sqrt(ResolveClosestDistanceSquared(Point));

    // 📝 A random rotation per voxel about a random axis keeps the ray set from imprinting a lattice on the sign.
    var Seed = PermuteTransfer((Voxel.x * 73856093u) ^ (Voxel.y * 19349663u) ^ (Voxel.z * 83492791u));
    Seed = PermuteTransfer(Seed);
    let Angle = f32(Seed >> 8u) * (6.283185307 / 16777216.0);
    Seed = PermuteTransfer(Seed);
    let AxisZ = f32(Seed >> 8u) * (2.0 / 16777216.0) - 1.0;
    Seed = PermuteTransfer(Seed);
    let AxisAngle = f32(Seed >> 8u) * (6.283185307 / 16777216.0);
    let AxisRadius = sqrt(max(0.0, 1.0 - AxisZ * AxisZ));
    let Axis = vec3f(AxisRadius * cos(AxisAngle), AxisRadius * sin(AxisAngle), AxisZ);
    let Cosine = cos(Angle);
    let Sine = sin(Angle);

    var BackHits = 0u;
    for (var Ray = 0u; Ray < Transfer.RayCount; Ray++)
    {
        let Fibonacci = ProjectSphericalFibonacci(Ray, Transfer.RayCount);
        // 📐 Rodrigues rotation of the Fibonacci direction about Axis by Angle.
        let Direction = Fibonacci * Cosine + cross(Axis, Fibonacci) * Sine + Axis * dot(Axis, Fibonacci) * (1.0 - Cosine);
        if (ResolveFirstHitFacing(Point, Direction) < 0.0)
        {
            BackHits++;
        }
    }
    let Inside = f32(BackHits) > 0.25 * f32(Transfer.RayCount);
    let Signed = select(Unsigned, -Unsigned, Inside);
    textureStore(VolumeSpace, vec3i(Transfer.TexelOrigin + Voxel), vec4f(Signed, 0.0, 0.0, 1.0));
}
