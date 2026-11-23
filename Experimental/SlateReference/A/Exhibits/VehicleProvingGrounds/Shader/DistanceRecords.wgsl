//============================================================================================================================================
//                                                           DISTANCERECORDS.WGSL
//============================================================================================================================================
// 🧩 Cascade records of the scene distance field, shared by the union pass that writes it and every pass that traverses it.

struct ClipRecord
{
    OriginVoxel        : vec3i,   // [voxel] world voxel coordinate of the window's minimum corner
    Resolution         : u32,     // [voxel] lattice edge
    VoxelExtent        : f32,     // [m]
    Band               : f32,     // [m]     truncation distance written by the union pass
    InverseWorldExtent : f32,     // [1/m]   1 / (Resolution · VoxelExtent)
    Pad                : f32,
}

struct DistanceRecord
{
    Clips                 : array<ClipRecord, 4>,
    ClipCount             : u32,     // [-]
    DistanceOccupantCount : u32,     // [-]  occupants [0, count) own distance volumes
    GroundHeight          : f32,     // [m]  analytic course plane
    HitFraction           : f32,     // [-]  surface threshold as a fraction of the voxel edge
    BrickCount            : u32,     // [-]  bricks the union pass rewrites this frame
    FrameIndex            : u32,     // [-]  stamped into every rewritten brick
    BricksPerAxis         : u32,     // [-]  Resolution / BrickEdge
    Pad                   : u32,
}

const BrickEdge      : u32 = 8u;
const SkyIdentity    : u32 = 2045u;
const GroundIdentity : u32 = 2046u;
const VacantIdentity : u32 = 2047u;

// 📝 Toroidal brick slot of a world brick in one cascade — the same wrap the voxels use, so an epoch follows its brick.
fn ResolveBrickSlot(Level: u32, WorldBrick: vec3i, BricksPerAxis: u32) -> u32
{
    let Edge = i32(BricksPerAxis);
    let Wrapped = vec3u(((WorldBrick % Edge) + Edge) % Edge);
    return Level * BricksPerAxis * BricksPerAxis * BricksPerAxis + (Wrapped.z * BricksPerAxis + Wrapped.y) * BricksPerAxis + Wrapped.x;
}

// 📝 A brick epoch is the frame of its last rewrite in the low 31 bits; bit 31 records that a moving occupant caused
//    it, as opposed to the cascade window scrolling over it. The inspection views glow the two causes differently.
const OccupantEpochBit : u32 = 0x80000000u;
const VacantEpoch      : u32 = 0xFFFFFFFFu;

fn ConstructBrickEpoch(FrameIndex: u32, OccupantCaused: bool) -> u32
{
    return (FrameIndex & 0x7FFFFFFFu) | select(0u, OccupantEpochBit, OccupantCaused);
}

// 📝 Returns (frames since the rewrite, 1 when a moving occupant caused it); a never-written brick is infinitely old.
fn ResolveBrickAge(Epoch: u32, FrameIndex: u32) -> vec2f
{
    if (Epoch == VacantEpoch)
    {
        return vec2f(1.0e9, 0.0);
    }
    let Written = Epoch & 0x7FFFFFFFu;
    return vec2f(f32((FrameIndex & 0x7FFFFFFFu) - Written), select(0.0, 1.0, (Epoch & OccupantEpochBit) != 0u));
}
