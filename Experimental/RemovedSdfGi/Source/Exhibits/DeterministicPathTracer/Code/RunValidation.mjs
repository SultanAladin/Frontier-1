/*====================================================================================================================================
                                                        RUNVALIDATION.MJS
====================================================================================================================================*/
// 🧩 Drives the actual prototype page in headless Chromium (SwiftShader WebGPU) and retains measurements and captures

// Usage:  npm install && node RunValidation.mjs [quadrature|analytic|determinism|convergence|chart|captures|interface|realtime|patterns|fleet|reconstruction|all]
// 📝 Phases write Results/<Phase>.json; captures write Images/*.png. The chart phase reads the JSON results.

import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import zlib from 'node:zlib';
import { execSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';
import { AvalancheKey, QuadraturePair, MeasureNetExponent, MeasureCrossPairStratification } from './QuadratureReference.mjs';
import { ConstructSoftboxLights } from '../../../Experimental/DeterministicPathTracer/Source/StudioSceneAssembly.js';
import { TracingDivision, ResolveDisplayPixels, DigestFloatBuffer } from '../../../Experimental/DeterministicPathTracer/Source/TracingDivision.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const CodeFolder      = path.dirname(fileURLToPath(import.meta.url));
const ExhibitFolder   = path.resolve(CodeFolder, '..');
const PrototypeFolder = path.resolve(ExhibitFolder, '../../Experimental/DeterministicPathTracer');
const ResultsFolder   = path.join(ExhibitFolder, 'Results');
const ImagesFolder    = path.join(ExhibitFolder, 'Images');
const RuntimeFolder   = path.join(CodeFolder, 'ChromiumRuntime');
const PairsInUse      = 2 + 16 * 4;     // [-] - subpixel, lens, 16 depths × (light, diffuse, specular, coat)

const ContentTypes = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wgsl': 'text/plain', '.facet': 'application/octet-stream' };


//------------------------------------------------------------------------------------------------------------------------
//                                                    RUNTIME
//------------------------------------------------------------------------------------------------------------------------

// 📝 @sparticuz/chromium ships Chromium plus SwiftShader's Vulkan ICD and loader as brotli archives; unpack once.
// 📝 The packaged fonts.conf points at serverless paths; point fontconfig at the unpacked Open Sans instead.
function ConfigureFontLookup()
{
    fs.writeFileSync(path.join(RuntimeFolder, 'fonts.conf'), `<?xml version="1.0"?>\n<!DOCTYPE fontconfig SYSTEM "fonts.dtd">\n<fontconfig>\n`
        + `  <dir>${path.join(RuntimeFolder, 'fonts')}</dir>\n  <dir>/usr/share/fonts</dir>\n  <cachedir>${path.join(RuntimeFolder, 'fonts-cache')}</cachedir>\n`
        + `  <alias><family>system-ui</family><prefer><family>Open Sans</family></prefer></alias>\n`
        + `  <alias><family>sans-serif</family><prefer><family>Open Sans</family></prefer></alias>\n</fontconfig>\n`);
}

function PrepareChromiumRuntime()
{
    const Executable = path.join(RuntimeFolder, 'chromium');
    if (fs.existsSync(Executable)) { ConfigureFontLookup(); return Executable; }
    const Archives = path.join(CodeFolder, 'node_modules/@sparticuz/chromium/bin');
    fs.mkdirSync(RuntimeFolder, { recursive: true });
    for (const Name of ['swiftshader', 'al2023', 'fonts'])
    {
        const TarPath = path.join(RuntimeFolder, `${Name}.tar`);
        fs.writeFileSync(TarPath, zlib.brotliDecompressSync(fs.readFileSync(path.join(Archives, `${Name}.tar.br`))));
        execSync(`tar xf "${TarPath}" -C "${RuntimeFolder}" && rm "${TarPath}"`);
    }
    fs.writeFileSync(Executable, zlib.brotliDecompressSync(fs.readFileSync(path.join(Archives, 'chromium.br'))));
    fs.chmodSync(Executable, 0o755);
    ConfigureFontLookup();
    return Executable;
}

function StartPrototypeServer()
{
    const Server = http.createServer((Request, Response) =>
    {
        const RequestPath = decodeURIComponent(new URL(Request.url, 'http://localhost').pathname);
        const FilePath = path.join(PrototypeFolder, RequestPath === '/' ? 'index.html' : RequestPath);
        if (!FilePath.startsWith(PrototypeFolder) || !fs.existsSync(FilePath)) { Response.writeHead(404); Response.end(); return; }
        Response.writeHead(200, { 'Content-Type': ContentTypes[path.extname(FilePath)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        fs.createReadStream(FilePath).pipe(Response);
    });
    return new Promise(Resolve => Server.listen(0, '127.0.0.1', () => Resolve(Server)));
}

async function LaunchBrowser()
{
    const Executable = PrepareChromiumRuntime();
    process.env.LD_LIBRARY_PATH = `${RuntimeFolder}:${path.join(RuntimeFolder, 'lib')}`;
    process.env.FONTCONFIG_PATH = RuntimeFolder;
    return puppeteer.launch({
        executablePath: Executable, headless: 'shell', protocolTimeout: 0,
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=swiftshader', '--use-webgpu-adapter=swiftshader',
               '--ignore-gpu-blocklist', '--disable-gpu-watchdog', '--disable-features=DawnGpuWatchdog']
    });
}

async function OpenPrototype(Browser, Server, Query)
{
    const Page = await Browser.newPage();
    Page.on('console', Message => { if (Message.type() === 'error') console.log('page error:', Message.text()); });
    Page.on('pageerror', Failure => console.log('page exception:', Failure.message));
    await Page.goto(`http://127.0.0.1:${Server.address().port}/index.html?validation=1&${Query}`);
    await Page.waitForFunction(() => document.body.dataset.ready, { timeout: 0 });
    const Ready = await Page.evaluate(() => document.body.dataset.ready);
    if (Ready !== 'true') throw new Error(`Prototype failed to initialize: ${await Page.evaluate(() => document.getElementById('FailureNotice').textContent)}`);
    return Page;
}

function WriteResult(Phase, Result)
{
    fs.mkdirSync(ResultsFolder, { recursive: true });
    fs.writeFileSync(path.join(ResultsFolder, `${Phase}.json`), JSON.stringify(Result, null, 2) + '\n');
    console.log(`[${Phase}] ${JSON.stringify(Result.Summary ?? Result)}`);
}

function WritePng(Name, EncodedPng)
{
    fs.mkdirSync(ImagesFolder, { recursive: true });
    fs.writeFileSync(path.join(ImagesFolder, Name), Buffer.from(EncodedPng.split(',')[1], 'base64'));
}

// 📝 Renders powers of two up to SampleTotal in one accumulation, retrieving the mean image at each checkpoint.
async function RenderCheckpoints(Page, SampleTotal, Batch, Checkpoints, CapturePng)
{
    return Page.evaluate(async (SampleTotal, Batch, Checkpoints, CapturePng) =>
    {
        const Tracing = window.DeterministicTracing;
        const Division = Tracing.Division;
        Division.ResetAccumulation();
        const Retained = [];
        const Start = performance.now();
        while (Division.SampleCount < SampleTotal)
        {
            const Next = Checkpoints.find(Checkpoint => Checkpoint > Division.SampleCount) ?? SampleTotal;
            Division.AdvanceSamples(Math.min(Batch, Next - Division.SampleCount));
            await Division.Device.queue.onSubmittedWorkDone();
            if (Checkpoints.includes(Division.SampleCount))
            {
                const Accumulation = await Division.RetrieveAccumulation();
                const Mean = new Float32Array(Accumulation.length / 4 * 3);
                for (let Pixel = 0; Pixel < Accumulation.length / 4; Pixel++)
                {
                    for (let Channel = 0; Channel < 3; Channel++) Mean[Pixel * 3 + Channel] = Accumulation[Pixel * 4 + Channel] / Accumulation[Pixel * 4 + 3];
                }
                Retained.push({ Samples: Division.SampleCount, Seconds: (performance.now() - Start) / 1000, Mean: Array.from(Mean),
                                Png: CapturePng ? await Tracing.EncodeDisplayPng() : null });
            }
        }
        return Retained;
    }, SampleTotal, Batch, Checkpoints, CapturePng);
}

function RelativeRootMeanSquare(Image, Reference)
{
    let SquaredError = 0, SquaredReference = 0;
    for (let Index = 0; Index < Reference.length; Index++)
    {
        SquaredError += (Image[Index] - Reference[Index]) ** 2;
        SquaredReference += Reference[Index] ** 2;
    }
    return Math.sqrt(SquaredError / SquaredReference);
}

function PowersOfTwo(Maximum)
{
    const Powers = [];
    for (let Power = 1; Power <= Maximum; Power *= 2) Powers.push(Power);
    return Powers;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    PHASES
//------------------------------------------------------------------------------------------------------------------------

// 📝 ① GPU QuadraturePair equals the host replica bit-for-bit. ② Every pair is a (0,m,2)-net at every 2^m prefix.
//    ③ Different pairs are decorrelated (binned deviation comparable to independent points, |ρ| small).
async function ValidateQuadrature(Browser, Server)
{
    const SampleTotal = 4096, PairTotal = 18;
    const Page = await OpenPrototype(Browser, Server, 'width=320&height=240');
    const Points = await Page.evaluate(async (SampleTotal, PairTotal) => Array.from(await window.DeterministicTracing.ExtractQuadrature(SampleTotal, PairTotal)), SampleTotal, PairTotal);
    await Page.close();
    let Mismatches = 0, FirstMismatch = null;
    for (let Pair = 0; Pair < PairTotal; Pair++)
    {
        for (let Sample = 0; Sample < SampleTotal; Sample++)
        {
            const Offset = (Pair * SampleTotal + Sample) * 4;
            const [X, Y] = QuadraturePair(Sample, Pair);
            if (Points[Offset] !== X || Points[Offset + 1] !== Y)
            {
                Mismatches++;
                FirstMismatch ??= { Sample, Pair, Device: [Points[Offset], Points[Offset + 1]], Host: [X, Y] };
            }
        }
    }
    const NetExponents = Array.from({ length: PairsInUse }, (_, Pair) => MeasureNetExponent(Pair, 14));
    const CrossPairs = [[0, 2], [2, 3], [2, 6], [3, 4], [5, 9], [10, 11]].map(([A, B]) => ({ Pairs: [A, B], ...MeasureCrossPairStratification(A, B, 4096) }));
    WriteResult('Quadrature', {
        Summary: { DeviceHostComparisons: SampleTotal * PairTotal * 2, Mismatches, MinimumNetExponent: Math.min(...NetExponents),
                   PairsExamined: PairsInUse, WorstCrossPairCorrelation: Math.max(...CrossPairs.map(Entry => Math.abs(Entry.Correlation))) },
        FirstMismatch, NetExponents, CrossPairs,
        Note: 'Net exponent m means every prefix of 2^1..2^m points is a (0,k,2)-net (one point per elementary box). Examined up to 14.'
    });
}

// 📝 Lambert polygon irradiance from a one-sided rectangle seen unoccluded from point P with normal +Y.
function AnalyticRectangleIrradiance(P, Light)
{
    const Corners = [[-0.5, -0.5], [0.5, -0.5], [0.5, 0.5], [-0.5, 0.5]].map(([A, B]) =>
        [0, 1, 2].map(Axis => Light.Centre[Axis] + A * Light.EdgeU[Axis] + B * Light.EdgeV[Axis] - P[Axis]));
    const Normalize = V => { const L = Math.hypot(...V); return V.map(C => C / L); };
    let Sum = 0;
    for (let Index = 0; Index < 4; Index++)
    {
        const A = Normalize(Corners[Index]), B = Normalize(Corners[(Index + 1) % 4]);
        const Cosine = Math.min(1, Math.max(-1, A[0] * B[0] + A[1] * B[1] + A[2] * B[2]));
        const Angle = Math.acos(Cosine);
        const CrossY = A[2] * B[0] - A[0] * B[2];
        const CrossLength = Math.hypot(A[1] * B[2] - A[2] * B[1], CrossY, A[0] * B[1] - A[1] * B[0]) || 1;
        Sum += Angle * CrossY / CrossLength;
    }
    return Light.Radiance * Math.abs(Sum) / 2;
}

async function ValidateAnalyticSoftbox(Browser, Server)
{
    const Width = 160, Height = 120, SampleTotal = Number(process.env.ANALYTIC_SAMPLES || 1024);
    const Page = await OpenPrototype(Browser, Server, `width=${Width}&height=${Height}`);
    const Camera = { Yaw: 0.5, Pitch: 1.1, Distance: 10.0, Target: [0.4, 0.0, 0.6], FieldOfView: 60 };
    await Page.evaluate(Camera => window.DeterministicTracing.ApplyConfiguration({
        VehicleEnabled: false, ShowcaseEnabled: false, EnvironmentIntensity: 0, KeyIntensity: 4.0, RimIntensity: 0, FillIntensity: 0,
        FloorPreset: 'Lambert', BounceLimit: 1, LightsVisibleToCamera: false, Camera }), Camera);
    // 📝 The reference reuses the prototype's own light construction and camera vectors (pure host modules).
    const LightFloats = ConstructSoftboxLights({ KeyIntensity: 4.0, RimIntensity: 0, FillIntensity: 0 }).Packed;
    const Setup = {
        Light:   { Centre: Array.from(LightFloats.slice(0, 3)), EdgeU: Array.from(LightFloats.slice(4, 7)), EdgeV: Array.from(LightFloats.slice(8, 11)), Radiance: LightFloats[12] },
        Vectors: TracingDivision.ResolveCameraVectors(Camera, Width / Height)
    };
    const Checkpoints = PowersOfTwo(SampleTotal);
    const Retained = await RenderCheckpoints(Page, SampleTotal, 16, Checkpoints, false);
    await Page.close();

    // 📝 Reference pixel = box-filtered (16 × 16 subpixel grid) albedo/π · E over the pixel footprint on the floor.
    const { Origin, Forward, Right, Up } = Setup.Vectors;
    const Reference = new Float64Array(Width * Height * 3);
    const Grid = 16;
    for (let Row = 0; Row < Height; Row++)
    {
        for (let Column = 0; Column < Width; Column++)
        {
            let Sum = 0, Hits = 0;
            for (let Sy = 0; Sy < Grid; Sy++) for (let Sx = 0; Sx < Grid; Sx++)
            {
                const Nx = ((Column + (Sx + 0.5) / Grid) / Width) * 2 - 1, Ny = ((Row + (Sy + 0.5) / Grid) / Height) * 2 - 1;
                const D = [0, 1, 2].map(Axis => Forward[Axis] + Nx * Right[Axis] - Ny * Up[Axis]);
                const L = Math.hypot(...D);
                const T = -Origin[1] / (D[1] / L);
                if (!(T > 0)) continue;
                const P = [0, 1, 2].map(Axis => Origin[Axis] + D[Axis] / L * T);
                Sum += 0.5 / Math.PI * AnalyticRectangleIrradiance(P, Setup.Light);
                Hits++;
            }
            for (let Channel = 0; Channel < 3; Channel++) Reference[(Row * Width + Column) * 3 + Channel] = Sum / (Grid * Grid);
        }
    }
    // 📝 Light radiance is (1.00, 0.98, 0.95) × 4; the reference above uses the red channel, so scale per channel.
    const ChannelScale = [1.0, 0.98, 0.95];
    for (let Index = 0; Index < Reference.length; Index++) Reference[Index] *= ChannelScale[Index % 3];

    const Curve = Retained.map(Entry => ({ Samples: Entry.Samples, Seconds: Entry.Seconds, RelativeRms: RelativeRootMeanSquare(Entry.Mean, Reference) }));
    const Final = Retained[Retained.length - 1].Mean;
    let MaximumRelative = 0, MeanRelative = 0, MeanSigned = 0, Counted = 0, WorstPixel = null;
    for (let Index = 0; Index < Reference.length; Index++)
    {
        if (Reference[Index] < 0.02) continue;
        const Signed = (Final[Index] - Reference[Index]) / Reference[Index];
        if (Math.abs(Signed) > MaximumRelative)
        {
            MaximumRelative = Math.abs(Signed);
            WorstPixel = { Column: Math.floor(Index / 3) % Width, Row: Math.floor(Math.floor(Index / 3) / Width), Rendered: Final[Index], Reference: Reference[Index] };
        }
        MeanRelative += Math.abs(Signed); MeanSigned += Signed; Counted++;
    }
    WriteResult('AnalyticSoftbox', {
        Summary: { Width, Height, Samples: SampleTotal, RelativeRmsAtFinal: Curve[Curve.length - 1].RelativeRms,
                   MeanRelativeError: MeanRelative / Counted, MeanSignedRelativeError: MeanSigned / Counted, MaximumRelativeError: MaximumRelative,
                   PixelsCompared: Counted / 3 },
        WorstPixel,
        Light: Setup.Light, Camera, Curve,
        Note: 'Lambert 50 % floor under one 5.6 x 2.6 m softbox (radiance 4), no other geometry or environment. Reference: closed-form polygon irradiance, box-filtered per pixel.'
    });
}

async function ValidateDeterminism(Browser, Server)
{
    const Query = 'width=160&height=90&camera=Hero';
    const SampleTotal = 32;
    const Digests = [];
    const Page = await OpenPrototype(Browser, Server, Query);
    for (const Batch of [1, 4, 32])
    {
        await Page.evaluate(async (SampleTotal, Batch) => window.DeterministicTracing.RenderFromZero(SampleTotal, Batch), SampleTotal, Batch);
        Digests.push({ Instance: 'first page', Batch, Digest: await Page.evaluate(() => window.DeterministicTracing.ComputeDigest()) });
    }
    // 📝 Disturb the configuration and come back: the digest must not depend on history.
    await Page.evaluate(async () => { const T = window.DeterministicTracing; T.ApplyConfiguration({ PaintPreset: 'Racing' }); await T.RenderFromZero(4, 4); T.ApplyConfiguration({ PaintPreset: 'Authored' }); });
    await Page.evaluate(async SampleTotal => window.DeterministicTracing.RenderFromZero(SampleTotal, 8), SampleTotal);
    Digests.push({ Instance: 'first page after reconfiguration', Batch: 8, Digest: await Page.evaluate(() => window.DeterministicTracing.ComputeDigest()) });
    await Page.close();

    const SecondBrowser = await LaunchBrowser();
    const SecondPage = await OpenPrototype(SecondBrowser, Server, Query);
    await SecondPage.evaluate(async SampleTotal => window.DeterministicTracing.RenderFromZero(SampleTotal, 2), SampleTotal);
    Digests.push({ Instance: 'second browser process', Batch: 2, Digest: await SecondPage.evaluate(() => window.DeterministicTracing.ComputeDigest()) });
    await SecondBrowser.close();

    WriteResult('Determinism', {
        Summary: { Samples: SampleTotal, Runs: Digests.length, DistinctDigests: new Set(Digests.map(Entry => Entry.Digest)).size },
        Digests,
        Note: 'FNV-1a over the raw float32 accumulation (sum RGB + sample count) of the 160 x 90 Hero view.'
    });
}

async function ValidateConvergence(Browser, Server)
{
    const Width = 160, Height = 90, ReferenceSamples = 1024;
    const Page = await OpenPrototype(Browser, Server, `width=${Width}&height=${Height}&camera=Hero`);
    const Retained = await RenderCheckpoints(Page, ReferenceSamples, 16, PowersOfTwo(ReferenceSamples), false);
    await Page.close();
    const Reference = Retained[Retained.length - 1].Mean;
    const Curve = Retained.slice(0, -1).map(Entry => ({ Samples: Entry.Samples, Seconds: Entry.Seconds, RelativeRms: RelativeRootMeanSquare(Entry.Mean, Reference) }));
    WriteResult('Convergence', {
        Summary: { Width, Height, ReferenceSamples, SecondsPerSample: Retained[Retained.length - 1].Seconds / ReferenceSamples,
                   RelativeRmsAt1: Curve[0].RelativeRms, RelativeRmsAt64: Curve[6].RelativeRms, RelativeRmsAt512: Curve[Curve.length - 1].RelativeRms },
        Curve,
        Note: 'Self-referenced: error of N samples against the same deterministic sequence at 1024 samples (Hero view, full scene).'
    });
}

async function RetainCaptures(Browser, Server)
{
    const Captures = [];
    const Filmstrip = await OpenPrototype(Browser, Server, 'width=320&height=180&camera=Hero');
    const Strip = await RenderCheckpoints(Filmstrip, 64, 4, [1, 4, 16, 64], true);
    for (const Entry of Strip) { WritePng(`HeroProgression_${String(Entry.Samples).padStart(3, '0')}spp.png`, Entry.Png); Captures.push({ Name: `HeroProgression_${Entry.Samples}spp`, Seconds: Entry.Seconds }); }
    await Filmstrip.close();

    for (const [Camera, Paint, Samples] of [['Hero', 'Authored', 128], ['Spheres', 'Authored', 128], ['Rear', 'Racing', 64]])
    {
        const Page = await OpenPrototype(Browser, Server, `width=640&height=360&camera=${Camera}&paint=${Paint}`);
        const [Entry] = await RenderCheckpoints(Page, Samples, 4, [Samples], true);
        const Name = `${Camera}_${Paint}_${Samples}spp.png`;
        WritePng(Name, Entry.Png);
        Captures.push({ Name, Seconds: Entry.Seconds, Digest: await Page.evaluate(() => window.DeterministicTracing.ComputeDigest()) });
        await Page.close();
    }

    WriteResult('Captures', { Summary: { Images: Captures.length }, Captures });
}

// 📝 The interactive page (render loop running, not validation mode) at 40 % render scale after 6 samples.
async function RetainInterfaceCapture(Browser, Server)
{
    const Interface = await Browser.newPage();
    await Interface.setViewport({ width: 1440, height: 860 });
    await Interface.goto(`http://127.0.0.1:${Server.address().port}/index.html?scale=0.4&target=16&present=readback`);
    await Interface.waitForFunction(() => document.body.dataset.ready === 'true', { timeout: 0 });
    await Interface.waitForFunction(() => document.getElementById('ReadoutSamples').textContent === '16', { timeout: 0, polling: 1000 });
    await new Promise(Resolve => setTimeout(Resolve, 1500));
    fs.mkdirSync(ImagesFolder, { recursive: true });
    await Interface.screenshot({ path: path.join(ImagesFolder, 'WorkspaceInterface.png') });
    await Interface.close();
    console.log('[interface] Images/WorkspaceInterface.png');
}

// 📝 Log–log error chart rasterized by the same headless browser from an inline SVG.
async function RetainChart(Browser)
{
    const Analytic = JSON.parse(fs.readFileSync(path.join(ResultsFolder, 'AnalyticSoftbox.json'), 'utf8'));
    const Convergence = JSON.parse(fs.readFileSync(path.join(ResultsFolder, 'Convergence.json'), 'utf8'));
    const Width = 760, Height = 440, Left = 70, Right = 200, Top = 40, Bottom = 56;
    const PlotX = Samples => Left + (Math.log2(Samples) / 10) * (Width - Left - Right);
    const LogMin = -4, LogMax = 0;
    const PlotY = Error => Top + (1 - (Math.log10(Math.max(Error, 1e-4)) - LogMin) / (LogMax - LogMin)) * (Height - Top - Bottom);
    const Polyline = (Curve, Colour) => `<polyline fill="none" stroke="${Colour}" stroke-width="2.4" points="${Curve.map(Entry => `${PlotX(Entry.Samples).toFixed(1)},${PlotY(Entry.RelativeRms).toFixed(1)}`).join(' ')}"/>`
        + Curve.map(Entry => `<circle cx="${PlotX(Entry.Samples).toFixed(1)}" cy="${PlotY(Entry.RelativeRms).toFixed(1)}" r="3.2" fill="${Colour}"/>`).join('');
    const Slope = (Start, Exponent, Colour, Label) =>
    {
        const Points = [1, 1024].map(Samples => `${PlotX(Samples).toFixed(1)},${PlotY(Start * Math.pow(Samples, -Exponent)).toFixed(1)}`).join(' ');
        return `<polyline fill="none" stroke="${Colour}" stroke-dasharray="5 5" stroke-width="1.3" points="${Points}"/>`
             + `<text x="${PlotX(1024) + 8}" y="${PlotY(Start * Math.pow(1024, -Exponent)) + 4}" fill="${Colour}" font-size="12">${Label}</text>`;
    };
    const GridLines = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map(Power => `<line x1="${PlotX(2 ** Power)}" x2="${PlotX(2 ** Power)}" y1="${Top}" y2="${Height - Bottom}" stroke="#262a30"/><text x="${PlotX(2 ** Power)}" y="${Height - Bottom + 18}" fill="#9aa1ab" font-size="11" text-anchor="middle">${2 ** Power}</text>`).join('')
                    + [-4, -3, -2, -1, 0].map(Exponent => `<line x1="${Left}" x2="${Width - Right}" y1="${PlotY(10 ** Exponent)}" y2="${PlotY(10 ** Exponent)}" stroke="#262a30"/><text x="${Left - 8}" y="${PlotY(10 ** Exponent) + 4}" fill="#9aa1ab" font-size="11" text-anchor="end">1e${Exponent}</text>`).join('');
    const Start = Analytic.Curve[0].RelativeRms;
    const Svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}" style="background:#0f1114;font-family:system-ui,sans-serif">
        <text x="${Left}" y="24" fill="#e7e9ec" font-size="15" font-weight="600">Deterministic quadrature: relative RMS error vs samples per pixel</text>
        ${GridLines}
        ${Slope(Start, 0.5, '#6b727c', 'N^-1/2 (Monte Carlo)')}
        ${Slope(Start, 1.0, '#8a8f98', 'N^-1')}
        ${Polyline(Analytic.Curve, '#c7f03a')}
        ${Polyline(Convergence.Curve, '#5ec8ff')}
        <rect x="${Left + 14}" y="${Height - Bottom - 62}" width="12" height="3" fill="#c7f03a"/><text x="${Left + 32}" y="${Height - Bottom - 57}" fill="#e7e9ec" font-size="12">Softbox vs closed-form reference</text>
        <rect x="${Left + 14}" y="${Height - Bottom - 40}" width="12" height="3" fill="#5ec8ff"/><text x="${Left + 32}" y="${Height - Bottom - 35}" fill="#e7e9ec" font-size="12">Full Hero scene vs its own 1024 spp</text>
        <text x="${(Left + Width - Right) / 2}" y="${Height - 12}" fill="#9aa1ab" font-size="12" text-anchor="middle">samples per pixel (log2)</text>
    </svg>`;
    fs.writeFileSync(path.join(ResultsFolder, 'ConvergenceChart.svg'), Svg);
    const Page = await Browser.newPage();
    await Page.setViewport({ width: Width, height: Height });
    await Page.setContent(`<html><body style="margin:0;background:#0f1114">${Svg}</body></html>`);
    await Page.evaluate(async () => { await document.fonts.ready; await new Promise(Resolve => requestAnimationFrame(() => requestAnimationFrame(Resolve))); });
    fs.mkdirSync(ImagesFolder, { recursive: true });
    await Page.screenshot({ path: path.join(ImagesFolder, 'ConvergenceChart.png') });
    await Page.close();
    console.log('[chart] Images/ConvergenceChart.png');
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    REAL-TIME OPTIONS
//------------------------------------------------------------------------------------------------------------------------

const RealtimeCheckpoints = [1, 2, 4, 8, 16];
const OrbitStep           = 0.05;   // [rad] - yaw change between the converged view and the moved view (≈ 8 px drag)

// 📝 Displayed linear radiance: sums, blended with reprojected history exactly as DisplayExposure.wgsl does.
function ResolveDisplayedMean(Accumulation, History)
{
    const Mean = new Float32Array(Accumulation.length / 4 * 3);
    for (let Pixel = 0; Pixel < Accumulation.length / 4; Pixel++)
    {
        const SampleWeight  = Accumulation[Pixel * 4 + 3];
        const HistoryWeight = History ? Math.max(History[Pixel * 4 + 3] - SampleWeight, 0) : 0;
        for (let Channel = 0; Channel < 3; Channel++)
        {
            const Blended = Accumulation[Pixel * 4 + Channel] + (HistoryWeight > 0 ? History[Pixel * 4 + Channel] * HistoryWeight : 0);
            Mean[Pixel * 3 + Channel] = Blended / (SampleWeight + HistoryWeight);
        }
    }
    return Mean;
}

// 📝 What the display shows (DisplayReconstruction.wgsl output when active), plus the raw history region.
async function RetrieveDisplayed(Page)
{
    const [Radiance, History] = await Page.evaluate(async () =>
    {
        const Tracing = window.DeterministicTracing;
        const Radiance = await Tracing.RetrieveDisplayRadiance();
        const History = await Tracing.RetrieveHistory();
        return [Array.from(Radiance), History ? Array.from(History) : null];
    });
    return { Mean: ResolveDisplayedMean(Radiance, null), History };
}

async function RenderModeCurve(Page, Partial, Reference)
{
    await Page.evaluate(Partial => window.DeterministicTracing.ApplyConfiguration(Partial), Partial);
    const Retained = await RenderCheckpoints(Page, RealtimeCheckpoints.at(-1), 1, RealtimeCheckpoints, false);
    const Digest = await Page.evaluate(() => window.DeterministicTracing.ComputeDigest());
    return { Curve: Retained.map(Entry => ({ Samples: Entry.Samples, Seconds: Entry.Seconds, RelativeRms: RelativeRootMeanSquare(Entry.Mean, Reference) })),
             SecondsPerSample: Retained.at(-1).Seconds / Retained.at(-1).Samples, Digest, Final: Retained.at(-1).Mean };
}

// 📝 Converges at Hero, orbits by OrbitStep, then measures the displayed image after k new samples. The reprojection
//    pass is timed on its own (explicit flush between camera change and first sample).
async function RenderReprojectedCurve(Page, Camera, Reference, PriorSamples)
{
    await Page.evaluate(async (Camera, PriorSamples) =>
    {
        const Tracing = window.DeterministicTracing;
        Tracing.ApplyConfiguration({ ReprojectionEnabled: true, Camera: { ...Camera, Target: [...Camera.Target] } });
        await Tracing.RenderFromZero(PriorSamples, 4);
    }, Camera, PriorSamples);
    const ReprojectionSeconds = await Page.evaluate(async OrbitStep =>
    {
        const Tracing = window.DeterministicTracing;
        Tracing.MoveCamera({ Yaw: Tracing.Configuration.Camera.Yaw + OrbitStep });
        const Start = performance.now();
        Tracing.Division.FlushPendingReprojection();
        await Tracing.Division.Device.queue.onSubmittedWorkDone();
        return (performance.now() - Start) / 1000;
    }, OrbitStep);
    const Curve = [];
    let Accepted = null, Displayed0 = null;
    const Start = Date.now();
    for (const Samples of [0, ...RealtimeCheckpoints])
    {
        await Page.evaluate(async Samples => window.DeterministicTracing.ContinueSamples(Samples), Samples);
        const Elapsed = (Date.now() - Start) / 1000;
        const { Mean, History } = await RetrieveDisplayed(Page);
        if (Samples === 0)
        {
            let Count = 0;
            for (let Pixel = 0; Pixel < History.length / 4; Pixel++) if (History[Pixel * 4 + 3] > 0) Count++;
            Accepted = Count / (History.length / 4);
            Displayed0 = RelativeRootMeanSquare(Mean.map(Entry => (Number.isFinite(Entry) ? Entry : 0)), Reference);
            continue;
        }
        Curve.push({ Samples, Seconds: Elapsed, RelativeRms: RelativeRootMeanSquare(Mean, Reference) });
    }
    await Page.evaluate(() => window.DeterministicTracing.ApplyConfiguration({ ReprojectionEnabled: false }));
    return { Curve, ReprojectionSeconds, AcceptedFraction: Accepted, RelativeRmsBeforeFirstSample: Displayed0 };
}

async function ValidateRealtime(Browser, Server)
{
    const Width = 160, Height = 90, ReferenceSamples = 256, PriorSamples = 32;
    const Page = await OpenPrototype(Browser, Server, `width=${Width}&height=${Height}&camera=Hero`);
    const Hero = await Page.evaluate(() => ({ ...window.DeterministicTracing.CameraPresets.Hero }));
    const Moved = { ...Hero, Target: [...Hero.Target], Yaw: Hero.Yaw + OrbitStep };

    const ReferenceAt = async Camera =>
    {
        await Page.evaluate(Camera => window.DeterministicTracing.ApplyConfiguration({ Camera: { ...Camera, Target: [...Camera.Target] } }), Camera);
        return (await RenderCheckpoints(Page, ReferenceSamples, 16, [ReferenceSamples], false))[0].Mean;
    };
    const HeroReference = await ReferenceAt(Hero);
    const Modes = {};
    Modes.SharedTraced = await RenderModeCurve(Page, { PointPattern: 'shared', VisibilityRasterEnabled: false }, HeroReference);
    Modes.PixelTraced  = await RenderModeCurve(Page, { PointPattern: 'pixel',  VisibilityRasterEnabled: false }, HeroReference);
    Modes.SharedRaster = await RenderModeCurve(Page, { PointPattern: 'shared', VisibilityRasterEnabled: true  }, HeroReference);
    await Page.evaluate(() => window.DeterministicTracing.ApplyConfiguration({ PointPattern: 'shared', VisibilityRasterEnabled: false }));

    // 📝 Raster vs traced visibility at 16 spp: how many pixels resolve differently at all.
    let DifferingPixels = 0;
    for (let Pixel = 0; Pixel < Width * Height; Pixel++)
    {
        for (let Channel = 0; Channel < 3; Channel++)
        {
            if (Modes.SharedRaster.Final[Pixel * 3 + Channel] !== Modes.SharedTraced.Final[Pixel * 3 + Channel]) { DifferingPixels++; break; }
        }
    }
    const RasterAgreement = { DifferingPixels, Pixels: Width * Height, RelativeRmsRasterVsTraced: RelativeRootMeanSquare(Modes.SharedRaster.Final, Modes.SharedTraced.Final) };

    const MovedReference = await ReferenceAt(Moved);
    const Restart = await RenderModeCurve(Page, {}, MovedReference);
    const Reprojected = await RenderReprojectedCurve(Page, Hero, MovedReference, PriorSamples);
    await Page.close();

    const Strip = Mode => ({ Curve: Mode.Curve, SecondsPerSample: Mode.SecondsPerSample, Digest: Mode.Digest });
    WriteResult('Realtime', {
        Summary: {
            Width, Height, ReferenceSamples,
            SecondsPerSample: { SharedTraced: Modes.SharedTraced.SecondsPerSample, PixelTraced: Modes.PixelTraced.SecondsPerSample, SharedRaster: Modes.SharedRaster.SecondsPerSample },
            RelativeRmsAt1:   { SharedTraced: Modes.SharedTraced.Curve[0].RelativeRms, PixelTraced: Modes.PixelTraced.Curve[0].RelativeRms },
            RelativeRmsAt16:  { SharedTraced: Modes.SharedTraced.Curve.at(-1).RelativeRms, PixelTraced: Modes.PixelTraced.Curve.at(-1).RelativeRms },
            RasterAgreement,
            AfterOrbit: { RestartRmsAt1: Restart.Curve[0].RelativeRms, ReprojectedRmsAt1: Reprojected.Curve[0].RelativeRms,
                          AcceptedFraction: Reprojected.AcceptedFraction, ReprojectionSeconds: Reprojected.ReprojectionSeconds }
        },
        Modes: { SharedTraced: Strip(Modes.SharedTraced), PixelTraced: Strip(Modes.PixelTraced), SharedRaster: Strip(Modes.SharedRaster) },
        Orbit: { Step: OrbitStep, PriorSamples, Restart: Restart.Curve, Reprojected },
        Note: 'Hero view. Pattern/visibility curves against shared-traced at 256 spp; orbit curves against 256 spp at the moved view. Seconds include checkpoint readbacks.'
    });
    await RetainRealtimeImages(Browser, Server, Hero, PriorSamples);
}

// 📝 Side-by-side evidence at 320 × 180: dither vs structure at 4 spp, and the first frame after an orbit.
async function RetainRealtimeImages(Browser, Server, Hero, PriorSamples)
{
    const Page = await OpenPrototype(Browser, Server, 'width=320&height=180&camera=Hero');
    Hero ??= await Page.evaluate(() => ({ ...window.DeterministicTracing.CameraPresets.Hero }));
    const Panels = [];
    const Capture = async (Label, Name) => { const Png = await Page.evaluate(() => window.DeterministicTracing.EncodeDisplayPng()); WritePng(Name, Png); Panels.push({ Label, Png }); };
    for (const [Pixel, Label, Name] of [[false, 'Shared pattern · 4 spp', 'Realtime_PatternShared_4spp.png'], [true, 'Per-pixel pattern · 4 spp', 'Realtime_PatternPixel_4spp.png']])
    {
        await Page.evaluate(async Pixel => { const Tracing = window.DeterministicTracing; Tracing.ApplyConfiguration({ PointPattern: Pixel ? 'pixel' : 'shared' }); await Tracing.RenderFromZero(4, 1); }, Pixel);
        await Capture(Label, Name);
    }
    const MoveAndSample = async Reproject => Page.evaluate(async (Hero, Reproject, PriorSamples, OrbitStep) =>
    {
        const Tracing = window.DeterministicTracing;
        Tracing.ApplyConfiguration({ PointPattern: 'shared', ReprojectionEnabled: Reproject, Camera: { ...Hero, Target: [...Hero.Target] } });
        await Tracing.RenderFromZero(PriorSamples, 4);
        Tracing.MoveCamera({ Yaw: Hero.Yaw + OrbitStep });
        await Tracing.ContinueSamples(1);
    }, Hero, Reproject, PriorSamples, OrbitStep);
    await MoveAndSample(false); await Capture('After orbit · restart · 1 spp', 'Realtime_OrbitRestart_1spp.png');
    await MoveAndSample(true);  await Capture('After orbit · reprojected · 1 spp', 'Realtime_OrbitReprojected_1spp.png');
    await Page.evaluate(async () => { const Tracing = window.DeterministicTracing; Tracing.ApplyConfiguration({ ReprojectionEnabled: false }); await Tracing.ContinueSamples(64); });
    await Capture('After orbit · reference · 64 spp', 'Realtime_OrbitReference_64spp.png');
    await Page.close();

    const Montage = await Browser.newPage();
    await Montage.setViewport({ width: 3 * 640 + 4 * 12, height: 2 * 360 + 2 * 34 + 3 * 12 });
    const Cells = [Panels[0], Panels[1], null, Panels[2], Panels[3], Panels[4]].map(Panel => Panel
        ? `<figure><figcaption>${Panel.Label}</figcaption><img src="${Panel.Png}"></figure>` : '<figure></figure>').join('');
    await Montage.setContent(`<html><body style="margin:0;background:#0f1114;font-family:system-ui,sans-serif">
        <style>main{display:grid;grid-template-columns:repeat(3,640px);gap:12px;padding:12px}figure{margin:0}
        figcaption{color:#e7e9ec;font-size:16px;height:34px;line-height:28px}img{width:640px;height:360px;image-rendering:pixelated;display:block}</style>
        <main>${Cells}</main></body></html>`);
    await Montage.evaluate(async () => { await document.fonts.ready; await Promise.all([...document.images].map(Image => Image.decode())); });
    await Montage.screenshot({ path: path.join(ImagesFolder, 'RealtimeComparison.png') });
    // 📝 Display-space error (8-bit sRGB after tone mapping) of each orbit panel against the 64 spp reference — what the
    //    viewer sees; linear RMS is dominated by the few very bright reflection pixels.
    const DisplayError = await Montage.evaluate(() =>
    {
        const Decode = Image => { const Surface = document.createElement('canvas'); Surface.width = Image.naturalWidth; Surface.height = Image.naturalHeight;
                                  const Context = Surface.getContext('2d'); Context.drawImage(Image, 0, 0); return Context.getImageData(0, 0, Surface.width, Surface.height).data; };
        const Images = [...document.images].map(Decode);
        const Reference = Images[4];
        return [2, 3].map(Index =>
        {
            let Absolute = 0, Visible = 0;
            for (let Pixel = 0; Pixel < Reference.length / 4; Pixel++)
            {
                let Largest = 0;
                for (let Channel = 0; Channel < 3; Channel++) { const Difference = Math.abs(Images[Index][Pixel * 4 + Channel] - Reference[Pixel * 4 + Channel]); Absolute += Difference; Largest = Math.max(Largest, Difference); }
                if (Largest > 8) Visible++;
            }
            return { MeanAbsoluteLevels: Absolute / (Reference.length / 4 * 3), PixelsOffByMoreThan8Levels: Visible / (Reference.length / 4) };
        });
    });
    WriteResult('RealtimeDisplay', {
        Summary: { RestartAt1: DisplayError[0], ReprojectedAt1: DisplayError[1] },
        Note: '320 x 180 Hero after a 0.05 rad orbit from 32 spp; 8-bit display levels vs 64 spp at the moved view.'
    });
    await Montage.close();
    console.log('[realtime] Images/RealtimeComparison.png');
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    SAMPLE PATTERNS
//------------------------------------------------------------------------------------------------------------------------

const PatternNames = { shared: 'Shared', pixel: 'Per-pixel', bluenoise: 'Blue-noise' };

// 📝 Perceptual proxy: the eye integrates over roughly a pixel, so error is also measured after a σ = 1 px Gaussian
//    applied to image and reference alike (fine dither cancels, banding and clumps survive).
function ApplyGaussianBlur(Image, Width, Height)
{
    const Weights = [-3, -2, -1, 0, 1, 2, 3].map(Offset => Math.exp(-Offset * Offset / 2));
    const Total = Weights.reduce((Sum, Weight) => Sum + Weight, 0);
    const BlurAlong = (Source, StepX, StepY) =>
    {
        const Target = new Float32Array(Source.length);
        for (let Row = 0; Row < Height; Row++)
        {
            for (let Column = 0; Column < Width; Column++)
            {
                for (let Channel = 0; Channel < 3; Channel++)
                {
                    let Sum = 0;
                    for (let Tap = -3; Tap <= 3; Tap++)
                    {
                        const X = Math.min(Width - 1, Math.max(0, Column + Tap * StepX)), Y = Math.min(Height - 1, Math.max(0, Row + Tap * StepY));
                        Sum += Source[(Y * Width + X) * 3 + Channel] * Weights[Tap + 3];
                    }
                    Target[(Row * Width + Column) * 3 + Channel] = Sum / Total;
                }
            }
        }
        return Target;
    };
    return BlurAlong(BlurAlong(Image, 1, 0), 0, 1);
}

// 📝 Power of a 64² mask (mean removed) and its radial average in bands of integer frequency radius.
function MeasureRadialSpectrum(Mask)
{
    const Edge = 64, Mean = Mask.reduce((Sum, Entry) => Sum + Entry, 0) / Mask.length;
    const Power = new Float64Array(Edge * Edge), Bands = new Float64Array(46), Counts = new Float64Array(46);
    for (let U = 0; U < Edge; U++)
    {
        for (let V = 0; V < Edge; V++)
        {
            let Real = 0, Imaginary = 0;
            for (let Y = 0; Y < Edge; Y++)
            {
                for (let X = 0; X < Edge; X++)
                {
                    const Phase = -2 * Math.PI * (U * X + V * Y) / Edge, Centred = Mask[Y * Edge + X] - Mean;
                    Real += Centred * Math.cos(Phase); Imaginary += Centred * Math.sin(Phase);
                }
            }
            Power[V * Edge + U] = (Real * Real + Imaginary * Imaginary) / (Edge * Edge);
            const Radius = Math.round(Math.hypot(Math.min(U, Edge - U), Math.min(V, Edge - V)));
            if (U || V) { Bands[Radius] += Power[V * Edge + U]; Counts[Radius]++; }
        }
    }
    return { Power: Array.from(Power), Radial: Array.from(Bands, (Sum, Radius) => (Counts[Radius] ? Sum / Counts[Radius] : 0)) };
}

async function ValidatePatterns(Browser, Server)
{
    const Width = 160, Height = 90, ReferenceSamples = 1024, Checkpoints = PowersOfTwo(64);
    const Page = await OpenPrototype(Browser, Server, `width=${Width}&height=${Height}&camera=Hero`);
    const Reference = (await RenderCheckpoints(Page, ReferenceSamples, 16, [ReferenceSamples], false))[0].Mean;
    const BlurredReference = ApplyGaussianBlur(Reference, Width, Height);
    const Curves = {};
    for (const Pattern of Object.keys(PatternNames))
    {
        await Page.evaluate(Pattern => window.DeterministicTracing.ApplyConfiguration({ PointPattern: Pattern }), Pattern);
        const Retained = await RenderCheckpoints(Page, 64, 1, Checkpoints, false);
        Curves[Pattern] = {
            SecondsPerSample: Retained.at(-1).Seconds / 64,
            Digest: await Page.evaluate(() => window.DeterministicTracing.ComputeDigest()),
            Curve: Retained.map(Entry => ({ Samples: Entry.Samples, RelativeRms: RelativeRootMeanSquare(Entry.Mean, Reference),
                                            BlurredRelativeRms: RelativeRootMeanSquare(ApplyGaussianBlur(Entry.Mean, Width, Height), BlurredReference) }))
        };
    }
    await Page.close();
    WriteResult('Patterns', {
        Summary: Object.fromEntries(Object.entries(Curves).map(([Pattern, Entry]) => [Pattern, {
            SecondsPerSample: +Entry.SecondsPerSample.toFixed(3), Digest64: Entry.Digest,
            Rms: Object.fromEntries(Entry.Curve.map(Point => [Point.Samples, +Point.RelativeRms.toFixed(4)])),
            BlurredRms: Object.fromEntries(Entry.Curve.map(Point => [Point.Samples, +Point.BlurredRelativeRms.toFixed(4)])) }])),
        Width, Height, ReferenceSamples, Curves,
        Note: 'Hero view. Reference: shared pattern at 1024 spp (its own prefixes are slightly favoured). Blurred = σ 1 px Gaussian on both.'
    });
    await RetainPatternImages(Browser, Server);
}

// 📝 Progression grid (rows = pattern, columns = spp) and the mask / spectrum proof of the blue-noise table.
async function RetainPatternImages(Browser, Server)
{
    const Columns = [1, 4, 16, 64], Rows = [];
    const Page = await OpenPrototype(Browser, Server, 'width=320&height=180&camera=Hero');
    for (const Pattern of Object.keys(PatternNames))
    {
        await Page.evaluate(Pattern => window.DeterministicTracing.ApplyConfiguration({ PointPattern: Pattern }), Pattern);
        const Strip = await RenderCheckpoints(Page, 64, 4, Columns, true);
        Strip.forEach(Entry => WritePng(`Pattern_${PatternNames[Pattern].replace('-', '')}_${String(Entry.Samples).padStart(3, '0')}spp.png`, Entry.Png));
        Rows.push({ Label: PatternNames[Pattern], Pngs: Strip.map(Entry => Entry.Png) });
    }
    await Page.close();

    const Grid = await Browser.newPage();
    await Grid.setViewport({ width: 110 + 4 * 492 + 16, height: 44 + 3 * 282 });
    await Grid.setContent(`<html><body style="margin:0;background:#0f1114;color:#e7e9ec;font:15px system-ui,sans-serif">
        <div style="display:grid;grid-template-columns:110px repeat(4,480px);gap:12px;padding:8px">
        <div></div>${Columns.map(Samples => `<div style="text-align:center">${Samples} spp</div>`).join('')}
        ${Rows.map(Row => `<div style="align-self:center">${Row.Label}</div>` + Row.Pngs.map(Png => `<img src="${Png}" style="width:480px;height:270px;image-rendering:pixelated">`).join('')).join('')}
        </div></body></html>`);
    await Grid.evaluate(async () => { await document.fonts.ready; await Promise.all([...document.images].map(Image => Image.decode())); });
    await Grid.screenshot({ path: path.join(ImagesFolder, 'PatternProgression.png') });
    await Grid.close();

    // 📝 Hash mask = the per-pixel pattern's key (ResolvePixelScramble in RadianceIntegration.wgsl) over a 64² tile.
    const Packed = new Uint32Array(fs.readFileSync(path.join(PrototypeFolder, 'Assets/BlueNoiseRanks.bin')).buffer.slice(0));
    const BlueMask = Array.from(Packed, Entry => (Entry & 0xfff) / 4096);
    const HashMask = Array.from(Packed, (_, Pixel) =>
        AvalancheKey((Math.imul(Pixel % 64, 0x8da6b343) ^ Math.imul(Math.floor(Pixel / 64), 0xd8163841) ^ 0x2545f491) >>> 0) / 4294967296);
    const Spectra = [MeasureRadialSpectrum(BlueMask), MeasureRadialSpectrum(HashMask)];
    const BandMean = (Radial, First, Last) => +(Radial.slice(First, Last + 1).reduce((Sum, Band) => Sum + Band, 0) / (Last - First + 1)).toExponential(2);
    WriteResult('BlueNoiseMask', {
        Summary: { LowBandPower: Spectra.map(Entry => BandMean(Entry.Radial, 1, 4)), HighBandPower: Spectra.map(Entry => BandMean(Entry.Radial, 24, 31)) },
        Order: ['blue-noise mask A', 'per-pixel hash'], Radial: Spectra.map(Entry => Entry.Radial),
        Note: 'Radially averaged power of the 64² mask; bands 1–4 = low frequency, 24–31 = high.'
    });
    const Proof = await Browser.newPage();
    await Proof.setViewport({ width: 4 * 268 + 12, height: 300 });
    await Proof.setContent(`<html><body style="margin:0;background:#0f1114;color:#e7e9ec;font:14px system-ui,sans-serif">
        <div id="Row" style="display:flex;gap:12px;padding:6px"></div></body></html>`);
    await Proof.evaluate(Panels =>
    {
        for (const Panel of Panels)
        {
            const Figure = document.createElement('div');
            Figure.innerHTML = `<div style="height:22px">${Panel.Label}</div>`;
            const Surface = document.createElement('canvas'); Surface.width = 64; Surface.height = 64;
            Surface.style.cssText = 'width:256px;height:256px;image-rendering:pixelated';
            const Context = Surface.getContext('2d'), Pixels = Context.createImageData(64, 64);
            const Largest = Math.max(...Panel.Values);
            Panel.Values.forEach((Entry, Pixel) =>
            {
                const Centred = Panel.Values[((Math.floor(Pixel / 64) + 32) % 64) * 64 + (Pixel % 64 + 32) % 64];
                const Level = Panel.Spectrum ? Math.max(0, 1 + Math.log10(Centred / Largest + 1e-9) / 4) : Entry;
                Pixels.data.set([Level * 255, Level * 255, Level * 255, 255], Pixel * 4);
            });
            Context.putImageData(Pixels, 0, 0);
            Figure.appendChild(Surface);
            document.getElementById('Row').appendChild(Figure);
        }
    }, [{ Label: 'Blue-noise mask A (64²)', Values: BlueMask }, { Label: 'Its power spectrum (log, DC centred)', Values: Spectra[0].Power, Spectrum: true },
        { Label: 'Per-pixel hash (64²)', Values: HashMask }, { Label: 'Its power spectrum', Values: Spectra[1].Power, Spectrum: true }]);
    await Proof.screenshot({ path: path.join(ImagesFolder, 'BlueNoiseMask.png') });
    await Proof.close();
    console.log('[patterns] Images/PatternProgression.png, Images/BlueNoiseMask.png');
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE COUNT
//------------------------------------------------------------------------------------------------------------------------

const FleetCounts = [1, 2, 4, 8, 16, 32, 50];

// 📝 Cost of N parked ControlVehicles: tree construction on the host, then seconds per sample (one sample per
//    dispatch, as the interactive loop runs it) from the Hero and Fleet cameras. One warm-up sample is discarded.
async function ValidateFleet(Browser, Server)
{
    const Width = 320, Height = 180, TimedSamples = 4;
    const Page = await OpenPrototype(Browser, Server, `width=${Width}&height=${Height}&camera=Hero`);
    const Presets = await Page.evaluate(() => window.DeterministicTracing.CameraPresets);
    const Rows = [];
    for (const VehicleCount of FleetCounts)
    {
        const Row = await Page.evaluate(async (VehicleCount, Presets, TimedSamples) =>
        {
            const Tracing = window.DeterministicTracing;
            Tracing.ApplyConfiguration({ VehicleCount });
            const Summary = { ...Tracing.Division.SceneSummary };
            const Timing = {};
            for (const Camera of ['Hero', 'Fleet'])
            {
                Tracing.ApplyConfiguration({ Camera: { ...Presets[Camera], Target: [...Presets[Camera].Target] } }, false);
                await Tracing.RenderFromZero(1, 1);
                const Start = performance.now();
                await Tracing.RenderFromZero(TimedSamples, 1);
                Timing[Camera] = (performance.now() - Start) / 1000 / TimedSamples;
            }
            // 📝 Work per pixel-sample from the Fleet camera (ray-statistics pipeline: counters instead of radiance).
            const Work = await Tracing.Division.MeasureRayStatistics(1);
            return { VehicleCount, Triangles: Summary.TriangleCount, BoundsEntries: Summary.BoundsEntryCount, BoundsDepth: Summary.BoundsDepthTier,
                     VehicleNodes: Summary.VehicleNodes, InstanceNodes: Summary.InstanceNodes, BoundsBytes: Summary.BoundsBytes,
                     ConstructionSeconds: Summary.ConstructionMilliseconds / 1000, HeroSecondsPerSample: Timing.Hero, FleetSecondsPerSample: Timing.Fleet,
                     FleetRaysPerSample: +(Work.ClosestRays + Work.ShadowRays).toFixed(2), FleetBoxTestsPerSample: +Work.BoxTests.toFixed(1),
                     FleetTriangleTestsPerSample: +Work.TriangleTests.toFixed(1) };
        }, VehicleCount, Presets, TimedSamples);
        Rows.push(Row);
        console.log(`[fleet] ${JSON.stringify(Row)}`);
    }
    const Baseline = Rows[0];
    for (const Row of Rows)
    {
        Row.HeroRelative  = Row.HeroSecondsPerSample / Baseline.HeroSecondsPerSample;
        Row.FleetRelative = Row.FleetSecondsPerSample / Baseline.FleetSecondsPerSample;
    }
    await Page.close();
    WriteResult('Fleet', {
        Summary: Rows.map(Row => ({ Vehicles: Row.VehicleCount, Triangles: Row.Triangles, Depth: Row.BoundsDepth, BuildSeconds: Row.ConstructionSeconds,
                                    HeroRelative: +Row.HeroRelative.toFixed(2), FleetRelative: +Row.FleetRelative.toFixed(2) })),
        Width, Height, TimedSamples, Rows,
        Note: 'SwiftShader (CPU WebGPU), so absolute seconds are not GPU timings; FPS at 1 sample per frame = 1 / seconds per sample. '
            + 'Geometry is instanced: one 4-wide vehicle tree shared by every bay plus a 4-wide instance tree, so BuildSeconds after the first count is the instance tree only.'
    });
    await RetainFleetImages(Browser, Server);
}

async function RetainFleetImages(Browser, Server)
{
    const Panels = [];
    const Page = await OpenPrototype(Browser, Server, 'width=320&height=180&camera=Fleet');
    for (const VehicleCount of [1, 16, 50])
    {
        await Page.evaluate(async VehicleCount => { const Tracing = window.DeterministicTracing; Tracing.ApplyConfiguration({ VehicleCount }); await Tracing.RenderFromZero(16, 4); }, VehicleCount);
        const Png = await Page.evaluate(() => window.DeterministicTracing.EncodeDisplayPng());
        WritePng(`Fleet_${String(VehicleCount).padStart(2, '0')}Vehicles_16spp.png`, Png);
        Panels.push({ Label: `${VehicleCount} vehicle${VehicleCount > 1 ? 's' : ''} · ${(25068 * VehicleCount).toLocaleString('en-US')} triangles · 16 spp`, Png });
    }
    await Page.close();
    const Montage = await Browser.newPage();
    await Montage.setViewport({ width: 3 * 652 + 12, height: 360 + 46 });
    await Montage.setContent(`<html><body style="margin:0;background:#0f1114;color:#e7e9ec;font:15px system-ui,sans-serif">
        <div style="display:flex;gap:12px;padding:6px">${Panels.map(Panel => `<figure style="margin:0"><figcaption style="height:28px">${Panel.Label}</figcaption>
        <img src="${Panel.Png}" style="width:640px;height:360px;image-rendering:pixelated;display:block"></figure>`).join('')}</div></body></html>`);
    await Montage.evaluate(async () => { await document.fonts.ready; await Promise.all([...document.images].map(Image => Image.decode())); });
    await Montage.screenshot({ path: path.join(ImagesFolder, 'FleetComparison.png') });
    await Montage.close();
    console.log('[fleet] Images/FleetComparison.png');
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    DISPLAY RECONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

const ReconstructionCheckpoints = [1, 2, 4, 8, 16, 64];
const OrbitCheckpoints          = [1, 2, 4, 8, 16];
const ReconstructionExposure    = 1.0;

// 📝 Linear (rgb, w) texels → 8-bit display pixels through the prototype's own host tone map.
function EncodeDisplayLevels(Texels, Width, Height)
{
    return ResolveDisplayPixels(Float32Array.from(Texels), Width, Height, ReconstructionExposure);
}

// 📝 Both error measures: relative linear RMS, and display-space error (mean |Δ| in 8-bit levels, share of pixels off by
//    more than 8 levels in any channel) — the second is what a viewer sees.
function MeasureDisplayedError(Texels, ReferenceTexels, Width, Height)
{
    const Mean = ResolveDisplayedMean(Texels, null), Reference = ResolveDisplayedMean(ReferenceTexels, null);
    const Levels = EncodeDisplayLevels(Texels, Width, Height), ReferenceLevels = EncodeDisplayLevels(ReferenceTexels, Width, Height);
    let Absolute = 0, Visible = 0;
    for (let Pixel = 0; Pixel < Width * Height; Pixel++)
    {
        let Largest = 0;
        for (let Channel = 0; Channel < 3; Channel++)
        {
            const Difference = Math.abs(Levels[Pixel * 4 + Channel] - ReferenceLevels[Pixel * 4 + Channel]);
            Absolute += Difference; Largest = Math.max(Largest, Difference);
        }
        if (Largest > 8) Visible++;
    }
    return { RelativeRms: +RelativeRootMeanSquare(Mean, Reference).toFixed(5), MeanAbsoluteLevels: +(Absolute / (Width * Height * 3)).toFixed(3),
             PixelsOffBy8: +(Visible / (Width * Height)).toFixed(4) };
}

// 📝 Toggles the denoiser on the live division without touching the sums (no reset), then reads the displayed radiance.
async function RetrieveDisplayedTexels(Page, DenoiseEnabled)
{
    return Page.evaluate(async DenoiseEnabled =>
    {
        const Division = window.DeterministicTracing.Division;
        Division.Configuration = { ...Division.Configuration, DenoiseEnabled };
        return Array.from(await Division.RetrieveDisplayRadiance());
    }, DenoiseEnabled);
}

// 📝 Converges PriorSamples at Hero, orbits by OrbitStep (as the mouse does), then reads the display after k samples.
async function RenderOrbitVariant(Page, Hero, Variant, ReferenceTexels, Width, Height, PriorSamples)
{
    await Page.evaluate(async (Hero, Variant, PriorSamples) =>
    {
        const Tracing = window.DeterministicTracing;
        Tracing.ApplyConfiguration({ ...Variant, Camera: { ...Hero, Target: [...Hero.Target] } });
        await Tracing.RenderFromZero(PriorSamples, 4);
        Tracing.MoveCamera({ Yaw: Hero.Yaw + 0.05 });
    }, Hero, Variant, PriorSamples);
    const Curve = [];
    for (const Samples of OrbitCheckpoints)
    {
        await Page.evaluate(async Samples => window.DeterministicTracing.ContinueSamples(Samples), Samples);
        Curve.push({ Samples, ...MeasureDisplayedError(await RetrieveDisplayedTexels(Page, Boolean(Variant.DenoiseEnabled)), ReferenceTexels, Width, Height) });
    }
    return Curve;
}

async function ValidateReconstruction(Browser, Server)
{
    const Width = 160, Height = 90, ReferenceSamples = 256, PriorSamples = 32;
    const Page = await OpenPrototype(Browser, Server, `width=${Width}&height=${Height}&camera=Hero`);
    const Hero  = await Page.evaluate(() => ({ ...window.DeterministicTracing.CameraPresets.Hero }));
    const Moved = { ...Hero, Target: [...Hero.Target], Yaw: Hero.Yaw + 0.05 };
    const ReferenceAt = async Camera =>
    {
        await Page.evaluate(async (Camera, ReferenceSamples) =>
        {
            const Tracing = window.DeterministicTracing;
            Tracing.ApplyConfiguration({ DenoiseEnabled: false, ReprojectionEnabled: false, Camera: { ...Camera, Target: [...Camera.Target] } });
            await Tracing.RenderFromZero(ReferenceSamples, 16);
        }, Camera, ReferenceSamples);
        return RetrieveDisplayedTexels(Page, false);
    };

    // ① Still frame: exact vs denoised display from the same sums, for the shared and the blue-noise pattern.
    const HeroReference = await ReferenceAt(Hero);
    const Still = {};
    for (const PointPattern of ['shared', 'bluenoise'])
    {
        Still[PointPattern] = [];
        await Page.evaluate(PointPattern => { const Tracing = window.DeterministicTracing; Tracing.ApplyConfiguration({ PointPattern }); Tracing.Division.ResetAccumulation(); }, PointPattern);
        for (const Samples of ReconstructionCheckpoints)
        {
            await Page.evaluate(async Samples => window.DeterministicTracing.ContinueSamples(Samples), Samples);
            const Strength = await Page.evaluate(() => { const Division = window.DeterministicTracing.Division; Division.Configuration = { ...Division.Configuration, DenoiseEnabled: true }; return Division.DenoiseStrength(); });
            Still[PointPattern].push({ Samples, Strength: +Strength.toFixed(3),
                                       Exact:    MeasureDisplayedError(await RetrieveDisplayedTexels(Page, false), HeroReference, Width, Height),
                                       Denoised: MeasureDisplayedError(await RetrieveDisplayedTexels(Page, true),  HeroReference, Width, Height) });
            console.log(`[reconstruction] still ${PointPattern} ${JSON.stringify(Still[PointPattern].at(-1))}`);
        }
    }
    await Page.evaluate(() => window.DeterministicTracing.ApplyConfiguration({ PointPattern: 'shared' }));

    // ② Determinism and cost: denoised display after 4 spp, batch 1 vs batch 4; reconstruction timed on its own.
    const Repeat = await Page.evaluate(async () =>
    {
        const Tracing = window.DeterministicTracing, Division = Tracing.Division;
        const Rounds = [];
        for (const Batch of [1, 4])
        {
            Division.Configuration = { ...Division.Configuration, DenoiseEnabled: true };
            await Tracing.RenderFromZero(4, Batch);
            Rounds.push(Array.from(await Division.RetrieveDisplayRadiance()));
        }
        const Timings = [];
        for (let Round = 0; Round < 6; Round++)
        {
            const Start = performance.now();
            const Encoder = Division.Device.createCommandEncoder();
            Division.EncodeReconstruction(Encoder);
            Division.Device.queue.submit([Encoder.finish()]);
            await Division.Device.queue.onSubmittedWorkDone();
            Timings.push(performance.now() - Start);
        }
        const SampleStart = performance.now();
        await Tracing.ContinueSamples(Division.SampleCount + 2);
        const SampleMilliseconds = (performance.now() - SampleStart) / 2;
        return { Rounds, ReconstructionMilliseconds: Timings.slice(1).sort((A, B) => A - B)[2], SampleMilliseconds };
    });
    const Digests = Repeat.Rounds.map(Round => DigestFloatBuffer(Float32Array.from(Round)));
    const Determinism = { DigestBatch1: Digests[0], DigestBatch4: Digests[1], BitIdentical: Digests[0] === Digests[1] };
    const Cost = { ReconstructionMilliseconds: +Repeat.ReconstructionMilliseconds.toFixed(1), SampleMilliseconds: +Repeat.SampleMilliseconds.toFixed(1),
                   RelativeToOneSample: +(Repeat.ReconstructionMilliseconds / Repeat.SampleMilliseconds).toFixed(3) };
    console.log(`[reconstruction] ${JSON.stringify({ Determinism, Cost })}`);

    // ③ Orbit: restart vs history variants (span = samples until history is gone, clamp γ) vs denoised.
    const MovedReference = await ReferenceAt(Moved);
    const Variants = {
        Restart:                   { ReprojectionEnabled: false, DenoiseEnabled: false },
        HistorySpan16:             { ReprojectionEnabled: true,  DenoiseEnabled: false, HistoryClamp: 0,   HistorySpan: 16 },
        HistorySpan8:              { ReprojectionEnabled: true,  DenoiseEnabled: false, HistoryClamp: 0,   HistorySpan: 8 },
        HistorySpan8Clamped:       { ReprojectionEnabled: true,  DenoiseEnabled: false, HistoryClamp: 1.5, HistorySpan: 8 },
        RestartDenoised:           { ReprojectionEnabled: false, DenoiseEnabled: true },
        HistoryDenoised:           { ReprojectionEnabled: true,  DenoiseEnabled: true,  HistoryClamp: 0,   HistorySpan: 8 },
        HistoryDenoisedBlueNoise:  { ReprojectionEnabled: true,  DenoiseEnabled: true,  HistoryClamp: 0,   HistorySpan: 8, PointPattern: 'bluenoise' }
    };
    const Orbit = {};
    for (const [Name, Variant] of Object.entries(Variants))
    {
        Orbit[Name] = await RenderOrbitVariant(Page, Hero, Variant, MovedReference, Width, Height, PriorSamples);
        console.log(`[reconstruction] orbit ${Name} ${Orbit[Name].map(Entry => `${Entry.Samples}:${Entry.RelativeRms}/${Entry.MeanAbsoluteLevels}`).join(' ')}`);
    }
    await Page.evaluate(() => window.DeterministicTracing.ApplyConfiguration({ ReprojectionEnabled: false, DenoiseEnabled: false, HistoryClamp: 0, HistorySpan: 8, PointPattern: 'shared' }));
    await Page.close();

    WriteResult('Reconstruction', {
        Summary: {
            Width, Height, ReferenceSamples,
            StillAt1: { Exact: Still.shared[0].Exact, Denoised: Still.shared[0].Denoised, BlueNoiseDenoised: Still.bluenoise[0].Denoised },
            StillAt4: { Exact: Still.shared[2].Exact, Denoised: Still.shared[2].Denoised, BlueNoiseDenoised: Still.bluenoise[2].Denoised },
            Determinism, Cost
        },
        Still, Orbit: { Step: 0.05, PriorSamples, Variants, Curves: Orbit },
        Note: 'Hero view, SwiftShader. Errors against the exact 256 spp image (still) or at the moved view (orbit). Display levels: ACES + sRGB, exposure 1.'
    });
    await RetainReconstructionImages(Browser, Server, Hero, PriorSamples);
}

// 📝 320 × 180 evidence: still frame exact vs denoised at 1 and 4 spp with the reference, and the first frame after an orbit.
async function RetainReconstructionImages(Browser, Server, Hero, PriorSamples)
{
    const Page = await OpenPrototype(Browser, Server, 'width=320&height=180&camera=Hero');
    Hero ??= await Page.evaluate(() => ({ ...window.DeterministicTracing.CameraPresets.Hero }));
    const Panels = [];
    const Capture = async (Label, Name, DenoiseEnabled) =>
    {
        const Png = await Page.evaluate(async DenoiseEnabled =>
        {
            const Division = window.DeterministicTracing.Division;
            Division.Configuration = { ...Division.Configuration, DenoiseEnabled };
            return window.DeterministicTracing.EncodeDisplayPng();
        }, DenoiseEnabled);
        WritePng(Name, Png); Panels.push({ Label, Png });
    };
    await Page.evaluate(async () => { const Tracing = window.DeterministicTracing; Tracing.ApplyConfiguration({ DenoiseEnabled: false }); await Tracing.RenderFromZero(1, 1); });
    await Capture('Exact · 1 spp', 'Reconstruction_Exact_1spp.png', false);
    await Capture('Denoised · 1 spp', 'Reconstruction_Denoised_1spp.png', true);
    await Page.evaluate(async () => window.DeterministicTracing.ContinueSamples(4));
    await Capture('Exact · 4 spp', 'Reconstruction_Exact_4spp.png', false);
    await Page.evaluate(async () => window.DeterministicTracing.ContinueSamples(128));
    await Capture('Reference · 128 spp (exact)', 'Reconstruction_Reference_128spp.png', false);
    await Page.evaluate(async () => { const Tracing = window.DeterministicTracing; Tracing.ApplyConfiguration({ PointPattern: 'bluenoise' }); await Tracing.RenderFromZero(1, 1); });
    await Capture('Blue-noise pattern + denoised · 1 spp', 'Reconstruction_BlueNoiseDenoised_1spp.png', true);
    const MoveAndSample = async Variant => Page.evaluate(async (Hero, Variant, PriorSamples) =>
    {
        const Tracing = window.DeterministicTracing;
        Tracing.ApplyConfiguration({ ...Variant, Camera: { ...Hero, Target: [...Hero.Target] } });
        await Tracing.RenderFromZero(PriorSamples, 4);
        Tracing.MoveCamera({ Yaw: Hero.Yaw + 0.05 });
        await Tracing.ContinueSamples(1);
    }, Hero, Variant, PriorSamples);
    await MoveAndSample({ ReprojectionEnabled: true, DenoiseEnabled: false, HistoryClamp: 0, HistorySpan: 8, PointPattern: 'bluenoise' });
    await Capture('After orbit · history + blue-noise + denoised · 1 spp', 'Reconstruction_OrbitHistoryDenoised_1spp.png', true);
    await Page.close();

    const Montage = await Browser.newPage();
    await Montage.setViewport({ width: 3 * 640 + 4 * 12, height: 2 * 360 + 2 * 34 + 3 * 12 });
    const Cells = [Panels[0], Panels[2], Panels[3], Panels[1], Panels[4], Panels[5]]
        .map(Panel => `<figure><figcaption>${Panel.Label}</figcaption><img src="${Panel.Png}"></figure>`).join('');
    await Montage.setContent(`<html><body style="margin:0;background:#0f1114;font-family:system-ui,sans-serif">
        <style>main{display:grid;grid-template-columns:repeat(3,640px);gap:12px;padding:12px}figure{margin:0}
        figcaption{color:#e7e9ec;font-size:16px;height:34px;line-height:28px}img{width:640px;height:360px;image-rendering:pixelated;display:block}</style>
        <main>${Cells}</main></body></html>`);
    await Montage.evaluate(async () => { await document.fonts.ready; await Promise.all([...document.images].map(Image => Image.decode())); });
    await Montage.screenshot({ path: path.join(ImagesFolder, 'ReconstructionComparison.png') });
    await Montage.close();
    console.log('[reconstruction] Images/ReconstructionComparison.png');
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    TRACE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

const TraceScales      = [1, 2, 4];
const TraceFrameCounts = [1, 2, 4, 8, 16, 64];   // [dispatches] - one sample index per frame, as the loop runs at batch 1

// 📝 Reduced trace resolution (`trace=2|4`): one ray per scale² tile per frame, holes upsampled from sampled neighbours.
//    Compares frames, not samples: the same number of displayed frames at each scale against the 256-spp full-resolution
//    reference, plus the time per frame — so "same frame rate, worse image" and "same image, faster" are both visible.
async function ValidateTraceScale(Browser, Server)
{
    const Width = 160, Height = 90, ReferenceSamples = 256;
    const Page = await OpenPrototype(Browser, Server, `width=${Width}&height=${Height}&camera=Hero`);
    await Page.evaluate(async ReferenceSamples =>
    {
        const Tracing = window.DeterministicTracing;
        Tracing.ApplyConfiguration({ TraceScale: 1, DenoiseEnabled: false });
        await Tracing.RenderFromZero(ReferenceSamples, 16);
    }, ReferenceSamples);
    const Reference = await RetrieveDisplayedTexels(Page, false);

    const Scales = {};
    for (const TraceScale of TraceScales)
    {
        // Time per frame: median of 5 single-index dispatches after a warm-up.
        const FrameMilliseconds = await Page.evaluate(async TraceScale =>
        {
            const Tracing = window.DeterministicTracing, Division = Tracing.Division;
            Tracing.ApplyConfiguration({ TraceScale, DenoiseEnabled: false });
            await Tracing.RenderFromZero(1, 1);
            const Timings = [];
            for (let Frame = 0; Frame < 5; Frame++)
            {
                const Start = performance.now();
                Division.AdvanceSamples(1);
                await Division.Device.queue.onSubmittedWorkDone();
                Timings.push(performance.now() - Start);
            }
            return Timings.sort((A, B) => A - B)[2];
        }, TraceScale);
        const Curve = [];
        await Page.evaluate(() => window.DeterministicTracing.Division.ResetAccumulation());
        for (const Frames of TraceFrameCounts)
        {
            await Page.evaluate(async Frames => window.DeterministicTracing.ContinueSamples(Frames), Frames);
            const PixelSamples = await Page.evaluate(() => window.DeterministicTracing.Division.PixelSamples());
            Curve.push({ Frames, PixelSamples,
                         Upsampled: MeasureDisplayedError(await RetrieveDisplayedTexels(Page, false), Reference, Width, Height),
                         Denoised:  MeasureDisplayedError(await RetrieveDisplayedTexels(Page, true),  Reference, Width, Height) });
            console.log(`[tracescale] ${TraceScale} ${JSON.stringify(Curve.at(-1))}`);
        }
        Scales[TraceScale] = { FrameMilliseconds: +FrameMilliseconds.toFixed(1), Curve };
    }

    // Determinism at scale 2: sums and denoised display after 8 frames, batch 1 vs batch 8; scale 1 keeps the reference.
    const Repeat = await Page.evaluate(async () =>
    {
        const Tracing = window.DeterministicTracing, Division = Tracing.Division;
        const Rounds = [];
        for (const Batch of [1, 8])
        {
            Tracing.ApplyConfiguration({ TraceScale: 2, DenoiseEnabled: true });
            await Tracing.RenderFromZero(8, Batch);
            Rounds.push({ Sums: await Tracing.ComputeDigest(), Display: Array.from(await Division.RetrieveDisplayRadiance()) });
        }
        Tracing.ApplyConfiguration({ TraceScale: 1, DenoiseEnabled: false });
        await Tracing.RenderFromZero(4, 1);
        return { Rounds, FullDigest4: await Tracing.ComputeDigest() };
    });
    const Determinism = {
        Scale2SumDigests:    Repeat.Rounds.map(Round => Round.Sums),
        Scale2DisplayDigests: Repeat.Rounds.map(Round => DigestFloatBuffer(Float32Array.from(Round.Display))),
        FullResolutionDigest4: Repeat.FullDigest4
    };
    Determinism.BitIdentical = Determinism.Scale2SumDigests[0] === Determinism.Scale2SumDigests[1]
                            && Determinism.Scale2DisplayDigests[0] === Determinism.Scale2DisplayDigests[1];
    console.log(`[tracescale] ${JSON.stringify(Determinism)}`);
    await Page.close();
    WriteResult('TraceScale', {
        Summary: Object.fromEntries(Object.entries(Scales).map(([Scale, Entry]) => [Scale, {
            FrameMilliseconds: Entry.FrameMilliseconds,
            Frames: Object.fromEntries(Entry.Curve.map(Point => [Point.Frames, `${Point.Upsampled.MeanAbsoluteLevels} / ${Point.Denoised.MeanAbsoluteLevels}`])) }])),
        Width, Height, ReferenceSamples, Scales, Determinism,
        Note: 'Errors are vs the full-resolution 256-spp reference: mean |Δ| in 8-bit display levels (upsampled / denoised). SwiftShader timings.'
    });
    await RetainTraceScaleImages(Browser, Server);
}

async function RetainTraceScaleImages(Browser, Server)
{
    const Panels = [];
    const Page = await OpenPrototype(Browser, Server, 'width=320&height=180&camera=Hero');
    for (const TraceScale of TraceScales)
    {
        for (const Frames of [4, 16])
        {
            await Page.evaluate(async (TraceScale, Frames) =>
            {
                const Tracing = window.DeterministicTracing;
                Tracing.ApplyConfiguration({ TraceScale, DenoiseEnabled: true });
                await Tracing.RenderFromZero(Frames, 4);
            }, TraceScale, Frames);
            const Png = await Page.evaluate(() => window.DeterministicTracing.EncodeDisplayPng());
            const Name = `TraceScale_${TraceScale}_${String(Frames).padStart(2, '0')}Frames.png`;
            WritePng(Name, Png);
            const Label = TraceScale === 1 ? 'Full' : (TraceScale === 2 ? '½ (2×2)' : '¼ (4×4)');
            Panels.push({ Label: `${Label} · ${Frames} frames = ${Frames / TraceScale ** 2} spp · denoised`, Png });
        }
    }
    await Page.close();
    const Montage = await Browser.newPage();
    await Montage.setViewport({ width: 3 * 640 + 4 * 12, height: 2 * 360 + 2 * 34 + 3 * 12 });
    const Cells = [0, 2, 4, 1, 3, 5].map(Index => `<figure><figcaption>${Panels[Index].Label}</figcaption><img src="${Panels[Index].Png}"></figure>`).join('');
    await Montage.setContent(`<html><body style="margin:0;background:#0f1114;font-family:system-ui,sans-serif">
        <style>main{display:grid;grid-template-columns:repeat(3,640px);gap:12px;padding:12px}figure{margin:0}
        figcaption{color:#e7e9ec;font-size:16px;height:34px;line-height:28px}img{width:640px;height:360px;image-rendering:pixelated;display:block}</style>
        <main>${Cells}</main></body></html>`);
    await Montage.evaluate(async () => { await document.fonts.ready; await Promise.all([...document.images].map(Image => Image.decode())); });
    await Montage.screenshot({ path: path.join(ImagesFolder, 'TraceScaleComparison.png') });
    await Montage.close();
    console.log('[tracescale] Images/TraceScaleComparison.png');
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    MANY LIGHTS
//------------------------------------------------------------------------------------------------------------------------

const LampFrameCounts = [1, 4, 16, 64, 256];
const LampScenarios = [
    { Label: 'Studio, no lamps',               VehicleCount: 1,  PracticalMode: 'off' },
    { Label: '1 car, 4 lamps',                 VehicleCount: 1,  PracticalMode: 'vehicles' },
    { Label: '8 cars + downlights, 60 lamps',  VehicleCount: 8,  PracticalMode: 'carpark' },
    { Label: '50 cars + downlights, 228 lamps', VehicleCount: 50, PracticalMode: 'carpark' }
];

async function MeasureFrameMilliseconds(Page, Partial)
{
    return Page.evaluate(async Partial =>
    {
        const Tracing = window.DeterministicTracing, Division = Tracing.Division;
        Tracing.ApplyConfiguration(Partial);
        await Tracing.RenderFromZero(1, 1);
        const Timings = [];
        for (let Frame = 0; Frame < 3; Frame++)
        {
            const Start = performance.now();
            Division.AdvanceSamples(1);
            await Division.Device.queue.onSubmittedWorkDone();
            Timings.push(performance.now() - Start);
        }
        return +Timings.sort((A, B) => A - B)[1].toFixed(1);
    }, Partial);
}

// 📝 Practical lamps: (1) the light tree is unbiased — at night with 8 cars and the car-park downlights (60 lamps) its
//    image converges to the every-lamp reference; (2) error per frame and time per frame of both rules; (3) cost against
//    lamp count; (4) determinism (batch 1 = batch 8).
async function ValidateManyLights(Browser, Server)
{
    const Width = 96, Height = 54, ReferenceSamples = 256;
    const Night = { VehicleCount: 8, PracticalMode: 'carpark', NightMode: true, DenoiseEnabled: false };
    const Page = await OpenPrototype(Browser, Server, `width=${Width}&height=${Height}&camera=Hero`);
    await Page.evaluate(async (Night, ReferenceSamples) =>
    {
        const Tracing = window.DeterministicTracing;
        Tracing.ApplyConfiguration({ ...Night, LampSampling: 'all' });
        await Tracing.RenderFromZero(ReferenceSamples, 16);
    }, Night, ReferenceSamples);
    const Reference = await RetrieveDisplayedTexels(Page, false);
    console.log('[manylights] reference done');

    const Rules = {};
    for (const LampSampling of ['tree', 'split8', 'all'])
    {
        const FrameMilliseconds = await MeasureFrameMilliseconds(Page, { ...Night, LampSampling });
        await Page.evaluate(() => window.DeterministicTracing.Division.ResetAccumulation());
        const Curve = [];
        for (const Frames of LampFrameCounts.filter(Frames => LampSampling !== 'all' || Frames <= 64))
        {
            await Page.evaluate(async Frames => window.DeterministicTracing.ContinueSamples(Frames), Frames);
            Curve.push({ Frames, Exact: MeasureDisplayedError(await RetrieveDisplayedTexels(Page, false), Reference, Width, Height),
                                 Denoised: MeasureDisplayedError(await RetrieveDisplayedTexels(Page, true), Reference, Width, Height) });
            console.log(`[manylights] ${LampSampling} ${JSON.stringify(Curve.at(-1))}`);
        }
        Rules[LampSampling] = { FrameMilliseconds, Curve };
    }

    const Repeat = await Page.evaluate(async Night =>
    {
        const Tracing = window.DeterministicTracing;
        const Digests = [];
        for (const Batch of [1, 8])
        {
            Tracing.ApplyConfiguration({ ...Night, LampSampling: 'split8' });
            await Tracing.RenderFromZero(8, Batch);
            Digests.push(await Tracing.ComputeDigest());
        }
        return Digests;
    }, Night);
    await Page.close();

    // Cost against lamp count: studio rig on, Hero camera, 160 × 90, one sample per frame.
    const Scaling = [];
    const Timed = await OpenPrototype(Browser, Server, 'width=160&height=90&camera=Hero');
    for (const Scenario of LampScenarios)
    {
        const Base = { VehicleCount: Scenario.VehicleCount, PracticalMode: Scenario.PracticalMode, NightMode: false, DenoiseEnabled: false };
        const Tree = await MeasureFrameMilliseconds(Timed, { ...Base, LampSampling: 'tree' });
        const Summary = await Timed.evaluate(() => ({ ...window.DeterministicTracing.Division.SceneSummary }));
        const Split = Scenario.PracticalMode === 'off' ? Tree : await MeasureFrameMilliseconds(Timed, { ...Base, LampSampling: 'split8' });
        const All = Scenario.PracticalMode === 'off' ? Tree : await MeasureFrameMilliseconds(Timed, { ...Base, LampSampling: 'all' });
        Scaling.push({ Label: Scenario.Label, Lamps: Summary.LampCount, TreeNodes: Summary.LightTreeNodes, TreeDepth: Summary.LightTreeDepth,
                       BuildMilliseconds: Summary.LightTreeMilliseconds, TreeFrameMilliseconds: Tree, Split8FrameMilliseconds: Split, AllLampsFrameMilliseconds: All });
        console.log(`[manylights] ${JSON.stringify(Scaling.at(-1))}`);
    }
    const HeroDigest = await Timed.evaluate(async () =>
    {
        const Tracing = window.DeterministicTracing;
        Tracing.ApplyConfiguration({ VehicleCount: 1, PracticalMode: 'off', LampSampling: 'split8' });
        await Tracing.RenderFromZero(4, 1);
        return Tracing.ComputeDigest();
    });
    await Timed.close();

    const Determinism = { Split8Digests: Repeat, BitIdentical: Repeat[0] === Repeat[1], LampsOffHeroDigest4: HeroDigest };
    WriteResult('ManyLights', {
        Summary: { Tree: Object.fromEntries(Rules.tree.Curve.map(Point => [Point.Frames, `${Point.Exact.MeanAbsoluteLevels} / ${Point.Denoised.MeanAbsoluteLevels}`])),
                   Split8: Object.fromEntries(Rules.split8.Curve.map(Point => [Point.Frames, `${Point.Exact.MeanAbsoluteLevels} / ${Point.Denoised.MeanAbsoluteLevels}`])),
                   Split8FrameMilliseconds: Rules.split8.FrameMilliseconds,
                   All:  Object.fromEntries(Rules.all.Curve.map(Point => [Point.Frames, `${Point.Exact.MeanAbsoluteLevels} / ${Point.Denoised.MeanAbsoluteLevels}`])),
                   TreeFrameMilliseconds: Rules.tree.FrameMilliseconds, AllFrameMilliseconds: Rules.all.FrameMilliseconds,
                   Scaling: Scaling.map(Row => `${Row.Lamps}: ${Row.TreeFrameMilliseconds} / ${Row.Split8FrameMilliseconds} / ${Row.AllLampsFrameMilliseconds} ms`), Determinism },
        Width, Height, ReferenceSamples, Scene: Night, Rules, Scaling, Determinism,
        Note: 'Night, Hero camera, 8 cars + 28 downlights (60 IES lamps). Errors are mean |Δ| in 8-bit levels (exact / denoised) vs the every-lamp 256-spp reference. Scaling: studio rig on, 160 × 90. SwiftShader timings.'
    });
    await RetainManyLightImages(Browser, Server);
}

async function RetainManyLightImages(Browser, Server)
{
    const Shots = [
        { Name: 'Lamps_Night_Fleet50_64spp.png', Label: 'Night · 50 cars · 228 IES lamps · tree split ≤ 8 · 64 spp', Camera: 'Fleet', Samples: 64, Denoise: false,
          Partial: { VehicleCount: 50, PracticalMode: 'carpark', NightMode: true } },
        { Name: 'Lamps_Night_Hero8_16spp.png', Label: 'Night · 8 cars · 60 lamps · tree split ≤ 8 · 16 spp · denoised', Camera: 'Hero', Samples: 16, Denoise: true,
          Partial: { VehicleCount: 8, PracticalMode: 'carpark', NightMode: true } },
        { Name: 'Lamps_Studio_Hero8_64spp.png', Label: 'Studio rig + car lamps · 8 cars, own finishes · 64 spp', Camera: 'Hero', Samples: 64, Denoise: false,
          Partial: { VehicleCount: 8, PracticalMode: 'vehicles', NightMode: false } }
    ];
    const Page = await OpenPrototype(Browser, Server, 'width=320&height=180&camera=Hero');
    const Presets = await Page.evaluate(() => window.DeterministicTracing.CameraPresets);
    for (const Shot of Shots)
    {
        await Page.evaluate(async (Shot, Preset) =>
        {
            const Tracing = window.DeterministicTracing;
            Tracing.ApplyConfiguration({ ...Shot.Partial, LampSampling: 'split8', DenoiseEnabled: Shot.Denoise, Camera: { ...Preset, Target: [...Preset.Target] } });
            await Tracing.RenderFromZero(Shot.Samples, 8);
        }, Shot, Presets[Shot.Camera]);
        Shot.Png = await Page.evaluate(() => window.DeterministicTracing.EncodeDisplayPng());
        WritePng(Shot.Name, Shot.Png);
        console.log(`[manylights] Images/${Shot.Name}`);
    }
    await Page.close();
    const Montage = await Browser.newPage();
    await Montage.setViewport({ width: 3 * 652 + 12, height: 360 + 46 });
    await Montage.setContent(`<html><body style="margin:0;background:#0f1114;color:#e7e9ec;font:15px system-ui,sans-serif">
        <div style="display:flex;gap:12px;padding:6px">${Shots.map(Shot => `<figure style="margin:0"><figcaption style="height:28px">${Shot.Label}</figcaption>
        <img src="${Shot.Png}" style="width:640px;height:360px;image-rendering:pixelated;display:block"></figure>`).join('')}</div></body></html>`);
    await Montage.evaluate(async () => { await document.fonts.ready; await Promise.all([...document.images].map(Image => Image.decode())); });
    await Montage.screenshot({ path: path.join(ImagesFolder, 'ManyLightsComparison.png') });
    await Montage.close();
    console.log('[manylights] Images/ManyLightsComparison.png');
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

const Phase = process.argv[2] || 'all';
const Server = await StartPrototypeServer();
const Browser = await LaunchBrowser();
try
{
    const Started = Date.now();
    if (Phase === 'quadrature'  || Phase === 'all') await ValidateQuadrature(Browser, Server);
    if (Phase === 'analytic'    || Phase === 'all') await ValidateAnalyticSoftbox(Browser, Server);
    if (Phase === 'determinism' || Phase === 'all') await ValidateDeterminism(Browser, Server);
    if (Phase === 'convergence' || Phase === 'all') await ValidateConvergence(Browser, Server);
    if (Phase === 'chart'       || Phase === 'all') await RetainChart(Browser);
    if (Phase === 'captures'    || Phase === 'all') await RetainCaptures(Browser, Server);
    if (Phase === 'interface'   || Phase === 'all') await RetainInterfaceCapture(Browser, Server);
    if (Phase === 'realtime'    || Phase === 'all') await ValidateRealtime(Browser, Server);
    if (Phase === 'patterns'    || Phase === 'all') await ValidatePatterns(Browser, Server);
    if (Phase === 'fleet'       || Phase === 'all') await ValidateFleet(Browser, Server);
    if (Phase === 'reconstruction' || Phase === 'all') await ValidateReconstruction(Browser, Server);
    if (Phase === 'tracescale'  || Phase === 'all') await ValidateTraceScale(Browser, Server);
    if (Phase === 'manylights'  || Phase === 'all') await ValidateManyLights(Browser, Server);
    if (Phase === 'manylightimages')                await RetainManyLightImages(Browser, Server);
    if (Phase === 'reconstructionimages')           await RetainReconstructionImages(Browser, Server, null, 32);
    if (Phase === 'patternimages')                  await RetainPatternImages(Browser, Server);
    if (Phase === 'realtimeimages')                 await RetainRealtimeImages(Browser, Server, null, 32);
    console.log(`[${Phase}] finished in ${((Date.now() - Started) / 1000).toFixed(1)} s`);
}
finally
{
    await Browser.close();
    Server.close();
}
