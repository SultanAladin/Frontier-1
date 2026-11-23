//============================================================================================================================================
//                                                           SURFACEPROJECTION.JS
//============================================================================================================================================
// 🧩 Primary visibility (depth, attributes, identity, motion) and the direct radiance resolved over it, sized to the screen.

import { VertexStride } from "./FacetStructure.js";

export class SurfaceProjection
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Extent   = [0, 0];
    }

    // 📝 TyreMarks: the TyreMarkDepot whose deposit the raster reads for the road families.
    // 📝 Effects (EffectSequence) shares this pass: its record masks the car here, and its cubes draw after the runs.
    // 📝 VehicleRecords: the VehicleRecord the vertex stage deforms the tyres and turns the steering wheel by.
    // 📝 Identity: the third target, the occupant identity screen traces compare against (r16float, exact below 2048).
    async ConstructPrograms(FrameRecords, OccupantRecords, MaterialRecords, DistanceLayout, TyreMarks, Effects, VehicleRecords)
    {
        this.FrameRecords    = FrameRecords;
        this.OccupantRecords = OccupantRecords;
        this.MaterialRecords = MaterialRecords;
        this.Effects         = Effects;
        this.RasterLayout = this.Exchange.ConstructDescriptorLayout(["Uniform", "StorageRead", "Texture2D", "Sampler", "Uniform", "StorageRead"],
                                                                    GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, "Surface raster");
        this.RasterGroup = this.Exchange.ConstructDescriptorGroup(this.RasterLayout, [FrameRecords, OccupantRecords, TyreMarks.View, TyreMarks.Sampler,
                                                                                      Effects.Records, VehicleRecords]);
        this.RasterProgram = await this.Shaders.ConstructRasterProgram("SurfaceRaster.wgsl", {
            VertexEntry: "SurfaceVertex", FragmentEntry: "SurfaceFragment",
            VertexLayouts: [{ arrayStride: VertexStride, attributes: [
                { shaderLocation: 0, offset: 0,  format: "float32x3" },
                { shaderLocation: 1, offset: 12, format: "uint32" },
                { shaderLocation: 2, offset: 16, format: "float32x3" },
                { shaderLocation: 3, offset: 28, format: "uint32" }] }],
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "greater" },
            Targets: [{ format: "rgba16float" }, { format: "rg16float" }, { format: "r16float" }]
        }, [this.RasterLayout]);
        this.DirectLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "Texture2D", "TextureDepth2D", "TextureDepth2DLayered", "SamplerComparison", "Uniform", "Texture2D",
             "Sampler", "StorageTexture2D:rgba16float", "StorageRead", "StorageTexture2D:rgba16float"], GPUShaderStage.COMPUTE, "Direct radiance");
        // 📝 The direct pass traverses the scene distance field to shadow a tyre fire's light and each light in its grid cell (FireIllumination.wgsl).
        this.DirectProgram = await this.Shaders.ConstructComputeProgram("DirectRadiance.wgsl", "IntegrateDirectRadiance",
                                                                        [this.DirectLayout, DistanceLayout]);
    }

    // 📝 LightRecords: the LightClusterRecords buffer (LightClusterSpace.js) that the direct pass reads.
    ConstructTargets(Width, Height, Shadow, Atmosphere, LightRecords = null)
    {
        this.Extent = [Width, Height];
        if (LightRecords) this.LightRecords = LightRecords;
        for (const Texture of [this.DepthTexture, this.AttributeTexture, this.MotionTexture, this.IdentityTexture, this.DirectTexture,
                               this.DirectIrradianceTexture])
            if (Texture) Texture.destroy();
        const Attachment = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING;
        const Storage    = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
        this.DepthTexture     = this.Exchange.ConstructTexture({ label: "Surface depth", size: [Width, Height], format: "depth32float", usage: Attachment });
        this.AttributeTexture = this.Exchange.ConstructTexture({ label: "Surface attributes", size: [Width, Height], format: "rgba16float", usage: Attachment });
        this.MotionTexture    = this.Exchange.ConstructTexture({ label: "Surface motion", size: [Width, Height], format: "rg16float", usage: Attachment });
        this.IdentityTexture  = this.Exchange.ConstructTexture({ label: "Surface identity", size: [Width, Height], format: "r16float", usage: Attachment });
        this.DirectTexture    = this.Exchange.ConstructTexture({ label: "Direct radiance", size: [Width, Height], format: "rgba16float", usage: Storage });
        this.DirectIrradianceTexture = this.Exchange.ConstructTexture({ label: "Direct irradiance", size: [Width, Height], format: "rgba16float",
                                                                        usage: Storage });
        this.Views = {
            Depth:      this.DepthTexture.createView(),
            Attributes: this.AttributeTexture.createView(),
            Motion:     this.MotionTexture.createView(),
            Identity:   this.IdentityTexture.createView(),
            Direct:     this.DirectTexture.createView(),
            DirectIrradiance: this.DirectIrradianceTexture.createView()
        };
        this.DirectGroup = this.Exchange.ConstructDescriptorGroup(this.DirectLayout,
            [this.FrameRecords, this.MaterialRecords, this.Views.Attributes, this.Views.Depth, Shadow.LayeredView, Shadow.ComparisonSampler,
             Shadow.ReceiverRecords, Atmosphere.SkyView, Atmosphere.SkySampler, this.Views.Direct, this.LightRecords, this.Views.DirectIrradiance]);
    }

    RecordSurface(Encoder, Scene, VertexRecords, TimestampWrites)
    {
        const Pass = Encoder.beginRenderPass({
            label: "Surface projection",
            colorAttachments: [
                { view: this.Views.Attributes, clearValue: [0, 0, 0, 0], loadOp: "clear", storeOp: "store" },
                { view: this.Views.Motion, clearValue: [0, 0, 0, 0], loadOp: "clear", storeOp: "store" },
                { view: this.Views.Identity, clearValue: [0, 0, 0, 0], loadOp: "clear", storeOp: "store" }],
            depthStencilAttachment: { view: this.Views.Depth, depthClearValue: 0.0, depthLoadOp: "clear", depthStoreOp: "store" },
            timestampWrites: TimestampWrites
        });
        Pass.setPipeline(this.RasterProgram);
        Pass.setBindGroup(0, this.RasterGroup);
        Pass.setVertexBuffer(0, VertexRecords);
        for (const Run of Scene.DrawRuns)
        {
            const Set = Scene.FacetSets[Run.FacetSet];
            Pass.draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
        }
        if (this.Effects)
            this.Effects.Record(Pass);
        Pass.end();
    }

    RecordDirect(Encoder, DistanceGroup, TimestampWrites)
    {
        const Pass = Encoder.beginComputePass({ label: "Direct radiance", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.DirectProgram);
        Pass.setBindGroup(0, this.DirectGroup);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.dispatchWorkgroups(Math.ceil(this.Extent[0] / 8), Math.ceil(this.Extent[1] / 8));
        Pass.end();
    }
}
