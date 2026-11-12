#!/usr/bin/env node
// Validation suite for Frontier Experimental Remastered · Photometric & Light Studio.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
    MeasuredIesProfileNames,
    PresetIesNames,
    LightCatalogue,
    MoonCatalogue,
    ParseIes,
    SampleIes,
    EvaluateIesIntensity,
    EncodeSyntheticIesFile,
    KelvinToRgb,
    ResolveEmissionRgb,
    ComputeFlux,
    CreateDefaultLightRig,
    CreateDefaultEnvironmentLights,
    SolveMoonMetrics
} from './Engine/PhotometryEngine.js';

import { EvaluateSolarEphemeris } from './Engine/CelestialAtmosphereModel.js';

const RootDir = path.dirname(fileURLToPath(import.meta.url));
let Passed = 0;
function Check(Name, Fn) {
    Fn();
    Passed++;
}

// 1. Validate all 7 measured LM-63 Type-C .ies profiles
for (const Name of MeasuredIesProfileNames) {
    Check(`LM-63 .ies parse + sample: ${Name}.ies`, () => {
        const Text = fs.readFileSync(path.join(RootDir, 'Profiles', `${Name}.ies`), 'utf8');
        const Parsed = ParseIes(Text);
        assert.ok(Parsed.Peak > 0, `${Name} peak > 0`);
        assert.ok(Parsed.Vertical.length >= 2 && Parsed.Horizontal.length >= 1);
        const Nadir = SampleIes(Parsed, 0, 0);
        const Oblique = SampleIes(Parsed, 45, 25);
        assert.ok(Nadir >= 0 && Nadir <= 1.001, `${Name} nadir bounded`);
        assert.ok(Oblique >= 0 && Oblique <= 1.001, `${Name} oblique bounded`);
    });
}

// 2. Validate analytical Project Zero IES presets + LM-63 round-trip export
for (const Preset of PresetIesNames) {
    Check(`Analytical Project Zero preset: ${Preset}`, () => {
        const Props = { profile: Preset, useMeasuredIes: false, cone: 58, lumens: 1650, multiplier: 1 };
        const V0 = EvaluateIesIntensity(Props, {}, 0, 0);
        const V25 = EvaluateIesIntensity(Props, {}, 0, 25);
        assert.ok(Number.isFinite(V0) && Number.isFinite(V25) && V0 >= 0 && V25 >= 0);
    });
}

Check('Synthetic .ies export round-trips cleanly through ParseIes', () => {
    const Exported = EncodeSyntheticIesFile('Test Batwing', {
        profile: 'Batwing', useMeasuredIes: false, cone: 64, lumens: 2200, multiplier: 1
    }, {});
    const Parsed = ParseIes(Exported);
    assert.ok(Parsed.Peak > 100);
    assert.equal(Parsed.Vertical[0], 0);
});

// 3. Validate all 7 Project Zero Light Types + flux computations + colour temperature
Check('All 7 light types in catalogue & default rig', () => {
    const Rig = CreateDefaultLightRig();
    const Types = new Set(Rig.map(L => L.Type));
    for (const Key of Object.keys(LightCatalogue)) {
        assert.ok(Types.has(Key), `Default rig includes ${Key}`);
    }
    for (const Light of Rig) {
        const Flux = ComputeFlux(Light.Type, Light.Props);
        assert.ok(Flux > 0, `${Light.Id} flux > 0`);
        const Rgb = ResolveEmissionRgb(Light.Props);
        assert.equal(Rgb.length, 3);
    }
    const Warm = KelvinToRgb(2200);
    const Cool = KelvinToRgb(10000);
    assert.ok(Warm[0] > Warm[2], 'Warm Kelvin red-biased');
    assert.ok(Cool[2] >= Cool[0], 'Cool Kelvin blue-biased');
});

// 4. Environment lights (HDRI Dome, Sun, Moon 6-world catalogue, Stars, Lens Flare, Fog, Post)
Check('Environment lights defaults + Solar & Moon solvers', () => {
    const Env = CreateDefaultEnvironmentLights();
    assert.ok(Env.EnvDome.Enabled && Env.Stars.Visible && Env.LensFlare.Enabled && Env.Post.Enabled);
    assert.equal(MoonCatalogue.length, 6);
    const M = SolveMoonMetrics(Env.Moon);
    assert.ok(M.IlluminationPct >= 0 && M.IlluminationPct <= 100);
    assert.ok(Math.abs(Math.hypot(...M.Direction) - 1) < 1e-4);

    const Sun = EvaluateSolarEphemeris({ Year: 2026, Month: 9, Day: 10, LocalHours: 12, UtcOffset: 2, LatitudeDeg: -26.19, LongitudeDeg: 28.32 });
    assert.ok(Sun.ElevationDeg > 50 && Sun.ElevationDeg < 70);
});

// 5. HTML DOM ID contract verification
Check('HTML DOM IDs match PhotometricEditor.js bindings', () => {
    const Html = fs.readFileSync(path.join(RootDir, 'index.html'), 'utf8');
    const Js = fs.readFileSync(path.join(RootDir, 'PhotometricEditor.js'), 'utf8');
    const RequiredIds = [...Js.matchAll(/getElementById\(['"]([A-Za-z0-9_-]+)['"]\)/g)].map(M => M[1]);
    assert.ok(RequiredIds.length >= 25);
    for (const Id of RequiredIds) {
        assert.ok(Html.includes(`id="${Id}"`), `index.html missing id="${Id}"`);
    }
});

// 6. Relative path verification for local assets
Check('Local vendor, fonts, CSS, and engine modules exist on disk', () => {
    const ExpectedFiles = [
        'PhotometricEditor.css',
        'PhotometricEditor.js',
        'StudioControls.js',
        'Serve.py',
        'Engine/PhotometryEngine.js',
        'Engine/ProjectZeroLightProjection.js',
        'Engine/PhotometricDiagrams.js',
        'Engine/PhotometricViewport.js',
        'Engine/CelestialAtmosphereModel.js',
        'Icons/editor-point-light.svg',
        'Icons/editor-spotlight.svg',
        'Icons/editor-dome-light.svg',
        'Icons/editor-area-light.svg',
        'Icons/light-area-2d.svg',
        'Icons/light-point-2d.svg',
        'Icons/slate-ring-light.svg',
        'Icons/sun.svg',
        'Icons/moon.svg',
        'Icons/outliner-stars.svg',
        'Icons/lens-flare.svg',
        'Icons/sky-scattering.svg',
        'Icons/environment-exposure.svg',
        'Textures/Moons/luna_2k.jpg',
        'Textures/Moons/ember_2k.jpg',
        'Textures/Moons/glacier_2k.jpg',
        'Textures/Moons/sulfur_2k.jpg',
        'Textures/Moons/shroud_2k.jpg',
        'Textures/Moons/shard_2k.jpg',
        'Vendor/three.esm.min.js',
        'Vendor/orbit-controls.esm.min.js',
        'Fonts/dm-sans-latin-300-normal.woff2',
        'Fonts/dm-sans-latin-400-normal.woff2',
        'Fonts/dm-sans-latin-500-normal.woff2'
    ];
    for (const Rel of ExpectedFiles) {
        assert.ok(fs.existsSync(path.join(RootDir, Rel)), `Missing file: ${Rel}`);
    }
});

console.log(`PASS ${Passed} Photometric & Light Studio validation checks.`);
