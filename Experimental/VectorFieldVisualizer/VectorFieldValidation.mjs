/*============================================================================================================================================
                                                    VECTORFIELDVALIDATION.MJS
============================================================================================================================================*/
// Automated 11-Suite Verification of Frontier VectorFieldVisualizer (GPU + Project Zero Rebuild)

import assert from 'node:assert/strict';
import fs from 'node:fs';
import {
    Attenuate,
    CompassOf,
    ComputeMotionPathLength,
    ConstructForce,
    ConstructScenario,
    EncodeSimulationSpaceToml,
    EnsureSourceMotionWaypoints,
    EvaluateComposite,
    EvaluateMotionPosition,
    EvaluateSource3D,
    ForceModels,
    GetActiveSliceAxes,
    IntegrateStreamline3D,
    IntegrateStreamlineBidirectional,
    MAX_GPU_FORCES,
    PackForcesForGPU,
    ResolveBeaufort,
    ResolveCascadeBudget,
    ResolveDurationEnvelope,
    ResolveMotionPhase,
    ResolveSpatialShare,
    SampleDifferential,
    SampleMotionPathPoints,
    StepAnimatedSources
} from './Engine/VectorFieldCatalogue.js';
import {
    BuildSliceBakeFrag,
    GLSL_FIELD_COMMON,
    GPU_ARROWS_FRAG,
    GPU_ARROWS_VERT,
    SLICE_BAKE_FRAG,
    SLICE_DISPLAY_FRAG,
    STREAMLINE_FRAG,
    STREAMLINE_VERT,
    BOKEH_RIBBON_VERT,
    BOKEH_RIBBON_FRAG,
    BOKEH_BEAD_VERT,
    BOKEH_BEAD_FRAG
} from './Engine/VectorFieldShaders.js';
import {
    BOKEH_MAX_TRACERS,
    BOKEH_TRAIL_SEGS,
    BokehStreamRibbonSim,
    BuildStreamlineRibbonGeometry,
    CollectAdaptiveStreamlineSeedsRect,
    CollectMultiAxisStreamlineSeeds,
    ComputeFallbackPlaneFrame,
    ComputeFallbackSliceFrame,
    ProjectRayToAxisParam,
    ResolveBokehColors,
    ResolveStreamlinePointColor
} from './Engine/VectorFieldGPUViewport.js';
import { GetForceModelSvgIcon } from './Engine/ForceIcons.js';
import {
    CompileFieldProgram,
    DEFAULT_FORMULA_CODE,
    FORMULA_PRESETS
} from './Engine/VectorFieldLang.js';

let Passed = 0;
function Suite(Name, Fn)
{
    Fn();
    Passed++;
    console.log(`  [PASS] ${String(Passed).padStart(2, '0')} · ${Name}`);
}

console.log('Frontier VectorFieldVisualizer (Project Zero GPU) Validation\n');

Suite('Catalogue covers all 19 ForceModel laws with unique GPU IDs 0..18', () => {
    assert.equal(ForceModels.length, 19);
    const Ids = new Set(ForceModels.map((M) => M.GpuId));
    assert.equal(Ids.size, 19);
    for (let I = 0; I < 19; ++I) assert.ok(Ids.has(I));
});

Suite('Every ForceModel evaluates non-zero physics inside bounds & vanishes cleanly outside Radius', () => {
    const Out = [0, 0, 0];
    for (const M of ForceModels)
    {
        const Src = ConstructForce(M.Id, { Centre: [0, 0, 0], Radius: 160, Height: 160 });
        let Peak = 0;
        for (const [X, Y, Z] of [[42, 12, 18], [95, 20, -24], [0, 48, 0], [-65, 14, 30]])
        {
            Out[0] = Out[1] = Out[2] = 0;
            const Res = EvaluateSource3D(Src, X, Y, Z, 0.85, Out);
            Peak = Math.max(Peak, Res.Mag);
        }
        assert.ok(Peak > 0.01, `${M.Id} produced zero peak (${Peak})`);

        // Strictly zero outside finite boundary
        Out[0] = Out[1] = Out[2] = 0;
        const Far = EvaluateSource3D(Src, 500, 12, 500, 0.85, Out);
        assert.equal(Far.Mag, 0, `${M.Id} leaked outside finite bounds`);
    }
});

Suite('EForceDuration Impulse propagates shockwave shell & recharges cleanly', () => {
    const Burst = ConstructForce('RadialBurst', { WaveSpeed: 100, InitialRadius: 10, Lasts: 5.0, Repeats: 12, Radius: 240 });
    const T0 = ResolveDurationEnvelope(Burst, 0.5);
    assert.equal(T0.Status, 'Impulse Active');
    assert.ok(T0.Gate > 0.9);
    const Rest = ResolveDurationEnvelope(Burst, 7.5);
    assert.equal(Rest.Status, 'Recharging');
    assert.equal(Rest.Gate, 0);
});

Suite('True 3D CurlNoise is incompressible (|∇·F| < 1e-4)', () => {
    const Scene = {
        Sources: [ConstructForce('CurlNoise', { Centre: [0, 0, 0], Radius: 500, Attenuation: 'None', Strength: 16, EddyScale: 52 })],
        Viz: {}
    };
    const Diff = SampleDifferential(Scene, 24, 18, -14, 1.2, 0.05);
    assert.ok(Math.abs(Diff.Divergence) < 1e-4, `Expected |∇·F| < 1e-4, got ${Diff.Divergence}`);
    assert.ok(Diff.CurlMag > 0.05);
});

Suite('Containment Shell maintains quiet interior core & restoring boundary wall', () => {
    const Shell = ConstructForce('Containment', { Centre: [0, 0, 0], Radius: 200, InnerRatio: 0.58, Strength: 28 });
    const Out = [0, 0, 0];
    const Inner = EvaluateSource3D(Shell, 40, 10, 20, 1.0, Out);
    assert.equal(Inner.Mag, 0);
    Out[0] = Out[1] = Out[2] = 0;
    const Wall = EvaluateSource3D(Shell, 175, 0, 0, 1.0, Out);
    assert.ok(Wall.Mag > 10 && Out[0] < -8);
});

Suite('RK4 3D Streamline integration tracks smooth trajectories across composite scene', () => {
    const Scene = ConstructScenario('lab');
    const Pts = IntegrateStreamline3D(Scene, -60, 16, 40, 1.5, 24, 14);
    assert.ok(Pts.length >= 10);
    assert.ok(Pts.every((P) => Number.isFinite(P.X) && Number.isFinite(P.Y) && Number.isFinite(P.Z)));
});

Suite('PackForcesForGPU encodes up to 24 active sources cleanly with valid finite Float32 buffers', () => {
    const Scene = ConstructScenario('all19');
    const Pack = PackForcesForGPU(Scene);
    assert.equal(Pack.Count, 19);
    assert.equal(Pack.A.length, MAX_GPU_FORCES * 4);
    for (const Arr of [Pack.A, Pack.B, Pack.C, Pack.D, Pack.E])
    {
        for (let I = 0; I < Arr.length; ++I) assert.ok(Number.isFinite(Arr[I]));
    }
});

Suite('Beaufort scale, Wind compass & ForceAttenuation helpers match meteorological bounds', () => {
    assert.equal(ResolveBeaufort(0.2).Force, 0);
    assert.equal(ResolveBeaufort(15.0).Force, 7);
    assert.equal(ResolveBeaufort(34.0).Force, 12);
    assert.equal(CompassOf(225), 'SW');
    assert.equal(Attenuate('SmoothStep', 0, 100), 1);
    assert.equal(Attenuate('SmoothStep', 100, 100), 0);
});

Suite('Spatial Cascade Clipmap budget & SimulationSpace TOML encoder succeed', () => {
    const Scene = ConstructScenario('maritime');
    const Budget = ResolveCascadeBudget(Scene);
    assert.equal(Budget.Tiers.length, 4);
    assert.equal(Budget.TotalBytes, 16 * 1024 * 1024);
    const Toml = EncodeSimulationSpaceToml(Scene).join('\n');
    assert.ok(Toml.includes('[VectorFieldWorkspace]'));
    assert.ok(Toml.includes('[[ForceSources]]'));
});

Suite('GPU GLSL3 4-Stage Shaders contain all 19 force models, zero particles & clean WebGL2 qualifiers', () => {
    const ShaderSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/Engine/VectorFieldShaders.js', 'utf8');
    assert.ok(!ShaderSrc.includes('PARTICLE_'), 'All PARTICLE_* shaders must be removed');
    for (let Id = 0; Id < 19; ++Id)
    {
        assert.ok(GLSL_FIELD_COMMON.includes(`model == ${Id}`), `GLSL missing model == ${Id}`);
    }
    assert.ok(!GLSL_FIELD_COMMON.includes('uint '), 'GLSL should avoid uint bitwise ops for cross-GPU speed');
    assert.ok(SLICE_BAKE_FRAG.includes('out vec4 FragColor;'));
    assert.ok(SLICE_DISPLAY_FRAG.includes('uniform sampler2D uFieldBakeTex;'));
    assert.ok(GPU_ARROWS_VERT.includes('in vec3 aSeedMetres;'));
    assert.ok(GPU_ARROWS_FRAG.includes('out vec4 FragColor;'));
    assert.ok(STREAMLINE_VERT.includes('in float aProgress;'));
    assert.ok(STREAMLINE_FRAG.includes('out vec4 FragColor;'));
    for (const M of ForceModels)
    {
        const Svg = GetForceModelSvgIcon(M.Id, M.Colour);
        assert.ok(Svg.includes('<svg') && Svg.includes('</svg>'), `Missing SVG icon for ${M.Id}`);
    }
});

Suite('Project Zero UI HTML references valid DOM IDs, 4-Stage Shader HUD, Fallback & Zero Particles', () => {
    const Html = fs.readFileSync('Experimental/VectorFieldVisualizer/index.html', 'utf8');
    const Js = fs.readFileSync('Experimental/VectorFieldVisualizer/VectorFieldVisualizer.js', 'utf8');
    const GpuJs = fs.readFileSync('Experimental/VectorFieldVisualizer/Engine/VectorFieldGPUViewport.js', 'utf8');
    assert.ok(!/https?:\/\//.test(Html), 'Unexpected external URL in index.html');
    assert.ok(!Html.includes('data-layer="Motes"'), 'Particle layer toggle must be removed from HTML');
    assert.ok(!Js.includes("'Motes'"), 'Particle layer toggle must be removed from JS');
    assert.ok(!GpuJs.includes('BuildGpuParticles') && !GpuJs.includes('FallbackMotes'), 'Particle buffers/motes must be removed from viewport');
    assert.ok(GpuJs.includes('webglcontextlost') && GpuJs.includes('OnCompileProgress'));
    assert.ok(GpuJs.includes('ActivateFallbackRenderer') && GpuJs.includes('TryCreateWebGLRenderer'));
    assert.ok(!SLICE_DISPLAY_FRAG.includes('vfEvalComposite'), 'Display shader must sample uFieldBakeTex');
    assert.ok(!GPU_ARROWS_VERT.includes('vfEvalComposite'), 'Arrows shader must sample uFieldBakeTex');
    assert.ok(GPU_ARROWS_VERT.includes('vfDecodeBake'));
    const Calls = [...Js.matchAll(/El\('([^']+)'\)/g)].map((M) => M[1]);
    for (const Id of Calls)
    {
        if (
            Id.startsWith('Sel') || Id.startsWith('Rng') || Id.startsWith('Num') ||
            Id.startsWith('Vfl') || Id === 'BtnCopyToml' || Id === 'BtnOpenFormulaEditor' || Id === 'RowToggleFormulaActive'
        ) continue;
        assert.ok(Html.includes(`id="${Id}"`), `Missing DOM id="${Id}" in index.html`);
    }
});

Suite('Symbolic VectorFieldLang compiler, Zoom Full-Viewport Coverage & Bidirectional Streamlines', () => {
    // 1. Default formula: a = -S · A(r) · r̂ + ω_swirl · (ŷ × r̂)
    const Compiled = CompileFieldProgram(DEFAULT_FORMULA_CODE, 0);
    assert.equal(Compiled.Ok, true, `Default formula failed: ${Compiled.Diagnostics?.[0]?.Message}`);
    assert.equal(Compiled.OutputChannel, 'Accelerate');
    assert.ok(Compiled.GlslCode.includes('vfEvalCustomFormula0'));
    assert.ok(Compiled.GlslCode.includes('cross(vec3(0.0, 1.0, 0.0), rHat)'));
    assert.ok(Compiled.HighlightHtml.includes('vfl-tok-hatvec') && Compiled.HighlightHtml.includes('vfl-tok-param'));

    // 2. All 5 presets compile cleanly to both JS and GLSL ES 3.00
    for (const Preset of FORMULA_PRESETS)
    {
        const Res = CompileFieldProgram(Preset.Code, 0);
        assert.equal(Res.Ok, true, `Preset ${Preset.Id} failed: ${Res.Diagnostics?.[0]?.Message}`);
        assert.equal(typeof Res.JsFn, 'function');
    }

    // 3. Static type error caught cleanly with token span
    const Bad = CompileFieldProgram('a = ŷ × S', 0);
    assert.equal(Bad.Ok, false);
    assert.ok(Bad.Diagnostics.length > 0);
    assert.ok(Bad.HighlightHtml.includes('vfl-tok-err'));

    // 4. CPU + GPU custom formula evaluation & Bake shader injection parity
    const FormulaSrc = ConstructForce('Vortex', {
        Centre: [0, 0, 0],
        Radius: 200,
        Strength: 20,
        Swirl: 15,
        Attenuation: 'Linear',
        UseFormula: true,
        FormulaCode: 'a = -S · A(r) · r̂ + ω_swirl · (ŷ × r̂)'
    });
    const Out = [0, 0, 0];
    const Info = EvaluateSource3D(FormulaSrc, 100, 0, 0, 1.0, Out);
    assert.equal(Info.Active, true);
    assert.ok(Math.abs(Out[0] - (-10)) < 1e-4, `Expected radial pull -10 at x=100, got ${Out[0]}`);
    assert.ok(Math.abs(Out[2] - (-15)) < 1e-4, `Expected azimuthal ŷ×x̂ = -ẑ swirl -15, got ${Out[2]}`);

    const Pack = PackForcesForGPU({ Sources: [FormulaSrc], Viz: {} });
    assert.equal(Pack.CustomSlots.length, 1);
    const BakeSrc = BuildSliceBakeFrag(Pack.CustomSlots);
    assert.ok(BakeSrc.includes('vfEvalCustomFormula0') && BakeSrc.includes('model == 19'));

    // 5. Particles removed completely & bidirectional RK4 streamlines produce smooth curves
    const Lab = ConstructScenario('lab');
    assert.equal(Lab.Viz.Motes, undefined, 'Particles property must be removed from scenario state');
    const BiCurve = IntegrateStreamlineBidirectional(Lab, 60, 16, -20, 1.2, 16, 10);
    assert.ok(BiCurve.length >= 12, `Expected bidirectional streamline >= 12 pts, got ${BiCurve.length}`);

    // 6. Zoom edge-to-edge coverage: all 4 screen corners MUST fall strictly inside [MinX..MaxX, MinZ..MaxZ]
    for (const Zoom of [0.4, 1.0, 2.18, 3.75, 6.0])
    {
        for (const [Pw, Ph] of [[1280, 720], [480, 780], [640, 780]])
        {
            for (const IsIso of [false, true])
            {
                const F = ComputeFallbackSliceFrame(0, 0, Pw, Ph, IsIso, Zoom, [15, -10], 0.62, 0.64, 16);
                for (const [Sx, Sy] of [[0, 0], [Pw, 0], [0, Ph], [Pw, Ph]])
                {
                    const [Wx, Wz] = F.ScreenToWorldXZ(Sx, Sy);
                    assert.ok(Wx >= F.MinX && Wx <= F.MaxX, `Corner X=${Wx} clipped outside [${F.MinX},${F.MaxX}] at zoom=${Zoom} iso=${IsIso} ${Pw}x${Ph}`);
                    assert.ok(Wz >= F.MinZ && Wz <= F.MaxZ, `Corner Z=${Wz} clipped outside [${F.MinZ},${F.MaxZ}] at zoom=${Zoom} iso=${IsIso} ${Pw}x${Ph}`);
                }
            }
        }
    }

    // 7. Adaptive streamline seeds strictly follow selected slice planes vs full 3D volumetric (All 3 axes)
    const Frame375 = ComputeFallbackSliceFrame(0, 0, 480, 780, true, 3.75, [0, 0], 0.62, 0.64, 16);
    const SeedsVol3D = CollectAdaptiveStreamlineSeedsRect(Lab, Frame375.MinX, Frame375.MaxX, Frame375.MinZ, Frame375.MaxZ, 16, 22, {
        Is3D: true,
        ActiveAxes: ['XZ', 'XY', 'YZ']
    });
    const MaxSeedZ = Math.max(...SeedsVol3D.map((S) => S[2]));
    assert.ok(MaxSeedZ > Frame375.MaxZ * 0.65, `Streamline seeds stopped early at Z=${MaxSeedZ} vs MaxZ=${Frame375.MaxZ}`);
    const DistinctElevations3D = new Set(SeedsVol3D.map((S) => Math.round(S[1])));
    assert.ok(DistinctElevations3D.size >= 3, `Expected >= 3 vertical seed altitudes when all 3 axes enabled in 3D, got ${DistinctElevations3D.size}`);

    // Single-slice selection strictly constrains ALL 3D seeds onto that chosen plane
    const SliceBounds = {
        XZ: { MinU: -220, MaxU: 220, MinV: -220, MaxV: 220 },
        XY: { MinU: -220, MaxU: 220, MinV: -60, MaxV: 180 },
        YZ: { MinU: -220, MaxU: 220, MinV: -60, MaxV: 180 }
    };
    const SeedsXYOnly = CollectMultiAxisStreamlineSeeds(Lab, SliceBounds, 24, 34, { ActiveAxes: ['XY'], Is3D: true });
    assert.ok(SeedsXYOnly.length >= 20, `Expected dense XY slice seeds, got ${SeedsXYOnly.length}`);
    assert.ok(SeedsXYOnly.every((S) => Math.abs(S[2] - 24) < 1e-6), 'XY-only 3D selection must seed strictly on Z = SliceOffsetM');

    const SeedsYZOnly = CollectMultiAxisStreamlineSeeds(Lab, SliceBounds, 18, 34, { ActiveAxes: ['YZ'], Is3D: true });
    assert.ok(SeedsYZOnly.length >= 20, `Expected dense YZ slice seeds, got ${SeedsYZOnly.length}`);
    assert.ok(SeedsYZOnly.every((S) => Math.abs(S[0] - 18) < 1e-6), 'YZ-only 3D selection must seed strictly on X = SliceOffsetM');

    const SeedsXZOnly = CollectMultiAxisStreamlineSeeds(Lab, SliceBounds, 16, 34, { ActiveAxes: ['XZ'], Is3D: true });
    assert.ok(SeedsXZOnly.length >= 20, `Expected dense XZ slice seeds, got ${SeedsXZOnly.length}`);
    assert.ok(SeedsXZOnly.every((S) => Math.abs(S[1] - 16) < 1e-6), 'XZ-only 3D selection must seed strictly on Y = SliceOffsetM');

    // 8. Attractor & Repulsor default to radial (Swirl=0) with crisp radial inward/outward 3D streamlines
    const AttrScene = { Sources: [ConstructForce('Attractor', { Centre: [0, 24, 0], Strength: 24, Radius: 180 })], Viz: {} };
    assert.equal(AttrScene.Sources[0].Swirl, 0, 'Attractor should default to pure radial inward pull');
    const AttrCurve = IntegrateStreamlineBidirectional(AttrScene, 60, 56, 36, 1.0, 24, 10);
    assert.ok(AttrCurve.length >= 10, `Expected radial Attractor streamline >= 10 pts, got ${AttrCurve.length}`);
    const AttrStartDist = Math.hypot(AttrCurve[0].X, AttrCurve[0].Y - 24, AttrCurve[0].Z);
    const AttrEndDist = Math.hypot(AttrCurve[AttrCurve.length - 1].X, AttrCurve[AttrCurve.length - 1].Y - 24, AttrCurve[AttrCurve.length - 1].Z);
    assert.ok(AttrStartDist > 120 && AttrEndDist < 16, `Attractor streamline should converge from outer edge (~${AttrStartDist.toFixed(1)}m) into core (~${AttrEndDist.toFixed(1)}m)`);

    const RepScene = { Sources: [ConstructForce('Repulsor', { Centre: [0, 24, 0], Strength: 24, Radius: 180 })], Viz: {} };
    assert.equal(RepScene.Sources[0].Swirl, 0, 'Repulsor should default to pure radial outward push');
    const RepCurve = IntegrateStreamlineBidirectional(RepScene, 60, 56, 36, 1.0, 24, 10);
    assert.ok(RepCurve.length >= 10, `Expected radial Repulsor streamline >= 10 pts, got ${RepCurve.length}`);
    const RepStartDist = Math.hypot(RepCurve[0].X, RepCurve[0].Y - 24, RepCurve[0].Z);
    const RepEndDist = Math.hypot(RepCurve[RepCurve.length - 1].X, RepCurve[RepCurve.length - 1].Y - 24, RepCurve[RepCurve.length - 1].Z);
    assert.ok(RepStartDist < 16 && RepEndDist > 120, `Repulsor streamline should radiate outward from core (~${RepStartDist.toFixed(1)}m) to boundary (~${RepEndDist.toFixed(1)}m)`);

    // 9. Multi-plane SliceAxes helper & ComputeFallbackPlaneFrame on XZ / XY / YZ (including edge-on yaw)
    assert.deepEqual(GetActiveSliceAxes({ SliceAxis: 'XY', SliceAxes: { XZ: false, XY: true, YZ: false } }), ['XY']);
    assert.deepEqual(GetActiveSliceAxes({ SliceAxis: 'XZ', SliceAxes: { XZ: true, XY: true, YZ: true } }), ['XZ', 'XY', 'YZ']);
    assert.deepEqual(GetActiveSliceAxes({ SliceAxis: 'YZ' }), ['YZ']);

    for (const [Axis, Yaw] of [['XZ', 0.62], ['XY', 0.62], ['YZ', 0.62], ['XY', Math.PI * 0.5], ['YZ', 0]])
    {
        const PF = ComputeFallbackPlaneFrame(0, 0, 800, 600, true, Axis, 1.0, [0, 0, 48], Yaw, 0.64, 16);
        assert.ok(Number.isFinite(PF.MinU) && Number.isFinite(PF.MaxU) && PF.MaxU > PF.MinU);
        assert.ok(Number.isFinite(PF.MinV) && Number.isFinite(PF.MaxV) && PF.MaxV > PF.MinV);
        const [Sx, Sy] = PF.MapPlaneUV(30, 55);
        assert.ok(Number.isFinite(Sx) && Number.isFinite(Sy));
        if (Math.abs(Yaw - 0.62) < 0.01)
        {
            const [Ru, Rv] = PF.ScreenToPlaneUV(Sx, Sy);
            assert.ok(Math.abs(Ru - 30) < 1e-6 && Math.abs(Rv - 55) < 1e-6, `Plane ${Axis} inverse UV mismatch`);
        }
    }
    const BakeCheck = BuildSliceBakeFrag([]);
    assert.ok(!BakeCheck.includes('110.0'), 'Shader must map uBakeCenter.y directly without hardcoded 110m offset');
});

Suite('Optical Ribbon Trail Streamline Shaders & Geometry match screen-space AA Gaussian + comet streaks', () => {
    assert.ok(STREAMLINE_VERT.includes('in vec3 aTangent;'));
    assert.ok(STREAMLINE_VERT.includes('in float aStrandSeed;'));
    assert.ok(STREAMLINE_VERT.includes('uniform float uViewportHeightPx;'));
    assert.ok(STREAMLINE_VERT.includes('cross(tangent, viewDir)'));
    assert.ok(STREAMLINE_FRAG.includes('exp(-0.5 * (vAcrossPx * vAcrossPx)'));
    assert.ok(STREAMLINE_FRAG.includes('headSpark'));

    const Lab = ConstructScenario('lab');
    const Curve = IntegrateStreamlineBidirectional(Lab, 60, 16, -20, 1.2, 16, 10);
    const Geo = BuildStreamlineRibbonGeometry([Curve], 'Engineering', 34);
    for (const Attr of ['position', 'aTangent', 'aColor', 'aProgress', 'aEdge', 'aStrandSeed', 'aMagShare'])
    {
        assert.ok(Geo.getAttribute(Attr), `Missing ribbon trail attribute ${Attr}`);
    }
    const ProgArr = Geo.getAttribute('aProgress').array;
    assert.equal(ProgArr[0], 0);
    assert.ok(Math.abs(ProgArr[ProgArr.length - 1] - 1) < 1e-5, 'Arc-length progress must terminate at 1.0');
    assert.equal(Geo.drawRange.count, (Curve.length - 1) * 6);

    // Vivid saturated streamline vertex colors span multiple hues across slow->fast trajectories (no white washout)
    const SlowCurve = [
        { X: 0, Y: 16, Z: 0, Mag: 6 },
        { X: 10, Y: 16, Z: 0, Mag: 8 },
        { X: 20, Y: 16, Z: 0, Mag: 10 }
    ];
    const FastCurve = [
        { X: 0, Y: 16, Z: 0, Mag: 28 },
        { X: 10, Y: 16, Z: 0, Mag: 32 },
        { X: 20, Y: 16, Z: 0, Mag: 34 }
    ];
    const SlowCols = BuildStreamlineRibbonGeometry([SlowCurve], 'Engineering', 34).getAttribute('aColor').array;
    const FastCols = BuildStreamlineRibbonGeometry([FastCurve], 'Engineering', 34).getAttribute('aColor').array;
    const ColorDiff = Math.hypot(FastCols[0] - SlowCols[0], FastCols[1] - SlowCols[1], FastCols[2] - SlowCols[2]);
    assert.ok(ColorDiff > 0.45, `Streamline colors distinguish slow vs fast speed clearly (ΔRGB=${ColorDiff.toFixed(2)})`);
    assert.ok(Math.max(FastCols[0], FastCols[1], FastCols[2]) - Math.min(FastCols[0], FastCols[1], FastCols[2]) > 0.35,
        'Fast streamline vertices stay saturated rather than washing out to white');

    const EmptyGeo = BuildStreamlineRibbonGeometry([], 'Engineering', 34);
    assert.equal(EmptyGeo.drawRange.count, 0);
});

Suite('Wind & Vortex Lab volumetric 3D flow lines & semi-transparent shaded debug bounds (no wireframes)', () => {
    const Lab = ConstructScenario('lab');
    const BoundsByAxis = {
        XZ: { MinU: -400, MaxU: 400, MinV: -400, MaxV: 400 },
        XY: { MinU: -400, MaxU: 400, MinV: -100, MaxV: 250 },
        YZ: { MinU: -400, MaxU: 400, MinV: -100, MaxV: 250 }
    };

    // Even with single XZ heatmap slice active, Volumetric: true in 3D view seeds multi-elevation 3D streamlines
    const Seeds3D = CollectMultiAxisStreamlineSeeds(Lab, BoundsByAxis, Lab.Viz.SliceHeight, Lab.Viz.StreamlineSpacing, {
        ActiveAxes: ['XZ'],
        Is3D: true,
        Volumetric: true
    });
    const Elevations = new Set(Seeds3D.map((S) => Math.round(S[1])));
    assert.ok(Elevations.size >= 4, `Volumetric 3D flow lines seed >= 4 elevation layers in Wind & Vortex Lab (got ${Elevations.size})`);
    assert.ok(Math.max(...Seeds3D.map((S) => S[1])) > 120, 'Volumetric 3D seeds reach upper chamber altitudes (>120 m)');

    // Non-vortex streamlines (e.g., over Repulsor / Attractor / CurlNoise / LinearWind) curve substantially in Y
    let MaxDeltaYOutsideVortex = 0;
    for (const [Sx, Sy, Sz] of Seeds3D)
    {
        if (Math.hypot(Sx - 20, Sz + 40) < 120) continue; // skip Vortex column itself
        const Curve = IntegrateStreamlineBidirectional(Lab, Sx, Sy, Sz, 1.2, 20, 12);
        if (Curve.length < 4) continue;
        const Ys = Curve.map((P) => P.Y);
        MaxDeltaYOutsideVortex = Math.max(MaxDeltaYOutsideVortex, Math.max(...Ys) - Math.min(...Ys));
    }
    assert.ok(MaxDeltaYOutsideVortex > 14, `Flow lines outside Vortex weave cleanly in 3D (max ΔY=${MaxDeltaYOutsideVortex.toFixed(1)} m)`);

    // Verify semi-transparent volume bounds in VectorFieldGPUViewport.js (no wireframe bounds)
    const VpSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/Engine/VectorFieldGPUViewport.js', 'utf8');
    const RebuildFn = VpSrc.slice(VpSrc.indexOf('RebuildSourceBeacons()'), VpSrc.indexOf('RebuildStreamlinesIfDirty('));
    assert.ok(RebuildFn.includes('SphereGeometry') && RebuildFn.includes('CylinderGeometry') && RebuildFn.includes('BoxGeometry'),
        'RebuildSourceBeacons uses 3D SphereGeometry, CylinderGeometry & BoxGeometry surfaces');
    assert.ok(RebuildFn.includes('transparent: true') && RebuildFn.includes('depthWrite: false'),
        'RebuildSourceBeacons renders debug bounds as semi-transparent surfaces');
    assert.ok(!RebuildFn.includes('LineLoop') && !RebuildFn.includes('EdgesGeometry') && !RebuildFn.includes('LineSegments'),
        'RebuildSourceBeacons must not draw wireframe LineLoop/EdgesGeometry debug bounds');
    assert.ok(VpSrc.includes('Volumetric: Is3D') && VpSrc.includes('Volumetric: IsIso'),
        '3D GPU & Fallback viewports enable Volumetric 3D streamlines');
});

Suite('Interactive Source Circle Entities & 3D Transform Move Gizmo update field in real time', () => {
    // 1. Exact 3D ray-to-axis parameter projection across X, Y, Z axes (zero snap/inversion)
    const Tx = ProjectRayToAxisParam([5, 10, 0], [0, -1, 0], [0, 0, 0], [1, 0, 0]);
    assert.ok(Math.abs(Tx - 5) < 1e-6, `Expected X axis projection +5, got ${Tx}`);

    const Ty = ProjectRayToAxisParam([0, 14, 10], [0, 0, -1], [0, 2, 0], [0, 1, 0]);
    assert.ok(Math.abs(Ty - 12) < 1e-6, `Expected Y axis projection +12, got ${Ty}`);

    const DiagLen = Math.hypot(1, -1, 0);
    const Tz = ProjectRayToAxisParam([3, 3, -7], [1 / DiagLen, -1 / DiagLen, 0], [3, 0, 0], [0, 0, 1]);
    assert.ok(Math.abs(Tz - (-7)) < 1e-6, `Expected Z axis projection -7, got ${Tz}`);

    // Parallel ray to axis degrades gracefully without NaN
    const TPar = ProjectRayToAxisParam([0, 0, 0], [1, 0, 0], [0, 0, 0], [1, 0, 0]);
    assert.ok(Number.isFinite(TPar));

    // 2. Viewport & Visualizer implement gizmo handles, circle entities, capture-phase drag & fast real-time sync
    const VpSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/Engine/VectorFieldGPUViewport.js', 'utf8');
    const VizSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/VectorFieldVisualizer.js', 'utf8');

    for (const Tok of ['BuildGizmoGroup', 'UpdateGizmoPlacement', 'UpdateGizmoScreenScale', 'SetSelectedToken', 'SyncSourceMoved', 'CircleRing', 'CircleDisc', 'PickTarget', "GizmoAxis: 'XZ'", 'capture: true'])
    {
        assert.ok(VpSrc.includes(Tok), `Viewport missing interactive gizmo feature: ${Tok}`);
    }
    assert.ok(VizSrc.includes('Viewport.SyncSourceMoved(Token') && VizSrc.includes('SyncSelectedCentreInputs()') && VizSrc.includes('Viewport.SetSelectedToken('),
        'Visualizer updates moved source coordinates, telemetry & gizmo selection in real time');
    assert.ok(VizSrc.includes('Math.round(Clamp(PosM[1], -80, 260))'),
        'OnMoveSource applies vertical Y gizmo translation alongside X/Z');

    // 3. Moving a source shifts composite field velocity cleanly in real time
    const Lab = ConstructScenario('lab');
    const Attractor = Lab.Sources.find((S) => S.Model === 'Attractor');
    const Before = EvaluateComposite(Lab, 180, 48, 95, 1.0);
    Attractor.Centre = [190, 48, 95];
    const After = EvaluateComposite(Lab, 180, 48, 95, 1.0);
    assert.ok(Math.hypot(After.TotalVec[0] - Before.TotalVec[0], After.TotalVec[1] - Before.TotalVec[1], After.TotalVec[2] - Before.TotalVec[2]) > 1.0,
        'Translating a selected force circle updates composite field immediately');
});

Suite('Streamline Visualization dropdown, thinner ribbons, color/emissive/opacity, single cyan, curve resolution & line count', () => {
    const Lab = ConstructScenario('lab');
    assert.ok(Lab.Viz.StreamThickness < 0.75, `Default streamlines are sleek & thin (${Lab.Viz.StreamThickness})`);
    assert.equal(Lab.Viz.StreamSingleColor, '#68dcff');
    assert.equal(Lab.Viz.StreamResolution, 28);
    assert.equal(Lab.Viz.StreamLineCount, 220);

    // 1. Shaders expose thickness, emissive & opacity uniforms
    assert.ok(STREAMLINE_VERT.includes('uniform float uStreamThickness;'));
    assert.ok(STREAMLINE_FRAG.includes('uniform float uStreamEmissive;') && STREAMLINE_FRAG.includes('uniform float uStreamOpacity;'));

    // 2. Single Cyan (#68dcff), Custom Single Color & Altitude Gradient produce clean expected colors
    const Cyan = ResolveStreamlinePointColor('Engineering', 'CyanSingle', '#68dcff', 0.5, 16);
    assert.ok(Cyan[2] > Cyan[1] * 0.9 && Cyan[2] > Cyan[0] * 1.4, `CyanSingle yields bluish-cyan hue (${Cyan.map((v) => v.toFixed(2)).join(',')})`);

    const Custom = ResolveStreamlinePointColor('Engineering', 'CustomSingle', '#ff5533', 0.5, 16);
    assert.ok(Custom[0] > Custom[1] * 1.8 && Custom[0] > Custom[2] * 2.0, 'CustomSingle preserves chosen hex hue');

    const ElevLow = ResolveStreamlinePointColor('Engineering', 'Elevation', '#68dcff', 0.5, 10);
    const ElevHigh = ResolveStreamlinePointColor('Engineering', 'Elevation', '#68dcff', 0.5, 145);
    assert.ok(ElevHigh[0] > ElevLow[0] + 0.4, 'Elevation gradient cleanly separates floor vs canopy colors');

    const Curve = IntegrateStreamlineBidirectional(Lab, 60, 16, -20, 1.2, 24, 10);
    const CyanGeo = BuildStreamlineRibbonGeometry([Curve], 'Engineering', 34, { ColorMode: 'CyanSingle', SingleColor: '#68dcff' });
    const CyanCol = CyanGeo.getAttribute('aColor').array;
    assert.ok(CyanCol[2] > CyanCol[0] * 1.3, 'BuildStreamlineRibbonGeometry honors ColorMode=CyanSingle');

    // 3. Number of Lines (TargetCount) scales 3D seed count cleanly & Curve Resolution scales trajectory sample granularity
    const BoundsByAxis = {
        XZ: { MinU: -400, MaxU: 400, MinV: -400, MaxV: 400 },
        XY: { MinU: -400, MaxU: 400, MinV: -100, MaxV: 250 },
        YZ: { MinU: -400, MaxU: 400, MinV: -100, MaxV: 250 }
    };
    const Few = CollectMultiAxisStreamlineSeeds(Lab, BoundsByAxis, 16, 84, { ActiveAxes: ['XZ'], Is3D: true, Volumetric: true, TargetCount: 60 });
    const Many = CollectMultiAxisStreamlineSeeds(Lab, BoundsByAxis, 16, 84, { ActiveAxes: ['XZ'], Is3D: true, Volumetric: true, TargetCount: 400 });
    assert.ok(Few.length <= 60 && Few.length >= 30, `TargetCount=60 scales seeds cleanly (got ${Few.length})`);
    assert.ok(Many.length > Few.length * 3, `TargetCount=400 multiplies streamline count cleanly (got ${Many.length})`);

    const CoarsePts = IntegrateStreamlineBidirectional(Lab, -120, 48, 30, 1.2, 12, 14);
    const FinePts = IntegrateStreamlineBidirectional(Lab, -120, 48, 30, 1.2, 48, 3.5);
    assert.ok(FinePts.length >= CoarsePts.length * 2.5, `Higher curve resolution yields finer RK4 steps (${CoarsePts.length} -> ${FinePts.length})`);

    // 4. UI markup exposes dropdown + style settings controls
    const HtmlSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/index.html', 'utf8');
    const VizSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/VectorFieldVisualizer.js', 'utf8');
    assert.ok(HtmlSrc.includes('SelectStreamVis') && HtmlSrc.includes('BtnStreamOptions') && HtmlSrc.includes('StreamOptionsPopover') && HtmlSrc.includes('StreamlineLayersCard'));
    for (const Key of ['StreamThickness', 'StreamEmissive', 'StreamOpacity', 'StreamResolution', 'StreamLineCount', 'CyanSingle', 'CustomSingle'])
    {
        assert.ok(VizSrc.includes(Key), `Visualizer UI missing streamline control ${Key}`);
    }
});

Suite('Play / Curve / Motion Path Animation (Line A->B, 3D Spline, Orbit, Loop modes & Live Field Stepping)', () => {
    // 1. LineAB exact midpoint & length
    const LineSrc = ConstructForce('Attractor', { Centre: [0, 10, 0] });
    EnsureSourceMotionWaypoints(LineSrc, 'LineAB');
    LineSrc.Motion.Waypoints = [[0, 10, 0], [100, 30, 0]];
    LineSrc.Motion.SpeedMps = 50;
    const LineLen = ComputeMotionPathLength(LineSrc);
    assert.ok(Math.abs(LineLen - Math.hypot(100, 20, 0)) < 1.5, `Expected ~102m LineAB length, got ${LineLen.toFixed(2)}`);
    const MidLine = EvaluateMotionPosition(LineSrc, 0.5);
    assert.ok(Math.abs(MidLine[0] - 50) < 1e-4 && Math.abs(MidLine[1] - 20) < 1e-4, 'LineAB evaluates exact A->B midpoint');

    // 2. PingPong, Loop, and Once phase cycles at fixed speed
    LineSrc.Motion.Loop = 'Once';
    assert.equal(ResolveMotionPhase(LineSrc.Motion, 0), 0);
    assert.equal(ResolveMotionPhase(LineSrc.Motion, 10), 1);

    LineSrc.Motion.Loop = 'PingPong';
    const PForward = ResolveMotionPhase({ ...LineSrc.Motion, SpeedMps: LineLen, Loop: 'PingPong' }, 0.5);
    const PBackward = ResolveMotionPhase({ ...LineSrc.Motion, SpeedMps: LineLen, Loop: 'PingPong' }, 1.5);
    assert.ok(Math.abs(PForward - 0.5) < 1e-3 && Math.abs(PBackward - 0.5) < 1e-3, 'PingPong bounces cleanly A -> B -> A');

    LineSrc.Motion.Loop = 'Loop';
    const PLoop = ResolveMotionPhase({ ...LineSrc.Motion, SpeedMps: LineLen, Loop: 'Loop' }, 1.25);
    assert.ok(Math.abs(PLoop - 0.25) < 1e-3, 'Loop wraps seamlessly back to A');

    // 3. Catmull-Rom 3D Spline hits waypoints cleanly & samples smooth arc
    const SplineSrc = ConstructForce('Vortex', { Centre: [0, 0, 0] });
    EnsureSourceMotionWaypoints(SplineSrc, 'SplineCurve');
    SplineSrc.Motion.Waypoints = [[0, 0, 0], [60, 40, 30], [120, 0, 90]];
    const S0 = EvaluateMotionPosition(SplineSrc, 0);
    const S5 = EvaluateMotionPosition(SplineSrc, 0.5);
    const S1 = EvaluateMotionPosition(SplineSrc, 1);
    assert.ok(Math.hypot(S0[0], S0[1], S0[2]) < 1e-4, 'Spline starts strictly at A');
    assert.ok(Math.hypot(S5[0] - 60, S5[1] - 40, S5[2] - 30) < 1e-4, 'Spline passes through midpoint P1');
    assert.ok(Math.hypot(S1[0] - 120, S1[1], S1[2] - 90) < 1e-4, 'Spline terminates cleanly at B');
    assert.equal(SampleMotionPathPoints(SplineSrc, 24).length, 25);

    // 4. Lab scenario Vortex Column advances along its curve during SIM/PLAY & updates Composite field
    const Lab = ConstructScenario('lab');
    const Vortex = Lab.Sources.find((S) => S.Model === 'Vortex');
    assert.ok(Vortex?.Motion?.Mode === 'SplineCurve' && Vortex.Motion.Waypoints.length >= 3, 'Lab Vortex carries default 3D spline motion');
    const StartPos = [...Vortex.Centre];
    const IdleChanged = StepAnimatedSources(Lab, 1.5, false);
    assert.equal(IdleChanged.length, 0, 'EDIT / paused state does not step animated sources automatically');

    const FieldBefore = EvaluateComposite(Lab, 90, 16, 85, 1.5);
    const ActiveChanged = StepAnimatedSources(Lab, 2.5, true);
    assert.ok(ActiveChanged.includes(Vortex.Token), 'StepAnimatedSources advances Vortex along its curve');
    assert.ok(Math.hypot(Vortex.Centre[0] - StartPos[0], Vortex.Centre[1] - StartPos[1], Vortex.Centre[2] - StartPos[2]) > 8,
        'Vortex translated significantly along spline at t=2.5s');
    const FieldAfter = EvaluateComposite(Lab, 90, 16, 85, 2.5);
    assert.ok(Math.hypot(FieldAfter.TotalVec[0] - FieldBefore.TotalVec[0], FieldAfter.TotalVec[1] - FieldBefore.TotalVec[1], FieldAfter.TotalVec[2] - FieldBefore.TotalVec[2]) > 0.5,
        'Field at destination updates immediately as source glides along curve');

    // 5. TOML SimulationSpace export & UI/Viewport wiring
    const Toml = EncodeSimulationSpaceToml(Lab).join('\n');
    assert.ok(Toml.includes('MotionMode = "SplineCurve"') && Toml.includes('MotionWaypoints ='), 'TOML exports Motion path metadata');

    const HtmlSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/index.html', 'utf8');
    const VizSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/VectorFieldVisualizer.js', 'utf8');
    const VpSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/Engine/VectorFieldGPUViewport.js', 'utf8');
    for (const Tok of ['BtnPlayPause', 'BtnResetTime', 'ViewportTimeBadge']) assert.ok(HtmlSrc.includes(Tok), `HTML missing transport UI ${Tok}`);
    for (const Tok of ['StepAnimatedSources', 'SelMotionMode', 'BtnMotionAddLine', 'BtnMotionAddSpline', 'RngMotionScrub', 'SyncTransportUi']) assert.ok(VizSrc.includes(Tok), `Visualizer missing ${Tok}`);
    for (const Tok of ['RebuildMotionPaths', 'SyncAnimatedSourcesFrame', 'SelectedWaypointIdx', 'WaypointHandles']) assert.ok(VpSrc.includes(Tok), `Viewport missing ${Tok}`);
});

Suite('Bokeh Stream Ribbons debug visualizer (tapered wakes + DOF aperture bokeh beads across 3D/2D GPU & fallback)', () => {
    // 1. Shaders expose BOKEH_RIBBON_* and BOKEH_BEAD_* GLSL3 kernels with aperture DOF & smooth ribbon fade
    assert.ok(BOKEH_RIBBON_VERT.includes('uBokehThickness') && BOKEH_RIBBON_VERT.includes('aTangent'));
    assert.ok(BOKEH_RIBBON_FRAG.includes('tailFade') && BOKEH_RIBBON_FRAG.includes('uBokehEmissive'));
    assert.ok(BOKEH_BEAD_VERT.includes('aDof') && BOKEH_BEAD_VERT.includes('aRadiusPx'));
    assert.ok(BOKEH_BEAD_FRAG.includes('sharpCore') && BOKEH_BEAD_FRAG.includes('bokehDisc') && BOKEH_BEAD_FRAG.includes('rimRing'));

    // 2. Default scenarios enable BokehRibbons debug visualizer at calm speed while keeping RK4 Streamlines toggleable
    const Lab = ConstructScenario('lab');
    assert.equal(Lab.Viz.BokehRibbons, true);
    assert.equal(Lab.Viz.BokehPalette, 'BokehTeal');
    assert.ok(Lab.Viz.BokehSpeed > 0 && Lab.Viz.BokehSpeed <= 0.6, `Default BokehSpeed stays calm for particle tracking (${Lab.Viz.BokehSpeed})`);
    assert.ok(Lab.Viz.BokehCount >= 200 && Lab.Viz.BokehCount <= BOKEH_MAX_TRACERS);

    // 3. Sim spawns pre-warmed curved trails with heterogeneous sharp+bokeh DOF values and advances cleanly
    const Sim = new BokehStreamRibbonSim(4242);
    const Bounds = { Is3D: true, ActiveAxes: ['XZ'], SliceAxis: 'XZ', SliceHeight: 16, HalfM: 460, Center: [0, 0, 48] };
    Sim.EnsureSynced(Lab, Bounds, 1.0, true);
    assert.equal(Sim.ActiveCount, Lab.Viz.BokehCount);

    let CurvedCount = 0;
    let SharpCount = 0;
    let BokehDiscCount = 0;
    let FiniteTrails = 0;
    for (let I = 0; I < Sim.ActiveCount; ++I)
    {
        const Tr = Sim.Tracers[I];
        assert.ok(Tr.ValidLen >= 10 && Tr.ValidLen <= BOKEH_TRAIL_SEGS, `Prewarmed ribbon must have multi-segment trail (${Tr.ValidLen})`);
        if (Tr.Dof <= 0.62) SharpCount++; else BokehDiscCount++;
        const Dx = Tr.Trail[0] - Tr.Trail[(Tr.ValidLen - 1) * 4];
        const Dz = Tr.Trail[2] - Tr.Trail[(Tr.ValidLen - 1) * 4 + 2];
        if (Math.hypot(Dx, Dz) > 1.0) CurvedCount++;
        if (Number.isFinite(Tr.X) && Number.isFinite(Tr.Y) && Number.isFinite(Tr.Z)) FiniteTrails++;
    }
    assert.equal(FiniteTrails, Sim.ActiveCount);
    assert.ok(CurvedCount > Sim.ActiveCount * 0.65, `Pre-warm produces extended ribbons on active forces (${CurvedCount}/${Sim.ActiveCount})`);
    assert.ok(SharpCount > 50 && BokehDiscCount > 50, `Mixed sharp (${SharpCount}) and creamy bokeh (${BokehDiscCount}) beads`);

    // 4. Live stepping advects ribbons along Composite field continuously without NaNs
    const SampleBefore = [Sim.Tracers[0].X, Sim.Tracers[0].Y, Sim.Tracers[0].Z];
    for (let Step = 0; Step < 20; ++Step) Sim.Step(Lab, Bounds, 1.0 + Step * 0.03, 0.03);
    const Moved = Math.hypot(Sim.Tracers[0].X - SampleBefore[0], Sim.Tracers[0].Y - SampleBefore[1], Sim.Tracers[0].Z - SampleBefore[2]);
    assert.ok(Moved > 0.5 || Sim.Tracers[0].Age < 0.8, 'Stepping advances tracer particles along the composite force field');

    // 5. Bokeh palette modes resolve distinct Mist Teal vs FollowStreams hues
    const TealCols = ResolveBokehColors({ BokehPalette: 'BokehTeal' }, 0.7, 20);
    assert.ok(TealCols.HeadRgb[2] >= TealCols.HeadRgb[0] && TealCols.MidRgb[1] > TealCols.MidRgb[0]);
    const StreamCols = ResolveBokehColors({ BokehPalette: 'FollowStreams', StreamColorMode: 'CustomSingle', StreamSingleColor: '#ff5533', Colormap: 'Engineering' }, 0.7, 20);
    assert.ok(StreamCols.MidRgb[0] > StreamCols.MidRgb[2] * 2.0, 'FollowStreams honors custom streamline palette');

    // 6. UI HTML & JS wire the Bokeh debug visualizer layer & style controls
    const HtmlSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/index.html', 'utf8');
    const VizSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/VectorFieldVisualizer.js', 'utf8');
    const VpSrc = fs.readFileSync('Experimental/VectorFieldVisualizer/Engine/VectorFieldGPUViewport.js', 'utf8');
    assert.ok(HtmlSrc.includes('data-layer="BokehRibbons"') && HtmlSrc.includes('FooterStreamMode'));
    for (const Tok of ['BokehRibbons', 'BokehPalette', 'BokehSpeed', 'BokehCount', 'BokehThickness', 'BokehEmissive'])
    {
        assert.ok(VizSrc.includes(Tok), `Visualizer missing bokeh control ${Tok}`);
    }
    assert.ok(VpSrc.includes('BuildGpuBokehRibbons') && VpSrc.includes('UpdateBokehRibbons') && VpSrc.includes('Bokeh Stream Ribbons Debug Visualizer'));
});

console.log(`\nAll ${Passed}/18 suites PASSED.`);
