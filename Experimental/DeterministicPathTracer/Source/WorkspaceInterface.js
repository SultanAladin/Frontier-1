/*====================================================================================================================================
                                                     WORKSPACEINTERFACE.JS
====================================================================================================================================*/
// 🧩 Workspace wiring: inspector fields, orbit camera input, progressive accumulation loop and determinism verification

import { TracingDivision, ResolveDisplayPixels } from './TracingDivision.js';
import { PaintPresets, FloorPresets, EnvironmentPresets } from './StudioSceneAssembly.js';
import { PracticalModes } from './PracticalLighting.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const QueryParameters             = new URLSearchParams(location.search);
const ValidationEnabled           = QueryParameters.has('validation');
const ReadbackPresentationEnabled = QueryParameters.get('present') === 'readback';

// 📝 Orbit presets: yaw about +Y from +Z, pitch above the horizon, distance to the target [m].
const CameraPresets = {
    Hero:    { Label: 'Hero',    Yaw: 0.78,  Pitch: 0.13, Distance: 9.6,  Target: [0.1, 0.70, 1.0],  FieldOfView: 36 },
    Profile: { Label: 'Profile', Yaw: 0.0,   Pitch: 0.06, Distance: 12.5, Target: [0.0, 0.80, 0.0],   FieldOfView: 34 },
    Front:   { Label: 'Front',   Yaw: 1.5708, Pitch: 0.10, Distance: 11.0, Target: [0.0, 0.75, 0.6],  FieldOfView: 30 },
    Spheres: { Label: 'Spheres', Yaw: 0.0,   Pitch: 0.20, Distance: 6.8,  Target: [0.0, 0.50, 2.9],   FieldOfView: 42 },
    Rear:    { Label: 'Rear',    Yaw: 3.95,  Pitch: 0.16, Distance: 11.8, Target: [0.0, 0.80, 0.0],   FieldOfView: 34 },
    Fleet:   { Label: 'Fleet',   Yaw: 0.62,  Pitch: 0.42, Distance: 52.0, Target: [0.0, 0.60, -14.0], FieldOfView: 44 }
};

const VehicleCountOptions = [1, 2, 4, 8, 16, 32, 50];

const Configuration = {
    PaintPreset:          QueryParameters.get('paint') || 'Authored',
    PaintRoughness:       0.0775,
    FloorPreset:          'Epoxy',
    EnvironmentPreset:    'Studio',
    EnvironmentCategory:  0,
    EnvironmentIntensity: 1.5,
    KeyIntensity:         4.0,
    RimIntensity:         12.0,
    FillIntensity:        3.0,
    LampExitance:         0.2,
    // 📝 Practical lamps (PracticalLighting.js): IES point emitters on every car and over the car park, one drawn per
    //    vertex from the light tree ('tree') or all of them ('all', the reference rule).
    PracticalMode:        Object.hasOwn(PracticalModes, QueryParameters.get('lamps') ?? '') ? QueryParameters.get('lamps') : 'off',
    PracticalIntensity:   Number(QueryParameters.get('lampscale') ?? 1),
    LampSampling:         ['tree', 'split4', 'split8', 'all'].includes(QueryParameters.get('lampsampling')) ? QueryParameters.get('lampsampling') : 'split8',
    NightMode:            QueryParameters.get('night') === '1',
    ExposureStops:        0.0,
    LightsVisibleToCamera: false,
    BounceLimit:          2,
    DepthLimit:           10,
    ThroughputCutoff:     0.002,
    SampleBatch:          1,
    RenderScale:          Number(QueryParameters.get('scale') || 0.75),
    TraceScale:           Number(QueryParameters.get('trace') || 1),
    TargetSamples:        Number(QueryParameters.get('target') || 1024),
    VehicleEnabled:       true,
    VehicleCount:         VehicleCountOptions.includes(Number(QueryParameters.get('vehicles'))) ? Number(QueryParameters.get('vehicles')) : 1,
    ShowcaseEnabled:      true,
    PointPattern:         ['pixel', 'bluenoise'].includes(QueryParameters.get('pattern')) ? QueryParameters.get('pattern') : 'shared',
    IntegratorMode:       ['wavefront', 'megakernel', 'auto'].includes(QueryParameters.get('integrator')) ? QueryParameters.get('integrator') : 'auto',
    VisibilityRasterEnabled:   QueryParameters.get('visibility') === 'raster',
    ReprojectionEnabled:       QueryParameters.get('reproject') === '1',
    HistoryCapacity:      32,
    // 📝 Display reconstruction (DisplayReconstruction.wgsl): never touches the exact sums.
    DenoiseEnabled:       QueryParameters.get('denoise') === '1',
    DenoiseLuminanceSigma: Number(QueryParameters.get('sigma') ?? 4),
    HistoryClamp:         Number(QueryParameters.get('clamp') ?? 0),     // [σ]   - Neighbourhood clamp of history; 0 = off (measured: hurts at 1–2 spp)
    HistorySpan:          Number(QueryParameters.get('span') ?? 8),      // [spp] - History fully faded after this many exact samples
    ApertureRadius:       0.0,
    FocusDistance:        0.0,
    CameraPreset:         QueryParameters.get('camera') || 'Hero',
    Camera:               null
};
Configuration.Camera = { ...CameraPresets[Configuration.CameraPreset] };

const Division = new TracingDivision();
const Interface = {
    Paused:                false,
    VerifyActive:          false,
    StartTime:             performance.now(),
    AccumulatedTime:       0,
    DisplayStale:          true,
    ConfigurationRevision: 0,
    FrameStamps:           [],     // 📝 performance.now() of recent traced frames (last FrameRateWindow ms, at least 2)
    FrameMilliseconds:     0,      // 📝 Smoothed interval between completed frames [ms]
    FrameRateRefreshed:    0,
    FramesInFlight:        [],     // 📝 onSubmittedWorkDone promises of submitted, unfinished frames
    LastCompletion:        0
};
const FrameRateWindow = 1000;      // [ms] - Sliding window of the FPS counter
const FramesInFlightLimit = 2;     // [-]  - CPU may run this many frames ahead of the GPU (no per-frame stall)


//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

function Locate(Identifier)
{
    return document.getElementById(Identifier);
}

function DescribeGeometry()
{
    const Summary = Division.SceneSummary;
    const Lamps = Summary.LampCount ? ` · ${Summary.LampCount} lamps (tree depth ${Summary.LightTreeDepth})` : '';
    Locate('StatusGeometry').textContent = `${Summary.TriangleCount.toLocaleString('en-US')} tris · ${Summary.BoundsEntryCount.toLocaleString('en-US')} bounds · ${Summary.SphereCount} spheres · ${Summary.LightCount} softboxes${Lamps}`;
}

function PopulateSelect(Identifier, Options, Current, OnSelect)
{
    const Select = Locate(Identifier);
    for (const [Key, Label] of Options)
    {
        const Option = document.createElement('option');
        Option.value = Key;
        Option.textContent = Label;
        Select.appendChild(Option);
    }
    Select.value = String(Current);
    Select.addEventListener('change', () => OnSelect(Select.value));
}

function AttachSlider(Identifier, Current, Format, OnInput)
{
    const Slider = Locate(Identifier);
    const Readout = Slider.parentElement.querySelector('output');
    Slider.value = Current;
    Readout.textContent = Format(Number(Current));
    Slider.addEventListener('input', () =>
    {
        Readout.textContent = Format(Number(Slider.value));
        OnInput(Number(Slider.value));
    });
    return Slider;
}

// 📝 CameraMoved = true lets the division reproject the current image instead of discarding it (if enabled).
function InvalidateAccumulation(SceneChanged = false, CameraMoved = false)
{
    if (SceneChanged)
    {
        Division.ConfigureScene(Configuration);
    }
    else
    {
        Division.Configuration = { ...Configuration };
        Division.ResetAccumulation(CameraMoved);
    }
    Interface.AccumulatedTime = 0;
    Interface.StartTime       = performance.now();
    Interface.ConfigurationRevision++;
}

function ConstructInspector()
{
    const PresetRow = Locate('CameraPresetRow');
    for (const [Key, Preset] of Object.entries(CameraPresets))
    {
        const Button = document.createElement('button');
        Button.textContent = Preset.Label;
        Button.dataset.preset = Key;
        Button.classList.toggle('Selected', Key === Configuration.CameraPreset);
        Button.addEventListener('click', () =>
        {
            Configuration.CameraPreset = Key;
            Configuration.Camera = { ...Preset, Target: [...Preset.Target] };
            Locate('FieldOfViewSlider').value = Preset.FieldOfView;
            Locate('FieldOfViewSlider').dispatchEvent(new Event('input'));
            PresetRow.querySelectorAll('button').forEach(Other => Other.classList.toggle('Selected', Other === Button));
            InvalidateAccumulation(false, true);
        });
        PresetRow.appendChild(Button);
    }

    const Fixed = Digits => Amount => Amount.toFixed(Digits);
    AttachSlider('FieldOfViewSlider', Configuration.Camera.FieldOfView, Amount => `${Amount}°`, Amount => { Configuration.Camera.FieldOfView = Amount; InvalidateAccumulation(false, true); });
    AttachSlider('ApertureSlider', Configuration.ApertureRadius, Fixed(3), Amount => { Configuration.ApertureRadius = Amount; InvalidateAccumulation(); });

    PopulateSelect('PaintSelect', Object.entries(PaintPresets).map(([Key, Preset]) => [Key, Preset.Label]), Configuration.PaintPreset,
        Key => { Configuration.PaintPreset = Key; InvalidateAccumulation(true); });
    AttachSlider('PaintRoughnessSlider', Configuration.PaintRoughness, Fixed(3), Amount => { Configuration.PaintRoughness = Amount; InvalidateAccumulation(true); });
    PopulateSelect('FloorSelect', Object.entries(FloorPresets).map(([Key, Preset]) => [Key, Preset.Label]), Configuration.FloorPreset,
        Key => { Configuration.FloorPreset = Key; InvalidateAccumulation(true); });
    PopulateSelect('EnvironmentSelect', Object.entries(EnvironmentPresets).map(([Key, Preset]) => [Key, Preset.Label]), Configuration.EnvironmentPreset,
        Key => { Configuration.EnvironmentPreset = Key; Configuration.EnvironmentCategory = EnvironmentPresets[Key].Category; InvalidateAccumulation(); });
    AttachSlider('EnvironmentSlider', Configuration.EnvironmentIntensity, Fixed(2), Amount => { Configuration.EnvironmentIntensity = Amount; InvalidateAccumulation(); });
    AttachSlider('KeySlider', Configuration.KeyIntensity, Fixed(2), Amount => { Configuration.KeyIntensity = Amount; InvalidateAccumulation(true); });
    AttachSlider('RimSlider', Configuration.RimIntensity, Fixed(1), Amount => { Configuration.RimIntensity = Amount; InvalidateAccumulation(true); });
    AttachSlider('FillSlider', Configuration.FillIntensity, Fixed(2), Amount => { Configuration.FillIntensity = Amount; InvalidateAccumulation(true); });
    PopulateSelect('LightVisibilitySelect', [['hidden', 'Hidden (reflections only)'], ['visible', 'Visible']], Configuration.LightsVisibleToCamera ? 'visible' : 'hidden',
        Key => { Configuration.LightsVisibleToCamera = Key === 'visible'; InvalidateAccumulation(); });
    AttachSlider('LampSlider', Configuration.LampExitance, Fixed(2), Amount => { Configuration.LampExitance = Amount; InvalidateAccumulation(true); });
    PopulateSelect('PracticalSelect', Object.entries(PracticalModes).map(([Key, Mode]) => [Key, Mode.Label]), Configuration.PracticalMode,
        Key => { Configuration.PracticalMode = Key; InvalidateAccumulation(true); DescribeGeometry(); });
    AttachSlider('PracticalSlider', Configuration.PracticalIntensity, Fixed(2), Amount => { Configuration.PracticalIntensity = Amount; InvalidateAccumulation(true); });
    PopulateSelect('RigSelect', [['day', 'Studio rig + sky'], ['night', 'Night (rig off, sky 2 %)']], Configuration.NightMode ? 'night' : 'day',
        Key => { Configuration.NightMode = Key === 'night'; InvalidateAccumulation(true); DescribeGeometry(); });
    AttachSlider('ExposureSlider', Configuration.ExposureStops, Amount => `${Amount >= 0 ? '+' : ''}${Amount.toFixed(1)}`,
        Amount => { Configuration.ExposureStops = Amount; Interface.DisplayStale = true; });

    PopulateSelect('BounceSelect', [[1, '1 (direct + 1)'], [2, '2'], [3, '3'], [4, '4']], Configuration.BounceLimit,
        Key => { Configuration.BounceLimit = Number(Key); InvalidateAccumulation(); });
    PopulateSelect('DepthSelect', [[6, '6'], [10, '10'], [14, '14']], Configuration.DepthLimit,
        Key => { Configuration.DepthLimit = Number(Key); InvalidateAccumulation(); });
    PopulateSelect('BatchSelect', [[1, '1'], [2, '2'], [4, '4'], [8, '8']], Configuration.SampleBatch,
        Key => { Configuration.SampleBatch = Number(Key); });
    PopulateSelect('LampSamplingSelect', [['tree', 'Light tree, 1 lamp per hit'], ['split4', 'Light tree, split ≤ 4'], ['split8', 'Light tree, split ≤ 8'], ['all', 'Every lamp (reference)']], Configuration.LampSampling,
        Key => { Configuration.LampSampling = Key; InvalidateAccumulation(); });
    PopulateSelect('TraceSelect', [[1, 'Full (every pixel)'], [2, '½ (one ray per 2×2)'], [4, '¼ (one ray per 4×4)']], Configuration.TraceScale,
        Key => { Configuration.TraceScale = Number(Key); InvalidateAccumulation(); });
    PopulateSelect('ScaleSelect', [[0.25, '25 %'], [0.5, '50 %'], [0.75, '75 %'], [1, '100 %']], Configuration.RenderScale,
        Key => { Configuration.RenderScale = Number(Key); ResizeViewport(); });
    PopulateSelect('PatternSelect', [['shared', 'Shared (structured)'], ['pixel', 'Per-pixel (dithered)'], ['bluenoise', 'Blue-noise (even dither)']], Configuration.PointPattern,
        Key => { Configuration.PointPattern = Key; InvalidateAccumulation(); });
    PopulateSelect('VisibilitySelect', [['traced', 'Traced'], ['raster', 'Rasterized']], Configuration.VisibilityRasterEnabled ? 'raster' : 'traced',
        Key => { Configuration.VisibilityRasterEnabled = Key === 'raster'; InvalidateAccumulation(); });
    PopulateSelect('MotionSelect', [['restart', 'Restart'], ['reproject', 'Reproject history']], Configuration.ReprojectionEnabled ? 'reproject' : 'restart',
        Key => { Configuration.ReprojectionEnabled = Key === 'reproject'; Division.Configuration = { ...Configuration }; });
    PopulateSelect('VehicleCountSelect', VehicleCountOptions.map(Count => [Count, `${Count} × ${(25068 * Count).toLocaleString('en-US')} tris`]), Configuration.VehicleCount,
        Key => { Configuration.VehicleCount = Number(Key); InvalidateAccumulation(true); DescribeGeometry(); });
    PopulateSelect('DenoiseSelect', [['off', 'Off (exact)'], ['atrous', 'Edge-aware à-trous']], Configuration.DenoiseEnabled ? 'atrous' : 'off',
        Key => { Configuration.DenoiseEnabled = Key === 'atrous'; Division.Configuration = { ...Configuration }; Interface.DisplayStale = true; });
    PopulateSelect('HistorySelect', [[8, '8'], [16, '16'], [32, '32'], [64, '64']], Configuration.HistoryCapacity,
        Key => { Configuration.HistoryCapacity = Number(Key); Division.Configuration = { ...Configuration }; });
    PopulateSelect('TargetSelect', [[64, '64'], [256, '256'], [1024, '1024'], [4096, '4096'], [16384, '16384']], Configuration.TargetSamples,
        Key => { Configuration.TargetSamples = Number(Key); });

    Locate('PauseButton').addEventListener('click', () =>
    {
        Interface.Paused = !Interface.Paused;
        Locate('PauseButton').textContent = Interface.Paused ? 'Resume' : 'Pause';
    });
    Locate('RestartButton').addEventListener('click', () => InvalidateAccumulation());
    Locate('SaveButton').addEventListener('click', SaveDisplayImage);
    Locate('VerifyButton').addEventListener('click', VerifyRepeatability);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    CAMERA INPUT
//------------------------------------------------------------------------------------------------------------------------

function AttachOrbitInput(Viewport)
{
    let Pointer = null;
    Viewport.addEventListener('contextmenu', Gesture => Gesture.preventDefault());
    Viewport.addEventListener('pointerdown', Gesture =>
    {
        Pointer = { X: Gesture.clientX, Y: Gesture.clientY, Panning: Gesture.shiftKey || Gesture.button === 2 };
        Viewport.setPointerCapture(Gesture.pointerId);
    });
    Viewport.addEventListener('pointerup', () => { Pointer = null; });
    Viewport.addEventListener('pointermove', Gesture =>
    {
        if (!Pointer) return;
        const DeltaX = Gesture.clientX - Pointer.X, DeltaY = Gesture.clientY - Pointer.Y;
        Pointer.X = Gesture.clientX; Pointer.Y = Gesture.clientY;
        const Camera = Configuration.Camera;
        if (Pointer.Panning)
        {
            const Vectors = TracingDivision.ResolveCameraVectors(Camera, 1.0);
            const Scale = Camera.Distance * 0.0016;
            const RightLength = Math.hypot(...Vectors.Right), UpLength = Math.hypot(...Vectors.Up);
            for (let Axis = 0; Axis < 3; Axis++)
            {
                Camera.Target[Axis] += (-DeltaX * Vectors.Right[Axis] / RightLength + DeltaY * Vectors.Up[Axis] / UpLength) * Scale;
            }
        }
        else
        {
            Camera.Yaw  -= DeltaX * 0.006;
            Camera.Pitch = Math.min(1.45, Math.max(-0.05, Camera.Pitch + DeltaY * 0.005));
        }
        InvalidateAccumulation(false, true);
    });
    Viewport.addEventListener('wheel', Gesture =>
    {
        Gesture.preventDefault();
        Configuration.Camera.Distance = Math.min(40, Math.max(2.0, Configuration.Camera.Distance * Math.exp(Gesture.deltaY * 0.001)));
        InvalidateAccumulation(false, true);
    }, { passive: false });
}

function ResizeViewport()
{
    const Viewport = Locate('ViewportPanel');
    const Canvas   = Locate('ViewportCanvas');
    let Width, Height;
    if (QueryParameters.has('width'))
    {
        Width  = Number(QueryParameters.get('width'));
        Height = Number(QueryParameters.get('height'));
    }
    else
    {
        const Ratio = Math.min(window.devicePixelRatio || 1, 2) * Configuration.RenderScale;
        Width  = Math.max(64, Math.round(Viewport.clientWidth * Ratio));
        Height = Math.max(64, Math.round(Viewport.clientHeight * Ratio));
    }
    Canvas.width  = Width;
    Canvas.height = Height;
    Division.ResizeAccumulation(Width, Height);
    Division.Configuration = { ...Configuration };
    Interface.AccumulatedTime = 0;
    Interface.StartTime = performance.now();
    Locate('StatusResolution').textContent = `${Width} × ${Height} px (${(Width * Height / 1e6).toFixed(2)} MP)`;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    ACCUMULATION LOOP
//------------------------------------------------------------------------------------------------------------------------

function ExposureMultiplier()
{
    return Math.pow(2.0, Configuration.ExposureStops);
}

function RefreshReadout(DispatchMilliseconds)
{
    const Samples = Division.PixelSamples();
    Locate('ReadoutSamples').textContent = Number.isInteger(Samples) ? Samples.toLocaleString('en-US') : Samples.toFixed(2);
    const Seconds = Interface.AccumulatedTime / 1000;
    const Rate = Seconds > 0 ? Samples / Seconds : 0;
    const Indices = Division.SampleCount;   // 📝 the net property is about sample indices (all pixels together)
    const NetExponent = Indices > 0 ? Math.floor(Math.log2(Indices)) : 0;
    Locate('ReadoutSecondary').textContent = Samples >= Configuration.TargetSamples
        ? `Converged to target · ${Seconds.toFixed(1)} s`
        : `${Rate.toFixed(1)} samples/s · target ${Configuration.TargetSamples.toLocaleString('en-US')}`;
    Locate('ConvergenceFill').style.width = `${Math.min(100, 100 * Samples / Configuration.TargetSamples)}%`;
    if (DispatchMilliseconds !== undefined)
    {
        const RaySamples = Division.AccumulationWidth * Division.AccumulationHeight * Rate;
        Locate('StatusRate').textContent = `${Rate.toFixed(2)} spp/s · ${(RaySamples / 1e6).toFixed(1)} M px-samples/s`;
    }
    Locate('StatusElapsed').textContent = `${Seconds.toFixed(1)} s`;
    Locate('StatusNet').textContent = Indices > 0 ? `2^${NetExponent} = ${2 ** NetExponent} (every pair a (0,${NetExponent},2)-net)` : '—';
}

// 📝 FPS counts completed traced frames (each SampleBatch samples) in the last second; idle frames are not counted.
function RefreshFrameRate(FrameEnd, FrameMilliseconds)
{
    if (FrameMilliseconds !== undefined)
    {
        Interface.FrameStamps.push(FrameEnd);
        Interface.FrameMilliseconds = Interface.FrameMilliseconds > 0 ? 0.9 * Interface.FrameMilliseconds + 0.1 * FrameMilliseconds : FrameMilliseconds;
    }
    while (Interface.FrameStamps.length > 2 && Interface.FrameStamps[0] < FrameEnd - FrameRateWindow) Interface.FrameStamps.shift();
    if (FrameMilliseconds === undefined && !Interface.FramesInFlight.length) Interface.FrameStamps.length = 0;
    if (FrameEnd - Interface.FrameRateRefreshed < 250) return;
    Interface.FrameRateRefreshed = FrameEnd;

    const Tracing = !Interface.Paused && Division.PixelSamples() < Configuration.TargetSamples;
    const Stamps  = Interface.FrameStamps;
    const FramesPerSecond = Stamps.length > 1 ? 1000 * (Stamps.length - 1) / (Stamps[Stamps.length - 1] - Stamps[0]) : 0;
    Locate('FrameRateValue').textContent  = Tracing && Stamps.length > 1 ? FramesPerSecond.toFixed(FramesPerSecond < 10 ? 1 : 0) : '—';
    Locate('FrameRateDetail').textContent = Tracing
        ? `${Interface.FrameMilliseconds.toFixed(1)} ms/frame · ${+(Configuration.SampleBatch / Division.TraceScale() ** 2).toFixed(2)} spp/frame`
        : (Interface.Paused ? 'paused' : 'idle · target reached');
    RefreshStageTiming();
}

// 📝 GPU time per stage from timestamp queries (smoothed). Shows why a frame costs what it costs.
function RefreshStageTiming()
{
    const Timing = Division.Timing;
    if (!Timing.Enabled) { Locate('StatusGpu').textContent = 'timestamp-query unavailable (adapter or timing=0)'; return; }
    const Averages = Timing.Averages;
    const Labels = { Reprojection: 'reproject', Features: 'guides', Integration: 'trace', Reconstruction: 'denoise', Display: 'display' };
    const Parts = Object.entries(Averages).filter(([, Milliseconds]) => Milliseconds >= 0.05).map(([Stage, Milliseconds]) => `${Labels[Stage]} ${Milliseconds.toFixed(1)}`);
    const Total = Object.values(Averages).reduce((Sum, Milliseconds) => Sum + Milliseconds, 0);
    Locate('StatusGpu').textContent = Parts.length ? `${Total.toFixed(1)} ms = ${Parts.join(' · ')}` : '—';
}

// 📝 No per-frame stall: up to FramesInFlightLimit frames are queued; the CPU waits only when it is that far ahead.
//    Frame time = interval between GPU completions, so it measures throughput, not CPU latency.
async function AdvanceAccumulationLoop()
{
    for (;;)
    {
        await new Promise(requestAnimationFrame);
        if (Interface.VerifyActive) continue;
        const TargetIndices = Configuration.TargetSamples * Division.TraceScale() ** 2;   // 📝 target is per pixel
        if (!Interface.Paused && Division.SampleCount < TargetIndices)
        {
            while (Interface.FramesInFlight.length >= FramesInFlightLimit) await Interface.FramesInFlight[0];
            const Revision = Interface.ConfigurationRevision;
            const Batch = Math.min(Configuration.SampleBatch, TargetIndices - Division.SampleCount);
            const Submitted = performance.now();
            await Division.StepFrame(Batch, ExposureMultiplier());
            const Completion = Division.Device.queue.onSubmittedWorkDone().then(() =>
            {
                const Finished = performance.now();
                const Interval = Interface.LastCompletion > 0 && Finished - Interface.LastCompletion < 2000 ? Finished - Interface.LastCompletion : Finished - Submitted;
                Interface.LastCompletion = Finished;
                if (Revision === Interface.ConfigurationRevision) Interface.AccumulatedTime += Interval;
                Interface.FramesInFlight.shift();
                RefreshReadout(Interval);
                RefreshFrameRate(Finished, Interval);
            });
            Interface.FramesInFlight.push(Completion);
            Interface.DisplayStale = false;
        }
        else if (Interface.DisplayStale)
        {
            await Division.PresentDisplay(ExposureMultiplier());
            Interface.DisplayStale = false;
            Interface.LastCompletion = 0;
            RefreshReadout();
        }
        else
        {
            Interface.LastCompletion = 0;
            RefreshFrameRate(performance.now());
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    EXPORT AND VERIFICATION
//------------------------------------------------------------------------------------------------------------------------

async function EncodeDisplayPng()
{
    const Radiance = await Division.RetrieveDisplayRadiance();
    const Width = Division.AccumulationWidth, Height = Division.AccumulationHeight;
    const Pixels = ResolveDisplayPixels(Radiance, Width, Height, ExposureMultiplier());
    const Surface = document.createElement('canvas');
    Surface.width = Width; Surface.height = Height;
    Surface.getContext('2d').putImageData(new ImageData(Pixels, Width, Height), 0, 0);
    return Surface.toDataURL('image/png');
}

async function SaveDisplayImage()
{
    const Link = document.createElement('a');
    Link.href = await EncodeDisplayPng();
    Link.download = `DeterministicPathTracer_${Division.PixelSamples()}spp.png`;
    Link.click();
}

// 📝 Renders SampleTotal samples from zero in batches of Batch, waiting for the GPU between batches.
async function RenderFromZero(SampleTotal, Batch)
{
    Division.ResetAccumulation();
    while (Division.SampleCount < SampleTotal)
    {
        Division.AdvanceSamples(Math.min(Batch, SampleTotal - Division.SampleCount));
        await Division.Device.queue.onSubmittedWorkDone();
    }
}

async function VerifyRepeatability()
{
    const SampleTotal = Math.max(1, Division.SampleCount);
    const DigestLine = Locate('DigestLine');
    Interface.VerifyActive = true;
    DigestLine.className = 'DigestLine';
    DigestLine.textContent = `Re-rendering ${SampleTotal} samples twice…`;
    try
    {
        await RenderFromZero(SampleTotal, 1);
        const FirstDigest = await Division.ComputeAccumulationDigest();
        await RenderFromZero(SampleTotal, Math.max(2, Math.min(8, SampleTotal)));
        const SecondDigest = await Division.ComputeAccumulationDigest();
        const Matched = FirstDigest === SecondDigest;
        DigestLine.className = `DigestLine ${Matched ? 'Matched' : 'Diverged'}`;
        DigestLine.textContent = `${SampleTotal} spp · batch 1: ${FirstDigest}\n${SampleTotal} spp · batch ${Math.max(2, Math.min(8, SampleTotal))}: ${SecondDigest}\n${Matched ? 'Bit-identical' : 'DIVERGED'}`;
        DigestLine.style.whiteSpace = 'pre-line';
    }
    finally
    {
        Interface.VerifyActive = false;
        Interface.DisplayStale = true;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    INITIALIZE
//------------------------------------------------------------------------------------------------------------------------

async function InitializeWorkspace()
{
    try
    {
        await Division.Initialize(Locate('ViewportCanvas'), new URL('../Assets/', import.meta.url), ReadbackPresentationEnabled);
        Division.Configuration = { ...Configuration };
        ResizeViewport();
        Division.ConfigureScene(Configuration);
        const Summary = Division.SceneSummary;
        Locate('StatusAdapter').textContent  = Division.AdapterDescription;
        DescribeGeometry();
        ConstructInspector();
        AttachOrbitInput(Locate('ViewportPanel'));
        if (!QueryParameters.has('width')) new ResizeObserver(() => { ResizeViewport(); Interface.DisplayStale = true; }).observe(Locate('ViewportPanel'));

        window.DeterministicTracing = {
            Division, Configuration, CameraPresets,
            ApplyConfiguration: (Partial, SceneChanged = true) => { Object.assign(Configuration, Partial); InvalidateAccumulation(SceneChanged); },
            RenderFromZero, EncodeDisplayPng,
            ComputeDigest: () => Division.ComputeAccumulationDigest(),
            ExtractQuadrature: (SampleTotal, PairTotal) => Division.ExtractQuadratureOnDevice(SampleTotal, PairTotal),
            RetrieveAccumulation: () => Division.RetrieveAccumulation(),
            RetrieveDisplayRadiance: () => Division.RetrieveDisplayRadiance(),
            // 📝 Camera motion as the orbit input performs it (reprojects when enabled), then more samples on top.
            MoveCamera: Partial => { Object.assign(Configuration.Camera, Partial); InvalidateAccumulation(false, true); },
            ContinueSamples: async SampleTotal =>
            {
                while (Division.SampleCount < SampleTotal) { Division.AdvanceSamples(1); await Division.Device.queue.onSubmittedWorkDone(); }
            },
            RetrieveHistory: async () => { await Division.RetrieveAccumulation(); return Division.HistoryActive ? Division.RetrieveHistory() : null; }
        };
        if (!ValidationEnabled) AdvanceAccumulationLoop();
        document.body.dataset.ready = 'true';
    }
    catch (Failure)
    {
        console.error(Failure);
        const Notice = Locate('FailureNotice');
        Notice.hidden = false;
        Notice.textContent = `Deterministic path tracer could not start.\n\n${Failure.message || Failure}\n\nRequires a WebGPU browser (Chrome / Edge 113+) on a GTX 900-series or newer GPU.`;
        Locate('ReadoutSecondary').textContent = 'Failed — see notice';
        document.body.dataset.ready = 'failed';
    }
}

InitializeWorkspace();
