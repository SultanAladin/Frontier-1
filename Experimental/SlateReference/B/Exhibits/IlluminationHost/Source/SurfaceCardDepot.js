//============================================================================================================================================
//                                                           SURFACECARDDEPOT.JS
//============================================================================================================================================
// 🧩 The surface cache: oriented cards per occupant in one texel space, captured once, relit by a scheduled tile budget.

import { OccupantRole, VertexStride } from "./FacetStructure.js";

const MinimumCardTexels = 4;     // [texel] - shortest card edge
const MaximumCardTexels = 256;   // [texel] - longest card edge
const CardStride        = 48;    // [B]     - CardRecord

export class SurfaceCardDepot
{
    // 📝 Configuration: Extent (card texel space edge), DensityScale, TileBudget (8×8 tiles relit per frame beyond the
    //    moving occupants' tiles, which are relit every frame).
    constructor(Exchange, Shaders, Configuration)
    {
        this.Exchange      = Exchange;
        this.Shaders       = Shaders;
        this.Configuration = Configuration;
        this.Cards         = [];
        this.DynamicTiles  = [];
        this.StaticTiles   = [];
        this.Cursor        = 0;
        this.WarmFrames    = 6;
        this.OccupantTiles = [];
        this.PriorityTiles = [];
        this.PriorityFrames = 0;
        this.ScheduledTiles = 0;
        this.OccupiedTexels = 0;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                               CARD ALLOCATION
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Six inward-looking cards on the faces of each occupant's local card bounds (one +Z card for a ground tile),
    //    sized by texel density and rounded to whole 4×4 radiosity sites, then shelf-packed. Should the packing
    //    overflow the texel space, every density is lowered together and the packing repeated.
    AllocateCards(Scene)
    {
        let DensityScale = this.Configuration.DensityScale;
        for (let Attempt = 0; Attempt < 12; ++Attempt)
        {
            this.Cards = [];
            for (let Index = 0; Index < Scene.Occupants.length; ++Index)
            {
                const Occupant = Scene.Occupants[Index];
                Occupant.FirstCard = this.Cards.length;
                Occupant.CardCount = 0;
                if (!(Occupant.Roles & OccupantRole.Cards))
                    continue;
                const Minimum = Occupant.CardMinimum.slice();
                const Maximum = Occupant.CardMaximum.slice();
                const Ground  = !(Occupant.Roles & OccupantRole.Distance);
                if (!Ground)
                {
                    for (let Axis = 0; Axis < 3; ++Axis)
                    {
                        const Grow = Math.max(0.01, 0.01 * (Maximum[Axis] - Minimum[Axis]));
                        Minimum[Axis] -= Grow;
                        Maximum[Axis] += Grow;
                    }
                }
                const Axes = Ground ? [4] : [0, 1, 2, 3, 4, 5];
                for (const Axis of Axes)
                {
                    const DepthAxis = Axis >> 1;
                    const AcrossU = (DepthAxis + 1) % 3, AcrossV = (DepthAxis + 2) % 3;
                    const Density = Occupant.CardDensity * DensityScale;
                    const Size = (Length) => Math.ceil(Math.min(MaximumCardTexels,
                                                       Math.max(MinimumCardTexels, Math.round(Length * Density))) / 4) * 4;
                    this.Cards.push({ LocalMinimum: Minimum, LocalMaximum: Maximum, Axis, Occupant: Index,
                                      TexelExtent: [Size(Maximum[AcrossU] - Minimum[AcrossU]), Size(Maximum[AcrossV] - Minimum[AcrossV])],
                                      TexelOrigin: [0, 0] });
                    Occupant.CardCount++;
                }
            }
            if (this.PackCards())
            {
                this.DensityScaleUsed = DensityScale;
                break;
            }
            DensityScale *= 0.8;
            if (Attempt === 11)
                throw new Error("Surface cards do not fit the card texel space");
        }

        this.GroundFirstCard = Scene.Occupants[Scene.GroundTiles.First].FirstCard;
        this.GroundStripCard = Scene.Occupants[Scene.GroundTiles.Strips[0]].FirstCard;

        this.OccupiedTexels = 0;
        this.TileTotal = 0;
        this.OccupantTiles = Scene.Occupants.map(() => []);
        this.Cards.forEach((Card, CardIndex) =>
        {
            this.OccupiedTexels += Card.TexelExtent[0] * Card.TexelExtent[1];
            for (let TileY = 0; TileY < Math.ceil(Card.TexelExtent[1] / 8); ++TileY)
            {
                for (let TileX = 0; TileX < Math.ceil(Card.TexelExtent[0] / 8); ++TileX)
                {
                    this.OccupantTiles[Card.Occupant].push([CardIndex, (TileY << 16) | TileX]);
                    this.TileTotal++;
                }
            }
        });
        this.RebuildTileLists(Scene);
    }

    // 📝 The relighting lists hold only enabled occupants: moving ones every frame, static ones round-robin. A disabled
    //    stress slot keeps its captured cards but costs no lighting work until it is enabled again. A Dynamic occupant
    //    with Moving false (a cone at rest) is relit round-robin like a static one until it is knocked.
    RebuildTileLists(Scene)
    {
        this.DynamicTiles = [];
        this.StaticTiles = [];
        Scene.Occupants.forEach((Occupant, Index) =>
        {
            if (Occupant.Inactive)
                return;
            const Target = ((Occupant.Roles & OccupantRole.Dynamic) && Occupant.Moving !== false) ? this.DynamicTiles : this.StaticTiles;
            for (const Assignment of this.OccupantTiles[Index])
                Target.push(Assignment);
        });
        if (this.Cursor >= this.StaticTiles.length)
            this.Cursor = 0;
    }

    // 📝 Newly enabled occupants are relit every frame for a few frames, so they arrive lit and with a few radiosity
    //    bounces instead of waiting for the round-robin to reach them.
    WarmOccupants(Scene, Indices, Frames = 6)
    {
        const Tiles = [];
        for (const Index of Indices)
        {
            // 📝 Moving occupants are relit every frame already; listing them twice would schedule a tile twice.
            if ((Scene.Occupants[Index].Roles & OccupantRole.Dynamic) && Scene.Occupants[Index].Moving !== false)
                continue;
            for (const Assignment of this.OccupantTiles[Index])
                Tiles.push(Assignment);
        }
        this.PriorityTiles = Tiles;
        this.PriorityFrames = Tiles.length > 0 ? Frames : 0;
    }

    PackCards()
    {
        const Extent = this.Configuration.Extent;
        const Order = this.Cards.map((Card, Index) => Index)
                                .sort((Left, Right) => this.Cards[Right].TexelExtent[1] - this.Cards[Left].TexelExtent[1]);
        let ShelfX = 0, ShelfY = 0, ShelfHeight = 0;
        for (const Index of Order)
        {
            const Card = this.Cards[Index];
            const [Width, Height] = Card.TexelExtent;
            if (ShelfX + Width > Extent)
            {
                ShelfY += ShelfHeight;
                ShelfX = 0;
                ShelfHeight = 0;
            }
            if (ShelfY + Height > Extent)
                return false;
            Card.TexelOrigin = [ShelfX, ShelfY];
            ShelfX += Width;
            ShelfHeight = Math.max(ShelfHeight, Height);
        }
        return true;
    }

    EncodeCardRecords()
    {
        const Bytes = new ArrayBuffer(Math.max(1, this.Cards.length) * CardStride);
        const Scalars = new Float32Array(Bytes);
        const Integers = new Uint32Array(Bytes);
        this.Cards.forEach((Card, Index) =>
        {
            const Offset = Index * (CardStride / 4);
            Scalars.set(Card.LocalMinimum, Offset);
            Integers[Offset + 3] = Card.Axis;
            Scalars.set(Card.LocalMaximum, Offset + 4);
            Integers[Offset + 7] = Card.Occupant;
            Integers[Offset + 8] = Card.TexelOrigin[0];
            Integers[Offset + 9] = Card.TexelOrigin[1];
            Integers[Offset + 10] = Card.TexelExtent[0];
            Integers[Offset + 11] = Card.TexelExtent[1];
        });
        return Scalars;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  RESOURCES
    //--------------------------------------------------------------------------------------------------------------------

    async ConstructResources(FrameRecords, OccupantRecords, MaterialRecords, DistanceLayout, LightRecords)
    {
        this.LightRecords = LightRecords;
        const Extent = this.Configuration.Extent;
        const Usage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING;
        const Construct = (Label, Format, Extra = 0) =>
            this.Exchange.ConstructTexture({ label: Label, size: [Extent, Extent], format: Format, usage: Usage | Extra });
        this.AlbedoTexture   = Construct("Card albedo", "rgba8unorm-srgb");
        this.NormalTexture   = Construct("Card normal", "rgba8unorm");
        this.EmissionTexture = Construct("Card emission", "rgba16float");
        this.DepthTexture    = Construct("Card depth", "r16float");
        this.DirectTexture   = this.Exchange.ConstructTexture({ label: "Card direct irradiance", size: [Extent, Extent], format: "rgba16float",
                                                                usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });
        this.FinalTexture    = this.Exchange.ConstructTexture({ label: "Card final radiance", size: [Extent, Extent], format: "rgba16float",
                                                                usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });
        this.Views = {
            Albedo:   this.AlbedoTexture.createView(),
            Normal:   this.NormalTexture.createView(),
            Emission: this.EmissionTexture.createView(),
            Depth:    this.DepthTexture.createView(),
            Direct:   this.DirectTexture.createView(),
            Final:    this.FinalTexture.createView()
        };
        this.SiteStride   = Extent / 4;
        this.CardRecords  = this.Exchange.ConstructRecordsFrom(this.EncodeCardRecords(), GPUBufferUsage.STORAGE, "Card records");
        this.SiteRecords  = this.Exchange.ConstructRecords(this.SiteStride * this.SiteStride * 16, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, "Radiosity sites");
        // 📝 Every tile once, plus the warm-up list, which may repeat tiles the round-robin also picks this frame.
        const TileCapacity = Math.max(1, 2 * this.TileTotal);
        this.TileRecords  = this.Exchange.ConstructRecords(TileCapacity * 8, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, "Card tiles");
        this.WorkRecords  = this.Exchange.ConstructRecords(16, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Card work");

        const Compute = GPUShaderStage.COMPUTE;
        this.DirectLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "StorageRead", "StorageRead", "Uniform", "Texture2D", "Texture2D", "StorageTexture2D:rgba16float", "StorageRead"], Compute, "Card direct");
        this.RadiosityLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "Uniform", "Texture2D", "Storage"], Compute, "Card radiosity");
        this.FinalLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "StorageRead", "StorageRead", "Uniform", "Texture2D", "Texture2D", "Texture2D", "Texture2D", "StorageRead",
             "StorageTexture2D:rgba16float"], Compute, "Card final");
        this.SamplingLayout = this.Exchange.ConstructDescriptorLayout(
            ["StorageRead", "StorageRead", "Texture2D", "Texture2D", "Texture2D", "Sampler"], Compute | GPUShaderStage.FRAGMENT, "Card sampling");
        this.EmptyLayout = this.Exchange.ConstructDescriptorLayout([], Compute, "Empty");

        this.DirectGroup = this.Exchange.ConstructDescriptorGroup(this.DirectLayout,
            [FrameRecords, this.CardRecords, OccupantRecords, this.TileRecords, this.WorkRecords, this.Views.Normal, this.Views.Depth, this.Views.Direct, this.LightRecords]);
        this.RadiosityGroup = this.Exchange.ConstructDescriptorGroup(this.RadiosityLayout,
            [FrameRecords, this.TileRecords, this.WorkRecords, this.Views.Normal, this.SiteRecords]);
        this.FinalGroup = this.Exchange.ConstructDescriptorGroup(this.FinalLayout,
            [FrameRecords, this.CardRecords, this.TileRecords, this.WorkRecords, this.Views.Albedo, this.Views.Emission, this.Views.Direct,
             this.Views.Depth, this.SiteRecords, this.Views.Final]);

        this.DirectProgram    = await this.Shaders.ConstructComputeProgram("CardDirect.wgsl", "IntegrateCardDirect", [this.DirectLayout, DistanceLayout]);
        this.RadiosityProgram = await this.Shaders.ConstructComputeProgram("CardRadiosity.wgsl", "IntegrateCardRadiosity",
                                                                           [this.RadiosityLayout, DistanceLayout, this.SamplingLayout]);
        this.FinalProgram     = await this.Shaders.ConstructComputeProgram("CardFinal.wgsl", "IntegrateCardFinal", [this.FinalLayout]);

        this.CaptureLayout = this.Exchange.ConstructDescriptorLayout(["StorageRead", "StorageRead", "StorageRead"],
                                                                     GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, "Card capture");
        this.CaptureGroup = this.Exchange.ConstructDescriptorGroup(this.CaptureLayout, [this.CardRecords, OccupantRecords, MaterialRecords]);
        this.CaptureProgram = await this.Shaders.ConstructRasterProgram("CardCapture.wgsl", {
            VertexEntry: "CaptureVertex", FragmentEntry: "CaptureFragment",
            VertexLayouts: [{ arrayStride: VertexStride, attributes: [
                { shaderLocation: 0, offset: 0,  format: "float32x3" },
                { shaderLocation: 1, offset: 12, format: "uint32" },
                { shaderLocation: 2, offset: 16, format: "float32x3" }] }],
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "less" },
            Targets: [{ format: "rgba8unorm-srgb" }, { format: "rgba8unorm" }, { format: "rgba16float" }, { format: "r16float" }]
        }, [this.CaptureLayout]);
    }

    ConstructSamplingGroup(OccupantRecords, SkyView, SkySampler)
    {
        return this.Exchange.ConstructDescriptorGroup(this.SamplingLayout,
            [OccupantRecords, this.CardRecords, this.Views.Depth, this.Views.Final, SkyView, SkySampler]);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   CAPTURE
    //--------------------------------------------------------------------------------------------------------------------

    CaptureCards(Scene, VertexRecords)
    {
        const Device = this.Exchange.Device;
        const Extent = this.Configuration.Extent;
        const DepthAttachment = this.Exchange.ConstructTexture({ label: "Card capture depth", size: [Extent, Extent],
                                                                 format: "depth32float", usage: GPUTextureUsage.RENDER_ATTACHMENT });
        const Encoder = Device.createCommandEncoder({ label: "Card capture" });
        const Clear = (View, Value) => ({ view: View, clearValue: Value, loadOp: "clear", storeOp: "store" });
        const Pass = Encoder.beginRenderPass({
            label: "Card capture",
            colorAttachments: [Clear(this.Views.Albedo, [0, 0, 0, 0]), Clear(this.Views.Normal, [0.5, 0.5, 1, 0]),
                               Clear(this.Views.Emission, [0, 0, 0, 0]), Clear(this.Views.Depth, [4, 0, 0, 0])],
            depthStencilAttachment: { view: DepthAttachment.createView(), depthClearValue: 1.0, depthLoadOp: "clear", depthStoreOp: "discard" }
        });
        Pass.setPipeline(this.CaptureProgram);
        Pass.setBindGroup(0, this.CaptureGroup);
        Pass.setVertexBuffer(0, VertexRecords);
        this.Cards.forEach((Card, Index) =>
        {
            const Set = Scene.FacetSets[Scene.Occupants[Card.Occupant].FacetSet];
            Pass.setViewport(Card.TexelOrigin[0], Card.TexelOrigin[1], Card.TexelExtent[0], Card.TexelExtent[1], 0.0, 1.0);
            Pass.setScissorRect(Card.TexelOrigin[0], Card.TexelOrigin[1], Card.TexelExtent[0], Card.TexelExtent[1]);
            Pass.draw(Set.VertexCount, 1, Set.FirstVertex, Index);
        });
        Pass.end();
        Device.queue.submit([Encoder.finish()]);
        DepthAttachment.destroy();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              LIGHTING SCHEDULE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 A step change of the light (a preset, day to night) would otherwise drain out of the radiosity sites at the
    //    weight floor over seconds. The sites are cleared, so their next gather is taken whole, and every tile is relit.
    InvalidateLighting()
    {
        this.Exchange.Device.queue.writeBuffer(this.SiteRecords, 0, new Float32Array(this.SiteStride * this.SiteStride * 4));
        this.WarmFrames = Math.max(this.WarmFrames, 3);
    }

    // 📝 Moving occupants' tiles every frame; static tiles round-robin within the budget. The first WarmFrames relight
    //    everything so the depot starts lit and its radiosity has taken several bounces before the first image.
    ScheduleTiles(TileBudget)
    {
        const Scheduled = this.DynamicTiles.slice();
        if (this.WarmFrames > 0)
        {
            Scheduled.push(...this.StaticTiles);
            this.WarmFrames--;
        }
        else
        {
            // 📝 A tile must not be scheduled twice in one frame: the radiosity pass folds into its sites in place.
            const Priority = this.PriorityFrames > 0 ? this.PriorityTiles : [];
            const Claimed = new Set(Priority.map((Assignment) => Assignment[0] * 4194304 + Assignment[1]));
            Scheduled.push(...Priority);
            if (this.PriorityFrames > 0)
                this.PriorityFrames--;
            const Budget = Math.min(this.StaticTiles.length, Math.max(0, TileBudget));
            for (let Index = 0; Index < Budget; ++Index)
            {
                const Assignment = this.StaticTiles[this.Cursor];
                this.Cursor = (this.Cursor + 1) % this.StaticTiles.length;
                if (!Claimed.has(Assignment[0] * 4194304 + Assignment[1]))
                    Scheduled.push(Assignment);
            }
        }
        const Packed = new Uint32Array(Scheduled.length * 2);
        Scheduled.forEach((Assignment, Index) =>
        {
            Packed[Index * 2] = Assignment[0];
            Packed[Index * 2 + 1] = Assignment[1];
        });
        this.ScheduledTiles = Scheduled.length;
        const Device = this.Exchange.Device;
        if (Scheduled.length > 0)
            Device.queue.writeBuffer(this.TileRecords, 0, Packed);
        const Work = new ArrayBuffer(16);
        new Uint32Array(Work, 0, 2).set([Scheduled.length, this.SiteStride]);
        new Float32Array(Work, 8, 2).set([0.15, 0.35]);
        Device.queue.writeBuffer(this.WorkRecords, 0, Work);
    }

    RecordLighting(Encoder, DistanceGroup, SamplingGroup, RadiosityEnabled, TimestampWrites)
    {
        if (this.ScheduledTiles === 0)
            return;
        const GroupsX = Math.min(this.ScheduledTiles, 65535);
        const GroupsY = Math.ceil(this.ScheduledTiles / 65535);
        const Pass = Encoder.beginComputePass({ label: "Card lighting", timestampWrites: TimestampWrites });
        Pass.setPipeline(this.DirectProgram);
        Pass.setBindGroup(0, this.DirectGroup);
        Pass.setBindGroup(1, DistanceGroup);
        Pass.dispatchWorkgroups(GroupsX, GroupsY);
        if (RadiosityEnabled)
        {
            Pass.setPipeline(this.RadiosityProgram);
            Pass.setBindGroup(0, this.RadiosityGroup);
            Pass.setBindGroup(1, DistanceGroup);
            Pass.setBindGroup(2, SamplingGroup);
            Pass.dispatchWorkgroups(GroupsX, GroupsY);
        }
        Pass.setPipeline(this.FinalProgram);
        Pass.setBindGroup(0, this.FinalGroup);
        Pass.dispatchWorkgroups(GroupsX, GroupsY);
        Pass.end();
    }
}
