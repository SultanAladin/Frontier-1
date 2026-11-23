//============================================================================================================================================
//                                                          RESAMPLINGSEQUENCE.JS
//============================================================================================================================================
// 🧩 The ReSTIR GI final gather over the scene distance field and the surface cards, plus the glossy reflection rays.

// 📝 Order per frame: ① ResampleTemporal (initial candidate + temporal reuse, writes this frame's guide and
//    reservoirs); ② ResampleSpatial (spatial reuse + resolve into demodulated irradiance); ③ TraverseReflection.
//    Reservoirs and guides alternate between two slots by frame parity; the resolved signals are single. The two
//    programs that trace new rays (① and ③) take the screen-traversal group at slot 3; ② only re-tests visibility.
const ReservoirStride = 32;   // [B] - ReservoirRecord

export class ResamplingSequence
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
    }

    async ConstructPrograms(FrameRecords, MaterialRecords, DistanceLayout, SamplingLayout, ScreenLayout)
    {
        this.FrameRecords    = FrameRecords;
        this.MaterialRecords = MaterialRecords;
        const Compute = GPUShaderStage.COMPUTE;
        this.TemporalLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture2D", "TextureDepth2D", "Texture2D", "Texture2D", "StorageTexture2D:rgba16float", "StorageRead", "Storage"],
            Compute, "Reservoir temporal");
        this.SpatialLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture2D", "TextureDepth2D", "StorageRead", "StorageTexture2D:rgba16float"], Compute, "Reservoir spatial");
        this.ReflectionLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "Texture2D", "TextureDepth2D", "StorageTexture2D:rgba16float",
             "StorageRead", "StorageRead"], Compute, "Reflection traversal");
        this.TemporalProgram   = await this.Shaders.ConstructComputeProgram("ReservoirTemporal.wgsl", "ResampleTemporal",
                                                                            [this.TemporalLayout, DistanceLayout, SamplingLayout, ScreenLayout]);
        this.SpatialProgram    = await this.Shaders.ConstructComputeProgram("ReservoirSpatial.wgsl", "ResampleSpatial",
                                                                            [this.SpatialLayout, DistanceLayout, SamplingLayout]);
        this.ReflectionProgram = await this.Shaders.ConstructComputeProgram("ReflectionTraversal.wgsl", "TraverseReflection",
                                                                            [this.ReflectionLayout, DistanceLayout, SamplingLayout, ScreenLayout]);
    }

    // 📝 RayTracing joins every reflection group at bindings 5–6 whether the toggle is on or off: WebGPU bind
    //    groups must bind every declared slot, so the buffers exist from bring-up and the toggle only picks the path.
    ConstructTargets(TraceExtent, Surface, RayTracing)
    {
        for (const Resource of this.Resources || [])
            Resource.destroy();
        const [Width, Height] = TraceExtent;
        const Usage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
        const Construct = (Label) => this.Exchange.ConstructTexture({ label: Label, size: TraceExtent, format: "rgba16float", usage: Usage });
        const Guides = [Construct("Resampling guide A"), Construct("Resampling guide B")];
        const Irradiance = Construct("Resampled irradiance");
        const Reflection = Construct("Reflection radiance");
        const Reservoirs = [0, 1].map((Slot) => this.Exchange.ConstructRecords(Width * Height * ReservoirStride,
                                                                               GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, `Reservoirs ${Slot}`));
        this.Resources = [...Guides, Irradiance, Reflection, ...Reservoirs];
        this.TraceExtent = TraceExtent;
        this.Views = { Guides: Guides.map((Texture) => Texture.createView()), Irradiance: Irradiance.createView(),
                       Reflection: Reflection.createView() };
        this.Reservoirs = Reservoirs;
        this.TemporalGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.TemporalLayout,
            [this.FrameRecords, Surface.Views.Attributes, Surface.Views.Depth, Surface.Views.Motion, this.Views.Guides[1 - Parity],
             this.Views.Guides[Parity], Reservoirs[1 - Parity], Reservoirs[Parity]]));
        this.SpatialGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.SpatialLayout,
            [this.FrameRecords, this.Views.Guides[Parity], Surface.Views.Depth, Reservoirs[Parity], this.Views.Irradiance]));
        this.ReflectionGroup = this.Exchange.ConstructDescriptorGroup(this.ReflectionLayout,
            [this.FrameRecords, this.MaterialRecords, Surface.Views.Attributes, Surface.Views.Depth, this.Views.Reflection,
             RayTracing.NodeBuffer, RayTracing.TriangleBuffer]);
    }

    RecordResampling(Encoder, Parity, DistanceGroup, SamplingGroup, ScreenGroup, TimestampWrites)
    {
        const [Width, Height] = this.TraceExtent;
        const Pass = Encoder.beginComputePass({ label: "ReSTIR GI", timestampWrites: TimestampWrites });
        Pass.setBindGroup(1, DistanceGroup);
        Pass.setBindGroup(2, SamplingGroup);
        Pass.setBindGroup(3, ScreenGroup);
        Pass.setPipeline(this.TemporalProgram);
        Pass.setBindGroup(0, this.TemporalGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Pass.setPipeline(this.SpatialProgram);
        Pass.setBindGroup(0, this.SpatialGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Pass.end();
    }

    RecordReflection(Encoder, DistanceGroup, SamplingGroup, ScreenGroup, TimestampWrites)
    {
        const [Width, Height] = this.TraceExtent;
        const Pass = Encoder.beginComputePass({ label: "Reflection traversal", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.ReflectionProgram);
        Pass.setBindGroup(0, this.ReflectionGroup);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.setBindGroup(2, SamplingGroup);
        Pass.setBindGroup(3, ScreenGroup);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Pass.end();
    }
}
