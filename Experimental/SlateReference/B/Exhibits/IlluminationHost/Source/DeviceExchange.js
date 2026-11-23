//============================================================================================================================================
//                                                            DEVICEEXCHANGE.JS
//============================================================================================================================================
// 🧩 WebGPU adapter and device negotiation, descriptor layout construction and resource creation across the vendor edge.

//------------------------------------------------------------------------------------------------------------------------
//                                                CAPABILITY NEGOTIATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Optional capabilities are requested only when the adapter offers them; nothing in the renderer depends on one.
const OptionalFeatures = ["timestamp-query"];

// 📝 Limits raised to the adapter's own maximum. The exhibit is written to the default limits, so these only add headroom.
const RaisedLimits = ["maxStorageBuffersPerShaderStage", "maxComputeWorkgroupStorageSize", "maxBufferSize",
                      "maxStorageBufferBindingSize", "maxSampledTexturesPerShaderStage", "maxTextureDimension3D"];

export class DeviceExchange
{
    constructor()
    {
        this.Adapter          = null;
        this.Device           = null;
        this.CanvasFormat     = "bgra8unorm";
        this.TimestampEnabled = false;
        this.ReportedErrors   = [];
        this.ErrorListeners   = [];
    }

    async ConstructDevice()
    {
        if (!("gpu" in navigator))
            throw new Error("This browser does not expose WebGPU (navigator.gpu is absent). Use a current Chrome, Edge or Safari.");
        this.Adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
        if (!this.Adapter)
            throw new Error("No WebGPU adapter is available on this system.");

        const RequiredFeatures = OptionalFeatures.filter((Feature) => this.Adapter.features.has(Feature));
        const RequiredLimits = {};
        for (const Limit of RaisedLimits)
        {
            if (Limit in this.Adapter.limits)
                RequiredLimits[Limit] = this.Adapter.limits[Limit];
        }

        this.Device = await this.Adapter.requestDevice({ requiredFeatures: RequiredFeatures, requiredLimits: RequiredLimits });
        this.TimestampEnabled = this.Device.features.has("timestamp-query");
        this.CanvasFormat = navigator.gpu.getPreferredCanvasFormat();

        this.Device.addEventListener("uncapturederror", (Event) =>
        {
            const Message = Event.error && Event.error.message ? Event.error.message : String(Event.error);
            if (this.ReportedErrors.length < 64)
                this.ReportedErrors.push(Message);
            console.error("[WebGPU]", Message);
            for (const Listener of this.ErrorListeners)
                Listener(Message);
        });
        this.Device.lost.then((Loss) =>
        {
            console.error("[WebGPU] device lost:", Loss.reason, Loss.message);
            for (const Listener of this.ErrorListeners)
                Listener(`Device lost: ${Loss.message}`);
        });
        return this.Device;
    }

    QueryAdapterDescription()
    {
        const Information = this.Adapter.info || {};
        const Parts = [Information.vendor, Information.architecture, Information.device, Information.description];
        return Parts.filter((Part) => Part && Part.length > 0).join(" · ") || "WebGPU adapter";
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              DESCRIPTOR LAYOUTS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 A layout is declared as an ordered list of resource codes; the descriptor slot is the position in the list.
    //    Explicit layouts keep a slot valid even when one entry point of a module leaves it unreferenced.
    ConstructDescriptorLayout(ResourceCodes, Visibility = GPUShaderStage.COMPUTE, Label = "")
    {
        const Entries = ResourceCodes.map((Code, Index) => ProjectLayoutEntry(Code, Index, Visibility));
        return this.Device.createBindGroupLayout({ label: Label, entries: Entries });
    }

    ConstructDescriptorGroup(Layout, Resources, Label = "")
    {
        const Entries = Resources.map((Resource, Index) => ({ binding: Index, resource: ProjectGroupResource(Resource) }));
        return this.Device.createBindGroup({ label: Label, layout: Layout, entries: Entries });
    }

    ConstructProgramLayout(Layouts, Label = "")
    {
        return this.Device.createPipelineLayout({ label: Label, bindGroupLayouts: Layouts });
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              RESOURCE CREATION
    //--------------------------------------------------------------------------------------------------------------------

    ConstructRecords(ByteExtent, Usage, Label = "")
    {
        const Aligned = Math.max(16, Math.ceil(ByteExtent / 16) * 16);
        return this.Device.createBuffer({ label: Label, size: Aligned, usage: Usage });
    }

    ConstructRecordsFrom(TypedContent, Usage, Label = "")
    {
        const Aligned = Math.max(16, Math.ceil(TypedContent.byteLength / 16) * 16);
        const Records = this.Device.createBuffer({ label: Label, size: Aligned, usage: Usage | GPUBufferUsage.COPY_DST,
                                                   mappedAtCreation: true });
        new Uint8Array(Records.getMappedRange()).set(new Uint8Array(TypedContent.buffer, TypedContent.byteOffset,
                                                                    TypedContent.byteLength));
        Records.unmap();
        return Records;
    }

    ConstructTexture(Descriptor)
    {
        return this.Device.createTexture(Descriptor);
    }

    ConstructSampler(Descriptor)
    {
        return this.Device.createSampler(Descriptor);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                               LAYOUT ENTRY PROJECTION
//------------------------------------------------------------------------------------------------------------------------

function ProjectLayoutEntry(Code, Index, Visibility)
{
    const [Resource, Format] = Code.split(":");
    const Entry = { binding: Index, visibility: Visibility };
    switch (Resource)
    {
        case "Uniform":               Entry.buffer = { type: "uniform" }; break;
        case "UniformDynamic":        Entry.buffer = { type: "uniform", hasDynamicOffset: true }; break;
        case "StorageRead":           Entry.buffer = { type: "read-only-storage" }; break;
        case "Storage":               Entry.buffer = { type: "storage" }; break;
        case "Texture2D":             Entry.texture = { sampleType: "float", viewDimension: "2d" }; break;
        case "Texture2DUnfiltered":   Entry.texture = { sampleType: "unfilterable-float", viewDimension: "2d" }; break;
        case "Texture2DLayered":        Entry.texture = { sampleType: "float", viewDimension: "2d-array" }; break;
        case "Texture3D":             Entry.texture = { sampleType: "float", viewDimension: "3d" }; break;
        case "TextureUint2D":         Entry.texture = { sampleType: "uint", viewDimension: "2d" }; break;
        case "TextureDepth2D":        Entry.texture = { sampleType: "depth", viewDimension: "2d" }; break;
        case "TextureDepth2DLayered":   Entry.texture = { sampleType: "depth", viewDimension: "2d-array" }; break;
        case "StorageTexture2D":      Entry.storageTexture = { access: "write-only", format: Format, viewDimension: "2d" }; break;
        case "StorageTexture3D":      Entry.storageTexture = { access: "write-only", format: Format, viewDimension: "3d" }; break;
        case "Sampler":               Entry.sampler = { type: "filtering" }; break;
        case "SamplerUnfiltered":     Entry.sampler = { type: "non-filtering" }; break;
        case "SamplerComparison":     Entry.sampler = { type: "comparison" }; break;
        default: throw new Error(`Unknown descriptor resource code '${Code}'`);
    }
    return Entry;
}

function ProjectGroupResource(Resource)
{
    if (typeof GPUBuffer !== "undefined" && Resource instanceof GPUBuffer)
        return { buffer: Resource };
    return Resource;
}
