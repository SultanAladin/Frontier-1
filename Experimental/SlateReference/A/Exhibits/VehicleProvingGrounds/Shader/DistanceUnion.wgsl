//============================================================================================================================================
//                                                            DISTANCEUNION.WGSL
//============================================================================================================================================
// 🧩 Rewrites the scene distance field brick by brick: min over the culled occupant volumes and the analytic course plane.

// 📝 This is the ONLY place that walks occupants, and it does so per invalidated 8³ brick, never per ray. A brick is
//    invalidated when the cascade window scrolls over it or when a moving occupant's old or new bounds, grown by the
//    cascade band, overlap it. Everything downstream traverses the assembled field alone.
//
//    The active list (every enabled distance occupant, rebuilt on the host when activity changes) is walked in pages
//    of 64: each lane tests one occupant's bounds, the survivors' volume records
//    are staged in workgroup memory, and every lane then folds the page into the eight voxels it owns. A page can
//    never overflow, so a brick under any number of occupants (a dense stress field, say) is still exact, and each
//    surviving record is read from device memory once per brick instead of once per voxel.

//! include "SceneRecords.wgsl"
//! include "DistanceRecords.wgsl"

@group(0) @binding(0) var<uniform> Distance          : DistanceRecord;
@group(0) @binding(1) var<storage, read> Bricks      : array<vec4i>;
@group(0) @binding(2) var<storage, read> Occupants   : array<OccupantRecord>;
@group(0) @binding(3) var VolumeSpace                : texture_3d<f32>;
@group(0) @binding(4) var VolumeSampler              : sampler;
@group(0) @binding(5) var TargetClip0                : texture_storage_3d<rgba16float, write>;
@group(0) @binding(6) var TargetClip1                : texture_storage_3d<rgba16float, write>;
@group(0) @binding(7) var TargetClip2                : texture_storage_3d<rgba16float, write>;
@group(0) @binding(8) var TargetClip3                : texture_storage_3d<rgba16float, write>;
@group(0) @binding(9) var<storage, read_write> BrickEpochs : array<u32>;
@group(0) @binding(10) var<storage, read> DistanceList : array<u32>;   // occupant indices; Distance.DistanceOccupantCount long

const PageSize : u32 = 64u;   // [-] - occupants tested per page, one per lane

var<workgroup> PageCount    : atomic<u32>;
var<workgroup> PageIdentity : array<u32, 64>;
var<workgroup> PageInverse  : array<mat4x4f, 64>;   // WorldToLocal
var<workgroup> PageVolume   : array<vec4f, 64>;     // xyz VolumeMinimum [m], w VoxelExtent [m]
var<workgroup> PageTexel    : array<vec4f, 64>;     // xyz VolumeTexelOrigin [texel]
var<workgroup> PageExtent   : array<vec4f, 64>;     // xyz VolumeTexelExtent [texel]

fn WriteClipTexel(Level: u32, Texel: vec3i, Texel4: vec4f)
{
    switch (Level)
    {
        case 0u:      { textureStore(TargetClip0, Texel, Texel4); }
        case 1u:      { textureStore(TargetClip1, Texel, Texel4); }
        case 2u:      { textureStore(TargetClip2, Texel, Texel4); }
        default:      { textureStore(TargetClip3, Texel, Texel4); }
    }
}

// 📐 Inside its volume an occupant answers with its transferred distance; outside, with the distance to the volume
//    plus the distance stored at the nearest volume point — the extension the union needs for its band.
fn SamplePageDistance(Slot: u32, Position: vec3f, InverseSpace: vec3f) -> f32
{
    let Local   = (PageInverse[Slot] * vec4f(Position, 1.0)).xyz;
    let Volume  = PageVolume[Slot];
    let Lowest  = Volume.xyz + vec3f(0.5 * Volume.w);
    let Highest = Volume.xyz + (PageExtent[Slot].xyz - vec3f(0.5)) * Volume.w;
    let Clamped = clamp(Local, Lowest, Highest);
    let Outside = length(Local - Clamped);
    let Texel   = PageTexel[Slot].xyz + (Clamped - Volume.xyz) / Volume.w;
    return textureSampleLevel(VolumeSpace, VolumeSampler, Texel * InverseSpace, 0.0).x + Outside;
}

@compute @workgroup_size(64)
fn UniteDistance(@builtin(workgroup_id) Group: vec3u, @builtin(local_invocation_index) Lane: u32)
{
    let BrickIndex = Group.x + Group.y * 65535u;
    if (BrickIndex >= Distance.BrickCount)
    {
        return;
    }

    let Brick = Bricks[BrickIndex];
    let Level = u32(Brick.w) & 0xFFu;
    let OccupantCaused = ((u32(Brick.w) >> 8u) & 2u) != 0u;
    let Clip  = Distance.Clips[Level];
    let BrickMinimum = vec3f(Brick.xyz * i32(BrickEdge)) * Clip.VoxelExtent;
    let BrickMaximum = BrickMinimum + vec3f(f32(BrickEdge) * Clip.VoxelExtent);
    let InverseSpace = 1.0 / vec3f(textureDimensions(VolumeSpace));

    // ① each lane owns a 2×2×2 sub-cube of the brick, seeded with the analytic course plane
    let LaneVoxel = vec3u(Lane & 3u, (Lane >> 2u) & 3u, Lane >> 4u) * 2u;
    var Nearest  : array<f32, 8>;
    var Identity : array<u32, 8>;
    for (var Sub = 0u; Sub < 8u; Sub++)
    {
        let WorldVoxel = Brick.xyz * i32(BrickEdge) + vec3i(LaneVoxel + vec3u(Sub & 1u, (Sub >> 1u) & 1u, Sub >> 2u));
        Nearest[Sub]  = (f32(WorldVoxel.z) + 0.5) * Clip.VoxelExtent - Distance.GroundHeight;
        Identity[Sub] = GroundIdentity;
    }

    // ② pages: cull 64 occupants against the brick grown by the band, stage the survivors, fold them in
    for (var Page = 0u; Page < Distance.DistanceOccupantCount; Page += PageSize)
    {
        if (Lane == 0u)
        {
            atomicStore(&PageCount, 0u);
        }
        workgroupBarrier();
        if (Page + Lane < Distance.DistanceOccupantCount)
        {
            let Index = DistanceList[Page + Lane];
            let Minimum = Occupants[Index].WorldMinimum;
            let Maximum = Occupants[Index].WorldMaximum;
            if (all(Maximum + vec3f(Clip.Band) >= BrickMinimum) && all(Minimum - vec3f(Clip.Band) <= BrickMaximum))
            {
                let Slot = atomicAdd(&PageCount, 1u);
                PageIdentity[Slot] = Index;
                PageInverse[Slot]  = Occupants[Index].WorldToLocal;
                PageVolume[Slot]   = vec4f(Occupants[Index].VolumeMinimum, Occupants[Index].VoxelExtent);
                PageTexel[Slot]    = vec4f(Occupants[Index].VolumeTexelOrigin, 0.0);
                PageExtent[Slot]   = vec4f(Occupants[Index].VolumeTexelExtent, 0.0);
            }
        }
        workgroupBarrier();
        let Count = atomicLoad(&PageCount);
        for (var Slot = 0u; Slot < Count; Slot++)
        {
            for (var Sub = 0u; Sub < 8u; Sub++)
            {
                let WorldVoxel = Brick.xyz * i32(BrickEdge) + vec3i(LaneVoxel + vec3u(Sub & 1u, (Sub >> 1u) & 1u, Sub >> 2u));
                let Sampled = SamplePageDistance(Slot, (vec3f(WorldVoxel) + 0.5) * Clip.VoxelExtent, InverseSpace);
                if (Sampled < Nearest[Sub])
                {
                    Nearest[Sub]  = Sampled;
                    Identity[Sub] = PageIdentity[Slot];
                }
            }
        }
        workgroupBarrier();
    }

    // ③ write the brick into its toroidal texels and stamp its epoch
    let Resolution = i32(Clip.Resolution);
    for (var Sub = 0u; Sub < 8u; Sub++)
    {
        let WorldVoxel = Brick.xyz * i32(BrickEdge) + vec3i(LaneVoxel + vec3u(Sub & 1u, (Sub >> 1u) & 1u, Sub >> 2u));
        let Texel = ((WorldVoxel % vec3i(Resolution)) + vec3i(Resolution)) % vec3i(Resolution);
        WriteClipTexel(Level, Texel, vec4f(clamp(Nearest[Sub], -Clip.Band, Clip.Band), f32(Identity[Sub]), 0.0, 1.0));
    }
    if (Lane == 0u)
    {
        BrickEpochs[ResolveBrickSlot(Level, Brick.xyz, Distance.BricksPerAxis)] = ConstructBrickEpoch(Distance.FrameIndex, OccupantCaused);
    }
}
