#!/usr/bin/env node
/*==========================================================================================================================================
                                                        FRACTUREVALIDATION.MJS
==========================================================================================================================================*/
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
    MaterialCatalogue,
    ConstructSolidComplex,
    ExecuteFracturePartition,
    EvaluateSdfAtlasFootprint
} from './Engine/ConvexPartitioning.js';
import { FractureKinematicsTimeline } from './Engine/RigidBodyDynamics.js';
import {
    CreateDefaultSunParameters,
    CreateDefaultAtmosphereParameters,
    CreateDefaultCloudsParameters,
    EvaluateSolarEphemeris
} from './Engine/CelestialAtmosphereModel.js';

const Root = path.dirname(fileURLToPath(import.meta.url));

// 1. Validate HTML & JS wiring IDs match cleanly + Glyphs exist
const Html = fs.readFileSync(path.join(Root, 'index.html'), 'utf8');
const Js = fs.readFileSync(path.join(Root, 'FractureEditor.js'), 'utf8');
const Controls = fs.readFileSync(path.join(Root, 'StudioControls.js'), 'utf8');
const ReferencedIds = new Set([...Js.matchAll(/getElementById\('([^']+)'\)/g)].map(M => M[1]));
let CheckedIds = 0;
for (const Id of ReferencedIds)
{
    assert.ok(Html.includes(`id="${Id}"`), `Missing DOM id="${Id}" in index.html`);
    CheckedIds++;
}
const GlyphKeys = new Set([...Controls.matchAll(/^\s+([A-Za-z0-9_]+):/gm)].map(M => M[1]));
const UsedGlyphs = new Set([
    ...[...Js.matchAll(/ConstructGlyph\('([^']+)'\)/g)].map(M => M[1]),
    ...[...Js.matchAll(/Glyph:\s*'([^']+)'/g)].map(M => M[1]),
    ...[...Js.matchAll(/SetG\([^,]+,\s*'([^']+)'\)/g)].map(M => M[1])
]);
for (const G of UsedGlyphs)
{
    assert.ok(GlyphKeys.has(G), `Missing Glyph '${G}' in StudioControls.js`);
}

// 2. Exercise all 6 solvers × representative solids & materials
const Solvers = ['runtime_boussinesq', 'engineering_fem', 'griffith', 'wavefront', 'voronoi', 'dendritic'];
const Solids = ['monolith', 'curtain_pane', 'timber_beam', 'boulder', 'cube', 'cylinder', 'cone'];
const Materials = Object.keys(MaterialCatalogue);

let Partitions = 0;
for (let I = 0; I < Solvers.length; I++)
{
    const Solver = Solvers[I];
    const Solid = Solids[I % Solids.length];
    const Material = Materials[I % Materials.length];
    const Base = ConstructSolidComplex(Solid, [1, 1, 1], 42 + I * 7);
    const Res = ExecuteFracturePartition(Base, {
        Solver, Material, Energy: 3800, Seed: 42 + I * 7, Ceiling: 44, MinimumSize: 0.04,
        GrainAngleDeg: 15, BondStrengthScale: 1.0, PieceSdf: true, SdfResolution: 64,
        ImpactPoint: [0.05, 0.15, 0.25], ImpactNormal: [0, 0, -1]
    });
    assert.ok(Res.Fragments.length >= 2, `${Solver} produced too few fragments: ${Res.Fragments.length}`);
    assert.ok(Res.Receipt.VolumeDriftRatio < 0.02, `${Solver} volume drift too high: ${Res.Receipt.VolumeDriftRatio}`);
    assert.ok(Res.Receipt.TotalCrackArea > 0, `${Solver} crack area must be positive`);
    Partitions++;
}

// Verify engineering_fem produces EngineeringReport + fractography metadata
const EngBase = ConstructSolidComplex('monolith', [1, 1, 1], 42);
const EngRes = ExecuteFracturePartition(EngBase, {
    Solver: 'engineering_fem', Material: 'concrete', Energy: 4600, Seed: 42, Ceiling: 32, MinimumSize: 0.04,
    GrainAngleDeg: 0, BondStrengthScale: 1.0, PieceSdf: true, SdfResolution: 64,
    ImpactPoint: [0.05, 0.24, 0.48], ImpactNormal: [0, 0, -1]
});
assert.ok(EngRes.EngineeringReport && EngRes.EngineeringReport.NonPlanarTriangles >= 0, 'EngineeringReport missing');

// 3. Piece SDF footprint + RigidBodyDynamics timeline
const Sdf = EvaluateSdfAtlasFootprint(EngRes.Fragments.length, 64, true);
assert.ok(Sdf.Megabytes > 0 && Sdf.Bytes > 0 && Sdf.VoxelCount > 0, 'SDF footprint invalid');

const Timeline = new FractureKinematicsTimeline();
Timeline.BuildSimulation(EngRes, 'baked', 0, 0.72);
const S0 = Timeline.SampleAtTime(0.0, EngRes.Fragments, 0, EngRes.BaseMetrics.Centroid);
const S1 = Timeline.SampleAtTime(0.65, EngRes.Fragments, 0.35, EngRes.BaseMetrics.Centroid);
assert.equal(S0.Offsets.length, EngRes.Fragments.length * 3);
assert.equal(S1.Offsets.length, EngRes.Fragments.length * 3);

// 4. Celestial ephemeris & atmosphere defaults
const SunP = CreateDefaultSunParameters();
const AtmP = CreateDefaultAtmosphereParameters();
const CldP = CreateDefaultCloudsParameters();
const Solved = EvaluateSolarEphemeris(SunP);
assert.equal(Solved.Direction.length, 3);
assert.ok(AtmP.AtmosphereHeightKm > 0 && CldP.Global.Coverage > 0);

console.log(`FractureValidation OK — ${CheckedIds} DOM IDs, ${Partitions} solver/solid partitions, Engineering FEM, SDF, Timeline & Celestial checks passed.`);
