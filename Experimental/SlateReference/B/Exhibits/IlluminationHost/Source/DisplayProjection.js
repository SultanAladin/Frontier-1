//============================================================================================================================================
//                                                           DISPLAYPROJECTION.JS
//============================================================================================================================================
// 🧩 Recombination, temporal antialiasing, eye adaptation, the distance-field inspection march and the swapchain present.

export class DisplayProjection
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
    }

    // 📝 OccupantRecords: the recombination finds the car's cabin through the body's world → local transform.
    async ConstructPrograms(FrameRecords, MaterialRecords, DistanceLayout, SamplingLayout, OccupantRecords)
    {
        this.FrameRecords    = FrameRecords;
        this.MaterialRecords = MaterialRecords;
        this.OccupantRecords = OccupantRecords;
        const Compute = GPUShaderStage.COMPUTE;
        this.RecombineLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "Texture2D", "TextureDepth2D", "Texture2D", "Texture2D", "Texture2D", "Texture2D", "Texture2D",
             "Texture2D", "StorageTexture2D:rgba16float", "StorageRead", "Texture2D", "Sampler", "Texture2D", "StorageTexture2D:rgba16float"], Compute, "Display recombination");
        this.RefractLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "Texture2D", "TextureDepth2D", "Texture2D", "StorageTexture2D:rgba16float"], Compute, "Display refraction");
        this.AntialiasLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture2D", "Texture2D", "Texture2D", "Sampler", "StorageTexture2D:rgba16float"], Compute, "Display antialias");
        this.ExposureLayout = this.Exchange.ConstructDescriptorLayout(["Uniform", "Texture2D", "Storage"], Compute, "Display exposure");
        this.InspectionLayout = this.Exchange.ConstructDescriptorLayout(["Uniform", "StorageTexture2D:rgba16float"], Compute, "Inspection march");
        this.PresentLayout = this.Exchange.ConstructDescriptorLayout(["Uniform", "Texture2D", "StorageRead"],
                                                                     GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, "Display present");
        this.RecombineProgram  = await this.Shaders.ConstructComputeProgram("DisplayRecombination.wgsl", "ResolveRecombination", [this.RecombineLayout]);
        this.RefractProgram    = await this.Shaders.ConstructComputeProgram("DisplayRefraction.wgsl", "ResolveRefraction", [this.RefractLayout]);
        this.AntialiasProgram  = await this.Shaders.ConstructComputeProgram("DisplayAntialias.wgsl", "IntegrateAntialias", [this.AntialiasLayout]);
        this.ExposureProgram   = await this.Shaders.ConstructComputeProgram("DisplayExposure.wgsl", "IntegrateExposure", [this.ExposureLayout]);
        this.InspectionProgram = await this.Shaders.ConstructComputeProgram("InspectionMarch.wgsl", "MarchInspection",
                                                                            [this.InspectionLayout, DistanceLayout, SamplingLayout]);
        this.PresentProgram = await this.Shaders.ConstructRasterProgram("DisplayPresent.wgsl", {
            VertexEntry: "PresentVertex", FragmentEntry: "PresentFragment",
            Primitive: { topology: "triangle-list" },
            Targets: [{ format: `${this.Exchange.CanvasFormat}-srgb` }]
        }, [this.PresentLayout, DistanceLayout]);
        this.LinearClamp = this.Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear" });
        this.AdaptationRecords = this.Exchange.ConstructRecords(16, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC,
                                                                "Eye adaptation");
    }

    ConstructTargets(ScreenExtent, Surface, Resampling, Diffuse, Glossy, Cards)
    {
        for (const Texture of this.Textures || [])
            Texture.destroy();
        const Usage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
        const Construct = (Label, Extra = 0) => this.Exchange.ConstructTexture({ label: Label, size: ScreenExtent, format: "rgba16float", usage: Usage | Extra });
        // 📝 Recombined is also a render target (the cabin overlay draws glass, screens and mirrors into it before the
        //    temporal antialiasing); the resolved pair can be copied out, and Post takes what is drawn after it (sparks).
        const Recombined = Construct("Recombined radiance", GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC);
        // 📝 The refraction pass reads a copy of the recombined radiance (the backdrop) and writes the result back into Recombined.
        const Backdrop   = Construct("Refraction backdrop", GPUTextureUsage.COPY_DST);
        const Inspection = Construct("Inspection radiance");
        const Resolved   = [Construct("Resolved radiance A", GPUTextureUsage.COPY_SRC), Construct("Resolved radiance B", GPUTextureUsage.COPY_SRC)];
        const Post       = Construct("Post radiance", GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST);
        // 📝 Diffuse exitance per pixel, never an inspection signal: the next frame's screen traces read it (DisplayRecombination.wgsl).
        const Exitance   = Construct("Diffuse exitance");
        this.Textures = [Recombined, Backdrop, Inspection, ...Resolved, Post, Exitance];
        this.ResolvedTextures = Resolved;
        this.PostTexture = Post;
        this.ScreenExtent = ScreenExtent;
        this.RecombinedTexture = Recombined;
        this.BackdropTexture = Backdrop;
        this.Views = { Recombined: Recombined.createView(), Backdrop: Backdrop.createView(), Inspection: Inspection.createView(), Resolved: Resolved.map((Texture) => Texture.createView()),
                       Post: Post.createView(), Exitance: Exitance.createView() };
        if (!this.EmptySmoke) this.EmptySmoke = this.Exchange.ConstructTexture({ label: "Idle plume", size: [1,1], format: "rgba16float", usage: GPUTextureUsage.TEXTURE_BINDING });
        this.RecombineResources = [this.FrameRecords, this.MaterialRecords, Surface.Views.Attributes, Surface.Views.Depth, Surface.Views.Direct, Diffuse.Views.Screen,
             Glossy.Views.Screen, Resampling.Views.Irradiance, this.Views.Inspection, Cards.Views.Final, this.Views.Recombined, this.OccupantRecords, this.EmptySmoke.createView(), this.LinearClamp,
             Surface.Views.DirectIrradiance, this.Views.Exitance];
        this.AssignSmoke(this.EmptySmoke.createView());
        this.AntialiasGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.AntialiasLayout,
            [this.FrameRecords, this.Views.Recombined, this.Views.Resolved[1 - Parity], Surface.Views.Motion, this.LinearClamp, this.Views.Resolved[Parity]]));
        this.ExposureGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.ExposureLayout,
            [this.FrameRecords, this.Views.Resolved[Parity], this.AdaptationRecords]));
        this.PresentGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.PresentLayout,
            [this.FrameRecords, this.Views.Resolved[Parity], this.AdaptationRecords]));
        this.RefractGroup = this.Exchange.ConstructDescriptorGroup(this.RefractLayout,
            [this.FrameRecords, this.MaterialRecords, Surface.Views.Attributes, Surface.Views.Depth, this.Views.Backdrop, this.Views.Recombined]);
        this.InspectionGroup = this.Exchange.ConstructDescriptorGroup(this.InspectionLayout, [this.FrameRecords, this.Views.Inspection]);
        this.PostGroups = {
            Exposure: this.Exchange.ConstructDescriptorGroup(this.ExposureLayout, [this.FrameRecords, this.Views.Post, this.AdaptationRecords]),
            Present:  this.Exchange.ConstructDescriptorGroup(this.PresentLayout, [this.FrameRecords, this.Views.Post, this.AdaptationRecords])
        };
        this.ComposedGroups = null;
        this.ComposedTexture = null;
    }

    AssignSmoke(View) {
        this.RecombineResources[12] = View;
        this.RecombineGroup = this.Exchange.ConstructDescriptorGroup(this.RecombineLayout, this.RecombineResources);
    }

    // 📝 A pass that composes over the resolved radiance (the smoke) writes its own screen target; exposure and the
    //    present read that target on the frames it is recorded, and the resolved history stays untouched.
    AssignComposed(ComposedView, ComposedTexture = null)
    {
        this.ComposedTexture = ComposedTexture;
        this.ComposedGroups = {
            Exposure: this.Exchange.ConstructDescriptorGroup(this.ExposureLayout, [this.FrameRecords, ComposedView, this.AdaptationRecords]),
            Present:  this.Exchange.ConstructDescriptorGroup(this.PresentLayout, [this.FrameRecords, ComposedView, this.AdaptationRecords])
        };
    }

    RecordRefraction(Encoder)
    {
        const [Width, Height] = this.ScreenExtent;
        Encoder.copyTextureToTexture({ texture: this.RecombinedTexture }, { texture: this.BackdropTexture }, [Width, Height]);
        const Pass = Encoder.beginComputePass({ label: "Display refraction" });
        Pass.setPipeline(this.RefractProgram);
        Pass.setBindGroup(0, this.RefractGroup);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Pass.end();
    }

    RecordInspection(Encoder, DistanceGroup, SamplingGroup)
    {
        const Pass = Encoder.beginComputePass({ label: "Inspection march" });
        Pass.setPipeline(this.InspectionProgram);
        Pass.setBindGroup(0, this.InspectionGroup);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.setBindGroup(2, SamplingGroup);
        Pass.dispatchWorkgroups(Math.ceil(this.ScreenExtent[0] / 8), Math.ceil(this.ScreenExtent[1] / 8));
        Pass.end();
    }

    // 📝 Composer, when given, records its own passes between temporal antialiasing and exposure and returns whether
    //    it wrote the composed target; without one the resolve is the single pass it always was.
    //    Overlay(Encoder, RecombinedView) draws over the recombined radiance before the antialiasing (the cabin: see-
    //    through glass, the HMI screens, the mirrors — antialiased and reprojected with the rest). PostOverlay(Encoder,
    //    PostView) draws additively after it (sparks: thin, bright and fast — temporal accumulation would dim them to
    //    a tenth); the radiance so far is copied into Post first, and exposure and the present read Post.
    RecordResolve(Encoder, Parity, TimestampWrites, Composer = null, Overlay = null, PostOverlay = null)
    {
        const [Width, Height] = this.ScreenExtent;
        let Pass = Encoder.beginComputePass({ label: "Display resolve", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.RecombineProgram);
        Pass.setBindGroup(0, this.RecombineGroup);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        // 📝 Glass refracts what the recombination left behind it: the backdrop is copied out, then only refractive pixels are rewritten.
        Pass.end();
        this.RecordRefraction(Encoder);
        if (Overlay)
        {
            Overlay(Encoder, this.Views.Recombined);
        }
        Pass = Encoder.beginComputePass({ label: "Display antialias" });
        Pass.setPipeline(this.AntialiasProgram);
        Pass.setBindGroup(0, this.AntialiasGroups[Parity]);
        Pass.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        this.Composed = false;
        this.Posted = false;
        if ((Composer && this.ComposedGroups) || PostOverlay)
        {
            Pass.end();
            if (Composer && this.ComposedGroups)
                this.Composed = Composer(Encoder, Parity);
            let Groups = this.Composed ? this.ComposedGroups : { Exposure: this.ExposureGroups[Parity], Present: this.PresentGroups[Parity] };
            if (PostOverlay)
            {
                const Source = this.Composed && this.ComposedTexture ? this.ComposedTexture : this.ResolvedTextures[Parity];
                Encoder.copyTextureToTexture({ texture: Source }, { texture: this.PostTexture }, [Width, Height]);
                PostOverlay(Encoder, this.Views.Post);
                this.Posted = true;
                Groups = this.PostGroups;
            }
            this.FrameGroups = Groups;
            const Exposure = Encoder.beginComputePass({ label: "Display exposure" });
            Exposure.setPipeline(this.ExposureProgram);
            Exposure.setBindGroup(0, Groups.Exposure);
            Exposure.dispatchWorkgroups(1);
            Exposure.end();
            return;
        }
        this.FrameGroups = null;
        Pass.setPipeline(this.ExposureProgram);
        Pass.setBindGroup(0, this.ExposureGroups[Parity]);
        Pass.dispatchWorkgroups(1);
        Pass.end();
    }

    RecordPresent(Encoder, Parity, TargetView, DistanceGroup)
    {
        const Pass = Encoder.beginRenderPass({
            label: "Display present",
            colorAttachments: [{ view: TargetView, clearValue: [0, 0, 0, 1], loadOp: "clear", storeOp: "store" }]
        });
        Pass.setPipeline(this.PresentProgram);
        Pass.setBindGroup(0, this.FrameGroups ? this.FrameGroups.Present : this.PresentGroups[Parity]);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.draw(3);
        Pass.end();
    }
}
