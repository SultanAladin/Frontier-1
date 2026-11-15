// ForceFieldCatalogue 17-type taxonomy, bounded reach/falloff/activation & GPU packer verification.
//
//     node Experimental/ParticleWorkspace/Engine/CheckForceFields.mjs

import assert from 'node:assert/strict';
import { PE } from './pe.js';
import './presets.js';
import './forcefields.js';
import {
    Contributions, Channels, Reaches, Falloffs, Activations, Types, TypeById, Falls,
    ConstructSource, ConstructChamberScenario, Prepare, EvaluateSource, EvaluatePrepared, Evaluate,
    EvaluateAllPrepared, ResolveActivation, ResolveReach, ResolveProfile, FloatToHalf,
    SampleWindTexData, PackForceEntries, EncodeLevelspace, ResolveBeaufort, ResolveBudget,
    EnsureCompiledFormula, SampleDifferential, DEFAULT_FORMULA_CODE
} from './ForceFieldCatalogue.js';
import { EvaluateFieldAcceleration } from '../ParticleFieldReadback.js';

let Checks = 0;
function Check(Condition, Claim)
{
    Checks++;
    if (!Condition) { console.error('  FAIL  ' + Claim); process.exit(1); }
    console.log('  PASS  ' + Claim);
}
function Banner(Title) { console.log('\n' + Title); }
const Near = (A, B, Slack = 1e-4) => Math.abs(A - B) <= Slack;

Banner('17 bounded force types across 3 distinct contributions');
{
    Check(Types.length === 17, 'catalogue defines all 17 bounded force types');
    Check(new Set(Types.map(T => T.Id)).size === 17, 'all 17 force ids are unique');
    Check(Types.filter(T => T.Contribution === 'Flow').length === 10, '10 Flow types (m/s)');
    Check(Types.filter(T => T.Contribution === 'Accelerate').length === 6, '6 Accelerate types (m/s²)');
    Check(Types.filter(T => T.Contribution === 'Damp').length === 1, '1 Damp type (1/s)');
    Check(Reaches.length === 3 && Falloffs.length === 4 && Activations.length === 3, '3 reaches, 4 falloffs, 3 activations defined');
    Check(Channels.length === 7, '7 receiver channels declared');
}

Banner('Every type evaluates cleanly inside chamber bounds and is strictly 0 far outside');
{
    const Scene = ConstructChamberScenario('lab');
    for (const Type of Types)
    {
        const Src = ConstructSource(Type.Id, { Centre: [0, 1.5, 0], Activation: 'Always', Radius: 3.0, HalfX: 2.0, HalfZ: 2.0, Height: 4.0 });
        const Inside = new Float64Array(3);
        const ProbeX = Type.Id === 'Tornado' || Type.Id === 'Microburst' || Type.Id === 'Orbit' || Type.Id === 'Blast' ? 0.8 : 0.2;
        const Hit = EvaluateSource(Src, Scene.Wind, ProbeX, 1.5, 0.2, 0.6, Inside);
        const MagIn = Math.hypot(Inside[0], Inside[1], Inside[2]);
        Check(Hit && Number.isFinite(MagIn) && MagIn > 1e-4, `${Type.Id} evaluates non-zero finite force inside bounds (${MagIn.toFixed(3)})`);

        const Outside = new Float64Array(3);
        EvaluateSource(Src, Scene.Wind, 25.0, 1.5, -25.0, 0.6, Outside);
        Check(Outside[0] === 0 && Outside[1] === 0 && Outside[2] === 0, `${Type.Id} exerts zero leakage outside spatial bounds`);
    }
}

Banner('Bounded Ambient Wind, Reaches (Sphere / Column / Box), Falloffs & Activations');
{
    Check(Near(Falls('None', 9, 10), 1), 'Falloff None remains 1 up to boundary');
    Check(Near(Falls('Linear', 5, 10), 0.5), 'Falloff Linear equals 0.5 at midpoint');
    Check(Near(Falls('Smooth', 0, 10), 1) && Near(Falls('Smooth', 10, 10), 0), 'Falloff Smooth transitions 1 -> 0');
    Check(Falls('InverseSquare', 9, 10) < Falls('Linear', 9, 10), 'Falloff InverseSquare decays steeper near rim');

    const Trig = ConstructSource('Blast', { Activation: 'Trigger', Lasts: 3.0, RampIn: 0.2, RampOut: 0.5, TriggeredAt: null });
    Check(ResolveActivation(Trig, 1.0).Status === 'Armed', 'Trigger source starts Armed before firing');
    Trig.TriggeredAt = 2.0;
    Check(ResolveActivation(Trig, 2.5).Status === 'Active', 'Trigger source is Active during pulse window');
    Check(ResolveActivation(Trig, 6.0).Status === 'Spent', 'Trigger source is Spent after pulse expires');

    const Win = ConstructSource('WindGenerator', { Activation: 'Window', Begins: 2.0, Lasts: 2.0, Repeats: 6.0, RampIn: 0.2, RampOut: 0.2 });
    Check(ResolveActivation(Win, 1.0).Status === 'Waiting', 'Window source is Waiting before Begins');
    Check(ResolveActivation(Win, 3.0).Status === 'Active', 'Window source is Active inside window');
    Check(ResolveActivation(Win, 9.0).Status === 'Active', 'Repeating window reactivates one period later');
}

Banner('Channel selectivity & WebGPU float16 windTex + 4-vec4 fieldBuf parity');
{
    const Scene = ConstructChamberScenario('lab');
    const DragOnlyDebris = ConstructSource('Drag', { Centre: [0, 1.5, 0], Acts: ['Debris'], Strength: 5.0 });
    Scene.Sources.push(DragOnlyDebris);

    const ParticlesAll = EvaluateAllPrepared(Scene, Prepare(Scene, 1.0), 0.1, 1.5, 0.1, 0.5, null, 'Particles');
    const DebrisAll = EvaluateAllPrepared(Scene, Prepare(Scene, 1.0), 0.1, 1.5, 0.1, 0.5, null, 'Debris');
    Check(Near(ParticlesAll.Damping, 0), 'selective Debris drag ignores Particles');
    Check(DebrisAll.Damping > 1.0, 'selective Debris drag acts on Debris');

    const Prepared = Prepare(Scene, 1.0);
    const Tex = SampleWindTexData(Scene, Prepared, PE.WIND, 1.0, null);
    Check(Tex instanceof Uint16Array && Tex.length === 24 * 12 * 24 * 4, 'SampleWindTexData fills exact 24×12×24×4 float16 lattice');
    Check(FloatToHalf(0) === 0 && FloatToHalf(1.0) === 0x3c00 && FloatToHalf(-2.0) === 0xc000, 'IEEE-754 float16 conversion matches exact bit patterns');

    // Verify PackForceEntries matches ParticleFieldReadback EvaluateFieldAcceleration across Sphere/Column/Box/Orbit/Gravity/Magnetic/Attractor
    const TestScene = ConstructChamberScenario('lab');
    TestScene.Sources = [
        ConstructSource('Attractor', { Centre: [1, 1.5, 0], Radius: 4.0, Strength: 6.0, Swirl: 2.0, Falloff: 'Smooth' }),
        ConstructSource('Orbit',     { Centre: [-1, 0, 1], Radius: 3.5, Height: 4.0, Strength: 5.0, Reach: 'Column', Falloff: 'Linear' }),
        ConstructSource('Gravity',   { Centre: [0, 0, 0],  Radius: 5.0, Height: 6.0, Strength: 9.81, Reach: 'Column', Falloff: 'None' }),
        ConstructSource('Magnetic',  { Centre: [0, 2, -1], Radius: 4.0, Strength: 7.0, Falloff: 'Smooth' }),
        ConstructSource('Drag',      { Centre: [0, 0, 0],  HalfX: 2.5, HalfZ: 2.5, Height: 4.0, Strength: 3.0, Reach: 'Box' })
    ];
    const P2 = Prepare(TestScene, 0.5);
    const Packed = PackForceEntries(TestScene, P2, []);
    Check(Packed[0] === 5, 'PackForceEntries packed all 5 Accelerate + Damp slots into fieldBuf');

    const Ref = EvaluateAllPrepared(TestScene, P2, 0.4, 1.6, 0.3, 0.5, null, 'Particles');
    const Gpu = [0, 0, 0];
    EvaluateFieldAcceleration(Packed, 0.4, 1.6, 0.3, Gpu);
    Check(Near(Gpu[0], Ref.Acceleration[0], 1e-3) && Near(Gpu[1], Ref.Acceleration[1], 1e-3) && Near(Gpu[2], Ref.Acceleration[2], 1e-3),
          'CPU/GPU fieldBuf packed acceleration matches ForceFieldCatalogue EvaluateAccelerateSource identically');
}

Banner('Levelspace encoding, profiles, Beaufort & GPU budget');
{
    const Scene = ConstructChamberScenario('storm');
    const Toml = EncodeLevelspace(Scene).join('\n');
    Check(Toml.includes('Type = "ForceField"') && Toml.includes('Type = "Wind"') && Toml.includes('Type = "ForceSource"'), 'EncodeLevelspace serializes ForceField, Wind, and ForceSource blocks');
    const Prof = ResolveProfile(Scene, Scene.Sources[0], 1.5);
    Check(Prof.Points.length === 65 && Prof.Points.some(([, V]) => V > 0), 'ResolveProfile produces radial strength curve');
    Check(ResolveBeaufort(0.1).Force === 0 && ResolveBeaufort(12.0).Name === 'Strong breeze', 'ResolveBeaufort maps wind speed cleanly');
    const Budget = ResolveBudget(Scene);
    Check(Budget.GpuSlots === 16 && Budget.CascadeBytes > 0, 'ResolveBudget reports 16 GPU slots and cascade bytes');
}

Banner('Symbolic VectorFieldLang formula compilation & SampleDifferential curl/div');
{
    const Scene = ConstructChamberScenario('lab');
    const FormulaSrc = ConstructSource('Attractor', {
        Name: 'Formula Vortex',
        Centre: [0, 1.5, 0],
        Radius: 4.0,
        Strength: 8.0,
        Swirl: 5.0,
        UseFormula: true,
        FormulaCode: 'a = -S · A(r) · r̂ + ω_swirl · (ŷ × r̂)'
    });
    const Comp = EnsureCompiledFormula(FormulaSrc, 0);
    Check(Comp && Comp.Ok && Comp.OutputChannel === 'Accelerate' && typeof Comp.JsFn === 'function', 'Symbolic formula compiles cleanly with Accelerate output channel');

    Scene.Sources = [FormulaSrc];
    const Prep = Prepare(Scene, 0.5);
    const All = EvaluateAllPrepared(Scene, Prep, 1.2, 1.5, 0.4, 0.5, null, 'Particles');
    Check(All.AccelMag > 1.0, `Symbolic Accelerate formula drives bounded force magnitude (${All.AccelMag.toFixed(2)} m/s²)`);

    const Out = EvaluateAllPrepared(Scene, Prep, 22.0, 1.5, 22.0, 0.5, null, 'Particles');
    Check(Out.AccelMag === 0, 'Symbolic formula strictly zero outside spatial reach bounds');

    FormulaSrc.FormulaCode = 'v = ω_swirl · A(r) · (ŷ × r̂)';
    delete FormulaSrc._CompiledFormula;
    const FlowPrep = Prepare(Scene, 0.5);
    const FlowDiff = SampleDifferential(Scene, FlowPrep, 'Flow', 1.0, 1.5, 0.0, 0.2, null, 'Particles');
    Check(Number.isFinite(FlowDiff.Curl) && FlowDiff.Curl > 0.1 && Number.isFinite(FlowDiff.Div), `SampleDifferential detects swirling vorticity (|∇×v|=${FlowDiff.Curl.toFixed(2)} /s)`);
}

console.log('\nPASS ' + Checks);
