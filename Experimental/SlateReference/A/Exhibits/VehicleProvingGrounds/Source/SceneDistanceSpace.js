//============================================================================================================================================
//                                                          SCENEDISTANCESPACE.JS
//============================================================================================================================================
// 🧩 The single scene (global) distance field: four camera-centred toroidal cascades, rewritten only where invalidated.

import { OccupantRole, IdentityCeiling } from "./FacetStructure.js";
import { TransformBounds } from "./TransformProjection.js";

const BrickEdge     = 8;   // [voxel] - edge of the unit of invalidation
const ScrollCause   = 1;   // [-]     - the cascade window newly covers the brick
const OccupantCause = 2;   // [-]     - a moving occupant's previous or current bounds overlap the brick

export class SceneDistanceSpace
{
    // 📝 Configuration: ClipCount ≤ 4, Resolution a multiple of 8, FinestVoxel [m], BandVoxels, HitFraction.
    constructor(Exchange, Shaders, Configuration)
    {
        this.Exchange      = Exchange;
        this.Shaders       = Shaders;
        this.Configuration = Configuration;
        this.BricksPerAxis = Configuration.Resolution / BrickEdge;
        this.Clips = [];
        for (let Level = 0; Level < 4; ++Level)
        {
            const VoxelExtent = Configuration.FinestVoxel * Math.pow(2, Level);
            this.Clips.push({ VoxelExtent, Band: Configuration.BandVoxels * VoxelExtent, OriginBrick: null });
        }
        this.Textures      = [];
        this.Views         = [];
        this.FrameBricks   = [];
        this.FrameLevelCounts = [0, 0, 0, 0];
        this.TotalBricks   = 0;
        this.Frozen        = false;
        this.PendingBounds = [];
    }

    // 📝 Bounds of occupants that appeared or vanished outside the moving-occupant path (stress objects enabled or
    //    disabled): their bricks are rewritten by the next schedule, exactly as a moving occupant's would be.
    InvalidateBounds(BoundsList)
    {
        this.PendingBounds.push(...BoundsList);
    }

    async ConstructResources(VolumeSpaceView, OccupantRecords)
    {
        const Device = this.Exchange.Device;
        const Resolution = this.Configuration.Resolution;
        for (let Level = 0; Level < 4; ++Level)
        {
            const Texture = this.Exchange.ConstructTexture({
                label: `Scene distance cascade ${Level}`,
                size: [Resolution, Resolution, Resolution],
                dimension: "3d",
                format: "rgba16float",
                usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING
            });
            this.Textures.push(Texture);
            this.Views.push(Texture.createView({ dimension: "3d" }));
        }
        const BrickSlots = 4 * this.BricksPerAxis ** 3;
        this.BrickRecords  = this.Exchange.ConstructRecords(BrickSlots * 16, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, "Invalidated bricks");
        this.EpochRecords  = this.Exchange.ConstructRecords(BrickSlots * 4, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, "Brick stamps");
        this.UniformRecords = this.Exchange.ConstructRecords(160, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Distance record");
        // 📝 The union's active list: occupant indices, at most one per identity.
        this.ListRecords = this.Exchange.ConstructRecords(IdentityCeiling * 4, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, "Distance list");
        Device.queue.writeBuffer(this.EpochRecords, 0, new Uint32Array(BrickSlots).fill(0xFFFFFFFF));

        this.VolumeSampler = this.Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear",
                                                              addressModeU: "clamp-to-edge", addressModeV: "clamp-to-edge", addressModeW: "clamp-to-edge" });
        this.CascadeSampler = this.Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear",
                                                               addressModeU: "repeat", addressModeV: "repeat", addressModeW: "repeat" });

        this.UnionLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "StorageRead", "Texture3D", "Sampler",
             "StorageTexture3D:rgba16float", "StorageTexture3D:rgba16float", "StorageTexture3D:rgba16float", "StorageTexture3D:rgba16float",
             "Storage", "StorageRead"], GPUShaderStage.COMPUTE, "Distance union");
        this.UnionProgram = await this.Shaders.ConstructComputeProgram("DistanceUnion.wgsl", "UniteDistance", [this.UnionLayout]);
        this.UnionGroup = this.Exchange.ConstructDescriptorGroup(this.UnionLayout,
            [this.UniformRecords, this.BrickRecords, OccupantRecords, VolumeSpaceView, this.VolumeSampler,
             this.Views[0], this.Views[1], this.Views[2], this.Views[3], this.EpochRecords, this.ListRecords]);

        this.TraversalLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture3D", "Texture3D", "Texture3D", "Texture3D", "Sampler", "StorageRead"],
            GPUShaderStage.COMPUTE | GPUShaderStage.FRAGMENT, "Distance traversal");
        this.TraversalGroup = this.Exchange.ConstructDescriptorGroup(this.TraversalLayout,
            [this.UniformRecords, this.Views[0], this.Views[1], this.Views[2], this.Views[3], this.CascadeSampler, this.EpochRecords]);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                            INVALIDATION SCHEDULE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 ① each cascade's window follows Focus in whole bricks (a quarter of it below the focus height, where the course
    //    plane is all that lives); ② bricks the window newly covers are invalidated; ③ every bounds in MovedBounds —
    //    the union of a moving occupant's previous and current world bounds — invalidates the bricks it overlaps once
    //    grown by that cascade's band. Bricks are deduplicated per cascade through a slot flag lattice.
    ScheduleInvalidation(Focus, MovedBounds)
    {
        this.FrameBricks = [];
        this.FrameLevelCounts = [0, 0, 0, 0];
        if (this.Frozen)
            return;
        if (this.PendingBounds.length > 0)
        {
            MovedBounds = MovedBounds.concat(this.PendingBounds);
            this.PendingBounds = [];
        }
        const Edge = this.BricksPerAxis;
        for (let Level = 0; Level < this.Configuration.ClipCount; ++Level)
        {
            const Clip = this.Clips[Level];
            const BrickWorld = BrickEdge * Clip.VoxelExtent;
            const Desired = [Math.floor(Focus[0] / BrickWorld) - Edge / 2,
                             Math.floor(Focus[1] / BrickWorld) - Edge / 2,
                             Math.floor(Focus[2] / BrickWorld) - Edge / 4];
            const Flags = new Uint8Array(Edge * Edge * Edge);
            const Previous = Clip.OriginBrick;
            const Moved = !Previous || Desired.some((Component, Axis) => Component !== Previous[Axis]);
            if (Moved)
            {
                for (let Z = 0; Z < Edge; ++Z)
                {
                    for (let Y = 0; Y < Edge; ++Y)
                    {
                        for (let X = 0; X < Edge; ++X)
                        {
                            const World = [Desired[0] + X, Desired[1] + Y, Desired[2] + Z];
                            const Covered = Previous && World.every((Component, Axis) =>
                                Component >= Previous[Axis] && Component < Previous[Axis] + Edge);
                            if (!Covered)
                                Flags[(Z * Edge + Y) * Edge + X] |= ScrollCause;
                        }
                    }
                }
                Clip.OriginBrick = Desired;
            }
            for (const Bounds of MovedBounds)
            {
                const Low  = Bounds.Minimum.map((Component, Axis) => Math.floor((Component - Clip.Band) / BrickWorld) - Desired[Axis]);
                const High = Bounds.Maximum.map((Component, Axis) => Math.floor((Component + Clip.Band) / BrickWorld) - Desired[Axis]);
                for (let Z = Math.max(0, Low[2]); Z <= Math.min(Edge - 1, High[2]); ++Z)
                    for (let Y = Math.max(0, Low[1]); Y <= Math.min(Edge - 1, High[1]); ++Y)
                        for (let X = Math.max(0, Low[0]); X <= Math.min(Edge - 1, High[0]); ++X)
                            Flags[(Z * Edge + Y) * Edge + X] |= OccupantCause;
            }
            for (let Slot = 0; Slot < Flags.length; ++Slot)
            {
                if (!Flags[Slot])
                    continue;
                const X = Slot % Edge, Y = Math.floor(Slot / Edge) % Edge, Z = Math.floor(Slot / (Edge * Edge));
                this.FrameBricks.push(Desired[0] + X, Desired[1] + Y, Desired[2] + Z, Level | (Flags[Slot] << 8));
                this.FrameLevelCounts[Level]++;
            }
        }
        this.TotalBricks += this.FrameBricks.length / 4;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  RECORDING
    //--------------------------------------------------------------------------------------------------------------------

    EncodeDistanceRecord(DistanceOccupantCount, FrameIndex)
    {
        const Bytes = new ArrayBuffer(160);
        const Integers = new Int32Array(Bytes);
        const Unsigned = new Uint32Array(Bytes);
        const Scalars = new Float32Array(Bytes);
        const Resolution = this.Configuration.Resolution;
        for (let Level = 0; Level < 4; ++Level)
        {
            const Clip = this.Clips[Level];
            const Offset = Level * 8;
            const Origin = Clip.OriginBrick || [0, 0, 0];
            Integers[Offset + 0] = Origin[0] * BrickEdge;
            Integers[Offset + 1] = Origin[1] * BrickEdge;
            Integers[Offset + 2] = Origin[2] * BrickEdge;
            Unsigned[Offset + 3] = Resolution;
            Scalars[Offset + 4] = Clip.VoxelExtent;
            Scalars[Offset + 5] = Clip.Band;
            Scalars[Offset + 6] = 1.0 / (Resolution * Clip.VoxelExtent);
        }
        Unsigned[32] = this.Configuration.ClipCount;
        Unsigned[33] = DistanceOccupantCount;
        Scalars[34]  = 0.0;
        Scalars[35]  = this.Configuration.HitFraction;
        Unsigned[36] = this.FrameBricks.length / 4;
        Unsigned[37] = FrameIndex;
        Unsigned[38] = this.BricksPerAxis;
        return Bytes;
    }

    // 📝 Uploads the active list (Scene.DistanceList) — only when it changed.
    UploadDistanceList(List)
    {
        if (List.length > 0)
            this.Exchange.Device.queue.writeBuffer(this.ListRecords, 0, List);
    }

    RecordUnion(Encoder, DistanceOccupantCount, FrameIndex, TimestampWrites)
    {
        const Device = this.Exchange.Device;
        Device.queue.writeBuffer(this.UniformRecords, 0, this.EncodeDistanceRecord(DistanceOccupantCount, FrameIndex));
        const BrickCount = this.FrameBricks.length / 4;
        if (BrickCount === 0)
            return;
        Device.queue.writeBuffer(this.BrickRecords, 0, new Int32Array(this.FrameBricks));
        const Pass = Encoder.beginComputePass({ label: "Scene distance union", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.UnionProgram);
        Pass.setBindGroup(0, this.UnionGroup);
        Pass.dispatchWorkgroups(Math.min(BrickCount, 65535), Math.ceil(BrickCount / 65535));
        Pass.end();
    }

    QueryClipWindow(Level)
    {
        const Clip = this.Clips[Level];
        const BrickWorld = BrickEdge * Clip.VoxelExtent;
        const Origin = Clip.OriginBrick || [0, 0, 0];
        const Minimum = Origin.map((Component) => Component * BrickWorld);
        return { Minimum, Maximum: Minimum.map((Component) => Component + this.BricksPerAxis * BrickWorld), VoxelExtent: Clip.VoxelExtent };
    }
}

// 📝 A moving occupant invalidates the union of where the field last drew it and where it is now. The comparison is
//    against the transform last unioned, not last frame's, so a parked vehicle's suspension settling by fractions of
//    a millimetre does not rewrite bricks every frame, while slow motion still accumulates until it crosses the
//    tolerance — 2 mm of travel or about a tenth of a degree of rotation.
const TranslationTolerance = 0.002;   // [m]
const RotationTolerance    = 0.002;   // [-] largest change of any rotation-matrix element

export function ResolveMovedBounds(Scene, Frozen = false)
{
    const Moved = [];
    for (let Index = 0; Index < Scene.Occupants.length; ++Index)
    {
        const Occupant = Scene.Occupants[Index];
        if (!(Occupant.Roles & OccupantRole.Dynamic) || !(Occupant.Roles & OccupantRole.Distance))
            continue;
        if (!Occupant.UnionTransform)
            Occupant.UnionTransform = Occupant.Transform.slice();
        if (Frozen)
            continue;
        let Rotation = 0.0;
        for (const Element of [0, 1, 2, 4, 5, 6, 8, 9, 10])
            Rotation = Math.max(Rotation, Math.abs(Occupant.Transform[Element] - Occupant.UnionTransform[Element]));
        const Translation = Math.hypot(Occupant.Transform[12] - Occupant.UnionTransform[12], Occupant.Transform[13] - Occupant.UnionTransform[13],
                                       Occupant.Transform[14] - Occupant.UnionTransform[14]);
        if (Rotation < RotationTolerance && Translation < TranslationTolerance)
            continue;
        const Set = Scene.FacetSets[Occupant.FacetSet];
        const Current = Scene.QueryWorldBounds(Index);
        const Before = TransformBounds(Occupant.UnionTransform, Occupant.LocalMinimum || Set.LocalMinimum, Occupant.LocalMaximum || Set.LocalMaximum);
        Moved.push({ Minimum: Current.Minimum.map((Component, Axis) => Math.min(Component, Before.Minimum[Axis])),
                     Maximum: Current.Maximum.map((Component, Axis) => Math.max(Component, Before.Maximum[Axis])) });
        Occupant.UnionTransform = Occupant.Transform.slice();
    }
    return Moved;
}

function ConstructPreviousBounds(Transform, Set)
{
    return TransformBounds(Transform, Set.LocalMinimum, Set.LocalMaximum);
}
