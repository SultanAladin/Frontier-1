//============================================================================================================================================
//                                                    TRIANGLEHIERARCHYTRAVERSAL.WGSL
//============================================================================================================================================
// 🧩 Closest-hit ray against the triangles: a two-level BVH (TLAS over the scene occupants, binned-SAH BLAS per facet set), software
//    traversal in compute. Used by the triangle-reflection path only; the global illumination stays on the scene distance field.

struct HierarchyNode
{
    Minimum     : vec3f,
    LeftOrFirst : u32,
    Maximum     : vec3f,
    Count       : u32,
}

// 📝 Group 3 is the reflection screen group (ScreenTraversal.wgsl, bindings 0 to 4); the hierarchy takes bindings 5 and 6.
@group(3) @binding(5) var<storage, read> BvhNodes     : array<HierarchyNode>;
@group(3) @binding(6) var<storage, read> BvhTriangles : array<vec4f>;

const TlasLeafBit        : u32 = 0x80000000u;
const TlasIndexMask      : u32 = 0x7FFFFFFFu;
const EmptyTlasSentinel  : u32 = 0x7FFFFFFFu;
const BvhStackCapacity   : u32 = 48u;
const MaxBvhSteps        : u32 = 384u;
const RayEpsilon         : f32 = 2.0e-3;

struct RayHit
{
    Position : vec3f,   // [m]   world-space intersection point
    Travel   : f32,     // [m]   ray parameter t
    Normal   : vec3f,   // [-]   interpolated smooth shading normal in world space
    Geometry : vec3f,   // [-]   geometric triangle normal in world space
    Identity : u32,     // [-]   Occupant index, GroundIdentity, or SkyIdentity on miss
    Family   : u32,     // [-]   exact triangle MaterialRecord index
    Steps    : u32,     // [-]   BVH traversal step count
    Hit      : bool,
}

fn SafeReciprocal(Direction: vec3f) -> vec3f
{
    let Safe = select(Direction, vec3f(1.0e-9), abs(Direction) < vec3f(1.0e-9));
    return 1.0 / Safe;
}

fn IntersectNodeBounds(Origin: vec3f, Reciprocal: vec3f, Minimum: vec3f, Maximum: vec3f, Limit: f32) -> f32
{
    let Near  = (Minimum - Origin) * Reciprocal;
    let Far   = (Maximum - Origin) * Reciprocal;
    let Low   = min(Near, Far);
    let High  = max(Near, Far);
    let Entry = max(max(Low.x, Low.y), max(Low.z, 0.0));
    let Exit  = min(min(High.x, High.y), min(High.z, Limit));
    return select(1.0e30, Entry, Entry <= Exit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TLAS + BLAS CLOSEST-HIT TRAVERSAL
//------------------------------------------------------------------------------------------------------------------------

// 📐 Exact Two-Level BVH ray-triangle intersection (Möller–Trumbore 1997) with near-first child ordering.
fn TraceSceneRay(Origin: vec3f, Direction: vec3f, MinimumTravel: f32, MaximumTravel: f32) -> RayHit
{
    var Result : RayHit;
    Result.Position = Origin + Direction * MaximumTravel;
    Result.Travel   = MaximumTravel;
    Result.Normal   = -Direction;
    Result.Geometry = -Direction;
    Result.Identity = SkyIdentity;
    Result.Family   = 0u;
    Result.Steps    = 0u;
    Result.Hit      = false;

    if (BvhNodes[0].LeftOrFirst == EmptyTlasSentinel && BvhNodes[0].Count == 0u)
    {
        return Result;
    }

    let WorldRecip = SafeReciprocal(Direction);
    var Stack      : array<u32, BvhStackCapacity>;
    var Depth      = 1u;
    Stack[0]       = 0u;
    var BlasFloor  = 0xFFFFFFFFu;

    var Nearest    = MaximumTravel;
    var BestTri    = 0xFFFFFFFFu;
    var BestOcc    = 0xFFFFFFFFu;
    var BestUV     = vec2f(0.0);
    var Steps      = 0u;

    var CurrentOcc  = 0xFFFFFFFFu;
    var OccRoles    = 0u;
    var LocalOrigin = Origin;
    var LocalDir    = Direction;
    var LocalRecip  = WorldRecip;

    while (Depth > 0u && Steps < MaxBvhSteps)
    {
        if (Depth <= BlasFloor)
        {
            BlasFloor = 0xFFFFFFFFu;
        }
        Depth -= 1u;
        let NodeIdx = Stack[Depth];
        let Node    = BvhNodes[NodeIdx];
        Steps += 1u;

        if (BlasFloor == 0xFFFFFFFFu)
        {
            if (IntersectNodeBounds(Origin, WorldRecip, Node.Minimum, Node.Maximum, Nearest) >= Nearest)
            {
                continue;
            }
            if ((Node.Count & TlasLeafBit) != 0u)
            {
                CurrentOcc  = Node.Count & TlasIndexMask;
                let Occ     = Occupants[CurrentOcc];
                OccRoles    = Occ.Roles;
                LocalOrigin = (Occ.WorldToLocal * vec4f(Origin, 1.0)).xyz;
                LocalDir    = (Occ.WorldToLocal * vec4f(Direction, 0.0)).xyz;
                LocalRecip  = SafeReciprocal(LocalDir);
                if (Depth < BvhStackCapacity)
                {
                    BlasFloor    = Depth;
                    Stack[Depth] = Node.LeftOrFirst;
                    Depth += 1u;
                }
            }
            else
            {
                let Left  = Node.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectNodeBounds(Origin, WorldRecip, BvhNodes[Left].Minimum, BvhNodes[Left].Maximum, Nearest);
                let RDist = IntersectNodeBounds(Origin, WorldRecip, BvhNodes[Right].Minimum, BvhNodes[Right].Maximum, Nearest);
                if (LDist < RDist)
                {
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
        else
        {
            if (IntersectNodeBounds(LocalOrigin, LocalRecip, Node.Minimum, Node.Maximum, Nearest) >= Nearest)
            {
                continue;
            }
            if (Node.Count > 0u)
            {
                let FirstTri = Node.LeftOrFirst;
                let TriEnd   = FirstTri + Node.Count;
                for (var Tri = FirstTri; Tri < TriEnd; Tri += 1u)
                {
                    let Base      = Tri * 4u;
                    let T0        = BvhTriangles[Base];
                    let RawFamily = bitcast<u32>(T0.w);
                    if (RawFamily == VehicleGlassFamily)
                    {
                        continue;
                    }
                    if (RawFamily == TyreFamily && (OccRoles & RoleShredded) != 0u)
                    {
                        continue;
                    }
                    let E1  = BvhTriangles[Base + 1u].xyz;
                    let E2  = BvhTriangles[Base + 2u].xyz;
                    let P   = cross(LocalDir, E2);
                    let Det = dot(E1, P);
                    if (abs(Det) < 1.0e-9)
                    {
                        continue;
                    }
                    let InvDet = 1.0 / Det;
                    let T      = LocalOrigin - T0.xyz;
                    let U      = dot(T, P) * InvDet;
                    if (U < 0.0 || U > 1.0)
                    {
                        continue;
                    }
                    let Q = cross(T, E1);
                    let V = dot(LocalDir, Q) * InvDet;
                    if (V < 0.0 || U + V > 1.0)
                    {
                        continue;
                    }
                    let Travel = dot(E2, Q) * InvDet;
                    if (Travel >= MinimumTravel && Travel < Nearest)
                    {
                        Nearest = Travel;
                        BestTri = Tri;
                        BestOcc = CurrentOcc;
                        BestUV  = vec2f(U, V);
                    }
                }
            }
            else
            {
                let Left  = Node.LeftOrFirst;
                let Right = Left + 1u;
                let LDist = IntersectNodeBounds(LocalOrigin, LocalRecip, BvhNodes[Left].Minimum, BvhNodes[Left].Maximum, Nearest);
                let RDist = IntersectNodeBounds(LocalOrigin, LocalRecip, BvhNodes[Right].Minimum, BvhNodes[Right].Maximum, Nearest);
                if (LDist < RDist)
                {
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                }
                else
                {
                    if (LDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Left;  Depth += 1u; }
                    if (RDist < Nearest && Depth < BvhStackCapacity) { Stack[Depth] = Right; Depth += 1u; }
                }
            }
        }
    }

    Result.Steps = Steps;

    if (BestTri != 0xFFFFFFFFu)
    {
        let Base = BestTri * 4u;
        let T0   = BvhTriangles[Base];
        let T1   = BvhTriangles[Base + 1u];
        let T2   = BvhTriangles[Base + 2u];
        let T3   = BvhTriangles[Base + 3u];

        let NA = UnpackOctahedral(bitcast<u32>(T1.w));
        let NB = UnpackOctahedral(bitcast<u32>(T2.w));
        let NC = UnpackOctahedral(bitcast<u32>(T3.w));
        let W  = 1.0 - BestUV.x - BestUV.y;
        let LocalShading = normalize(NA * W + NB * BestUV.x + NC * BestUV.y);

        let Occ        = Occupants[BestOcc];
        var WorldNorm  = normalize((Occ.LocalToWorld * vec4f(LocalShading, 0.0)).xyz);
        var WorldGeom  = normalize((Occ.LocalToWorld * vec4f(T3.xyz, 0.0)).xyz);
        if (dot(WorldGeom, Direction) > 0.0)
        {
            WorldGeom = -WorldGeom;
        }
        if (dot(WorldNorm, Direction) > 0.0)
        {
            WorldNorm = -WorldNorm;
        }

        let RawFamily   = bitcast<u32>(T0.w);
        Result.Position = Origin + Direction * Nearest;
        Result.Travel   = Nearest;
        Result.Normal   = WorldNorm;
        Result.Geometry = WorldGeom;
        Result.Family   = select(RawFamily, Occ.MaterialOverride, Occ.MaterialOverride != NoMaterialOverride);
        Result.Identity = select(BestOcc, GroundIdentity, BestOcc == GroundOccupantIndex);
        Result.Hit      = true;
        return Result;
    }

    // Far-field horizon ground fallback beyond the authored 220m mesh
    if (Direction.z < -1.0e-4)
    {
        let GroundTravel = (Distance.GroundHeight - Origin.z) / Direction.z;
        if (GroundTravel >= MinimumTravel && GroundTravel <= MaximumTravel)
        {
            Result.Position = Origin + Direction * GroundTravel;
            Result.Travel   = GroundTravel;
            Result.Normal   = vec3f(0.0, 0.0, 1.0);
            Result.Geometry = vec3f(0.0, 0.0, 1.0);
            Result.Identity = GroundIdentity;
            Result.Family   = SurroundFamily;
            Result.Hit      = true;
        }
    }

    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TLAS + BLAS ANY-HIT VISIBILITY
//------------------------------------------------------------------------------------------------------------------------

