#!/usr/bin/env node
//============================================================================================================================================
//                                                   GENERATEFLUIDREFERENCEORACLES.MJS
//     Slice S01: Reference-Generated Golden Oracle Exporter directly executing authoritative SultanAladin/Frontier-1 FluidEditor JS modules
//============================================================================================================================================

import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const ReferenceRoot = process.argv[2] || '/tmp/frontier1-ref/Experimental/FluidEditor';
const OutputDir = process.argv[3] || path.resolve('Exhibits/Oracles/FluidReference');
fs.mkdirSync(OutputDir, { recursive: true });

const loadModule = (rel) => import(pathToFileURL(path.join(ReferenceRoot, rel)).href);

const ForceCatalog = await loadModule('Engine/ForceGeneratorCatalog.js');
const KeyTracks = await loadModule('Engine/KeyTracks.js');
const LayerStack = await loadModule('Engine/LayerStack.js');
const PresetScenes = await loadModule('Engine/PresetScenes.js');
const ParticleCatalog = await loadModule('Engine/ParticleCatalog.js');
const ObjectMeshCatalog = await loadModule('Engine/ObjectMeshCatalog.js');
const FireworkPatterns = await loadModule('Engine/FireworkPatterns.js');

const round6 = (v) => (Number.isFinite(v) ? Math.round(v * 1e6) / 1e6 : null);
const roundVec = (arr) => arr.map(round6);

function writeJson(fileName, data)
{
    const fullPath = path.join(OutputDir, fileName);
    fs.writeFileSync(fullPath, JSON.stringify(data, null, 2) + '\n', 'utf8');
    return fullPath;
}

//------------------------------------------------------------------------------------------------------------------------
//  1. ForceGeneratorCatalog Oracle (20 Force Models + 7 Falloffs + Emitter Shapes + Boundary Modes)
//------------------------------------------------------------------------------------------------------------------------
const forceCatalogOracle = {
    SourceReference: 'Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js L1-L458',
    FalloffCurves: ForceCatalog.FalloffCurves,
    EmitterShapes: ForceCatalog.EmitterShapes,
    BoundaryModes: ForceCatalog.BoundaryModes,
    MaximumForceGenerators: ForceCatalog.MaximumForceGenerators,
    ForceGeneratorTypes: ForceCatalog.ForceGeneratorTypes,
};
writeJson('01_ForceGeneratorCatalogOracle.json', forceCatalogOracle);

//------------------------------------------------------------------------------------------------------------------------
//  2. KeyTracks Oracle (Constant / Linear / Smooth Fritsch-Carlson / Bezier / Hold / Cycle / PingPong / Vector3 / Colour)
//------------------------------------------------------------------------------------------------------------------------
const sampleTimes = [-0.5, 0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 3.75, 4.5, 5.25];
const testTracks = [
    {
        Name: 'ScalarConstantHold',
        Track: { Path: 'Strength', Extrapolation: 0, Period: 0, Keys: [
            { Time: 0.0, Amount: 1.0, Ease: 0, In: 0, Out: 0 },
            { Time: 1.0, Amount: 4.0, Ease: 0, In: 0, Out: 0 },
            { Time: 2.0, Amount: -2.0, Ease: 0, In: 0, Out: 0 },
        ] },
        Stepped: false,
    },
    {
        Name: 'ScalarLinearCycle',
        Track: { Path: 'Strength', Extrapolation: 1, Period: 2.0, Keys: [
            { Time: 0.0, Amount: 0.0, Ease: 1, In: 0, Out: 0 },
            { Time: 1.0, Amount: 10.0, Ease: 1, In: 0, Out: 0 },
            { Time: 2.0, Amount: 2.0, Ease: 1, In: 0, Out: 0 },
        ] },
        Stepped: false,
    },
    {
        Name: 'ScalarSmoothMonotonePingPong',
        Track: { Path: 'Buoyancy', Extrapolation: 2, Period: 2.0, Keys: [
            { Time: 0.0, Amount: 0.0, Ease: 2, In: 0, Out: 0 },
            { Time: 0.6, Amount: 3.5, Ease: 2, In: 0, Out: 0 },
            { Time: 1.4, Amount: 1.2, Ease: 2, In: 0, Out: 0 },
            { Time: 2.0, Amount: 5.0, Ease: 2, In: 0, Out: 0 },
        ] },
        Stepped: false,
    },
    {
        Name: 'ScalarBezierHold',
        Track: { Path: 'Radius', Extrapolation: 0, Period: 0, Keys: [
            { Time: 0.0, Amount: 1.0, Ease: 3, In: 0.0, Out: 4.5 },
            { Time: 1.5, Amount: 2.5, Ease: 3, In: -2.0, Out: 1.0 },
            { Time: 3.0, Amount: 0.5, Ease: 3, In: 0.0, Out: 0.0 },
        ] },
        Stepped: false,
    },
    {
        Name: 'Vector3SmoothHold',
        Track: { Path: 'Position', Extrapolation: 0, Period: 0, Keys: [
            { Time: 0.0, Amount: [0.0, 0.5, -1.0], Ease: 2, In: 0, Out: 0 },
            { Time: 1.0, Amount: [2.0, 1.5, 0.5], Ease: 2, In: 0, Out: 0 },
            { Time: 2.5, Amount: [-1.0, 3.0, 2.0], Ease: 2, In: 0, Out: 0 },
        ] },
        Stepped: false,
    },
    {
        Name: 'ColourLinearSmoothHold',
        Track: { Path: 'FireFlame', Extrapolation: 0, Period: 0, Keys: [
            { Time: 0.0, Amount: '#ff3300', Ease: 2, In: 0, Out: 0 },
            { Time: 1.0, Amount: '#ffe066', Ease: 1, In: 0, Out: 0 },
            { Time: 2.0, Amount: '#2266ff', Ease: 2, In: 0, Out: 0 },
        ] },
        Stepped: false,
    },
    {
        Name: 'SteppedChoiceHold',
        Track: { Path: 'Mode', Extrapolation: 0, Period: 0, Keys: [
            { Time: 0.0, Amount: 'Emit', Ease: 2, In: 0, Out: 0 },
            { Time: 1.25, Amount: 'Cloner', Ease: 2, In: 0, Out: 0 },
        ] },
        Stepped: true,
    },
];

const timingNodes = [
    { Name: 'ImmediateInfinite', StartTime: 0, Duration: 0, Fade: 0.15, Repeat: 0 },
    { Name: 'WindowedFade', StartTime: 0.5, Duration: 1.5, Fade: 0.25, Repeat: 0 },
    { Name: 'RepeatingPulse', StartTime: 0.2, Duration: 0.8, Fade: 0.1, Repeat: 1.5 },
];

const keyTracksOracle = {
    SourceReference: 'Experimental/FluidEditor/Engine/KeyTracks.js L1-L360',
    KeyTimeEpsilon: KeyTracks.KeyTimeEpsilon,
    SampleTimes: sampleTimes,
    TrackSamples: testTracks.map((entry) => ({
        Name: entry.Name,
        Track: entry.Track,
        Stepped: entry.Stepped,
        Values: sampleTimes.map((t) => {
            const v = KeyTracks.sampleTrack(entry.Track, t, entry.Stepped);
            if (typeof v === 'number') return round6(v);
            if (Array.isArray(v)) return roundVec(v);
            return v;
        }),
    })),
    TimingEnvelopeWeights: timingNodes.map((node) => ({
        Node: node,
        Weights: sampleTimes.map((t) => round6(KeyTracks.nodeWeight(node, t))),
    })),
};
writeJson('02_KeyTracksOracle.json', keyTracksOracle);

//------------------------------------------------------------------------------------------------------------------------
//  3. LayerStack Oracle (All 54 Presets Gas Stacks + All 71 Particle Template Stacks)
//------------------------------------------------------------------------------------------------------------------------
const gasPresetEvals = PresetScenes.ScenePresets.map((preset) => {
    const scene = PresetScenes.buildSceneFromDocument(preset.Document);
    const ev = LayerStack.evaluateGasStack(scene.GasStack, scene.Emitters, 0);
    return {
        Id: preset.Id,
        Name: preset.Name,
        Stopped: ev.Stopped,
        Problems: ev.Problems,
        Domain: ev.Domain,
        Volume: ev.Volume,
        ForceCount: (scene.Forces || []).length,
        EmitterCount: (scene.Emitters || []).length,
        ParticleCount: (scene.Particles || []).length,
        ColliderCount: (scene.Colliders || []).length,
    };
});

const particleTemplateEvals = ParticleCatalog.ParticleTemplates.map((tmpl) => {
    const ev = LayerStack.evaluateParticleStack(tmpl.Stack);
    return {
        Id: tmpl.Id,
        Name: tmpl.Name,
        Category: tmpl.Category,
        Stopped: ev.Stopped,
        Problems: ev.Problems,
        Particles: ev.Particles,
    };
});

const layerStackOracle = {
    SourceReference: 'Experimental/FluidEditor/Engine/LayerSpecifications.toml L1-L2274 + LayerStack.js L1-L441',
    SpecificationHash: LayerStack.SpecificationHash,
    MaximumLayers: LayerStack.MaximumLayers,
    LayerCount: LayerStack.LayerSpecifications.length,
    GasLayerCodes: LayerStack.GasLayerSpecifications.map((l) => l.Code),
    ParticleLayerCodes: LayerStack.ParticleLayerSpecifications.map((l) => l.Code),
    GasPresetCount: gasPresetEvals.length,
    ParticleTemplateCount: particleTemplateEvals.length,
    GasPresets: gasPresetEvals,
    ParticleTemplates: particleTemplateEvals,
};
writeJson('03_LayerStackOracle.json', layerStackOracle);

//------------------------------------------------------------------------------------------------------------------------
//  4. ObjectMeshCatalog Oracle (All 19 Built-In 3D Solids × 3 Sample Modes)
//------------------------------------------------------------------------------------------------------------------------
const objectMeshOracle = {
    SourceReference: 'Experimental/FluidEditor/Engine/ObjectMeshCatalog.js L1-L1167',
    BuiltInObjectNames: ObjectMeshCatalog.BuiltInObjectNames,
    ObjectSampleModes: ObjectMeshCatalog.ObjectSampleModes,
    Solids: ObjectMeshCatalog.BuiltInObjectNames.map((name) => {
        const mesh = ObjectMeshCatalog.ResolveObjectMesh(name);
        const modes = {};
        for (const mode of ObjectMeshCatalog.ObjectSampleModes)
        {
            const sampled = ObjectMeshCatalog.SampleObjectAnchors(mesh, 64, mode, 15);
            const first8Pos = [];
            const first8Norm = [];
            const first8Ridge = [];
            for (let i = 0; i < 8; i++)
            {
                first8Pos.push(roundVec([sampled.Positions[i * 3], sampled.Positions[i * 3 + 1], sampled.Positions[i * 3 + 2]]));
                first8Norm.push(roundVec([sampled.Normals[i * 3], sampled.Normals[i * 3 + 1], sampled.Normals[i * 3 + 2]]));
                first8Ridge.push(round6(sampled.Ridges[i]));
            }
            modes[mode] = {
                Count: sampled.Count,
                First8Positions: first8Pos,
                First8Normals: first8Norm,
                First8Ridges: first8Ridge,
            };
        }
        return {
            Name: name,
            TriangleCount: mesh.Triangles,
            VertexCount: mesh.VertexCount,
            TotalArea: round6(mesh.TotalArea),
            Radius: round6(mesh.Radius),
            BoundsMinimum: roundVec(mesh.Bounds.Minimum),
            BoundsMaximum: roundVec(mesh.Bounds.Maximum),
            SampleModes: modes,
        };
    }),
};
writeJson('04_ObjectMeshOracle.json', objectMeshOracle);

//------------------------------------------------------------------------------------------------------------------------
//  5. FireworkPatterns Oracle (All 10 Patterns including SkyText "2026")
//------------------------------------------------------------------------------------------------------------------------
const fireworkOracle = {
    SourceReference: 'Experimental/FluidEditor/Engine/FireworkPatterns.js L1-L623',
    FireworkPatternNames: FireworkPatterns.FireworkPatternNames,
    Patterns: FireworkPatterns.FireworkPatternNames.map((patternName) => {
        const sampled = FireworkPatterns.SampleFireworkAnchors(patternName, '2026', 64, 0);
        const first12Pos = [];
        const first12Norm = [];
        const first12Ridge = [];
        for (let i = 0; i < 12; i++)
        {
            first12Pos.push(roundVec([sampled.Positions[i * 3], sampled.Positions[i * 3 + 1], sampled.Positions[i * 3 + 2]]));
            first12Norm.push(roundVec([sampled.Normals[i * 3], sampled.Normals[i * 3 + 1], sampled.Normals[i * 3 + 2]]));
            first12Ridge.push(round6(sampled.Ridges[i]));
        }
        return {
            Pattern: patternName,
            ShellName: sampled.ShellName,
            SampleCount: sampled.Count,
            First12Positions: first12Pos,
            First12Normals: first12Norm,
            First12Ridges: first12Ridge,
        };
    }),
};
writeJson('05_FireworkPatternsOracle.json', fireworkOracle);

//------------------------------------------------------------------------------------------------------------------------
//  6. ForceFieldChunk Oracle (Exact float32 GLSL Semantics of FluidShaders.js L234-L493: 20 Models × 7 Falloffs × 16 Probes)
//------------------------------------------------------------------------------------------------------------------------
const f32 = Math.fround;
const fract = (x) => x - Math.floor(x);
const clamp = (x, lo, hi) => Math.min(Math.max(x, lo), hi);
const mix = (a, b, t) => a + (b - a) * t;
const smoothstep = (e0, e1, x) => { const t = clamp((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); };
const dot3 = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const len3 = (a) => Math.sqrt(dot3(a, a));
const cross3 = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const add3 = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const mul3 = (a, s) => [a[0] * s, a[1] * s, a[2] * s];

// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L234-L293 (NoiseChunk — IEEE-754 float32 matching GLSL highp float)
function Hash33(p)
{
    const p0 = f32(p[0]), p1 = f32(p[1]), p2 = f32(p[2]);
    const px = f32(f32(f32(p0 * f32(127.1)) + f32(p1 * f32(311.7))) + f32(p2 * f32(74.7)));
    const py = f32(f32(f32(p0 * f32(269.5)) + f32(p1 * f32(183.3))) + f32(p2 * f32(246.1)));
    const pz = f32(f32(f32(p0 * f32(113.5)) + f32(p1 * f32(271.9))) + f32(p2 * f32(124.6)));
    const k = f32(43758.5453123);
    const h = (q) => f32(f32(fract(f32(f32(Math.sin(q)) * k))) * 2.0 - 1.0);
    return [h(px), h(py), h(pz)];
}

function GradientNoise(p)
{
    const p0 = f32(p[0]), p1 = f32(p[1]), p2 = f32(p[2]);
    const i = [Math.floor(p0), Math.floor(p1), Math.floor(p2)];
    const f = [f32(p0 - i[0]), f32(p1 - i[1]), f32(p2 - i[2])];
    const u = f.map((v) => f32(f32(f32(v * v) * v) * f32(f32(v * f32(f32(v * 6.0) - 15.0)) + 10.0)));
    const corner = (dx, dy, dz) => {
        const h = Hash33([f32(i[0] + dx), f32(i[1] + dy), f32(i[2] + dz)]);
        return f32(f32(f32(h[0] * f32(f[0] - dx)) + f32(h[1] * f32(f[1] - dy))) + f32(h[2] * f32(f[2] - dz)));
    };
    const x00 = f32(mix(corner(0, 0, 0), corner(1, 0, 0), u[0]));
    const x10 = f32(mix(corner(0, 1, 0), corner(1, 1, 0), u[0]));
    const x01 = f32(mix(corner(0, 0, 1), corner(1, 0, 1), u[0]));
    const x11 = f32(mix(corner(0, 1, 1), corner(1, 1, 1), u[0]));
    return f32(mix(f32(mix(x00, x10, u[1])), f32(mix(x01, x11, u[1])), u[2]));
}

function ValueNoise(p)
{
    // GLSL mat3(c0x, c0y, c0z, c1x, c1y, c1z, c2x, c2y, c2z) is column-major!
    const rot = (v) => [
        f32(f32(f32(0.00 * v[0]) + f32(f32(-0.80) * v[1])) + f32(f32(-0.60) * v[2])),
        f32(f32(f32(f32(0.80) * v[0]) + f32(f32(0.36) * v[1])) + f32(f32(-0.48) * v[2])),
        f32(f32(f32(f32(0.60) * v[0]) + f32(f32(-0.48) * v[1])) + f32(f32(0.64) * v[2])),
    ];
    let q = [f32(p[0]), f32(p[1]), f32(p[2])];
    let sum = GradientNoise(q);
    q = mul3(rot(q), f32(2.02)).map(f32);
    sum = f32(sum + f32(0.5 * GradientNoise(q)));
    q = mul3(rot(q), f32(2.03)).map(f32);
    sum = f32(sum + f32(0.25 * GradientNoise(q)));
    return clamp(f32(f32(f32(sum / f32(1.75)) * f32(0.75)) + 0.5), 0.0, 1.0);
}

function PotentialNoise(p)
{
    return [
        GradientNoise(p),
        GradientNoise([f32(p[0] + f32(31.7)), f32(p[1] + f32(17.3)), f32(p[2] + f32(9.1))]),
        GradientNoise([f32(p[0] - f32(12.9)), f32(p[1] + f32(44.2)), f32(p[2] + f32(23.6))]),
    ];
}

function CurlNoise(p)
{
    const e = f32(0.03);
    const p0 = f32(p[0]), p1 = f32(p[1]), p2 = f32(p[2]);
    const dx = sub3(PotentialNoise([f32(p0 + e), p1, p2]), PotentialNoise([f32(p0 - e), p1, p2])).map(f32);
    const dy = sub3(PotentialNoise([p0, f32(p1 + e), p2]), PotentialNoise([p0, f32(p1 - e), p2])).map(f32);
    const dz = sub3(PotentialNoise([p0, p1, f32(p2 + e)]), PotentialNoise([p0, p1, f32(p2 - e)])).map(f32);
    return mul3([f32(dy[2] - dz[1]), f32(dz[0] - dx[2]), f32(dx[1] - dy[0])], f32(1.0 / f32(2.0 * e))).map(f32);
}

// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L294-L493 (ForceFieldChunk)
function Falloff(mode, d)
{
    if (mode === 3) return 1.0;
    if (d >= 1.0) return 0.0;
    if (mode === 0) return 1.0 - smoothstep(0.0, 1.0, d);
    if (mode === 1) return 1.0 - d;
    if (mode === 4) return Math.exp(-d * d * 5.0) * (1.0 - smoothstep(0.9, 1.0, d));
    if (mode === 5) return 1.0 - smoothstep(0.55, 1.0, d);
    if (mode === 6) return 0.5 + 0.5 * Math.cos(d * 3.14159265);
    return (1.0 / (1.0 + 16.0 * d * d)) * (1.0 - smoothstep(0.85, 1.0, d));
}

function PulseEnvelope(uTime, period, width, delay)
{
    const local = uTime - delay;
    if (local < 0.0) return 0.0;
    const phase = fract(local / Math.max(period, 0.05));
    return Math.exp(-phase / Math.max(width, 0.01)) * (1.0 - smoothstep(0.8, 1.0, phase));
}

function SafeNormalise(v)
{
    const l = len3(v);
    return l > 1e-5 ? mul3(v, 1.0 / l) : [0.0, 1.0, 0.0];
}

function EvaluateForceGenerator(gen, position, velocity, uTime)
{
    const type = Math.round(gen.C[0]);
    const falloffMode = Math.round(gen.C[1]);
    const frequency = gen.C[2];
    const secondary = gen.C[3];
    const tertiary = gen.D[0];
    const centre = [gen.A[0], gen.A[1], gen.A[2]];
    const radius = Math.max(gen.A[3], 1e-3);
    const axis = SafeNormalise([gen.B[0], gen.B[1], gen.B[2]]);
    const strength = gen.B[3];
    const r = sub3(position, centre);
    const distance = len3(r);
    const radial = sub3(r, mul3(axis, dot3(r, axis)));
    const radialDistance = len3(radial);

    if (type === 0)
    {
        const w = Falloff(falloffMode, distance / radius);
        const gust = ValueNoise([position[0] * 0.6 + uTime * frequency, position[1] * 0.6, position[2] * 0.6 + uTime * frequency * 0.7]) * 2.0 - 1.0;
        return mul3(axis, strength * (1.0 + secondary * gust) * w);
    }
    if (type === 1 || type === 2)
    {
        const w = Falloff(falloffMode, distance / radius);
        const core = distance / (distance + Math.max(secondary, 0.02) * radius + 1e-4);
        const dir = distance > 1e-4 ? mul3(r, 1.0 / distance) : [0.0, 0.0, 0.0];
        return mul3(dir, strength * w * core * (type === 1 ? -1.0 : 1.0));
    }
    if (type === 3)
    {
        const w = Falloff(falloffMode, radialDistance / radius);
        const radialDir = radialDistance > 1e-4 ? mul3(radial, 1.0 / radialDistance) : [0.0, 0.0, 0.0];
        const tangent = cross3(axis, radialDir);
        const core = radialDistance / (radialDistance + 0.12 * radius);
        return mul3(add3(sub3(mul3(tangent, strength * core), mul3(radialDir, strength * 0.3 * frequency)), mul3(axis, secondary)), w);
    }
    if (type === 4)
    {
        const w = Falloff(falloffMode, distance / radius);
        return mul3(CurlNoise([position[0] * frequency, position[1] * frequency + uTime * secondary, position[2] * frequency + uTime * secondary * 0.31]), strength * w * 0.35);
    }
    if (type === 5)
    {
        const w = Falloff(falloffMode, distance / radius);
        return mul3(velocity, -strength * w);
    }
    if (type === 6 || type === 13)
    {
        const pulse = PulseEnvelope(uTime, frequency, secondary, tertiary);
        const w = Falloff(falloffMode, distance / radius);
        const dir = distance > 1e-4 ? mul3(r, 1.0 / distance) : [0.0, 0.0, 0.0];
        return mul3(dir, strength * pulse * w * (type === 13 ? -1.0 : 1.0));
    }
    if (type === 14)
    {
        const pulse = PulseEnvelope(uTime, frequency, secondary, tertiary);
        const w = Falloff(falloffMode, distance / radius);
        return mul3(axis, strength * pulse * w);
    }
    if (type === 12)
    {
        const local = uTime - tertiary;
        if (local < 0.0) return [0.0, 0.0, 0.0];
        const phase = fract(local / Math.max(frequency, 0.05));
        const shellRadius = phase * radius;
        const thickness = Math.max(secondary, 0.02) * radius;
        const shell = Math.exp(-Math.pow((distance - shellRadius) / thickness, 2.0));
        const dir = distance > 1e-4 ? mul3(r, 1.0 / distance) : [0.0, 0.0, 0.0];
        return mul3(dir, strength * shell * (1.0 - phase * 0.7));
    }
    if (type === 10)
    {
        const w = Falloff(falloffMode, distance / radius);
        const radialDir = radialDistance > 1e-4 ? mul3(radial, 1.0 / radialDistance) : [0.0, 0.0, 0.0];
        const tangent = cross3(axis, radialDir);
        const toCentre = distance > 1e-4 ? mul3(r, -1.0 / distance) : [0.0, 0.0, 0.0];
        const core = radialDistance / (radialDistance + 0.1 * radius);
        return mul3(add3(mul3(tangent, core), mul3(toCentre, secondary)), strength * w);
    }
    if (type === 11)
    {
        const overshoot = distance - radius * (1.0 - Math.max(secondary, 0.02));
        if (overshoot <= 0.0) return [0.0, 0.0, 0.0];
        const wall = clamp(overshoot / (Math.max(secondary, 0.02) * radius), 0.0, 1.0);
        const dir = distance > 1e-4 ? mul3(r, -1.0 / distance) : [0.0, 0.0, 0.0];
        return mul3(dir, strength * wall * wall);
    }
    if (type === 15)
    {
        const w = Falloff(falloffMode, distance / radius);
        const stepT = Math.floor(uTime * Math.max(secondary, 0.1)) * 17.0;
        const kick = Hash33([Math.floor(position[0] * frequency) + stepT, Math.floor(position[1] * frequency) + stepT, Math.floor(position[2] * frequency) + stepT]);
        return mul3(kick, strength * w);
    }
    if (type === 16)
    {
        const w = Falloff(falloffMode, distance / radius);
        return mul3(sub3(mul3(axis, frequency), velocity), strength * w);
    }
    if (type === 17)
    {
        const w = Falloff(falloffMode, distance / radius);
        const height = clamp(dot3(r, axis) / radius, -1.0, 1.0);
        const upRef = Math.abs(axis[1]) < 0.9 ? [0.0, 1.0, 0.0] : [1.0, 0.0, 0.0];
        const u = SafeNormalise(cross3(axis, upRef));
        const v = cross3(axis, u);
        const angle = height * secondary * 3.14159265;
        return mul3(add3(mul3(u, Math.cos(angle)), mul3(v, Math.sin(angle))), strength * w);
    }
    if (type === 18)
    {
        const w = Falloff(falloffMode, radialDistance / radius);
        return mul3(cross3(axis, radial), (strength * w) / radius);
    }
    if (type === 19)
    {
        const w = Falloff(falloffMode, distance / radius);
        const core = Math.max(secondary, 0.05) * radius;
        const d = Math.max(distance, core);
        const n = mul3(r, 1.0 / Math.max(distance, 1e-4));
        const field = sub3(mul3(n, 3.0 * dot3(axis, n)), axis);
        const scale = (core * core * core) / (d * d * d);
        return mul3(SafeNormalise(field), strength * w * mix(scale, 1.0, 0.35));
    }
    if (type === 7)
    {
        const w = Falloff(falloffMode, radialDistance / radius);
        return mul3(axis, strength * w);
    }
    if (type === 8)
    {
        const w = Falloff(falloffMode, radialDistance / radius);
        const radialDir = radialDistance > 1e-4 ? mul3(radial, 1.0 / radialDistance) : [0.0, 0.0, 0.0];
        return mul3(radialDir, -strength * w);
    }
    if (type === 9)
    {
        const tube = Math.max(frequency, 0.05);
        const radialDir = radialDistance > 1e-4 ? mul3(radial, 1.0 / radialDistance) : [1.0, 0.0, 0.0];
        const ringPoint = add3(centre, mul3(radialDir, radius));
        const d = sub3(position, ringPoint);
        const coreDistance = len3(d);
        const w = Falloff(falloffMode, coreDistance / tube);
        const ringTangent = cross3(axis, radialDir);
        const circulation = cross3(ringTangent, mul3(d, 1.0 / Math.max(coreDistance, 1e-4)));
        return mul3(circulation, strength * w);
    }
    return [0.0, 0.0, 0.0];
}

// 16 deterministic probe points spanning core, mid-radius, rim, beyond-radius, axial, off-axis, and moving velocities
const probes = [];
for (let i = 0; i < 16; i++)
{
    const a = i * 0.73;
    const rad = 0.15 + (i / 15) * 3.1;
    probes.push({
        Position: roundVec([0.4 + Math.cos(a) * rad, -0.2 + ((i % 5) - 2) * 0.55, 0.1 + Math.sin(a) * rad]),
        Velocity: roundVec([Math.sin(a * 1.3) * 2.0, 0.5 - (i % 3) * 0.7, Math.cos(a * 0.9) * 1.5]),
        Time: round6(0.35 + i * 0.17),
    });
}

const forceVectors = [];
for (const typeSpec of ForceCatalog.ForceGeneratorTypes)
{
    for (let falloffIdx = 0; falloffIdx < ForceCatalog.FalloffCurves.length; falloffIdx++)
    {
        const gen = {
            A: [0.4, -0.2, 0.1, 2.5],
            B: [0.25, 0.92, -0.28, 4.2],
            C: [typeSpec.Code, falloffIdx, typeSpec.Defaults.Frequency ?? 1.4, typeSpec.Defaults.Secondary ?? 0.35],
            D: [typeSpec.Defaults.Tertiary ?? 0.1, 0.0, 0.0, 0.0],
        };
        const samples = probes.map((p) => roundVec(EvaluateForceGenerator(gen, p.Position, p.Velocity, p.Time)));
        forceVectors.push({
            Code: typeSpec.Code,
            Id: typeSpec.Id,
            FalloffCode: falloffIdx,
            FalloffName: ForceCatalog.FalloffCurves[falloffIdx],
            Generator: gen,
            Vectors: samples,
        });
    }
}

writeJson('06_ForceFieldChunkOracle.json', {
    SourceReference: 'Experimental/FluidEditor/Engine/FluidShaders.js L234-L493',
    ModelCount: ForceCatalog.ForceGeneratorTypes.length,
    FalloffCount: ForceCatalog.FalloffCurves.length,
    ProbeCount: probes.length,
    TotalVectors: forceVectors.length * probes.length,
    Probes: probes,
    Cases: forceVectors,
});

console.log(`Generated 6 reference oracles in ${OutputDir}`);
console.log(`  01_ForceGeneratorCatalogOracle: ${forceCatalogOracle.ForceGeneratorTypes.length} models, ${forceCatalogOracle.FalloffCurves.length} falloffs`);
console.log(`  02_KeyTracksOracle: ${keyTracksOracle.TrackSamples.length} tracks x ${sampleTimes.length} times`);
console.log(`  03_LayerStackOracle: ${layerStackOracle.GasPresetCount} gas presets, ${layerStackOracle.ParticleTemplateCount} particle templates`);
console.log(`  04_ObjectMeshOracle: ${objectMeshOracle.Solids.length} built-in 3D solids x 3 sample modes`);
console.log(`  05_FireworkPatternsOracle: ${fireworkOracle.Patterns.length} firework patterns`);
console.log(`  06_ForceFieldChunkOracle: ${forceVectors.length * probes.length} force vectors`);
