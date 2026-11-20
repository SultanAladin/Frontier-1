#!/usr/bin/env node
/*============================================================================================================================================
                                                    FORCE FIELD VALIDATION
============================================================================================================================================*/
// 🧩 Node ESM validation for the ForceField Studio engine: finite-bound leakage, channel separation, curl-noise divergence, RK4 streamlines,
// formula compilation errors, document round-trips and probe advection stability. Run with `node Experimental/ForceFieldEditor/Validation/ForceFieldValidation.mjs`.

import assert from 'node:assert/strict';
import * as M from '../Engine/FieldMath.js';
import { FORCE_LAWS, LawById, ParamSchema, SanitizeParams } from '../Engine/ForceFieldCatalog.js';
import {
  NewSource, PrepareFrame, EvaluateAt, QuantityAt, SuggestMaximum, AdvectMote,
  DefaultDocument, SanitizeDocument, DocumentToJson, DocumentFromJson, DocumentToCascadeText, DEFAULT_DOMAIN,
} from '../Engine/FieldEvaluator.js';
import { CompileFieldProgram, FORMULA_PRESETS } from '../Engine/VectorFieldLang.js';

let passed = 0;
let failed = 0;
const failures = [];

function test(name, fn) {
  try {
    fn();
    passed += 1;
    process.stdout.write(`  ✓ ${name}\n`);
  } catch (error) {
    failed += 1;
    failures.push({ name, message: error.message });
    process.stdout.write(`  ✗ ${name}\n      ${error.message.split('\n')[0]}\n`);
  }
}

const group = (name) => process.stdout.write(`\n${name}\n`);
const finite = (v) => Array.isArray(v) ? v.every(Number.isFinite) : Number.isFinite(v);

// A representative runtime for one law, anchored at the origin facing +Z.
function FrameFor(lawId, overrides = {}, time = 0.7) {
  const source = NewSource(lawId, { Position: [0, 0, 0], Direction: [0, 0, 1], ...overrides });
  return { source, runtime: PrepareFrame([source], time, DEFAULT_DOMAIN) };
}

group('Vector algebra');
test('Cross product is right-handed', () => {
  assert.deepEqual(M.Cross([1, 0, 0], [0, 1, 0]), [0, 0, 1]);
});
test('Normalize keeps a fallback for the zero vector', () => {
  assert.deepEqual(M.Normalize([0, 0, 0], [0, 1, 0]), [0, 1, 0]);
});
test('DirectionFrame is orthonormal for a non-trivial axis', () => {
  const frame = M.DirectionFrame([0.3, -0.7, 0.4]);
  for (const v of [frame.Axis, frame.E1, frame.E2]) assert.ok(Math.abs(M.Length(v) - 1) < 1e-9);
  assert.ok(Math.abs(M.Dot(frame.E1, frame.E2)) < 1e-9);
  assert.ok(Math.abs(M.Dot(frame.E1, frame.Axis)) < 1e-9);
});

group('Envelopes');
test('Attenuation is exactly zero at and beyond the boundary', () => {
  for (const kind of ['SmoothStep', 'Linear', 'InverseSquare', 'Constant']) {
    assert.equal(M.Attenuation(kind, 1), 0, kind);
    assert.equal(M.Attenuation(kind, 1.4), 0, kind);
    assert.ok(M.Attenuation(kind, 0) > 0.99, kind);
  }
});
test('Sphere envelope normalises to the radius', () => {
  const env = M.EnvelopeCoordinate({ Envelope: 'Sphere', Radius: 4 }, [0, 0, 4]);
  assert.ok(Math.abs(env.U - 1) < 1e-9);
  assert.equal(M.EnvelopeCoordinate({ Envelope: 'Sphere', Radius: 4 }, [0, 0, 4.01]).Inside, false);
});
test('Column envelope is capped in height as well as radius', () => {
  const source = { Envelope: 'Column', Radius: 3, Height: 6 };
  assert.equal(M.EnvelopeCoordinate(source, [0, 0, 2.9]).Inside, true);
  assert.equal(M.EnvelopeCoordinate(source, [0, 0, 3.1]).Inside, false);
  assert.equal(M.EnvelopeCoordinate(source, [2.9, 0, 0]).Inside, true);
});
test('Box envelope is oriented by the source direction', () => {
  const source = { Envelope: 'Box', HalfExtents: [1, 1, 6], Direction: [1, 0, 0], Anchor: [0, 0, 0] };
  source.Frame = M.DirectionFrame(source.Direction);
  assert.equal(M.EnvelopeCoordinate(source, [5.5, 0, 0]).Inside, true);
  assert.equal(M.EnvelopeCoordinate(source, [0, 0, 5.5]).Inside, false);
  assert.equal(M.EnvelopeCoordinate(source, [0, 0.5, 0]).Inside, true);
});
test('Wall stand-off reports a signed distance', () => {
  const source = { Envelope: 'Sphere', Radius: 4, Anchor: [0, 0, 0] };
  assert.ok(M.EnvelopeWall(source, [0, 0, 2]).Distance < 0);
  assert.ok(M.EnvelopeWall(source, [0, 0, 6]).Distance > 0);
});

group('Noise');
test('Curl noise is divergence free to truncation error', () => {
  const h = 0.05;
  let worst = 0;
  for (let i = 0; i < 24; i++) {
    const p = [((i * 7) % 11) - 5, ((i * 5) % 9) - 4, ((i * 3) % 7) - 3];
    const xp = M.CurlNoise([p[0] + h, p[1], p[2]], { Scale: 4, Seed: 3 })[0];
    const xm = M.CurlNoise([p[0] - h, p[1], p[2]], { Scale: 4, Seed: 3 })[0];
    const yp = M.CurlNoise([p[0], p[1] + h, p[2]], { Scale: 4, Seed: 3 })[1];
    const ym = M.CurlNoise([p[0], p[1] - h, p[2]], { Scale: 4, Seed: 3 })[1];
    const zp = M.CurlNoise([p[0], p[1], p[2] + h], { Scale: 4, Seed: 3 })[2];
    const zm = M.CurlNoise([p[0], p[1], p[2] - h], { Scale: 4, Seed: 3 })[2];
    worst = Math.max(worst, Math.abs((xp - xm + yp - ym + zp - zm) / (2 * h)));
  }
  assert.ok(worst < 0.9, `divergence ${worst.toFixed(3)} should stay small for a curl field`);
});
test('Gradient noise is deterministic and bounded', () => {
  for (let i = 0; i < 40; i++) {
    const a = M.GradientNoise(i * 0.37, i * 0.11, i * 0.73, 5);
    const b = M.GradientNoise(i * 0.37, i * 0.11, i * 0.73, 5);
    assert.equal(a, b);
    assert.ok(Math.abs(a) <= 1.6, `noise ${a}`);
  }
});
test('Different seeds decorrelate', () => {
  let diff = 0;
  for (let i = 0; i < 20; i++) diff += Math.abs(M.GradientNoise(i, i * 0.5, 0, 1) - M.GradientNoise(i, i * 0.5, 0, 2));
  assert.ok(diff > 0.5, 'seeds should not produce the same field');
});

group('Duration gates');
test('Continuous gate is one', () => assert.equal(M.DurationGate({ Duration: 'Continuous' }, 3.2), 1));
test('Modulated gate opens and closes once per period', () => {
  const source = { Duration: 'Modulated', ModulationPeriod: 4, ModulationAttack: 1, ModulationRelease: 1, ModulationPhase: 0 };
  const mid = M.DurationGate(source, 2);
  assert.ok(mid > 0.2, `mid gate ${mid}`);
  assert.ok(M.DurationGate(source, 0.001) < 0.05);
  assert.ok(M.DurationGate(source, 3.999) < 0.05);
});
test('Impulse shell expands to its authored reach and never past it', () => {
  // A front accelerates through the authored reach over one interval: the crest must sit on ImpulseFrontRadius.
  const source = { Duration: 'Impulse', ImpulseInterval: 4, ImpulseSpeed: 6, ImpulseWidth: 0.8, ImpulseStartRadius: 0.4, Radius: 10, ImpulseScope: 8 };
  const front = M.ImpulseFrontRadius(source, 2);
  assert.ok(front > 0.4 && front < 8.0001, `front inside the reach: ${front}`);
  assert.ok(Math.abs(front - 4.2) < 1e-6, `front ${front}`);
  const crest = M.DurationGate(source, 2, front);
  assert.ok(Math.abs(crest - 1) < 1e-6, `crest ${crest}`);
  // Falloff is Gaussian in the signed stand-off from the front: σ = ImpulseWidth, symmetric, monotone both ways.
  const sigma = 0.8;
  assert.ok(Math.abs(M.DurationGate(source, 2, front - sigma) - Math.exp(-0.5)) < 1e-6, 'one sigma behind');
  assert.ok(Math.abs(M.DurationGate(source, 2, front + sigma) - Math.exp(-0.5)) < 1e-6, 'one sigma ahead');
  assert.ok(M.DurationGate(source, 2, front + 4 * sigma) < 1e-3, 'four sigma ahead');
  assert.ok(M.DurationGate(source, 2, front - 4 * sigma) < 1e-3, 'four sigma behind');
  // The front is periodic with the interval, and it is clamped to the envelope so it cannot wander off.
  assert.ok(Math.abs(M.ImpulseFrontRadius(source, 2 + 4) - front) < 1e-6);
  assert.ok(M.ImpulseFrontRadius(source, 3.999) <= 8.0001);
  // The reach is bounded by the envelope: nothing escapes the authored radius.
  assert.equal(M.ImpulseScope({ Radius: 8 }, 9, 2), 0);
  assert.ok(M.ImpulseScope({ Radius: 8 }, 4, 2) > 0.99);
});

group('Catalog integrity');
test('Twenty generators: nineteen canonical laws plus the formula field', () => {
  assert.equal(FORCE_LAWS.length, 20);
  assert.equal(new Set(FORCE_LAWS.map((l) => l.Id)).size, 20);
  assert.equal(new Set(FORCE_LAWS.map((l) => l.Ordinal)).size, 20);
});
test('Every law declares a domain, channel, envelope and sane parameter schema', () => {
  for (const law of FORCE_LAWS) {
    assert.ok(['VectorField', 'ChannelField', 'TurbulenceField'].includes(law.Domain), law.Id);
    assert.ok(['velocity', 'acceleration', 'damping'].includes(law.Channel), law.Id);
    assert.ok(['Sphere', 'Column', 'Box', 'Infinite'].includes(law.Envelope), law.Id);
    assert.ok(law.Params.length >= 1, law.Id);
    for (const p of law.Params) {
      assert.ok(p.Min <= p.Default && p.Default <= p.Max, `${law.Id}.${p.Key} default outside range`);
      assert.ok(p.Max > p.Min, `${law.Id}.${p.Key} empty range`);
    }
    assert.ok(law.Glyph.includes('<svg'), law.Id);
    assert.ok(law.Summary.length > 20, law.Id);
  }
});
test('SanitizeParams clamps and drops unknown keys', () => {
  const law = LawById.Attractor;
  const seed = Object.fromEntries(law.Params.map((p) => [p.Key, p.Default]));
  const dirty = { ...seed, Swirl: 1e9, Bogus: 42 };
  const clean = SanitizeParams('Attractor', dirty);
  assert.equal(clean.Bogus, undefined);
  assert.equal(clean.Swirl, law.Params.find((p) => p.Key === 'Swirl').Max);
});

group('Finite-bound discipline');
test('Every generator is exactly zero outside its envelope', () => {
  for (const law of FORCE_LAWS) {
    const { runtime } = FrameFor(law.Id, law.Envelope === 'Infinite' ? {} : {});
    const far = law.Envelope === 'Sphere' || law.Envelope === 'Column' ? [0, 0, (law.Radius || 6) * 4 + 60] : [0, 0, 0];
    const point = law.Envelope === 'Box' ? [0, 0, (law.HalfExtents?.[2] || 4) * 30 + 40] : law.Envelope === 'Infinite' ? [400, 0, 0] : far;
    const sample = EvaluateAt(runtime, point, 0.7);
    assert.ok(M.Length(sample.Velocity) < 1e-9, `${law.Id} leaked velocity ${M.Length(sample.Velocity)}`);
    assert.ok(M.Length(sample.Acceleration) < 1e-9, `${law.Id} leaked acceleration ${M.Length(sample.Acceleration)}`);
    assert.equal(sample.Damping, 0, law.Id);
  }
});
test('Envelope scaling changes reach, not correctness', () => {
  const { runtime } = FrameFor('Repulsor', { Radius: 20 });
  const near = M.Length(EvaluateAt(runtime, [0, 0, 2], 0.5).Acceleration);
  const out = M.Length(EvaluateAt(runtime, [0, 0, 25], 0.5).Acceleration);
  assert.ok(near > 0);
  assert.equal(out, 0);
});
test('Damping channel never contributes velocity or acceleration', () => {
  const { runtime } = FrameFor('Drag');
  const sample = EvaluateAt(runtime, [0, 0, 1], 0.5, { Ambient: [3, 0, 0] });
  assert.equal(M.Length(sample.Velocity), 0);
  assert.equal(M.Length(sample.Acceleration), 0);
  assert.ok(sample.Damping > 0);
});

group('Law behaviour');
test('Linear wind follows its direction and rises with shear', () => {
  const { runtime, source } = FrameFor('LinearWind', { Radius: 12, Height: 12, Params: { Strength: 6, Shear: 0.2, Veer: 0, GustAmplitude: 0, GustFrequency: 0.2, Turbulence: 0 }, Attenuation: 'Constant' });
  const low = EvaluateAt(runtime, [0, 0, -4], 0.4).Velocity;
  const high = EvaluateAt(runtime, [0, 0, 4], 0.4).Velocity;
  assert.ok(M.Dot(low, source.Direction) > 1);
  assert.ok(M.Length(high) > M.Length(low));
});
test('Attractor pulls inward and swirl adds a tangential term', () => {
  const plain = FrameFor('Attractor', { Params: { Strength: 10, CoreRadius: 1.5, Swirl: 0, SpinBias: 0 } }).runtime;
  const swirl = FrameFor('Attractor', { Params: { Strength: 10, CoreRadius: 1.5, Swirl: 6, SpinBias: 0 } }).runtime;
  const a = EvaluateAt(plain, [2.5, 0, 0], 0.5).Acceleration;
  const b = EvaluateAt(swirl, [2.5, 0, 0], 0.5).Acceleration;
  assert.ok(a[0] < 0, 'pull should be inward');
  assert.ok(Math.abs(b[1]) > Math.abs(a[1]) + 0.5, 'swirl should dominate the tangential axis');
});
test('Rotational column turns counter-clockwise about its axis', () => {
  const { runtime } = FrameFor('RotationalColumn', { Radius: 8, Height: 10, Params: { Strength: 10, CoreRadius: 1.5, Inflow: 0, Updraft: 0 } });
  const v = EvaluateAt(runtime, [2, 0, 0], 0.5).Velocity;
  assert.ok(v[1] > 0.5, `tangential speed ${v[1]}`);
  assert.ok(Math.abs(v[0]) < v[1]);
});
test('Repulsor accelerates outward, attractor inward, on the same probe', () => {
  const rep = EvaluateAt(FrameFor('Repulsor').runtime, [2, 0, 0], 0.5).Acceleration[0];
  const att = EvaluateAt(FrameFor('Attractor').runtime, [2, 0, 0], 0.5).Acceleration[0];
  assert.ok(rep > 0 && att < 0);
});
test('Containment is inert inside the inner radius and pulls back at the wall', () => {
  const { runtime } = FrameFor('Containment', { Radius: 8, Params: { Strength: 9, InnerRatio: 0.5, WallDamping: 0.8 } });
  const inner = EvaluateAt(runtime, [1, 0, 0], 0.5).Acceleration;
  const wall = EvaluateAt(runtime, [6.5, 0, 0], 0.5).Acceleration;
  assert.equal(M.Length(inner), 0);
  assert.ok(wall[0] < 0);
});
test('Radial burst pushes outward only on its expanding shell', () => {
  // The wavefront expands from the anchor; the probe sits on the radius it reaches at t = 1 s.
  const speed = 9;
  const { runtime } = FrameFor('RadialBurst', {
    Radius: 14, Duration: 'Continuous',
    Params: { Strength: 20, WaveSpeed: speed, ShellWidth: 0.9, StartRadius: 0.5, Decay: 0.65 },
  });
  const onShell = EvaluateAt(runtime, [speed, 0, 0], 1).Acceleration;
  const aheadOfShell = EvaluateAt(runtime, [Math.min(13.5, speed * 2), 0, 0], 1).Acceleration;
  assert.ok(onShell[0] > 0.5, `expected an outward push on the shell, got ${onShell[0]}`);
  assert.ok(M.Length(aheadOfShell) < M.Length(onShell) * 0.15);
});
test('Boundary adhesion points at the wall from inside', () => {
  const { runtime } = FrameFor('BoundaryAdhesion', { Radius: 5, Params: { Strength: 12, TargetDistance: 0.4, Softness: 0.6, NormalBias: 0 } });
  const a = EvaluateAt(runtime, [2, 0, 0], 0.5).Acceleration;
  assert.ok(a[0] > 0, `adhesion should pull outward toward the wall, got ${a[0]}`);
});
test('Flow channel separation: velocity laws never write acceleration', () => {
  for (const law of FORCE_LAWS.filter((l) => l.Channel === 'velocity')) {
    const { runtime } = FrameFor(law.Id);
    const sample = EvaluateAt(runtime, [0.4, 0.3, 0.2], 0.6);
    assert.ok(M.Length(sample.Acceleration) < 1e-9, `${law.Id} wrote acceleration`);
  }
});
test('Acceleration laws never write velocity', () => {
  for (const law of FORCE_LAWS.filter((l) => l.Channel === 'acceleration')) {
    const { runtime } = FrameFor(law.Id);
    const sample = EvaluateAt(runtime, [0.4, 0.3, 0.2], 0.6);
    assert.ok(M.Length(sample.Velocity) < 1e-9, `${law.Id} wrote velocity`);
  }
});
test('Every law produces a finite sample at its anchor', () => {
  for (const law of FORCE_LAWS) {
    const { runtime } = FrameFor(law.Id);
    for (const p of [[0, 0, 0], [0.01, 0.02, 0.03], [1.5, -0.7, 0.9]]) {
      const sample = EvaluateAt(runtime, p, 1.3, { Ambient: [1.2, 0.4, -0.3] });
      assert.ok(finite(sample.Velocity) && finite(sample.Acceleration) && Number.isFinite(sample.Damping), `${law.Id} non-finite at ${p}`);
      assert.ok(M.Length(sample.Acceleration) < 1e6, `${law.Id} acceleration exploded`);
    }
  }
});
test('Law strength scales the response monotonically', () => {
  for (const law of FORCE_LAWS.filter((l) => l.Id !== 'Drag')) {
    const schema = law.Params.find((p) => p.Key === 'Strength');
    const low = FrameFor(law.Id, { Params: { ...SanitizeParams(law.Id, {}), Strength: schema.Min + (schema.Max - schema.Min) * 0.2 } }).runtime;
    const high = FrameFor(law.Id, { Params: { ...SanitizeParams(law.Id, {}), Strength: schema.Min + (schema.Max - schema.Min) * 0.8 } }).runtime;
    const probe = [0.7, 0.5, 0.6];
    const a = EvaluateAt(low, probe, 0.8, { Ambient: [1, 0, 0] });
    const b = EvaluateAt(high, probe, 0.8, { Ambient: [1, 0, 0] });
    const magA = M.Length(a.Velocity) + M.Length(a.Acceleration);
    const magB = M.Length(b.Velocity) + M.Length(b.Acceleration);
    assert.ok(magB >= magA - 1e-6, `${law.Id} does not respond to Strength (${magA} → ${magB})`);
  }
});
test('Linear wind gust period is honoured', () => {
  const { runtime } = FrameFor('LinearWind', {
    Radius: 30, Height: 30, Attenuation: 'Constant',
    Params: { Strength: 10, Shear: 0, Veer: 0, GustAmplitude: 0.5, GustFrequency: 0.5, Turbulence: 0 },
  });
  const at = (t) => M.Length(EvaluateAt(runtime, [0, 0, 0], t).Velocity);
  const samples = Array.from({ length: 40 }, (_, i) => at(i / 8));
  assert.ok(Math.max(...samples) - Math.min(...samples) > 5, 'gust should modulate the speed');
});

group('Formula field');
test('Default program compiles and samples', () => {
  const program = CompileFieldProgram('fx = x + 1\nfy = y * 2\nfz = sin(z)');
  assert.ok(program.Ok);
  const v = program.Sample(2, 3, 0, 0);
  assert.deepEqual(v.map((c) => +c.toFixed(6)), [3, 6, 0]);
});
test('Missing channels default to zero', () => {
  const program = CompileFieldProgram('fx = 1');
  assert.ok(program.Ok);
  assert.deepEqual(program.Sample(0, 0, 0, 0), [1, 0, 0]);
});
test('Operator precedence and associativity follow mathematics', () => {
  assert.equal(CompileFieldProgram('fx = 1 + 2 * 3').Sample(0, 0, 0, 0)[0], 7);
  assert.equal(CompileFieldProgram('fx = 2 ^ 3 ^ 2').Sample(0, 0, 0, 0)[0], 512);
  assert.equal(CompileFieldProgram('fx = -2 ^ 2').Sample(0, 0, 0, 0)[0], -4);
  assert.equal(CompileFieldProgram('fx = (1 + 2) * 3').Sample(0, 0, 0, 0)[0], 9);
  assert.ok(Math.abs(CompileFieldProgram('fx = 1 / 4').Sample(0, 0, 0, 0)[0] - 0.25) < 1e-9);
});
test('Division by zero yields zero rather than a non-finite value', () => {
  const program = CompileFieldProgram('fx = 1 / x');
  assert.equal(program.Sample(0, 0, 0, 0)[0], 0);
});
test('Unknown identifiers report a tagged error and zero the channel', () => {
  const program = CompileFieldProgram('fx = wobble(y)');
  assert.equal(program.Ok, false);
  assert.ok(program.Errors[0].Message.includes('Unknown function'));
  assert.equal(program.Sample(1, 1, 1, 1)[0], 0);
});
test('Unbalanced parentheses report an error', () => {
  const program = CompileFieldProgram('fx = sin(y');
  assert.equal(program.Ok, false);
  assert.ok(program.Errors.length > 0);
});
test('Comments, semicolons and case-insensitive channels are accepted', () => {
  const program = CompileFieldProgram('# comment\nFX = 2; // trailing\nfy = 3');
  assert.ok(program.Ok, JSON.stringify(program.Errors));
  assert.deepEqual(program.Sample(0, 0, 0, 0), [2, 3, 0]);
});
test('Wrong argument count is rejected', () => {
  const program = CompileFieldProgram('fx = clamp(1)');
  assert.equal(program.Ok, false);
});
test('Every formula preset compiles cleanly', () => {
  for (const preset of FORMULA_PRESETS) {
    const program = CompileFieldProgram(preset.Code);
    assert.ok(program.Ok, `${preset.Id}: ${JSON.stringify(program.Errors)}`);
    const v = program.Sample(1.5, -0.5, 0.8, 2.2);
    assert.ok(finite(v), preset.Id);
  }
});
test('Formula field respects its envelope like any other generator', () => {
  const { runtime } = FrameFor('CustomFormula', { HalfExtents: [4, 4, 4] });
  const inside = EvaluateAt(runtime, [1, 0, 0], 0.5).Velocity;
  const outside = EvaluateAt(runtime, [0, 0, 40], 0.5).Velocity;
  assert.ok(M.Length(inside) >= 0);
  assert.equal(M.Length(outside), 0);
});
test('Deeply nested expressions stay finite', () => {
  const program = CompileFieldProgram('fx = ' + 'sin('.repeat(24) + 'x' + ')'.repeat(24));
  assert.ok(program.Ok);
  assert.ok(Number.isFinite(program.Sample(0.7, 0, 0, 0)[0]));
});

group('Streamlines and quantities');
test('RK4 streamline advances along a uniform field', () => {
  const line = M.IntegrateStreamline(() => [0, 0, 1], [0, 0, 0], { Steps: 20, StepSize: 0.25 });
  assert.equal(line.length, 21);
  assert.ok(Math.abs(line[20][2] - 5) < 1e-6);
  assert.ok(Math.abs(line[20][0]) < 1e-9);
});
test('RK4 streamline turns in a rotational field', () => {
  const line = M.IntegrateStreamline((p) => [-p[1], p[0], 0], [2, 0, 0], { Steps: 90, StepSize: 0.05 });
  const last = line[line.length - 1];
  assert.ok(Math.hypot(last[0], last[1]) > 1.5, 'radius should be preserved');
  assert.ok(last[1] > 0.5, 'should have curved toward +Y');
});
test('Streamline integration stops at the domain bounds', () => {
  const line = M.IntegrateStreamline(() => [1, 0, 0], [0, 0, 0], { Steps: 400, StepSize: 0.1, Bounds: { Min: [-1, -1, -1], Max: [1, 1, 1] } });
  assert.ok(line.length < 20);
});
test('Zero field produces a single-point streamline', () => {
  const line = M.IntegrateStreamline(() => [0, 0, 0], [1, 1, 1], { Steps: 10 });
  assert.equal(line.length, 1);
});
test('Quantity channels all return finite scalars', () => {
  const doc = DefaultDocument();
  const runtime = PrepareFrame(doc.Sources, 1.2, doc.Domain);
  for (const quantity of ['Speed', 'Acceleration', 'Curl', 'Divergence', 'Influence']) {
    for (const p of [[0, 0, 0], [3, -2, 1], [-8, 6, 2]]) {
      const value = QuantityAt(runtime, p, 1.2, quantity);
      assert.ok(Number.isFinite(value), `${quantity} at ${p}`);
    }
  }
});
test('Influence never exceeds the number of sources', () => {
  const doc = DefaultDocument();
  const runtime = PrepareFrame(doc.Sources, 0.5, doc.Domain);
  const sample = EvaluateAt(runtime, [0, 0, 0], 0.5);
  assert.ok(sample.Influence <= doc.Sources.length + 1e-6);
});
test('Suggested maximum is positive and finite', () => {
  const doc = DefaultDocument();
  const runtime = PrepareFrame(doc.Sources, 0.5, doc.Domain);
  for (const quantity of ['Speed', 'Curl', 'Acceleration', 'Divergence']) {
    const peak = SuggestMaximum(runtime, doc.Domain, 0.5, quantity);
    assert.ok(Number.isFinite(peak) && peak > 0, quantity);
  }
});

group('Probe advection');
test('Motes stay inside the domain and remain finite', () => {
  const doc = DefaultDocument();
  const runtime = PrepareFrame(doc.Sources, 0, doc.Domain);
  const motes = Array.from({ length: 60 }, (_, i) => ({
    P: [((i % 10) - 5) * 2, (Math.floor(i / 10) - 3) * 2, ((i % 3) - 1) * 1.5],
    V: [0, 0, 0],
    Age: 0,
  }));
  for (let step = 0; step < 240; step++) {
    for (const mote of motes) {
      AdvectMote(runtime, mote, 1 / 60, step / 60, { Coupling: 2.6 });
      assert.ok(finite(mote.P) && finite(mote.V), `mote became non-finite at step ${step}`);
      for (let axis = 0; axis < 3; axis++) {
        const lo = doc.Domain.Min[axis] - 30;
        const hi = doc.Domain.Max[axis] + 30;
        assert.ok(mote.P[axis] > lo && mote.P[axis] < hi, `mote escaped on axis ${axis}`);
      }
    }
  }
});
test('A still mote picks up the medium velocity', () => {
  const { runtime } = FrameFor('LinearWind', { Radius: 30, Height: 30, Attenuation: 'Constant', Params: { Strength: 8, Shear: 0, Veer: 0, GustAmplitude: 0, GustFrequency: 0.2, Turbulence: 0 } });
  const mote = { P: [0, 0, 0], V: [0, 0, 0], Age: 0 };
  for (let i = 0; i < 120; i++) AdvectMote(runtime, mote, 1 / 60, 0, { Coupling: 3 });
  assert.ok(Math.abs(mote.V[2]) > 2, `mote should have accelerated along +Z, got ${mote.V[2]}`);
});

group('Document round-trip');
test('Default document survives sanitise → JSON → parse', () => {
  const doc = DefaultDocument();
  const back = DocumentFromJson(DocumentToJson(doc));
  assert.equal(back.Sources.length, doc.Sources.length);
  assert.deepEqual(back.Sources.map((s) => s.Law), doc.Sources.map((s) => s.Law));
  assert.deepEqual(back.Sources.map((s) => s.Params), doc.Sources.map((s) => s.Params));
});
test('Unknown laws and malformed values are repaired, not trusted', () => {
  const doc = SanitizeDocument({
    Sources: [
      { Law: 'NotALaw', Position: [1e9, 0, 'x'], Params: { Strength: -50 } },
      { Law: 'Drag', Envelope: 'Nope', Attenuation: 'Nope', Duration: 'Nope', Motion: 'Nope' },
    ],
    Overlay: { ArrowScale: 'huge', ShowVectors: 'yes' },
    Domain: { Min: [1, 2], Max: [3, 4, 5] },
  });
  assert.equal(doc.Sources[0].Law, 'LinearWind');
  assert.ok(Math.abs(doc.Sources[0].Position[0]) <= 500);
  assert.ok(Number.isFinite(doc.Sources[0].Position[2]));
  assert.equal(doc.Sources[1].Envelope, 'Sphere');
  assert.equal(typeof doc.Overlay.ArrowScale, 'number');
  assert.equal(doc.Overlay.ShowVectors, true);
  assert.equal(doc.Domain.Min.length, 3);
});
test('Cascade text export lists every source and its parameters', () => {
  const text = DocumentToCascadeText(DefaultDocument());
  assert.ok(text.includes('[Domain]'));
  assert.equal((text.match(/\[\[Source\]\]/g) || []).length, DefaultDocument().Sources.length);
  assert.ok(text.includes('[Source.Params]'));
});
test('Rejected foreign format is reported', () => {
  assert.throws(() => DocumentFromJson(JSON.stringify({ Format: 'SomethingElse', Sources: [] })));
  assert.throws(() => DocumentFromJson(JSON.stringify({ Format: 'FrontierForceField' })));
});

group('Motion paths');
test('Static sources never move', () => {
  const source = { Motion: 'Static', Position: [1, 2, 3] };
  assert.deepEqual(M.MotionAnchorAt(source, 0), [1, 2, 3]);
  assert.deepEqual(M.MotionAnchorAt(source, 99), [1, 2, 3]);
});
test('Orbiting sources stay on their circle', () => {
  const source = { Motion: 'CircularOrbit', Position: [0, 0, 0], Direction: [0, 0, 1], SwirlRadius: 3, OrbitSpeed: 0.3 };
  for (let i = 0; i < 12; i++) {
    const p = M.MotionAnchorAt(source, i * 0.7);
    assert.ok(Math.abs(Math.hypot(p[0], p[1]) - 3) < 1e-6);
  }
});
test('Ping-pong paths return to their origin', () => {
  const source = { Motion: 'LinearAB', Position: [0, 0, 0], Direction: [1, 0, 0], PathLength: 8, OrbitSpeed: 0.5, LoopPolicy: 'PingPong' };
  // OrbitSpeed 0.5 with `speed · t · 0.25` means the half-cycle takes 2 s: far end at 2 s, home at 4 s.
  // OrbitSpeed 0.5 with `speed · t · 0.25` advances a quarter of the path per second: far end at 4 s, home at 8 s.
  const start = M.MotionAnchorAt(source, 0);
  const far = M.MotionAnchorAt(source, 4);
  const back = M.MotionAnchorAt(source, 8);
  assert.ok(Math.abs(start[0]) < 1e-9, `start ${start[0]}`);
  assert.ok(Math.abs(far[0] - 8) < 1e-6, `far end ${far[0]}`);
  assert.ok(Math.abs(back[0]) < 1e-6, `return ${back[0]}`);
});

group('Colour ramps');
test('Ramps clamp outside the unit interval', () => {
  for (const name of M.ColormapNames) {
    assert.deepEqual(M.SampleColormap(name, -3), M.SampleColormap(name, 0));
    assert.deepEqual(M.SampleColormap(name, 4), M.SampleColormap(name, 1));
  }
});
test('Ramp samples are finite and in range', () => {
  for (const name of M.ColormapNames) {
    for (let i = 0; i <= 10; i++) {
      const c = M.SampleColormap(name, i / 10);
      assert.ok(c.every((v) => v >= 0 && v <= 255), name);
    }
  }
});
test('Marching squares finds a closed contour around a peak', () => {
  const N = 9;
  const grid = new Float32Array(N * N);
  for (let r = 0; r < N; r++) {
    for (let c = 0; c < N; c++) grid[r * N + c] = Math.hypot(r - 4, c - 4);
  }
  const segments = M.MarchingSquares(grid, N, N, 2.2);
  assert.ok(segments.length >= 8, `expected a ring of segments, got ${segments.length}`);
  for (const [a] of segments) assert.ok(Number.isFinite(a[0]) && Number.isFinite(a[1]));
});

group('Beaufort');
test('Beaufort bands agree with the met scale', () => {
  assert.equal(M.Beaufort(0.2).Name, 'Calm');
  assert.equal(M.Beaufort(12).Force, 6);
  assert.equal(M.Beaufort(33).Force, 12);
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  SUMMARY
//------------------------------------------------------------------------------------------------------------------------

process.stdout.write(`\n${failed === 0 ? 'PASS' : 'FAIL'} — ${passed} passed, ${failed} failed\n`);
if (failed > 0) {
  for (const f of failures) process.stdout.write(`  · ${f.name}: ${f.message.split('\n')[0]}\n`);
  process.exit(1);
}
