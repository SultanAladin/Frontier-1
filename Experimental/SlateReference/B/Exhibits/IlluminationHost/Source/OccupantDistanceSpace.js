//============================================================================================================================================
//                                                         OCCUPANTDISTANCESPACE.JS
//============================================================================================================================================
// 🧩 Per-facet-set signed distance volumes, stacked in one 3D texel space and transferred on the device at bring-up.

import { ConstructBoundingStructure } from "./BoundingStructure.js";
import { OccupantRole } from "./FacetStructure.js";

const MaximumVolumeTexels = 128;    // [texel] - longest volume axis
const MinimumVolumeTexels = 8;      // [texel] - shortest volume axis
const SignRayCount        = 32;     // [-]     - rays per voxel for the back-face sign heuristic
const SlabVoxelBudget     = 65536;  // [voxel] - voxels per submission, so no single dispatch can trip a device watchdog

// 📝 The fragment pool: cubes of Edge³ texels appended below the stacked volumes, each holding one live fragment's
//    distance volume, computed on the device the frame the fragment is born (FragmentDistance.wgsl).
export const FragmentPool = Object.freeze({
    Edge:         12,    // [texel] per axis, two of them margin on each side
    Slots:        256,   // [-]
    MaxTriangles: 48,    // [-]     a fragment with more is left out of the field
    MaxPlanes:    40,    // [-]
    JobsPerFrame: 48     // [-]     volumes computed per frame at most; the rest wait a frame
});
const PoolStride = FragmentPool.MaxTriangles * 3 + FragmentPool.MaxPlanes;   // [vec4] geometry per job

export class OccupantDistanceSpace
{
    constructor(Exchange, Shaders)
    {
        this.Exchange    = Exchange;
        this.Shaders     = Shaders;
        this.Volumes     = [];
        this.Texture     = null;
        this.TextureView = null;
        this.SpaceExtent = [0, 0, 0];
        this.Pool        = { Origins: [], Owners: [], Free: [], Queue: [], Order: [] };
        this.PoolMetrics = { Live: 0, Computed: 0, Evicted: 0 };
    }

    // 📝 ① choose each volume's voxel and resolution from its facet set; ② stack them along z with one-texel gaps;
    //    ③ transfer each volume slab by slab. VoxelScale coarsens every volume uniformly for weaker adapters.
    async ConstructVolumes(Scene, VoxelScale, ReportProgress)
    {
        const Required = new Set();
        for (const Occupant of Scene.Occupants)
        {
            if (Occupant.Roles & OccupantRole.Distance)
                Required.add(Occupant.FacetSet);
        }

        let Width = 0, Height = 0, Depth = 0;
        for (const SetIndex of Required)
        {
            const Set = Scene.FacetSets[SetIndex];
            let Voxel = Set.DistanceVoxel * VoxelScale;
            const Extent = [0, 1, 2].map((Axis) => Set.LocalMaximum[Axis] - Set.LocalMinimum[Axis]);
            const Margin = () => Math.max(4.0 * Voxel, 0.12);
            let Longest = Math.max(...Extent.map((Length) => Length + 2.0 * Margin()));
            if (Longest / Voxel > MaximumVolumeTexels)
                Voxel = Longest / MaximumVolumeTexels;
            const Resolution = Extent.map((Length) =>
                Math.max(MinimumVolumeTexels, Math.min(MaximumVolumeTexels, Math.ceil((Length + 2.0 * Margin()) / Voxel))));
            const Centre = [0, 1, 2].map((Axis) => 0.5 * (Set.LocalMinimum[Axis] + Set.LocalMaximum[Axis]));
            const Minimum = [0, 1, 2].map((Axis) => Centre[Axis] - 0.5 * Resolution[Axis] * Voxel);
            const Volume = { FacetSet: SetIndex, VoxelExtent: Voxel, Resolution, Minimum, TexelOrigin: [0, 0, Depth] };
            Set.VolumeIndex = this.Volumes.length;
            this.Volumes.push(Volume);
            Width  = Math.max(Width, Resolution[0]);
            Height = Math.max(Height, Resolution[1]);
            Depth += Resolution[2] + 1;
        }
        // 📝 The fragment pool, layer after layer of Edge-cubes below the last volume.
        const Edge = FragmentPool.Edge;
        Width = Math.max(Width, Edge);
        Height = Math.max(Height, Edge);
        const PerRow = Math.floor(Width / Edge), PerColumn = Math.floor(Height / Edge);
        const Layers = Math.ceil(FragmentPool.Slots / (PerRow * PerColumn));
        for (let Slot = 0; Slot < FragmentPool.Slots; ++Slot)
        {
            const Layer = Math.floor(Slot / (PerRow * PerColumn)), Within = Slot % (PerRow * PerColumn);
            this.Pool.Origins.push([(Within % PerRow) * Edge, Math.floor(Within / PerRow) * Edge, Depth + Layer * (Edge + 1)]);
            this.Pool.Owners.push(null);
            this.Pool.Free.push(FragmentPool.Slots - 1 - Slot);
        }
        Depth += Layers * (Edge + 1);
        const Limit = this.Exchange.Device.limits.maxTextureDimension3D;
        if (Depth > Limit)
            throw new Error(`Distance volumes need ${Depth} slices; the adapter allows ${Limit}`);
        this.SpaceExtent = [Width, Height, Math.max(Depth, 1)];

        this.Texture = this.Exchange.ConstructTexture({
            label: "Occupant distance space",
            size: this.SpaceExtent,
            dimension: "3d",
            format: "rgba16float",
            usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING
        });
        this.TextureView = this.Texture.createView({ dimension: "3d" });

        const Layout = this.Exchange.ConstructDescriptorLayout(["Uniform", "StorageRead", "StorageRead", "StorageTexture3D:rgba16float"],
                                                                GPUShaderStage.COMPUTE, "Distance transfer");
        const Program = await this.Shaders.ConstructComputeProgram("DistanceTransfer.wgsl", "TransferDistance", [Layout]);
        const Device = this.Exchange.Device;

        let VoxelTotal = 0, VoxelDone = 0;
        for (const Volume of this.Volumes)
            VoxelTotal += Volume.Resolution[0] * Volume.Resolution[1] * Volume.Resolution[2];

        for (const Volume of this.Volumes)
        {
            const Set = Scene.FacetSets[Volume.FacetSet];
            const Corners = new Float32Array(Set.Facets.length * 9);
            Set.Facets.forEach((Facet, Index) =>
            {
                for (let Corner = 0; Corner < 3; ++Corner)
                    Corners.set(Facet.Corners[Corner], Index * 9 + Corner * 3);
            });
            const Structure = ConstructBoundingStructure(Corners);
            const NodeRecords  = this.Exchange.ConstructRecordsFrom(Structure.NodeScalars, GPUBufferUsage.STORAGE, `${Set.Name} nodes`);
            const FacetRecords = this.Exchange.ConstructRecordsFrom(Structure.FacetCorners, GPUBufferUsage.STORAGE, `${Set.Name} facets`);
            const SlicePlane = Volume.Resolution[0] * Volume.Resolution[1];
            const SlabSlices = Math.max(1, Math.floor(SlabVoxelBudget / SlicePlane));
            for (let Slab = 0; Slab < Volume.Resolution[2]; Slab += SlabSlices)
            {
                const SlabExtent = Math.min(SlabSlices, Volume.Resolution[2] - Slab);
                const Bytes = new ArrayBuffer(64);
                const Scalars = new Float32Array(Bytes);
                const Integers = new Uint32Array(Bytes);
                Scalars.set(Volume.Minimum, 0);
                Scalars[3] = Volume.VoxelExtent;
                Integers.set(Volume.TexelOrigin, 4);
                Integers[7] = Structure.NodeCount;
                Integers.set(Volume.Resolution, 8);
                Integers[11] = Structure.FacetCount;
                Integers[12] = Slab;
                Integers[13] = SlabExtent;
                Integers[14] = SignRayCount;
                const Uniform = this.Exchange.ConstructRecordsFrom(new Uint8Array(Bytes), GPUBufferUsage.UNIFORM, "Transfer record");
                const Group = this.Exchange.ConstructDescriptorGroup(Layout, [Uniform, NodeRecords, FacetRecords, this.TextureView]);
                const Encoder = Device.createCommandEncoder({ label: "Distance transfer" });
                const Pass = Encoder.beginComputePass({ label: `Transfer ${Set.Name}` });
                Pass.setPipeline(Program);
                Pass.setBindGroup(0, Group);
                Pass.dispatchWorkgroups(Math.ceil(Volume.Resolution[0] / 4), Math.ceil(Volume.Resolution[1] / 4), Math.ceil(SlabExtent / 4));
                Pass.end();
                Device.queue.submit([Encoder.finish()]);
                await Device.queue.onSubmittedWorkDone();
                Uniform.destroy();
                VoxelDone += SlicePlane * SlabExtent;
                if (ReportProgress)
                    ReportProgress(`Transferring distance volume: ${Set.Name}`, VoxelDone / VoxelTotal);
            }
            NodeRecords.destroy();
            FacetRecords.destroy();
        }
    }

    QueryVolume(Scene, FacetSet)
    {
        const Index = Scene.FacetSets[FacetSet].VolumeIndex;
        return Index >= 0 ? this.Volumes[Index] : null;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                               FRAGMENT POOL
    //--------------------------------------------------------------------------------------------------------------------

    async ConstructFragmentProgram()
    {
        this.FragmentLayout = this.Exchange.ConstructDescriptorLayout(["StorageRead", "StorageRead", "StorageTexture3D:rgba16float"],
                                                                      GPUShaderStage.COMPUTE, "Fragment distance");
        this.FragmentProgram = await this.Shaders.ConstructComputeProgram("FragmentDistance.wgsl", "ComputeFragmentDistance", [this.FragmentLayout]);
        this.JobRecords = this.Exchange.ConstructRecords(FragmentPool.JobsPerFrame * 32, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, "Fragment jobs");
        this.GeometryRecords = this.Exchange.ConstructRecords(FragmentPool.JobsPerFrame * PoolStride * 16, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
                                                              "Fragment geometry");
        this.FragmentGroup = this.Exchange.ConstructDescriptorGroup(this.FragmentLayout, [this.JobRecords, this.GeometryRecords, this.TextureView]);
    }

    // 📝 Gives a fragment a pool volume: { Minimum, VoxelExtent, TexelOrigin, Resolution, Slot } in its own (centroid)
    //    frame, or null when it does not fit the job layout. When the pool is full the oldest live volume is evicted
    //    and Evict(Owner) is called for it, so its occupant can leave the field.
    AssignFragment(Owner, Fragment, Evict)
    {
        if (!this.FragmentProgram || Fragment.Triangles.length > FragmentPool.MaxTriangles || Fragment.Planes.length > FragmentPool.MaxPlanes)
            return null;
        if (this.Pool.Free.length === 0)
        {
            const Oldest = this.Pool.Order.shift();
            if (Oldest === undefined)
                return null;
            const Previous = this.Pool.Owners[Oldest];
            this.Pool.Owners[Oldest] = null;
            this.Pool.Queue = this.Pool.Queue.filter((Job) => Job.Slot !== Oldest);
            this.Pool.Free.push(Oldest);
            this.PoolMetrics.Evicted++;
            if (Previous && Evict)
                Evict(Previous);
        }
        const Slot = this.Pool.Free.pop();
        const Edge = FragmentPool.Edge;
        const Extent = [0, 1, 2].map((Axis) => Fragment.Maximum[Axis] - Fragment.Minimum[Axis]);
        const Voxel = Math.max(...Extent, 0.02) / (Edge - 4);
        const Centre = [0, 1, 2].map((Axis) => 0.5 * (Fragment.Minimum[Axis] + Fragment.Maximum[Axis]));
        const Volume = { Minimum: Centre.map((Component) => Component - 0.5 * Edge * Voxel), VoxelExtent: Voxel,
                         TexelOrigin: this.Pool.Origins[Slot], Resolution: [Edge, Edge, Edge], Slot };
        this.Pool.Owners[Slot] = Owner;
        this.Pool.Order.push(Slot);
        this.Pool.Queue.push({ Slot, Volume, Fragment });
        return Volume;
    }

    ReleaseFragment(Volume)
    {
        if (!Volume || this.Pool.Owners[Volume.Slot] === null)
            return;
        this.Pool.Owners[Volume.Slot] = null;
        this.Pool.Order = this.Pool.Order.filter((Slot) => Slot !== Volume.Slot);
        this.Pool.Queue = this.Pool.Queue.filter((Job) => Job.Slot !== Volume.Slot);
        this.Pool.Free.push(Volume.Slot);
    }

    // 📝 Computes the queued volumes (up to JobsPerFrame) before the union reads them; returns how many it computed.
    RecordFragments(Encoder)
    {
        this.PoolMetrics.Live = FragmentPool.Slots - this.Pool.Free.length;
        if (!this.FragmentProgram || this.Pool.Queue.length === 0)
            return 0;
        const Jobs = this.Pool.Queue.splice(0, FragmentPool.JobsPerFrame);
        const JobBytes = new ArrayBuffer(Jobs.length * 32);
        const JobScalars = new Float32Array(JobBytes), JobIntegers = new Uint32Array(JobBytes);
        const Geometry = new Float32Array(Jobs.length * PoolStride * 4);
        Jobs.forEach((Job, Index) =>
        {
            JobIntegers.set(Job.Volume.TexelOrigin, Index * 8);
            JobIntegers[Index * 8 + 3] = Job.Fragment.Triangles.length | (Job.Fragment.Planes.length << 16);
            JobScalars.set(Job.Volume.Minimum, Index * 8 + 4);
            JobScalars[Index * 8 + 7] = Job.Volume.VoxelExtent;
            const Base = Index * PoolStride * 4;
            Job.Fragment.Triangles.forEach((Triangle, TriangleIndex) =>
                Triangle.Corners.forEach((Corner, CornerIndex) => Geometry.set(Corner, Base + (TriangleIndex * 3 + CornerIndex) * 4)));
            Job.Fragment.Planes.forEach((Plane, PlaneIndex) => Geometry.set(Plane, Base + (FragmentPool.MaxTriangles * 3 + PlaneIndex) * 4));
        });
        const Device = this.Exchange.Device;
        Device.queue.writeBuffer(this.JobRecords, 0, JobBytes);
        Device.queue.writeBuffer(this.GeometryRecords, 0, Geometry);
        const Pass = Encoder.beginComputePass({ label: "Fragment distance volumes" });
        Pass.setPipeline(this.FragmentProgram);
        Pass.setBindGroup(0, this.FragmentGroup);
        Pass.dispatchWorkgroups(Jobs.length);
        Pass.end();
        this.PoolMetrics.Computed += Jobs.length;
        return Jobs.length;
    }
}
