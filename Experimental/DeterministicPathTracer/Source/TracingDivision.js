/*====================================================================================================================================
                                                       TRACINGDIVISION.JS
====================================================================================================================================*/
// 🧩 WebGPU lifecycle owner: scene upload, deterministic accumulation dispatch, display resolve, readback and digest

import { RetrieveVehicleArchives } from './VehicleAssembly.js';
import { ConstructBoundingVolumes, ConstructBinaryTree, CollapseWideBounds, WideNodeWords, EmptyLink } from './BoundingVolumeConstruction.js';
import { StageTiming } from './StageTiming.js';
import { ConstructMaterialTable, PackMaterialTable, ConstructShowcaseSpheres, ConstructSoftboxLights, ResolveParkingBays, SurfaceCategory } from './StudioSceneAssembly.js';
import { ParseIesProfile, ResampleProfile } from './PhotometricProfile.js';
import { PhotometryFiles, ConstructPracticalLamps, ConstructLightTree, PackLightRecords } from './PracticalLighting.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const TracingUniformBytes  = 304;   // [B] - 19 × vec4
const TracingSlotBytes     = 512;   // [B] - 256-byte aligned slot per pass (0 trace, 1 reproject, 2 features)
const TracingSlotCount     = 3;     // [-] - Enables single-submit frame encoding with zero uniform overwrite stalls
const ThinDielectricFlag   = 0x80000000; // [-] - Bit 31 of TrianglePositions[t*3].w flags thin glass for 1-cycle opaque shadow early-out
const PointPatternCodes    = Object.freeze({ shared: 0, pixel: 1, bluenoise: 2 });   // 📝 Tracing.Realtime.x
const DisplayUniformBytes  = 32;    // [B] - 2 × vec4
const RasterUniformBytes   = 80;    // [B] - 5 × vec4
const InstanceRecordWords  = 8;     // [-]  - 2 × vec4u per instance: (Cosine, Sine, Tx, Tz) + (root, paint swap, rim swap)
const UnusedMaterialSlot   = 0xFFFF; // [-] - Sentinel for unchanged material slot in 16-bit packed instance swap words
const InstanceLeafLimit    = 1;     // [-]  - Exactly one instance per instance-tree leaf (the traversal relies on it)
const AccumulationRegions  = 4;     // [-]  - Exact sums, two ping-pong history regions, luminance moments
const ReconstructionRegions = 4;    // [-]  - Estimate, two à-trous ping-pong regions, display radiance
const ReconstructionSlotBytes = 256; // [B] - One uniform block per reconstruction dispatch (offset alignment)
const AtrousSteps          = [1, 2, 4, 8, 16];   // [px]
const DenoiseFullUntil     = 4;     // [spp] - Full filter strength up to here …
const DenoiseFadedAt       = 64;    // [spp] - … fading (log₂) to the exact image here
const FloorRadius          = 60.0;  // [m]
const WorkgroupEdge        = 8;     // [px]
const WaveWorkgroupSize    = 64;    // [-] - 1D wavefront compute workgroup size
const WaveUniformSlotBytes = 256;   // [B] - 256-byte dynamic uniform alignment per wave stage
const WaveMaxBatchTiles    = 32768; // [-] - Tiles per wavefront batch (1.125 MiB L2-resident 96-bit RGB accumulator)
const WaveQueueCapacity    = 131072;// [-] - Max active rays per wave depth (4 × WaveMaxBatchTiles)
const WaveShadowCapacity   = 524288;// [-] - Max shadow rays per wave depth (16 × WaveMaxBatchTiles)
const LampCutLimits        = Object.freeze({ tree: 1, split4: 4, split8: 8, all: 1 });   // 📝 Tracing.Shading.w
const NightSkyFraction     = 0.02;  // [-]  - Environment left on at night so unlit geometry stays readable


//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

async function RetrieveShaderSource(Name)
{
    const Response = await fetch(new URL(Name, import.meta.url));
    if (!Response.ok) throw new Error(`${Name}: HTTP ${Response.status}`);
    return Response.text();
}

// 📝 Two 64² void-and-cluster rank masks built offline by Tools/BlueNoiseConstruction.mjs (16 KiB, one r32uint texture).
async function RetrieveBlueNoiseRanks(AssetRoot)
{
    const Response = await fetch(new URL('BlueNoiseRanks.bin', AssetRoot));
    if (!Response.ok) throw new Error(`BlueNoiseRanks.bin: HTTP ${Response.status}`);
    const Ranks = new Uint32Array(await Response.arrayBuffer());
    if (Ranks.length !== 4096) throw new Error(`BlueNoiseRanks.bin: expected 4096 texels, found ${Ranks.length}`);
    return Ranks;
}

// 📝 IES files (Assets/Photometry, written by Tools/PhotometryConstruction.mjs) parsed and resampled once.
async function RetrievePhotometry(AssetRoot)
{
    return Promise.all(PhotometryFiles.map(async Name =>
    {
        const Response = await fetch(new URL(`Photometry/${Name}`, AssetRoot));
        if (!Response.ok) throw new Error(`${Name}: HTTP ${Response.status}`);
        return ResampleProfile(ParseIesProfile(await Response.text(), Name));
    }));
}

async function ValidateShaderCompilation(ShaderUnit, Label)
{
    const Compilation = await ShaderUnit.getCompilationInfo();
    const Failures = Compilation.messages.filter(Message => Message.type === 'error');
    if (Failures.length)
    {
        throw new Error(`${Label} failed to compile:\n` + Failures.map(Message => `${Message.lineNum}:${Message.linePos} ${Message.message}`).join('\n'));
    }
}

// 📝 FNV-1a over the raw IEEE-754 bytes of the accumulation: equal digests mean bit-identical sums.
export function DigestFloatBuffer(Floats)
{
    const Bytes = new Uint8Array(Floats.buffer, Floats.byteOffset, Floats.byteLength);
    let Hash = 0x811c9dc5;
    for (let Index = 0; Index < Bytes.length; Index++)
    {
        Hash ^= Bytes[Index];
        Hash = Math.imul(Hash, 0x01000193) >>> 0;
    }
    return Hash.toString(16).padStart(8, '0');
}

// 📝 Denoiser strength by sample count: 1 up to DenoiseFullUntil, smooth (in log₂ n) to 0 at DenoiseFadedAt, so a
//    converged image is exactly the accumulation.
export function ResolveDenoiseStrength(SampleCount)
{
    const Position = (Math.log2(Math.max(SampleCount, 1)) - Math.log2(DenoiseFullUntil)) / (Math.log2(DenoiseFadedAt) - Math.log2(DenoiseFullUntil));
    const Clamped  = Math.min(1, Math.max(0, Position));
    return 1 - Clamped * Clamped * (3 - 2 * Clamped);
}

// 📝 Host replica of DisplayExposure.wgsl (ACES fitted + sRGB) for PNG export straight from the accumulation, so saved
//    images never depend on canvas compositor timing.
//    History (optional) is the reprojected region, blended exactly as DisplayExposure.wgsl does.
export function ResolveDisplayPixels(Accumulation, Width, Height, Exposure, History = null)
{
    const Pixels = new Uint8ClampedArray(Width * Height * 4);
    const Encode = Linear => (Linear <= 0.0031308 ? Linear * 12.92 : 1.055 * Math.pow(Linear, 1.0 / 2.4) - 0.055);
    for (let Pixel = 0; Pixel < Width * Height; Pixel++)
    {
        const SampleWeight  = Accumulation[Pixel * 4 + 3];
        const HistoryWeight = History ? Math.max(History[Pixel * 4 + 3] - SampleWeight, 0) : 0;
        const Weight = SampleWeight + HistoryWeight;
        const Blend  = Channel => (Accumulation[Pixel * 4 + Channel] + (HistoryWeight > 0 ? History[Pixel * 4 + Channel] * HistoryWeight : 0)) / Weight * Exposure;
        const R = Weight > 0 ? Blend(0) : 0;
        const G = Weight > 0 ? Blend(1) : 0;
        const B = Weight > 0 ? Blend(2) : 0;
        const V = [0.59719 * R + 0.35458 * G + 0.04823 * B, 0.07600 * R + 0.90834 * G + 0.01566 * B, 0.02840 * R + 0.13383 * G + 0.83777 * B];
        const T = V.map(C => (C * (C + 0.0245786) - 0.000090537) / (C * (0.983729 * C + 0.4329510) + 0.238081));
        const Mapped = [
            1.60475 * T[0] - 0.53108 * T[1] - 0.07367 * T[2],
           -0.10208 * T[0] + 1.10813 * T[1] - 0.00605 * T[2],
           -0.00327 * T[0] - 0.07276 * T[1] + 1.07602 * T[2]];
        for (let Channel = 0; Channel < 3; Channel++)
        {
            Pixels[Pixel * 4 + Channel] = Math.round(255 * Encode(Math.min(1, Math.max(0, Mapped[Channel]))));
        }
        Pixels[Pixel * 4 + 3] = 255;
    }
    return Pixels;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    TRACING DIVISION
//------------------------------------------------------------------------------------------------------------------------

export class TracingDivision
{
    constructor()
    {
        this.Device          = null;
        this.CanvasContext   = null;
        this.ReadbackSurface = null;
        this.SampleCount     = 0;
        this.AccumulationWidth  = 0;
        this.AccumulationHeight = 0;
        this.SceneSummary    = null;
        this.HistoryActive   = false;    // 📝 A reprojected history region is blended into the display
        this.HistoryRegion   = 1;        // 📝 Region (1 or 2) holding the current history
        this.ReprojectionPending = false;
        this.AccumulatedCamera   = null; // 📝 Camera vectors the exact sums were traced with
    }

    // 📝 ReadbackPresentationEnabled shows the image through a 2D canvas (GPU readback + CPU tone map) instead of a
    //    WebGPU swap chain, for browsers that cannot back a WebGPU canvas (e.g. headless SwiftShader).
    async Initialize(Canvas, AssetRoot, ReadbackPresentationEnabled = false)
    {
        if (!navigator.gpu) throw new Error('WebGPU is unavailable in this browser (navigator.gpu missing).');
        const Adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
        if (!Adapter) throw new Error('No WebGPU adapter was granted.');
        this.AdapterDescription = [Adapter.info?.vendor, Adapter.info?.architecture, Adapter.info?.description].filter(Boolean).join(' · ') || 'WebGPU adapter';
        this.IsSoftwareAdapter  = Boolean(Adapter.isFallbackAdapter) || /swiftshader|llvmpipe|lavapipe|software|basic render/i.test(this.AdapterDescription);
        const TimestampsAvailable = Adapter.features.has('timestamp-query') && new URLSearchParams(location.search).get('timing') !== '0';
        this.Device = await Adapter.requestDevice({
            requiredFeatures: TimestampsAvailable ? ['timestamp-query'] : [],
            requiredLimits: { maxStorageBuffersPerShaderStage: 8, maxStorageBufferBindingSize: Math.min(Adapter.limits.maxStorageBufferBindingSize, 256 * 1024 * 1024) }
        });
        this.Device.lost.then(Loss => console.error(`WebGPU device lost: ${Loss.message}`));
        this.Timing = new StageTiming(this.Device, TimestampsAvailable);

        this.CanvasFormat = navigator.gpu.getPreferredCanvasFormat();
        if (ReadbackPresentationEnabled)
        {
            this.ReadbackSurface = Canvas.getContext('2d');
        }
        else
        {
            this.CanvasContext = Canvas.getContext('webgpu');
            this.CanvasContext.configure({ device: this.Device, format: this.CanvasFormat, alphaMode: 'opaque' });
        }

        const [QuadratureSource, IntegrationSource, WavefrontSource, DisplaySource, RasterSource, ReconstructionSource, Vehicle, BlueNoiseRanks, Photometry] = await Promise.all([
            RetrieveShaderSource('QuadratureSequence.wgsl'),
            RetrieveShaderSource('RadianceIntegration.wgsl'),
            RetrieveShaderSource('WavefrontIntegration.wgsl'),
            RetrieveShaderSource('DisplayExposure.wgsl'),
            RetrieveShaderSource('VisibilityRasterization.wgsl'),
            RetrieveShaderSource('DisplayReconstruction.wgsl'),
            RetrieveVehicleArchives(AssetRoot),
            RetrieveBlueNoiseRanks(AssetRoot),
            RetrievePhotometry(AssetRoot)
        ]);
        this.Photometry = Photometry;
        this.ConstructPipelines(QuadratureSource + IntegrationSource, QuadratureSource + WavefrontSource, DisplaySource, QuadratureSource + RasterSource, ReconstructionSource);
        await Promise.all([
            ValidateShaderCompilation(this.ReconstructionUnit, 'DisplayReconstruction.wgsl'),
            ValidateShaderCompilation(this.IntegrationUnit, 'RadianceIntegration.wgsl'),
            ValidateShaderCompilation(this.WavefrontUnit, 'WavefrontIntegration.wgsl'),
            ValidateShaderCompilation(this.DisplayUnit, 'DisplayExposure.wgsl'),
            ValidateShaderCompilation(this.RasterUnit, 'VisibilityRasterization.wgsl')
        ]);
        this.Vehicle = Vehicle;
        this.TracingUniformBuffer = this.Device.createBuffer({ size: TracingSlotBytes * TracingSlotCount, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.DisplayUniformBuffer = this.Device.createBuffer({ size: DisplayUniformBytes, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.RasterUniformBuffer  = this.Device.createBuffer({ size: RasterUniformBytes, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.ReconstructionUniformBuffer = this.Device.createBuffer({ size: ReconstructionSlotBytes * (1 + AtrousSteps.length), usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        this.BlueNoiseTexture     = this.Device.createTexture({ label: 'BlueNoiseRanks', size: [64, 64], format: 'r32uint', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
        this.Device.queue.writeTexture({ texture: this.BlueNoiseTexture }, BlueNoiseRanks, { bytesPerRow: 64 * 4 }, [64, 64]);
    }

    ConstructPipelines(IntegrationSource, WavefrontSource, DisplaySource, RasterSource, ReconstructionSource)
    {
        const Device = this.Device;
        this.ReconstructionUnit = Device.createShaderModule({ label: 'DisplayReconstruction', code: ReconstructionSource });
        this.IntegrationUnit = Device.createShaderModule({ label: 'RadianceIntegration', code: IntegrationSource });
        this.WavefrontUnit   = Device.createShaderModule({ label: 'WavefrontIntegration', code: WavefrontSource });
        this.DisplayUnit     = Device.createShaderModule({ label: 'DisplayExposure', code: DisplaySource });
        this.RasterUnit      = Device.createShaderModule({ label: 'VisibilityRasterization', code: RasterSource });

        const ReadOnly = { type: 'read-only-storage' };
        this.IntegrationLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
            ...[1, 2, 3, 4, 5, 6].map(Slot => ({ binding: Slot, visibility: GPUShaderStage.COMPUTE, buffer: ReadOnly })),
            { binding: 7, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
            { binding: 8, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: 'uint' } },
            { binding: 9, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: 'uint' } },
            { binding: 10, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } }
        ] });
        const IntegrationPipelineLayout = Device.createPipelineLayout({ bindGroupLayouts: [this.IntegrationLayout] });
        this.IntegrationPipeline = Device.createComputePipeline({ layout: IntegrationPipelineLayout, compute: { module: this.IntegrationUnit, entryPoint: 'IntegrateRadiance' } });
        this.QuadraturePipeline  = Device.createComputePipeline({ layout: IntegrationPipelineLayout, compute: { module: this.IntegrationUnit, entryPoint: 'ExtractQuadrature' } });
        this.ReprojectionPipeline = Device.createComputePipeline({ layout: IntegrationPipelineLayout, compute: { module: this.IntegrationUnit, entryPoint: 'ReprojectHistory' } });
        this.FeaturePipeline      = Device.createComputePipeline({ layout: IntegrationPipelineLayout, compute: { module: this.IntegrationUnit, entryPoint: 'ResolveSurfaceFeatures' } });

        // 📝 Wavefront stage layouts (every pipeline keeps total storage buffers <= 8, matching WebGPU core minimum limits).
        const ComputeStage = GPUShaderStage.COMPUTE;
        const DynUniform   = { type: 'uniform', hasDynamicOffset: true };
        const ReadWrite    = { type: 'storage' };

        this.WaveEmptyLayout        = Device.createBindGroupLayout({ entries: [] });
        this.WavePrimarySceneLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ComputeStage, buffer: { type: 'uniform' } },
            { binding: 1, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 2, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 3, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 5, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 6, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 7, visibility: ComputeStage, texture: { sampleType: 'uint' } },
            { binding: 8, visibility: ComputeStage, texture: { sampleType: 'uint' } }
        ] });
        this.WaveShadeSceneLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ComputeStage, buffer: { type: 'uniform' } },
            { binding: 4, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 5, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 8, visibility: ComputeStage, texture: { sampleType: 'uint' } }
        ] });
        this.WaveShadowSceneLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ComputeStage, buffer: { type: 'uniform' } },
            { binding: 1, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 2, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 4, visibility: ComputeStage, buffer: ReadOnly },
            { binding: 6, visibility: ComputeStage, buffer: ReadOnly }
        ] });
        this.WaveFinalizeSceneLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ComputeStage, buffer: { type: 'uniform' } }
        ] });

        this.WavePrepareGroupLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ComputeStage, buffer: DynUniform },
            { binding: 1, visibility: ComputeStage, buffer: ReadWrite },
            { binding: 6, visibility: ComputeStage, buffer: ReadWrite }
        ] });
        this.WaveQueueGroupLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ComputeStage, buffer: DynUniform },
            { binding: 1, visibility: ComputeStage, buffer: ReadWrite },
            { binding: 2, visibility: ComputeStage, buffer: ReadWrite },
            { binding: 3, visibility: ComputeStage, buffer: ReadWrite }
        ] });
        this.WaveShadeGroupLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ComputeStage, buffer: DynUniform },
            { binding: 1, visibility: ComputeStage, buffer: ReadWrite },
            { binding: 2, visibility: ComputeStage, buffer: ReadWrite },
            { binding: 3, visibility: ComputeStage, buffer: ReadWrite },
            { binding: 4, visibility: ComputeStage, buffer: ReadWrite }
        ] });
        this.WaveShadowGroupLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ComputeStage, buffer: DynUniform },
            { binding: 1, visibility: ComputeStage, buffer: ReadWrite },
            { binding: 3, visibility: ComputeStage, buffer: ReadWrite },
            { binding: 4, visibility: ComputeStage, buffer: ReadWrite }
        ] });
        this.WaveFinalizeGroupLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: ComputeStage, buffer: DynUniform },
            { binding: 4, visibility: ComputeStage, buffer: ReadWrite },
            { binding: 5, visibility: ComputeStage, buffer: ReadWrite }
        ] });

        this.WaveGeneratePipeline = Device.createComputePipeline({
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.WavePrimarySceneLayout, this.WaveQueueGroupLayout] }),
            compute: { module: this.WavefrontUnit, entryPoint: 'WaveGeneratePrimary' }
        });
        this.WavePreparePipeline = Device.createComputePipeline({
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.WaveEmptyLayout, this.WavePrepareGroupLayout] }),
            compute: { module: this.WavefrontUnit, entryPoint: 'WavePrepareStage' }
        });
        this.WaveClosestPipeline = Device.createComputePipeline({
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.WavePrimarySceneLayout, this.WaveQueueGroupLayout] }),
            compute: { module: this.WavefrontUnit, entryPoint: 'WaveTraceClosest' }
        });
        this.WaveShadePipeline = Device.createComputePipeline({
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.WaveShadeSceneLayout, this.WaveShadeGroupLayout] }),
            compute: { module: this.WavefrontUnit, entryPoint: 'WaveShadeScatter' }
        });
        this.WaveShadowPipeline = Device.createComputePipeline({
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.WaveShadowSceneLayout, this.WaveShadowGroupLayout] }),
            compute: { module: this.WavefrontUnit, entryPoint: 'WaveTraceShadow' }
        });
        this.WaveFinalizePipeline = Device.createComputePipeline({
            layout: Device.createPipelineLayout({ bindGroupLayouts: [this.WaveFinalizeSceneLayout, this.WaveFinalizeGroupLayout] }),
            compute: { module: this.WavefrontUnit, entryPoint: 'WaveFinalizeSample' }
        });

        this.ReconstructionLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: ReadOnly },
            { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: ReadOnly },
            { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } }
        ] });
        const ReconstructionPipelineLayout = Device.createPipelineLayout({ bindGroupLayouts: [this.ReconstructionLayout] });
        this.GatherPipeline = Device.createComputePipeline({ layout: ReconstructionPipelineLayout, compute: { module: this.ReconstructionUnit, entryPoint: 'GatherEstimate' } });
        this.AtrousPipeline = Device.createComputePipeline({ layout: ReconstructionPipelineLayout, compute: { module: this.ReconstructionUnit, entryPoint: 'FilterAtrous' } });

        this.RasterLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.VERTEX, buffer: ReadOnly },
            { binding: 2, visibility: GPUShaderStage.VERTEX, buffer: ReadOnly }
        ] });
        this.RasterPipeline = Device.createRenderPipeline({
            layout:       Device.createPipelineLayout({ bindGroupLayouts: [this.RasterLayout] }),
            vertex:       { module: this.RasterUnit, entryPoint: 'ProjectTriangleCorner' },
            fragment:     { module: this.RasterUnit, entryPoint: 'WriteTriangleIdentifier', targets: [{ format: 'r32uint' }] },
            primitive:    { topology: 'triangle-list', cullMode: 'none' },
            depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less' }
        });

        this.DisplayLayout = Device.createBindGroupLayout({ entries: [
            { binding: 0, visibility: GPUShaderStage.FRAGMENT | GPUShaderStage.VERTEX, buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: ReadOnly },
            { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: ReadOnly }
        ] });
        this.DisplayPipeline = Device.createRenderPipeline({
            layout:    Device.createPipelineLayout({ bindGroupLayouts: [this.DisplayLayout] }),
            vertex:    { module: this.DisplayUnit, entryPoint: 'ProjectScreenTriangle' },
            fragment:  { module: this.DisplayUnit, entryPoint: 'ResolveDisplay', targets: [{ format: this.CanvasFormat }] },
            primitive: { topology: 'triangle-list' }
        });
    }

    CreateStorage(Contents, Label)
    {
        const Buffer = this.Device.createBuffer({ label: Label, size: Math.max(16, Contents.byteLength), usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
        this.Device.queue.writeBuffer(Buffer, 0, Contents);
        return Buffer;
    }

    // 📝 Materials and lights change with the inspector; geometry is uploaded once per material layout.
    ConfigureScene(Configuration)
    {
        const MaterialTable = ConstructMaterialTable(Configuration);
        const Spheres       = ConstructShowcaseSpheres(MaterialTable.SlotIndex);
        // 📝 Night: the three softboxes are removed (and the sky dimmed in WriteTracingUniforms); only lamps remain.
        const Lights        = ConstructSoftboxLights(Configuration.NightMode ? { KeyIntensity: 0, RimIntensity: 0, FillIntensity: 0 } : Configuration);

        const VehicleCount = Configuration.VehicleCount ?? 1;
        if (!this.TrianglePositionBuffer || this.GeometryVehicleCount !== VehicleCount)
        {
            const ConstructionStart = performance.now();
            // 📝 One vehicle tree (object space, built once) shared by every parked copy through the instance tree.
            if (!this.VehicleTree)
            {
                const Vehicle = this.Vehicle;
                const UnsplittableMask = new Uint8Array(Vehicle.TriangleCount);
                for (let Triangle = 0; Triangle < Vehicle.TriangleCount; Triangle++)
                {
                    const SlotNumber = MaterialTable.SlotIndex[Vehicle.Slots[Triangle]];
                    if (MaterialTable.Slots[SlotNumber]?.[1]?.Category === SurfaceCategory.ThinDielectric) UnsplittableMask[Triangle] = 1;
                }
                const Binary  = ConstructBoundingVolumes(Vehicle.Positions, Vehicle.TriangleCount, UnsplittableMask);
                const Wide    = CollapseWideBounds(Binary, 0);
                const ReferenceCount = Binary.Order.length;
                const Positions = new Float32Array(ReferenceCount * 12);
                const PositionWords = new Uint32Array(Positions.buffer);
                const Normals   = new Float32Array(ReferenceCount * 12);
                for (let Slot = 0; Slot < ReferenceCount; Slot++)
                {
                    const Source = Binary.Order[Slot], P = Source * 9, O = Slot * 12;
                    const Ax = Vehicle.Positions[P], Ay = Vehicle.Positions[P + 1], Az = Vehicle.Positions[P + 2];
                    Positions.set([Ax, Ay, Az, 0,
                                   Vehicle.Positions[P + 3] - Ax, Vehicle.Positions[P + 4] - Ay, Vehicle.Positions[P + 5] - Az, 0,
                                   Vehicle.Positions[P + 6] - Ax, Vehicle.Positions[P + 7] - Ay, Vehicle.Positions[P + 8] - Az, 0], O);
                    const SlotNumber = MaterialTable.SlotIndex[Vehicle.Slots[Source]];
                    const IsThinGlass = MaterialTable.Slots[SlotNumber]?.[1]?.Category === SurfaceCategory.ThinDielectric;
                    PositionWords[O + 3] = (SlotNumber | (IsThinGlass ? ThinDielectricFlag : 0)) >>> 0;
                    Normals.set([Vehicle.Normals[P], Vehicle.Normals[P + 1], Vehicle.Normals[P + 2], 0,
                                 Vehicle.Normals[P + 3], Vehicle.Normals[P + 4], Vehicle.Normals[P + 5], 0,
                                 Vehicle.Normals[P + 6], Vehicle.Normals[P + 7], Vehicle.Normals[P + 8], 0], O);
                }
                const Bounds = [Infinity, Infinity, Infinity, -Infinity, -Infinity, -Infinity];
                for (let Corner = 0; Corner < Vehicle.Positions.length; Corner += 3)
                {
                    for (let Axis = 0; Axis < 3; Axis++)
                    {
                        Bounds[Axis] = Math.min(Bounds[Axis], Vehicle.Positions[Corner + Axis]);
                        Bounds[3 + Axis] = Math.max(Bounds[3 + Axis], Vehicle.Positions[Corner + Axis]);
                    }
                }
                this.TrianglePositionBuffer = this.CreateStorage(Positions, 'TrianglePositions');
                this.TriangleNormalBuffer   = this.CreateStorage(Normals, 'TriangleNormals');
                this.VehicleTree = { Wide, ReferenceCount, BinaryEntries: Binary.EntryCount, BinaryDepth: Binary.MaximumDepthTier, Bounds,
                                     Milliseconds: Math.round(performance.now() - ConstructionStart) };
            }
            const Instances = TracingDivision.ConstructInstanceTree(this.VehicleTree, ResolveParkingBays(VehicleCount), MaterialTable.SlotIndex);
            this.BoundsBuffer?.destroy();
            this.BoundsBuffer = this.CreateStorage(Instances.Words, 'BoundsEntries');
            this.GeometryLayout = Instances.Layout;
            this.SceneSummary = {
                TriangleCount:        this.Vehicle.TriangleCount * VehicleCount,
                VehicleTriangleCount: this.Vehicle.TriangleCount,
                ArchiveFacets:        this.Vehicle.ArchiveFacets,
                BoundsEntryCount:     this.VehicleTree.Wide.NodeCount + Instances.NodeCount,
                BoundsDepthTier:      this.VehicleTree.Wide.MaximumDepth + Instances.MaximumDepth,
                VehicleNodes:         this.VehicleTree.Wide.NodeCount,
                InstanceNodes:        Instances.NodeCount,
                BinaryEntries:        this.VehicleTree.BinaryEntries,
                BinaryDepth:          this.VehicleTree.BinaryDepth,
                BoundsBytes:          Instances.Words.byteLength,
                VehicleCount,
                ConstructionMilliseconds: Math.round(performance.now() - ConstructionStart)
            };
            this.GeometryVehicleCount = VehicleCount;
        }

        // 📝 Practical lamps ride on the parked cars (and the car-park ceiling); rebuilt with the light tree whenever the
        //    lamp mode, intensity or vehicle count changes (≈ 1 ms for 228 lamps).
        const LampStart = performance.now();
        const Bays  = Configuration.VehicleEnabled === false ? [] : ResolveParkingBays(VehicleCount);
        const Lamps = ConstructPracticalLamps(Configuration.PracticalMode, Bays, this.Photometry, Configuration.PracticalIntensity ?? 1);
        const LightTree = ConstructLightTree(Lamps);
        const Records = PackLightRecords(Lights.Packed, Lights.Count, Lamps, LightTree, this.Photometry);
        this.PracticalLayout = Records.Layout;

        this.MaterialBuffer?.destroy(); this.LightBuffer?.destroy(); this.SphereBuffer?.destroy();
        this.MaterialBuffer = this.CreateStorage(PackMaterialTable(MaterialTable), 'Materials');
        this.LightBuffer    = this.CreateStorage(Records.Floats, 'LightRecords');
        this.SphereBuffer   = this.CreateStorage(Spheres.Packed, 'Spheres');
        this.SphereBounds   = Spheres.BoundsSphere;
        this.SceneSummary.SphereCount = Spheres.Count;
        this.SceneSummary.LightCount  = Lights.Count;
        this.SceneSummary.LampCount   = Lamps.length;
        this.SceneSummary.LightTreeNodes = LightTree.Nodes.length;
        this.SceneSummary.LightTreeDepth = LightTree.Depth;
        this.SceneSummary.LightTreeMilliseconds = Math.round((performance.now() - LampStart) * 10) / 10;
        this.SceneSummary.MaterialCount = MaterialTable.Slots.length;
        this.FloorSlot  = MaterialTable.SlotIndex.StudioFloor;
        this.Configuration = { ...Configuration };
        this.RebuildIntegrationGroup();
        this.ResetAccumulation();
    }

    ResizeAccumulation(Width, Height)
    {
        if (Width === this.AccumulationWidth && Height === this.AccumulationHeight && this.AccumulationBuffer) return;
        this.AccumulationBuffer?.destroy();
        this.ReconstructionBuffer?.destroy();
        this.FeatureBuffer?.destroy();
        this.VisibilityTexture?.destroy();
        this.RasterDepthTexture?.destroy();
        this.AccumulationWidth  = Width;
        this.AccumulationHeight = Height;
        this.AccumulationBuffer = this.Device.createBuffer({
            label: 'Accumulation', size: Width * Height * 16 * AccumulationRegions,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST
        });
        this.ReconstructionBuffer = this.Device.createBuffer({
            label: 'Reconstruction', size: Width * Height * 16 * ReconstructionRegions,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC
        });
        this.FeatureBuffer = this.Device.createBuffer({ label: 'SurfaceFeatures', size: Width * Height * 16 * 2, usage: GPUBufferUsage.STORAGE });
        this.VisibilityTexture = this.Device.createTexture({ label: 'VisibilityIds', size: [Width, Height], format: 'r32uint',
            usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });
        this.RasterDepthTexture = this.Device.createTexture({ label: 'VisibilityDepth', size: [Width, Height], format: 'depth32float',
            usage: GPUTextureUsage.RENDER_ATTACHMENT });
        this.AccumulatedCamera = null;
        this.RebuildIntegrationGroup();
        this.ResetAccumulation();
    }

    RebuildIntegrationGroup()
    {
        if (!this.AccumulationBuffer || !this.MaterialBuffer) return;
        const StorageBuffers = [this.BoundsBuffer, this.TrianglePositionBuffer, this.TriangleNormalBuffer,
                                this.MaterialBuffer, this.LightBuffer, this.SphereBuffer, this.AccumulationBuffer];
        const CreatePassGroup = Slot => this.Device.createBindGroup({ layout: this.IntegrationLayout, entries: [
            { binding: 0, resource: { buffer: this.TracingUniformBuffer, offset: Slot * TracingSlotBytes, size: TracingUniformBytes } },
            ...StorageBuffers.map((Buffer, Index) => ({ binding: Index + 1, resource: { buffer: Buffer } })),
            { binding: 8, resource: this.VisibilityTexture.createView() },
            { binding: 9, resource: this.BlueNoiseTexture.createView() },
            { binding: 10, resource: { buffer: this.FeatureBuffer } }
        ] });
        this.IntegrationGroup  = CreatePassGroup(0);
        this.ReprojectionGroup = CreatePassGroup(1);
        this.FeatureGroup      = CreatePassGroup(2);
        this.ReconstructionGroups = Array.from({ length: 1 + AtrousSteps.length }, (_, Slot) => this.Device.createBindGroup({ layout: this.ReconstructionLayout, entries: [
            { binding: 0, resource: { buffer: this.ReconstructionUniformBuffer, offset: Slot * ReconstructionSlotBytes, size: 64 } },
            { binding: 1, resource: { buffer: this.AccumulationBuffer } },
            { binding: 2, resource: { buffer: this.FeatureBuffer } },
            { binding: 3, resource: { buffer: this.ReconstructionBuffer } }
        ] }));
        this.FeaturesStale = true;
        this.RasterGroup = this.Device.createBindGroup({ layout: this.RasterLayout, entries: [
            { binding: 0, resource: { buffer: this.RasterUniformBuffer } },
            { binding: 1, resource: { buffer: this.TrianglePositionBuffer } },
            { binding: 2, resource: { buffer: this.BoundsBuffer } }
        ] });
        this.DisplayGroup = this.Device.createBindGroup({ layout: this.DisplayLayout, entries: [
            { binding: 0, resource: { buffer: this.DisplayUniformBuffer } },
            { binding: 1, resource: { buffer: this.AccumulationBuffer } },
            { binding: 2, resource: { buffer: this.ReconstructionBuffer } }
        ] });
        if (this.WaveControlBuffer) this.RebuildWavefrontGroups();
    }

    WavefrontActive()
    {
        const Mode = this.Configuration?.IntegratorMode ?? 'auto';
        if (Mode === 'megakernel') return false;
        return true;
    }

    EnsureWavefrontResources(RequiredUniformSlots)
    {
        const Device = this.Device;
        let Rebuilt = false;
        if (!this.WaveControlBuffer)
        {
            this.WaveControlBuffer = Device.createBuffer({
                label: 'WaveControl', size: 32,
                usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC
            });
            this.WaveIndirectBuffer = Device.createBuffer({
                label: 'WaveIndirect', size: 48,
                usage: GPUBufferUsage.STORAGE | GPUBufferUsage.INDIRECT | GPUBufferUsage.COPY_DST
            });
            this.WaveRaysBuffer = Device.createBuffer({
                label: 'WaveRays', size: 2 * WaveQueueCapacity * 4 * 16,
                usage: GPUBufferUsage.STORAGE
            });
            this.WaveWorkBuffer = Device.createBuffer({
                label: 'WaveWork', size: (WaveQueueCapacity * 3 + WaveShadowCapacity * 3) * 16,
                usage: GPUBufferUsage.STORAGE
            });
            this.WaveAccumBuffer = Device.createBuffer({
                label: 'WaveAccum', size: WaveMaxBatchTiles * 9 * 4,
                usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST
            });
            Rebuilt = true;
        }
        const MinSlots = Math.max(64, RequiredUniformSlots);
        if (!this.WaveUniformBuffer || this.WaveUniformSlotCapacity < MinSlots)
        {
            this.WaveUniformBuffer?.destroy();
            this.WaveUniformSlotCapacity = 1 << Math.ceil(Math.log2(MinSlots));
            this.WaveUniformBuffer = Device.createBuffer({
                label: 'WaveUniforms', size: this.WaveUniformSlotCapacity * WaveUniformSlotBytes,
                usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
            });
            Rebuilt = true;
        }
        if (Rebuilt || !this.WavePrimarySceneGroup) this.RebuildWavefrontGroups();
    }

    RebuildWavefrontGroups()
    {
        if (!this.WaveControlBuffer || !this.AccumulationBuffer || !this.MaterialBuffer) return;
        const Device = this.Device;
        const TracingBinding = { buffer: this.TracingUniformBuffer, offset: 0, size: TracingUniformBytes };
        const WaveBinding    = { buffer: this.WaveUniformBuffer, offset: 0, size: 32 };

        this.WaveEmptyGroup = Device.createBindGroup({ layout: this.WaveEmptyLayout, entries: [] });
        this.WavePrimarySceneGroup = Device.createBindGroup({ layout: this.WavePrimarySceneLayout, entries: [
            { binding: 0, resource: TracingBinding },
            { binding: 1, resource: { buffer: this.BoundsBuffer } },
            { binding: 2, resource: { buffer: this.TrianglePositionBuffer } },
            { binding: 3, resource: { buffer: this.TriangleNormalBuffer } },
            { binding: 5, resource: { buffer: this.LightBuffer } },
            { binding: 6, resource: { buffer: this.SphereBuffer } },
            { binding: 7, resource: this.VisibilityTexture.createView() },
            { binding: 8, resource: this.BlueNoiseTexture.createView() }
        ] });
        this.WaveShadeSceneGroup = Device.createBindGroup({ layout: this.WaveShadeSceneLayout, entries: [
            { binding: 0, resource: TracingBinding },
            { binding: 4, resource: { buffer: this.MaterialBuffer } },
            { binding: 5, resource: { buffer: this.LightBuffer } },
            { binding: 8, resource: this.BlueNoiseTexture.createView() }
        ] });
        this.WaveShadowSceneGroup = Device.createBindGroup({ layout: this.WaveShadowSceneLayout, entries: [
            { binding: 0, resource: TracingBinding },
            { binding: 1, resource: { buffer: this.BoundsBuffer } },
            { binding: 2, resource: { buffer: this.TrianglePositionBuffer } },
            { binding: 4, resource: { buffer: this.MaterialBuffer } },
            { binding: 6, resource: { buffer: this.SphereBuffer } }
        ] });
        this.WaveFinalizeSceneGroup = Device.createBindGroup({ layout: this.WaveFinalizeSceneLayout, entries: [
            { binding: 0, resource: TracingBinding }
        ] });

        this.WavePrepareGroup = Device.createBindGroup({ layout: this.WavePrepareGroupLayout, entries: [
            { binding: 0, resource: WaveBinding },
            { binding: 1, resource: { buffer: this.WaveControlBuffer } },
            { binding: 6, resource: { buffer: this.WaveIndirectBuffer } }
        ] });
        this.WaveQueueGroup = Device.createBindGroup({ layout: this.WaveQueueGroupLayout, entries: [
            { binding: 0, resource: WaveBinding },
            { binding: 1, resource: { buffer: this.WaveControlBuffer } },
            { binding: 2, resource: { buffer: this.WaveRaysBuffer } },
            { binding: 3, resource: { buffer: this.WaveWorkBuffer } }
        ] });
        this.WaveShadeGroup = Device.createBindGroup({ layout: this.WaveShadeGroupLayout, entries: [
            { binding: 0, resource: WaveBinding },
            { binding: 1, resource: { buffer: this.WaveControlBuffer } },
            { binding: 2, resource: { buffer: this.WaveRaysBuffer } },
            { binding: 3, resource: { buffer: this.WaveWorkBuffer } },
            { binding: 4, resource: { buffer: this.WaveAccumBuffer } }
        ] });
        this.WaveShadowGroup = Device.createBindGroup({ layout: this.WaveShadowGroupLayout, entries: [
            { binding: 0, resource: WaveBinding },
            { binding: 1, resource: { buffer: this.WaveControlBuffer } },
            { binding: 3, resource: { buffer: this.WaveWorkBuffer } },
            { binding: 4, resource: { buffer: this.WaveAccumBuffer } }
        ] });
        this.WaveFinalizeGroup = Device.createBindGroup({ layout: this.WaveFinalizeGroupLayout, entries: [
            { binding: 0, resource: WaveBinding },
            { binding: 4, resource: { buffer: this.WaveAccumBuffer } },
            { binding: 5, resource: { buffer: this.AccumulationBuffer } }
        ] });
    }

    ResolveWaveBatchTileLimit()
    {
        const AreaCount = this.SceneSummary?.LightCount ?? 0;
        const Lamps     = this.SceneSummary?.LampCount ?? 0;
        const LampDraws = this.Configuration?.LampSampling === 'all' ? Lamps : Math.min(Lamps, LampCutLimits[this.Configuration?.LampSampling] ?? 1);
        const PerHitShadows = Math.max(4, AreaCount + LampDraws);
        // 📝 Size each tile batch so worst-case depth-1 shadow rays never exceed WaveShadowCapacity, even under 'all' 228 lamps.
        return Math.max(256, Math.min(WaveMaxBatchTiles, Math.floor(WaveShadowCapacity / (PerHitShadows * 2.5))));
    }

    // 📝 Encodes [FirstSample, FirstSample + SampleCount) through the 6-stage deterministic wavefront compute pipeline.
    EncodeWavefrontSamples(Encoder, FirstSample, SampleCount, TimingBegin = true, TimingEnd = true)
    {
        const Scale      = this.TraceScale();
        const TilesX     = Math.ceil(this.AccumulationWidth / Scale);
        const TilesY     = Math.ceil(this.AccumulationHeight / Scale);
        const TotalTiles = TilesX * TilesY;
        const BatchLimit = this.ResolveWaveBatchTileLimit();
        const BatchCount = Math.ceil(TotalTiles / BatchLimit);
        const DepthLimit = Math.max(1, this.Configuration?.DepthLimit ?? 10);
        const SlotsPerPass = DepthLimit * 3;
        const TotalSlots   = SampleCount * BatchCount * SlotsPerPass;
        this.EnsureWavefrontResources(TotalSlots);

        const UniformBytes = new ArrayBuffer(TotalSlots * WaveUniformSlotBytes);
        const UniformWords = new Uint32Array(UniformBytes);
        let CursorSlot = 0;
        for (let SampleOffset = 0; SampleOffset < SampleCount; SampleOffset++)
        {
            const SampleIndex = FirstSample + SampleOffset;
            for (let Batch = 0; Batch < BatchCount; Batch++)
            {
                const BatchStart = Batch * BatchLimit;
                const BatchTiles = Math.min(BatchLimit, TotalTiles - BatchStart);
                for (let Depth = 0; Depth < DepthLimit; Depth++)
                {
                    const ReadSide  = Depth & 1;
                    const WriteSide = (Depth + 1) & 1;
                    for (let Stage = 0; Stage < 3; Stage++)
                    {
                        const Base = (CursorSlot++) * (WaveUniformSlotBytes / 4);
                        UniformWords.set([BatchStart, BatchTiles, SampleIndex, Stage,
                                          WaveQueueCapacity, WaveShadowCapacity, ReadSide, WriteSide], Base);
                    }
                }
            }
        }
        this.Device.queue.writeBuffer(this.WaveUniformBuffer, 0, UniformBytes);

        const TotalPasses = SampleCount * BatchCount;
        let PassIndex = 0;
        CursorSlot = 0;
        for (let SampleOffset = 0; SampleOffset < SampleCount; SampleOffset++)
        {
            for (let Batch = 0; Batch < BatchCount; Batch++)
            {
                const BatchStart = Batch * BatchLimit;
                const BatchTiles = Math.min(BatchLimit, TotalTiles - BatchStart);
                const TileGroups = Math.ceil(BatchTiles / WaveWorkgroupSize);
                Encoder.clearBuffer(this.WaveControlBuffer, 0, 32);
                Encoder.clearBuffer(this.WaveAccumBuffer, 0, BatchTiles * 9 * 4);

                const IsFirst = TimingBegin && PassIndex === 0;
                const IsLast  = TimingEnd && PassIndex === TotalPasses - 1;
                const PassDesc = (IsFirst || IsLast) ? { timestampWrites: this.Timing.PassWrites('Integration', IsFirst, IsLast) } : undefined;
                const Compute = Encoder.beginComputePass(PassDesc);
                const BaseSlotOffset = CursorSlot * WaveUniformSlotBytes;

                // ① Fused Depth-0 primary generation + closest-hit trace (in 8×8 spatially coherent blocks)
                Compute.setPipeline(this.WaveGeneratePipeline);
                Compute.setBindGroup(0, this.WavePrimarySceneGroup);
                Compute.setBindGroup(1, this.WaveQueueGroup, [BaseSlotOffset]);
                Compute.dispatchWorkgroups(TileGroups);

                // ② Wave depth loop: Depth 0 jumps straight to shading; Depth >= 1 runs indirect TraceClosest first.
                for (let Depth = 0; Depth < DepthLimit; Depth++)
                {
                    const SlotClosest = (CursorSlot++) * WaveUniformSlotBytes;
                    const SlotShade   = (CursorSlot++) * WaveUniformSlotBytes;
                    const SlotShadow  = (CursorSlot++) * WaveUniformSlotBytes;

                    if (Depth > 0)
                    {
                        Compute.setPipeline(this.WavePreparePipeline);
                        Compute.setBindGroup(0, this.WaveEmptyGroup);
                        Compute.setBindGroup(1, this.WavePrepareGroup, [SlotClosest]);
                        Compute.dispatchWorkgroups(1);

                        Compute.setPipeline(this.WaveClosestPipeline);
                        Compute.setBindGroup(0, this.WavePrimarySceneGroup);
                        Compute.setBindGroup(1, this.WaveQueueGroup, [SlotClosest]);
                        Compute.dispatchWorkgroupsIndirect(this.WaveIndirectBuffer, 0);
                    }

                    Compute.setPipeline(this.WavePreparePipeline);
                    Compute.setBindGroup(0, this.WaveEmptyGroup);
                    Compute.setBindGroup(1, this.WavePrepareGroup, [SlotShade]);
                    Compute.dispatchWorkgroups(1);

                    Compute.setPipeline(this.WaveShadePipeline);
                    Compute.setBindGroup(0, this.WaveShadeSceneGroup);
                    Compute.setBindGroup(1, this.WaveShadeGroup, [SlotShade]);
                    Compute.dispatchWorkgroupsIndirect(this.WaveIndirectBuffer, 12);

                    Compute.setPipeline(this.WavePreparePipeline);
                    Compute.setBindGroup(0, this.WaveEmptyGroup);
                    Compute.setBindGroup(1, this.WavePrepareGroup, [SlotShadow]);
                    Compute.dispatchWorkgroups(1);

                    Compute.setPipeline(this.WaveShadowPipeline);
                    Compute.setBindGroup(0, this.WaveShadowSceneGroup);
                    Compute.setBindGroup(1, this.WaveShadowGroup, [SlotShadow]);
                    Compute.dispatchWorkgroupsIndirect(this.WaveIndirectBuffer, 24);
                }

                // ③ Finalize exact 96-bit sample sums into IEEE-754 accumulation & luminance moments
                Compute.setPipeline(this.WaveFinalizePipeline);
                Compute.setBindGroup(0, this.WaveFinalizeSceneGroup);
                Compute.setBindGroup(1, this.WaveFinalizeGroup, [BaseSlotOffset]);
                Compute.dispatchWorkgroups(TileGroups);
                Compute.end();
                PassIndex++;
            }
        }
    }

    // 📝 ReprojectHistory = true (camera moved, reprojection enabled): the clear is deferred to the next dispatch or
    //    present, so several camera events within one display frame cost a single reprojection.
    ResetAccumulation(ReprojectHistory = false)
    {
        this.SampleCount = 0;
        if (!this.AccumulationBuffer) return;
        if (ReprojectHistory && this.Configuration?.ReprojectionEnabled && this.AccumulatedCamera)
        {
            this.ReprojectionPending = true;
            return;
        }
        this.ReprojectionPending = false;
        this.HistoryActive       = false;
        this.AccumulatedCamera   = this.Configuration?.Camera ? this.ResolveCurrentCamera() : null;
        this.FeaturesStale       = true;
        const Encoder = this.Device.createCommandEncoder();
        this.EncodeSumClear(Encoder);
        this.Device.queue.submit([Encoder.finish()]);
    }

    // 📝 Instance tree over the parking bays plus one record per instance (instance-tree order). Buffer layout in vec4u:
    //    [vehicle nodes][instance nodes][instance records]. Each bay is a rotation about +Y then a translation; bay 0 of
    //    a single vehicle is the exact identity, so its rays keep their float bits. The paint slot swap replaces
    //    BodyCoat with the bay's own finish and RimAlloy with its wheel finish (row 6 z / w, row 7 x = rim slot).
    static ConstructInstanceTree(VehicleTree, Bays, SlotIndex)
    {
        const Count = Bays.length;
        const Bounds = new Float32Array(Count * 6), Centroids = new Float32Array(Count * 3);
        const Transforms = Bays.map(Bay =>
        {
            const Cosine = Bay.Yaw === 0 ? 1 : (Bay.Yaw === Math.PI ? -1 : Math.cos(Bay.Yaw));
            const Sine   = (Bay.Yaw === 0 || Bay.Yaw === Math.PI) ? 0 : Math.sin(Bay.Yaw);
            return { Cosine, Sine, X: Bay.X, Z: Bay.Z };
        });
        Transforms.forEach((Transform, Index) =>
        {
            const B = VehicleTree.Bounds, Lower = [Infinity, Infinity, Infinity], Upper = [-Infinity, -Infinity, -Infinity];
            for (let Corner = 0; Corner < 8; Corner++)
            {
                const X = B[(Corner & 1) ? 3 : 0], Y = B[(Corner & 2) ? 4 : 1], Z = B[(Corner & 4) ? 5 : 2];
                const World = [Transform.Cosine * X + Transform.Sine * Z + Transform.X, Y, -Transform.Sine * X + Transform.Cosine * Z + Transform.Z];
                for (let Axis = 0; Axis < 3; Axis++) { Lower[Axis] = Math.min(Lower[Axis], World[Axis]); Upper[Axis] = Math.max(Upper[Axis], World[Axis]); }
            }
            // 📝 Pad by a float32 ulp-scale margin: the vehicle rays are transformed on the GPU in float32.
            for (let Axis = 0; Axis < 3; Axis++)
            {
                const Margin = 1e-5 * Math.max(1, Math.abs(Lower[Axis]), Math.abs(Upper[Axis]));
                Bounds[Index * 6 + Axis] = Lower[Axis] - Margin; Bounds[Index * 6 + 3 + Axis] = Upper[Axis] + Margin;
                Centroids[Index * 3 + Axis] = (Lower[Axis] + Upper[Axis]) / 2;
            }
        });
        const VehicleNodes = VehicleTree.Wide.NodeCount;
        const Binary = ConstructBinaryTree(Bounds, Centroids, Count, InstanceLeafLimit, InstanceLeafLimit);
        const Wide   = CollapseWideBounds(Binary, VehicleNodes, InstanceLeafLimit);
        const RecordBase = (VehicleNodes + Wide.NodeCount) * WideNodeWords;
        const Words  = new Uint32Array(RecordBase + Count * InstanceRecordWords);
        const Floats = new Float32Array(Words.buffer);
        Words.set(VehicleTree.Wide.Words, 0);
        Words.set(Wide.Words, VehicleNodes * WideNodeWords);
        for (let Slot = 0; Slot < Count; Slot++)
        {
            const Bay = Bays[Binary.Order[Slot]], T = Transforms[Binary.Order[Slot]], O = RecordBase + Slot * InstanceRecordWords;
            Floats.set([T.Cosine, T.Sine, T.X, T.Z], O);
            const BodySlot  = SlotIndex.BodyCoat ?? UnusedMaterialSlot;
            const PaintSwap = Bay.PaintSlot ? (SlotIndex[Bay.PaintSlot] ?? UnusedMaterialSlot) : UnusedMaterialSlot;
            const RimSlot   = SlotIndex.RimAlloy ?? UnusedMaterialSlot;
            const RimSwap   = Bay.RimSlot   ? (SlotIndex[Bay.RimSlot]   ?? UnusedMaterialSlot) : UnusedMaterialSlot;
            Words.set([0,
                       (BodySlot & 0xFFFF) | ((PaintSwap & 0xFFFF) << 16),
                       (RimSlot  & 0xFFFF) | ((RimSwap   & 0xFFFF) << 16),
                       0], O + 4);
        }
        return { Words, NodeCount: Wide.NodeCount, MaximumDepth: Wide.MaximumDepth,
                 Layout: { InstanceRoot: VehicleNodes, RecordBase: RecordBase / 4, InstanceCount: Count } };
    }

    ResolveCurrentCamera()
    {
        return TracingDivision.ResolveCameraVectors(this.Configuration.Camera, this.AccumulationWidth / this.AccumulationHeight);
    }

    // 📝 Carries the displayed image of the previous camera into the other history region, then clears the sums.
    EncodePendingReprojection(Encoder)
    {
        if (!this.ReprojectionPending) return;
        const PixelCount   = this.AccumulationWidth * this.AccumulationHeight;
        const TargetRegion = 3 - this.HistoryRegion;
        this.WriteTracingUniforms(0, 1, this.AccumulatedCamera, this.HistoryRegion * PixelCount, TargetRegion * PixelCount, 1);
        const Compute = Encoder.beginComputePass({ timestampWrites: this.Timing.PassWrites('Reprojection') });
        Compute.setPipeline(this.ReprojectionPipeline);
        Compute.setBindGroup(0, this.ReprojectionGroup);
        Compute.dispatchWorkgroups(Math.ceil(this.AccumulationWidth / WorkgroupEdge), Math.ceil(this.AccumulationHeight / WorkgroupEdge));
        Compute.end();
        this.EncodeSumClear(Encoder);
        // 📝 ReprojectHistory already wrote SurfaceFeatures for the new camera from its primary centre-ray hit.
        this.FeaturesStale       = false;
        this.HistoryRegion       = TargetRegion;
        this.HistoryActive       = true;
        this.ReprojectionPending = false;
        this.AccumulatedCamera   = this.ResolveCurrentCamera();
    }

    FlushPendingReprojection()
    {
        if (!this.ReprojectionPending) return;
        const Encoder = this.Device.createCommandEncoder();
        this.EncodePendingReprojection(Encoder);
        this.Device.queue.submit([Encoder.finish()]);
    }

    // 📝 Clears the exact sums and their luminance moments (regions 0 and 3); history regions are kept.
    EncodeSumClear(Encoder)
    {
        const RegionBytes = this.AccumulationWidth * this.AccumulationHeight * 16;
        Encoder.clearBuffer(this.AccumulationBuffer, 0, RegionBytes);
        Encoder.clearBuffer(this.AccumulationBuffer, 3 * RegionBytes, RegionBytes);
    }

    // 📝 The display goes through DisplayReconstruction.wgsl when history is blended in or the denoiser has strength;
    //    otherwise it shows the exact sums directly.
    DenoiseStrength()
    {
        return this.Configuration?.DenoiseEnabled ? ResolveDenoiseStrength(this.PixelSamples()) : 0;
    }

    // 📝 A reduced trace always reconstructs: until every pixel has a sample, holes are upsampled from neighbours.
    ReconstructionActive()
    {
        return this.HistoryActive || this.DenoiseStrength() > 0 || this.TraceScale() > 1;
    }

    // 📝 Trace resolution: 1 = one invocation per pixel (reference), 2 = per 2 × 2 tile, 4 = per 4 × 4 tile. The shared
    //    subpixel pair is a (0,2)-sequence, so every scale² consecutive samples land once in each pixel of every tile.
    TraceScale()
    {
        const Scale = this.Configuration?.TraceScale ?? 1;
        return Scale === 2 || Scale === 4 ? Scale : 1;
    }

    // 📝 Samples per display pixel (SampleCount counts dispatched sample indices).
    PixelSamples()
    {
        return this.SampleCount / (this.TraceScale() * this.TraceScale());
    }

    TraceWorkgroups()
    {
        const Scale = this.TraceScale();
        return [Math.ceil(Math.ceil(this.AccumulationWidth / Scale) / WorkgroupEdge), Math.ceil(Math.ceil(this.AccumulationHeight / Scale) / WorkgroupEdge)];
    }

    // 📝 ① Guide buffers when the camera or scene changed, ② gather (history clamp + fade), ③ à-trous steps when the
    //    denoiser is on. Result: display radiance (rgb, 1) in region 3 of the reconstruction buffer.
    EncodeReconstruction(Encoder)
    {
        const Width = this.AccumulationWidth, Height = this.AccumulationHeight, PixelCount = Width * Height;
        const Workgroups = [Math.ceil(Width / WorkgroupEdge), Math.ceil(Height / WorkgroupEdge)];
        const Configuration = this.Configuration;
        if (this.FeaturesStale)
        {
            this.WriteTracingUniforms(this.SampleCount, 0, null, 0, 0, 2);
            const Compute = Encoder.beginComputePass({ timestampWrites: this.Timing.PassWrites('Features') });
            Compute.setPipeline(this.FeaturePipeline);
            Compute.setBindGroup(0, this.FeatureGroup);
            Compute.dispatchWorkgroups(...Workgroups);
            Compute.end();
            this.FeaturesStale = false;
        }
        const Strength  = this.DenoiseStrength();
        const Steps     = Strength > 0 ? AtrousSteps : [];
        const Footprint = 2.0 * Math.tan(Configuration.Camera.FieldOfView * Math.PI / 360.0) / Height;
        const HistoryOffset = this.HistoryRegion * PixelCount;
        const Bytes = new ArrayBuffer(ReconstructionSlotBytes * (1 + AtrousSteps.length));
        const WriteSlot = (Slot, Step, Mode, ReadOffset, WriteOffset) =>
        {
            const Base = Slot * ReconstructionSlotBytes / 4;
            new Uint32Array(Bytes).set([Width, Height, Step, Mode, ReadOffset, WriteOffset, HistoryOffset, this.SampleCount], Base);
            new Float32Array(Bytes).set([Configuration.HistoryClamp ?? 0, Configuration.HistorySpan ?? 8, Strength,
                                         (Configuration.DenoiseLuminanceSigma ?? 4) / Math.max(this.PixelSamples(), 1),
                                         Footprint, 64, 2, this.TraceScale()], Base + 8);
        };
        const HistoryMode = this.HistoryActive ? 1 : 0;
        WriteSlot(0, 0, HistoryMode | 4 | (Steps.length ? 0 : 2), 0, Steps.length ? 0 : 3 * PixelCount);
        let ReadRegion = 0;
        Steps.forEach((Step, Index) =>
        {
            const Last = Index === Steps.length - 1;
            const WriteRegion = Last ? 3 : (ReadRegion === 1 ? 2 : 1);
            WriteSlot(1 + Index, Step, Last ? 2 : 0, ReadRegion * PixelCount, WriteRegion * PixelCount);
            ReadRegion = WriteRegion;
        });
        this.Device.queue.writeBuffer(this.ReconstructionUniformBuffer, 0, Bytes);
        const Compute = Encoder.beginComputePass({ timestampWrites: this.Timing.PassWrites('Reconstruction') });
        Compute.setPipeline(this.GatherPipeline);
        Compute.setBindGroup(0, this.ReconstructionGroups[0]);
        Compute.dispatchWorkgroups(...Workgroups);
        Compute.setPipeline(this.AtrousPipeline);
        Steps.forEach((_, Index) => { Compute.setBindGroup(0, this.ReconstructionGroups[1 + Index]); Compute.dispatchWorkgroups(...Workgroups); });
        Compute.end();
    }

    // 📝 Linear radiance exactly as displayed, as (rgb sum, weight) texels: reconstruction region 3 when active, the exact
    //    sums otherwise. Feed to ResolveDisplayPixels without history.
    async RetrieveDisplayRadiance()
    {
        this.FlushPendingReprojection();
        if (!this.ReconstructionActive())
        {
            const Encoder = this.Device.createCommandEncoder();
            const Pending = this.Timing.Encode(Encoder);
            this.Device.queue.submit([Encoder.finish()]);
            this.Timing.Collect(Pending);
            return this.RetrieveAccumulation();
        }
        const Encoder = this.Device.createCommandEncoder();
        this.EncodeReconstruction(Encoder);
        const Pending = this.Timing.Encode(Encoder);
        this.Device.queue.submit([Encoder.finish()]);
        this.Timing.Collect(Pending);
        const RegionBytes = this.AccumulationWidth * this.AccumulationHeight * 16;
        return this.RetrieveBuffer(this.ReconstructionBuffer, RegionBytes, 3 * RegionBytes);
    }

    // 📝 Camera = orbit about a target. Returns the vectors the integration uniform expects.
    static ResolveCameraVectors(Camera, Aspect)
    {
        const CosPitch = Math.cos(Camera.Pitch);
        const Origin = [Camera.Target[0] + Camera.Distance * CosPitch * Math.sin(Camera.Yaw),
                        Camera.Target[1] + Camera.Distance * Math.sin(Camera.Pitch),
                        Camera.Target[2] + Camera.Distance * CosPitch * Math.cos(Camera.Yaw)];
        const Normalize = V => { const L = Math.hypot(V[0], V[1], V[2]) || 1; return V.map(C => C / L); };
        const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
        const Forward = Normalize([Camera.Target[0] - Origin[0], Camera.Target[1] - Origin[1], Camera.Target[2] - Origin[2]]);
        const Right   = Normalize(Cross(Forward, [0, 1, 0]));
        const Up      = Cross(Right, Forward);
        const TangentHalf = Math.tan(Camera.FieldOfView * Math.PI / 360.0);
        return { Origin, Forward, Right: Right.map(C => C * TangentHalf * Aspect), Up: Up.map(C => C * TangentHalf) };
    }

    WriteTracingUniforms(FirstSample, SampleBatch, PreviousCamera = null, HistoryReadOffset = 0, HistoryWriteOffset = 0, UniformSlot = 0)
    {
        const Configuration = this.Configuration;
        const Camera = this.ResolveCurrentCamera();
        const Bytes  = new ArrayBuffer(TracingUniformBytes);
        const Floats = new Float32Array(Bytes), Words = new Uint32Array(Bytes);
        const FocusDistance = Configuration.FocusDistance > 0 ? Configuration.FocusDistance : Configuration.Camera.Distance;
        Floats.set([...Camera.Origin, Configuration.ApertureRadius], 0);
        Floats.set([...Camera.Forward, FocusDistance], 4);
        Floats.set([...Camera.Right, 0], 8);
        Floats.set([...Camera.Up, 0], 12);
        Floats.set([1, 1, 1, Configuration.EnvironmentIntensity * (Configuration.NightMode ? NightSkyFraction : 1)], 16);
        Words.set([this.AccumulationWidth, this.AccumulationHeight, FirstSample, SampleBatch], 20);
        Words.set([Configuration.BounceLimit, Configuration.DepthLimit, this.SceneSummary.LightCount, Configuration.ShowcaseEnabled === false ? 0 : this.SceneSummary.SphereCount], 24);
        Words.set([this.FloorSlot, Configuration.EnvironmentCategory, this.SceneSummary.BoundsEntryCount, Configuration.VehicleEnabled === false ? 0 : 1], 28);
        Floats.set([Configuration.ThroughputCutoff, FloorRadius, Configuration.LightsVisibleToCamera ? 1 : 0, 0], 32);
        const Previous = PreviousCamera ?? Camera;
        Floats.set([...Previous.Origin, 0, ...Previous.Forward, 0, ...Previous.Right, 0, ...Previous.Up, 0], 36);
        Words.set([PointPatternCodes[Configuration.PointPattern] ?? 0, this.RasterizedVisibilityActive() ? 1 : 0, HistoryReadOffset, HistoryWriteOffset], 52);
        Floats.set([Configuration.HistoryCapacity ?? 32, this.HistoryActive ? 1 : 0, 0, 0], 56);
        const Layout = this.GeometryLayout;
        Words.set([Layout.InstanceRoot, Layout.RecordBase, Layout.InstanceCount, this.TraceScale()], 60);
        Floats.set([0, 0, Configuration.LampSampling === 'all' ? 1 : 0, LampCutLimits[Configuration.LampSampling] ?? 1], 64);
        const Practical = this.PracticalLayout;
        Words.set([Practical.LampCount, Practical.NodeBase, Practical.LampBase, Practical.ProfileBase], 68);
        Floats.set(this.SphereBounds ?? [0, 0, 0, 0], 72);
        this.Device.queue.writeBuffer(this.TracingUniformBuffer, UniformSlot * TracingSlotBytes, Bytes);
    }

    // 📝 Unified single-submit frame step: encodes Reprojection, Integration, Features, Reconstruction, and Display into
    //    one CommandEncoder and one queue.submit() so interactive frames avoid multi-submission Dawn/D3D12 overhead.
    StepFrame(SampleBatch, Exposure)
    {
        if (this.ReadbackSurface || (SampleBatch > 1 && this.RasterizedVisibilityActive()))
        {
            if (SampleBatch > 0) this.AdvanceSamples(SampleBatch);
            return this.PresentDisplay(Exposure);
        }
        const Encoder = this.Device.createCommandEncoder();
        this.EncodePendingReprojection(Encoder);
        if (SampleBatch > 0)
        {
            const Rasterized = this.RasterizedVisibilityActive();
            this.WriteTracingUniforms(this.SampleCount, SampleBatch, null, 0, 0, 0);
            if (Rasterized) this.EncodeVisibilityRasterization(Encoder, this.SampleCount);
            if (this.WavefrontActive())
            {
                this.EncodeWavefrontSamples(Encoder, this.SampleCount, SampleBatch, true, true);
            }
            else
            {
                const Compute = Encoder.beginComputePass({ timestampWrites: this.Timing.PassWrites('Integration') });
                Compute.setPipeline(this.IntegrationPipeline);
                Compute.setBindGroup(0, this.IntegrationGroup);
                Compute.dispatchWorkgroups(...this.TraceWorkgroups());
                Compute.end();
            }
            this.SampleCount += SampleBatch;
        }
        const Reconstructed = this.ReconstructionActive();
        const PixelCount = this.AccumulationWidth * this.AccumulationHeight;
        const Bytes = new ArrayBuffer(DisplayUniformBytes);
        new Uint32Array(Bytes).set([this.AccumulationWidth, this.AccumulationHeight, 0, Reconstructed ? 3 * PixelCount : 0], 0);
        new Float32Array(Bytes).set([Exposure, 0, Reconstructed ? 1 : 0, 0], 4);
        this.Device.queue.writeBuffer(this.DisplayUniformBuffer, 0, Bytes);
        if (Reconstructed) this.EncodeReconstruction(Encoder);
        const Raster = Encoder.beginRenderPass({ colorAttachments: [{
            view: this.CanvasContext.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 }
        }], timestampWrites: this.Timing.PassWrites('Display') });
        Raster.setPipeline(this.DisplayPipeline);
        Raster.setBindGroup(0, this.DisplayGroup);
        Raster.draw(3);
        Raster.end();
        const Pending = this.Timing.Encode(Encoder);
        this.Device.queue.submit([Encoder.finish()]);
        this.Timing.Collect(Pending);
    }

    // 📝 Integrates samples [SampleCount, SampleCount + SampleBatch). Sample indices — and therefore results — do not
    //    depend on batch size, timing or device speed.
    //    With the visibility raster each sample needs its own raster (its own subpixel offset), so samples are submitted
    //    one by one; the integrated values are the same either way.
    AdvanceSamples(SampleBatch)
    {
        this.FlushPendingReprojection();
        const Rasterized = this.RasterizedVisibilityActive();
        const Wavefront  = this.WavefrontActive();
        const MaxChunk   = Rasterized ? 1 : (Wavefront ? 8 : SampleBatch);
        for (let Submitted = 0; Submitted < SampleBatch; Submitted += MaxChunk)
        {
            const FirstSample = this.SampleCount + Submitted;
            const Count       = Math.min(MaxChunk, SampleBatch - Submitted);
            this.WriteTracingUniforms(FirstSample, Count);
            const Encoder = this.Device.createCommandEncoder();
            if (Rasterized) this.EncodeVisibilityRasterization(Encoder, FirstSample);
            if (Wavefront)
            {
                this.EncodeWavefrontSamples(Encoder, FirstSample, Count, Submitted === 0, Submitted + Count >= SampleBatch);
            }
            else
            {
                const Compute = Encoder.beginComputePass({ timestampWrites: this.Timing.PassWrites('Integration', Submitted === 0, Submitted + Count >= SampleBatch) });
                Compute.setPipeline(this.IntegrationPipeline);
                Compute.setBindGroup(0, this.IntegrationGroup);
                Compute.dispatchWorkgroups(...this.TraceWorkgroups());
                Compute.end();
            }
            this.Device.queue.submit([Encoder.finish()]);
        }
        this.SampleCount += SampleBatch;
    }

    // 📝 Raster replaces only pinhole camera rays to the vehicle; a thick lens, a hidden vehicle or a reduced trace
    //    resolution (one ray per tile) uses the traced path.
    RasterizedVisibilityActive()
    {
        const Configuration = this.Configuration;
        return Boolean(Configuration?.VisibilityRasterEnabled) && Configuration.ApertureRadius <= 0 && Configuration.VehicleEnabled !== false
            && this.TraceScale() === 1;
    }

    EncodeVisibilityRasterization(Encoder, SampleIndex)
    {
        const Camera = this.ResolveCurrentCamera();
        const Bytes  = new ArrayBuffer(RasterUniformBytes);
        new Float32Array(Bytes).set([...Camera.Origin, 0, ...Camera.Forward, 0, ...Camera.Right, 0, ...Camera.Up, 0], 0);
        new Uint32Array(Bytes).set([this.AccumulationWidth, this.AccumulationHeight, SampleIndex, this.GeometryLayout.RecordBase], 16);
        this.Device.queue.writeBuffer(this.RasterUniformBuffer, 0, Bytes);
        const Raster = Encoder.beginRenderPass({
            colorAttachments: [{ view: this.VisibilityTexture.createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
            depthStencilAttachment: { view: this.RasterDepthTexture.createView(), depthLoadOp: 'clear', depthStoreOp: 'discard', depthClearValue: 1.0 }
        });
        Raster.setPipeline(this.RasterPipeline);
        Raster.setBindGroup(0, this.RasterGroup);
        Raster.draw(this.VehicleTree.ReferenceCount * 3, this.GeometryLayout.InstanceCount);
        Raster.end();
    }

    PresentDisplay(Exposure)
    {
        this.FlushPendingReprojection();
        if (this.ReadbackSurface) return this.PresentThroughReadback(Exposure);
        const Reconstructed = this.ReconstructionActive();
        const PixelCount = this.AccumulationWidth * this.AccumulationHeight;
        const Bytes = new ArrayBuffer(DisplayUniformBytes);
        new Uint32Array(Bytes).set([this.AccumulationWidth, this.AccumulationHeight, 0, Reconstructed ? 3 * PixelCount : 0], 0);
        new Float32Array(Bytes).set([Exposure, 0, Reconstructed ? 1 : 0, 0], 4);
        this.Device.queue.writeBuffer(this.DisplayUniformBuffer, 0, Bytes);
        const Encoder = this.Device.createCommandEncoder();
        if (Reconstructed) this.EncodeReconstruction(Encoder);
        const Raster  = Encoder.beginRenderPass({ colorAttachments: [{
            view: this.CanvasContext.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 }
        }], timestampWrites: this.Timing.PassWrites('Display') });
        Raster.setPipeline(this.DisplayPipeline);
        Raster.setBindGroup(0, this.DisplayGroup);
        Raster.draw(3);
        Raster.end();
        const Pending = this.Timing.Encode(Encoder);
        this.Device.queue.submit([Encoder.finish()]);
        this.Timing.Collect(Pending);
    }

    async PresentThroughReadback(Exposure)
    {
        const Width = this.AccumulationWidth, Height = this.AccumulationHeight;
        const Pixels = ResolveDisplayPixels(await this.RetrieveDisplayRadiance(), Width, Height, Exposure);
        this.ReadbackSurface.putImageData(new ImageData(Pixels, Width, Height), 0, 0);
    }

    async RetrieveBuffer(Source, ByteLength, ByteOffset = 0)
    {
        const Staging = this.Device.createBuffer({ size: ByteLength, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const Encoder = this.Device.createCommandEncoder();
        Encoder.copyBufferToBuffer(Source, ByteOffset, Staging, 0, ByteLength);
        this.Device.queue.submit([Encoder.finish()]);
        await Staging.mapAsync(GPUMapMode.READ);
        const Copy = new Float32Array(Staging.getMappedRange().slice(0));
        Staging.unmap();
        Staging.destroy();
        return Copy;
    }

    RetrieveAccumulation()
    {
        this.FlushPendingReprojection();
        return this.RetrieveBuffer(this.AccumulationBuffer, this.AccumulationWidth * this.AccumulationHeight * 16);
    }

    RetrieveHistory()
    {
        const RegionBytes = this.AccumulationWidth * this.AccumulationHeight * 16;
        return this.RetrieveBuffer(this.AccumulationBuffer, RegionBytes, this.HistoryRegion * RegionBytes);
    }

    // 📝 Profiling: integrates samples [0, SampleTotal) with the ray-statistics pipeline (counters instead of radiance)
    //    and returns per pixel-sample means of closest rays, shadow rays, child box tests and triangle tests, plus the
    //    worst pixel. Clears the accumulation afterwards; the caller re-renders.
    async MeasureRayStatistics(SampleTotal = 1)
    {
        this.FlushPendingReprojection();
        this.StatisticsPipeline ??= this.Device.createComputePipeline({ layout: this.Device.createPipelineLayout({ bindGroupLayouts: [this.IntegrationLayout] }),
            compute: { module: this.IntegrationUnit, entryPoint: 'IntegrateRadiance', constants: { RayStatisticsEnabled: 1 } } });
        this.ResetAccumulation();
        this.WriteTracingUniforms(0, SampleTotal);
        const Encoder = this.Device.createCommandEncoder();
        if (this.RasterizedVisibilityActive()) this.EncodeVisibilityRasterization(Encoder, 0);
        const Compute = Encoder.beginComputePass();
        Compute.setPipeline(this.StatisticsPipeline);
        Compute.setBindGroup(0, this.IntegrationGroup);
        Compute.dispatchWorkgroups(...this.TraceWorkgroups());
        Compute.end();
        this.Device.queue.submit([Encoder.finish()]);
        const PixelCount = this.AccumulationWidth * this.AccumulationHeight, RegionBytes = PixelCount * 16;
        const Counts  = await this.RetrieveBuffer(this.AccumulationBuffer, RegionBytes);
        const Moments = await this.RetrieveBuffer(this.AccumulationBuffer, RegionBytes, 3 * RegionBytes);
        const Totals = [0, 0, 0, 0];
        let Samples = 0, WorstRays = 0;
        for (let Pixel = 0; Pixel < PixelCount; Pixel++)
        {
            Totals[0] += Counts[Pixel * 4]; Totals[1] += Counts[Pixel * 4 + 1]; Totals[2] += Moments[Pixel * 4]; Totals[3] += Counts[Pixel * 4 + 2];
            Samples += Counts[Pixel * 4 + 3];
            WorstRays = Math.max(WorstRays, (Counts[Pixel * 4] + Counts[Pixel * 4 + 1]) / Math.max(1, Counts[Pixel * 4 + 3]));
        }
        this.ResetAccumulation();
        const Mean = Value => Value / Math.max(1, Samples);
        return { ClosestRays: Mean(Totals[0]), ShadowRays: Mean(Totals[1]), BoxTests: Mean(Totals[2]), TriangleTests: Mean(Totals[3]), WorstPixelRays: WorstRays, Samples };
    }

    async ComputeAccumulationDigest()
    {
        return DigestFloatBuffer(await this.RetrieveAccumulation());
    }

    // 📝 Validation: runs the shader's own QuadraturePair for k < SampleTotal, p < PairTotal through the accumulation
    //    buffer (which must hold at least SampleTotal × PairTotal texels), then resets it.
    async ExtractQuadratureOnDevice(SampleTotal, PairTotal)
    {
        const Bytes = new ArrayBuffer(TracingUniformBytes);
        new Uint32Array(Bytes).set([SampleTotal, PairTotal, 0, 0], 20);
        this.Device.queue.writeBuffer(this.TracingUniformBuffer, 0, Bytes);
        const Encoder = this.Device.createCommandEncoder();
        const Compute = Encoder.beginComputePass();
        Compute.setPipeline(this.QuadraturePipeline);
        Compute.setBindGroup(0, this.IntegrationGroup);
        Compute.dispatchWorkgroups(Math.ceil(SampleTotal * PairTotal / 64));
        Compute.end();
        this.Device.queue.submit([Encoder.finish()]);
        const Points = await this.RetrieveBuffer(this.AccumulationBuffer, SampleTotal * PairTotal * 16);
        this.ResetAccumulation();
        return Points;
    }
}
