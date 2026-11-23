//============================================================================================================================================
//                                                          NEARESTDEPTHSPACE.JS
//============================================================================================================================================
// 🧩 The nearest-depth pyramid over the screen — recursively halved tiles, each holding its nearest depth — kept two deep.

// 📝 Rebuilt from the depth attachment every frame into the slot of the frame's parity, so the other slot still holds the
//    previous frame's pyramid: the screen traces walk the current one and confirm reprojections against the prior one.
//    Group 3 of every screen-tracing program is constructed here: both pyramids, the surface attributes (normal and
//    occupant identity) and the previous frame's diffuse exitance.

export class NearestDepthSpace
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Textures = [];
    }

    async ConstructPrograms()
    {
        const Compute = GPUShaderStage.COMPUTE;
        this.TransferLayout  = this.Exchange.ConstructDescriptorLayout(["TextureDepth2D", "StorageTexture2D:r32float"], Compute,
                                                                       "Nearest depth transfer");
        this.ReductionLayout = this.Exchange.ConstructDescriptorLayout(["Texture2DUnfiltered", "StorageTexture2D:r32float"], Compute,
                                                                       "Nearest depth reduction");
        this.TraversalLayout = this.Exchange.ConstructDescriptorLayout(
            ["Texture2DUnfiltered", "Texture2DUnfiltered", "Texture2D", "Texture2D", "Texture2D"], Compute, "Screen traversal");
        // 📝 Reflections also trace the triangle hierarchy: the same group 3 plus its two buffers (bindings 5 and 6).
        this.TriangleTraversalLayout = this.Exchange.ConstructDescriptorLayout(
            ["Texture2DUnfiltered", "Texture2DUnfiltered", "Texture2D", "Texture2D", "Texture2D", "StorageRead", "StorageRead"],
            Compute, "Screen and triangle traversal");
        this.TransferProgram  = await this.Shaders.ConstructComputeProgram("NearestDepthTransfer.wgsl", "TransferNearestDepth",
                                                                           [this.TransferLayout]);
        this.ReductionProgram = await this.Shaders.ConstructComputeProgram("NearestDepthReduction.wgsl", "ReduceNearestDepth",
                                                                           [this.ReductionLayout]);
    }

    // 📝 Level extents follow max(1, ⌊extent / 2^L⌋) down to a single texel, the chain WebGPU itself defines.
    ConstructTargets(ScreenExtent, Surface)
    {
        for (const Texture of this.Textures)
            Texture.destroy();
        const [Width, Height] = ScreenExtent;
        this.Extent = ScreenExtent;
        this.LevelCount = Math.floor(Math.log2(Math.max(Width, Height))) + 1;
        this.LevelExtents = [];
        for (let Level = 0; Level < this.LevelCount; ++Level)
            this.LevelExtents.push([Math.max(1, Width >> Level), Math.max(1, Height >> Level)]);
        const Usage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
        this.Textures = ["A", "B"].map((Slot) => this.Exchange.ConstructTexture({ label: `Nearest depth ${Slot}`, size: [Width, Height],
                                                                                   format: "r32float", mipLevelCount: this.LevelCount,
                                                                                   usage: Usage }));
        this.Views = this.Textures.map((Texture) => Texture.createView());
        const LevelView = (Texture, Level) => Texture.createView({ baseMipLevel: Level, mipLevelCount: 1 });
        this.TransferGroups = this.Textures.map((Texture) => this.Exchange.ConstructDescriptorGroup(this.TransferLayout,
            [Surface.Views.Depth, LevelView(Texture, 0)], "Nearest depth transfer"));
        this.ReductionGroups = this.Textures.map((Texture) =>
        {
            const Groups = [];
            for (let Level = 1; Level < this.LevelCount; ++Level)
                Groups.push(this.Exchange.ConstructDescriptorGroup(this.ReductionLayout,
                                                                   [LevelView(Texture, Level - 1), LevelView(Texture, Level)],
                                                                   `Nearest depth level ${Level}`));
            return Groups;
        });
    }

    // 📝 Needs the recombination's exitance target, so it follows DisplayProjection.ConstructTargets. Triangles: the
    //    TriangleHierarchySpace whose buffers the reflection group binds (its ConstructResources runs first).
    ConstructTraversalGroups(Surface, Display, Triangles)
    {
        this.TraversalGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.TraversalLayout,
            [this.Views[Parity], this.Views[1 - Parity], Surface.Views.Attributes, Display.Views.Exitance, Surface.Views.Identity],
            `Screen traversal ${Parity}`));
        this.TriangleTraversalGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.TriangleTraversalLayout,
            [this.Views[Parity], this.Views[1 - Parity], Surface.Views.Attributes, Display.Views.Exitance, Surface.Views.Identity,
             Triangles.NodeRecords, Triangles.TriangleRecords], `Screen and triangle traversal ${Parity}`));
    }

    // 📝 One compute pass: level 0 from the depth attachment, then each level from the one beneath. Every dispatch is its
    //    own usage scope, so level L written as storage is visible to the dispatch that samples it for level L + 1.
    RecordReduction(Encoder, Parity, TimestampWrites)
    {
        const [Width, Height] = this.Extent;
        const Reduction = Encoder.beginComputePass({ label: "Nearest depth reduction", timestampWrites: TimestampWrites });
        Reduction.setPipeline(this.TransferProgram);
        Reduction.setBindGroup(0, this.TransferGroups[Parity]);
        Reduction.dispatchWorkgroups(Math.ceil(Width / 8), Math.ceil(Height / 8));
        Reduction.setPipeline(this.ReductionProgram);
        for (let Level = 1; Level < this.LevelCount; ++Level)
        {
            const [LevelWidth, LevelHeight] = this.LevelExtents[Level];
            Reduction.setBindGroup(0, this.ReductionGroups[Parity][Level - 1]);
            Reduction.dispatchWorkgroups(Math.ceil(LevelWidth / 8), Math.ceil(LevelHeight / 8));
        }
        Reduction.end();
    }
}
