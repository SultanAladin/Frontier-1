//============================================================================================================================================
//                                                          VARIANCEINTEGRATOR.JS
//============================================================================================================================================
// 🧩 Reconstruction of one resolved signal: temporal moments, à-trous wavelet iterations, joint bilateral screen projection.

// 📝 Instantiated twice — once for the ReSTIR irradiance, once for the glossy reflection — with its own history and
//    settings. The first wavelet iteration's output is the history the next frame integrates against (SVGF feedback).
export class VarianceIntegrator
{
    // 📝 Settings: MinimumWeight, TallyLimit, WaveletIterations (0–4), ColourSigma.
    constructor(Exchange, Shaders, Label, Settings)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Label    = Label;
        this.Settings = Settings;
    }

    async ConstructPrograms(FrameRecords)
    {
        this.FrameRecords = FrameRecords;
        const Compute = GPUShaderStage.COMPUTE;
        this.TemporalLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Uniform", "Texture2D", "Texture2D", "Texture2D", "Texture2D", "Texture2D", "Texture2D",
             "StorageTexture2D:rgba16float", "StorageTexture2D:rgba16float"], Compute, `${this.Label} temporal`);
        this.WaveletLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Uniform", "Texture2D", "Texture2D", "StorageTexture2D:rgba16float"], Compute, `${this.Label} wavelet`);
        this.ProjectionLayout = this.Exchange.ConstructDescriptorLayout(
            ["Uniform", "Texture2D", "Texture2D", "Texture2D", "TextureDepth2D", "StorageTexture2D:rgba16float"], Compute, `${this.Label} projection`);
        this.TemporalProgram   = await this.Shaders.ConstructComputeProgram("VarianceTemporal.wgsl", "IntegrateTemporal", [this.TemporalLayout]);
        this.WaveletProgram    = await this.Shaders.ConstructComputeProgram("VarianceWavelet.wgsl", "ConvolveWavelet", [this.WaveletLayout]);
        this.ProjectionProgram = await this.Shaders.ConstructComputeProgram("VarianceUpsample.wgsl", "ProjectScreenSignal", [this.ProjectionLayout]);
        this.StepRecords = [];
        for (let Iteration = 0; Iteration < 5; ++Iteration)
            this.StepRecords.push(this.Exchange.ConstructRecords(16, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, `${this.Label} step ${Iteration}`));
        this.AssignSettings(this.Settings);
    }

    AssignSettings(Settings)
    {
        this.Settings = Settings;
        if (!this.StepRecords)
            return;
        for (let Iteration = 0; Iteration < 5; ++Iteration)
        {
            const Bytes = new ArrayBuffer(16);
            new Uint32Array(Bytes, 0, 1)[0] = 1 << Math.max(0, Iteration - 1);
            new Float32Array(Bytes, 4, 3).set([Settings.MinimumWeight, Settings.ColourSigma, Settings.TallyLimit]);
            this.Exchange.Device.queue.writeBuffer(this.StepRecords[Iteration], 0, Bytes);
        }
    }

    // 📝 Source views are per frame parity (the resolved signal may itself be ping-ponged); guides likewise.
    ConstructTargets(TraceExtent, ScreenExtent, Surface, SourceViews, GuideViews)
    {
        for (const Texture of this.Textures || [])
            Texture.destroy();
        const Construct = (Label, Extent) => this.Exchange.ConstructTexture({ label: `${this.Label} ${Label}`, size: Extent, format: "rgba16float",
                                                                              usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });
        const Integrated = Construct("integrated", TraceExtent);
        const Retained   = [Construct("retained A", TraceExtent), Construct("retained B", TraceExtent)];
        const Moments    = [Construct("moments A", TraceExtent), Construct("moments B", TraceExtent)];
        const Wavelet    = [Construct("wavelet A", TraceExtent), Construct("wavelet B", TraceExtent)];
        const Screen     = Construct("screen", ScreenExtent);
        this.Textures = [Integrated, ...Retained, ...Moments, ...Wavelet, Screen];
        const View = (Texture) => Texture.createView();
        this.Views = { Integrated: View(Integrated), Retained: Retained.map(View), Moments: Moments.map(View),
                       Wavelet: Wavelet.map(View), Screen: View(Screen) };
        this.TraceExtent = TraceExtent;
        this.ScreenExtent = ScreenExtent;

        this.TemporalGroups = [0, 1].map((Parity) => this.Exchange.ConstructDescriptorGroup(this.TemporalLayout,
            [this.FrameRecords, this.StepRecords[0], SourceViews[Parity], Surface.Views.Motion, GuideViews[Parity], GuideViews[1 - Parity],
             this.Views.Retained[1 - Parity], this.Views.Moments[1 - Parity], this.Views.Integrated, this.Views.Moments[Parity]]));
        // 📝 Iteration 0 writes the history; later iterations alternate between the two wavelet targets.
        this.WaveletGroups = [0, 1].map((Parity) =>
        {
            const Groups = [];
            let Source = this.Views.Integrated;
            for (let Iteration = 0; Iteration < 5; ++Iteration)
            {
                const Target = Iteration === 0 ? this.Views.Retained[Parity] : this.Views.Wavelet[(Iteration - 1) & 1];
                Groups.push({ Group: this.Exchange.ConstructDescriptorGroup(this.WaveletLayout,
                                         [this.FrameRecords, this.StepRecords[Iteration], Source, GuideViews[Parity], Target]), Target });
                Source = Target;
            }
            return Groups;
        });
        this.ProjectionGroups = [0, 1].map((Parity) =>
        {
            const Groups = [];
            const Candidates = [SourceViews[Parity], this.Views.Retained[Parity], this.Views.Wavelet[0], this.Views.Wavelet[1]];
            for (const Candidate of Candidates)
                Groups.push(this.Exchange.ConstructDescriptorGroup(this.ProjectionLayout,
                    [this.FrameRecords, Candidate, GuideViews[Parity], Surface.Views.Attributes, Surface.Views.Depth, this.Views.Screen]));
            return Groups;
        });
    }

    Record(Encoder, Parity, Reconstruct, TimestampWrites)
    {
        const [TraceWidth, TraceHeight] = this.TraceExtent;
        const Pass = Encoder.beginComputePass({ label: `${this.Label} reconstruction`, timestampWrites: TimestampWrites });
        let Projection = 0;
        if (Reconstruct)
        {
            Pass.setPipeline(this.TemporalProgram);
            Pass.setBindGroup(0, this.TemporalGroups[Parity]);
            Pass.dispatchWorkgroups(Math.ceil(TraceWidth / 8), Math.ceil(TraceHeight / 8));
            const Iterations = Math.max(1, Math.min(4, this.Settings.WaveletIterations + 1));
            Pass.setPipeline(this.WaveletProgram);
            for (let Iteration = 0; Iteration < Iterations; ++Iteration)
            {
                Pass.setBindGroup(0, this.WaveletGroups[Parity][Iteration].Group);
                Pass.dispatchWorkgroups(Math.ceil(TraceWidth / 8), Math.ceil(TraceHeight / 8));
            }
            Projection = Iterations === 1 ? 1 : 2 + ((Iterations - 2) & 1);
        }
        Pass.setPipeline(this.ProjectionProgram);
        Pass.setBindGroup(0, this.ProjectionGroups[Parity][Projection]);
        Pass.dispatchWorkgroups(Math.ceil(this.ScreenExtent[0] / 8), Math.ceil(this.ScreenExtent[1] / 8));
        Pass.end();
    }
}
