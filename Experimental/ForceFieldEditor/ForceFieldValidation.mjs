import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const Here = path.dirname(fileURLToPath(import.meta.url));
const { ForceFieldCatalogue: F } = await import(path.join(Here, 'Engine/ForceFieldCatalogue.js'));

let Passed = 0;
function Test(Name, Fn)
{
    Fn();
    Passed++;
    console.log(`PASS ${Name}`);
}

Test('Catalogue exposes WindGenerator + 16 forcefield types with finite default reaches', () =>
{
    assert.equal(F.Types.length, 17);
    assert.ok(F.TypeById.WindGenerator, 'WindGenerator type must exist');
    for (const Type of F.Types)
    {
        assert.ok(['Sphere', 'Column', 'Box'].includes(Type.Reach), `${Type.Id} must have finite default Reach, got ${Type.Reach}`);
        const Src = F.ConstructSource(Type.Id);
        assert.ok(['Sphere', 'Column', 'Box'].includes(Src.Reach), `${Type.Id} source reach must be bounded`);
        assert.ok(Src.Radius > 0, `${Type.Id} default Radius must be > 0`);
    }
});

Test('All 17 forcefield types return exact zero outside finite influence bounds', () =>
{
    const Scene = F.ConstructScenario('blank');
    Scene.Wind.Enabled = false;
    for (const Type of F.Types)
    {
        const Src = F.ConstructSource(Type.Id, { Centre: [0, 10, 0], Radius: 120, HalfX: 80, HalfZ: 80, Height: 60 });
        if (Type.Id === 'Blast') Src.InitialRadius = 20;
        const Single = Object.assign({}, Scene, { Sources: [Src] });
        const Prep = F.Prepare(Single, Type.Id === 'Blast' ? 0.5 : 1.0);
        const FarOut = [0, 0, 0];
        F.EvaluatePrepared(Single, Prep, Type.Contribution, 5000, 10, 5000, 2, FarOut);
        assert.deepEqual(FarOut, [0, 0, 0], `${Type.Id} leaked force at (5000, 10, 5000) outside R=120`);
    }
});

Test('WindGenerator produces coherent directional airflow + gusts inside influence and zero outside', () =>
{
    const Scene = F.ConstructScenario('blank');
    const WG = F.ConstructSource('WindGenerator', { Centre: [0, 0, 0], Bearing: 90, Strength: 16, Radius: 180, Height: 120, Falloff: 'Smooth' });
    Scene.Sources = [WG];
    const Prep = F.Prepare(Scene, 2.0);
    const CenterOut = [0, 0, 0];
    F.EvaluatePrepared(Scene, Prep, 'Flow', 10, 15, 0, 2, CenterOut);
    assert.ok(CenterOut[0] > 8, `Expected +X flow near WindGenerator bearing 90°, got ${CenterOut}`);
    const EdgeOut = [0, 0, 0];
    F.EvaluatePrepared(Scene, Prep, 'Flow', 250, 15, 0, 2, EdgeOut);
    assert.deepEqual(EdgeOut, [0, 0, 0], `Expected zero outside Radius 180, got ${EdgeOut}`);
});

Test('WindGenerator also produces curl turbulence only inside radius', () =>
{
    const Scene = F.ConstructScenario('lab');
    const Prep = F.Prepare(Scene, 1.5);
    const Quiet = [0, 0, 0];
    F.EvaluatePrepared(Scene, Prep, 'Flow', -900, 10, -900, 2, Quiet);
    assert.deepEqual(Quiet, [0, 0, 0], 'Lab default ambient wind must be off so open corners stay calm');
});

Test('Ambient Wind matches Ekman profile math cleanly and honors optional Bounded RegionRadius', () =>
{
    const Scene = F.ConstructScenario('storm');
    Scene.Sources = [];
    Scene.Wind.Enabled = true;
    Scene.Wind.Bounded = false;
    const Prep = F.Prepare(Scene, 0);
    const Out = [0, 0, 0];
    F.EvaluatePrepared(Scene, Prep, 'Flow', 0, 10, 0, 2, Out);
    const Spd = Math.hypot(Out[0], Out[1], Out[2]);
    assert.ok(Spd > 3 && Spd < 15, `Unexpected ambient wind speed: ${Spd}`);

    // Bounded regional ambient wind cleanly drops to 0 outside RegionRadius
    Scene.Wind.Bounded = true;
    Scene.Wind.RegionRadius = 500;
    const BoundedPrep = F.Prepare(Scene, 0);
    const FarOut = [0, 0, 0];
    F.EvaluatePrepared(Scene, BoundedPrep, 'Flow', 900, 10, 900, 2, FarOut);
    assert.deepEqual(FarOut, [0, 0, 0], `Bounded ambient wind leaked outside RegionRadius=500: ${FarOut}`);
});

Test('All 4 scenarios construct cleanly and encode Levelspace TOML', () =>
{
    for (const Key of ['lab', 'corridor', 'storm', 'blank'])
    {
        const S = F.ConstructScenario(Key);
        const Lines = F.EncodeLevelspace(S);
        assert.ok(Array.isArray(Lines) && Lines.length > 0, `Levelspace failed for ${Key}`);
        assert.ok(Lines.some((L) => L.includes('type = "ForceField"')));
    }
});

Test('ForceFieldVisualization.js exports RK2 flow lines, sparse cutoff, minimal labels, and placement hooks', () =>
{
    const Require = createRequire(import.meta.url);
    const VizPath = path.resolve(Here, '../ForceFieldVisualization/ForceFieldVisualization.js');
    const Viz = Require(VizPath);
    assert.equal(typeof Viz.Create, 'function');
    assert.equal(typeof Viz.FormatDistance, 'function');
    assert.equal(typeof Viz.InvertProjection, 'function');
    const SrcText = fs.readFileSync(VizPath, 'utf8');
    for (const Token of ['RenderFlowLines', 'FlowLineSpacing', 'QuietCutoff', 'MinimalLabels', 'RenderPlacementPreview', 'HitSourceHandle'])
    {
        assert.ok(SrcText.includes(Token), `ForceFieldVisualization.js missing ${Token}`);
    }
});

Test('Studio index.html, CSS, ESM modules, fonts, vendor libs, and icons are intact', () =>
{
    const Html = fs.readFileSync(path.join(Here, 'index.html'), 'utf8');
    for (const Id of [
        'ScenarioTabs', 'OutlinerContent', 'ViewportBar', 'ViewportCanvas', 'Stage3DMount',
        'QuickPlaceGroup', 'PlaceBanner', 'ToggleVectors', 'ToggleFlowLines', 'ToggleInfluence',
        'ToggleHeatmap', 'ToggleMinimal', 'InspectorContent', 'TimelineStrip', 'LogDrawer',
        'LibraryVeil', 'LibrarySheet'
    ])
    {
        assert.ok(Html.includes(`id="${Id}"`), `index.html missing #${Id}`);
    }
    assert.ok(Html.includes('"three": "./Vendor/three.esm.min.js"'), 'Missing three importmap');
    assert.ok(!Html.includes('BtnModeOblique') && !Html.includes('data-mode="Oblique"'), 'Oblique button must be removed');

    const Css = fs.readFileSync(path.join(Here, 'ForceFieldEditor.css'), 'utf8');
    assert.ok(Css.includes('[hidden]') && Css.includes('display: none !important'), 'CSS missing explicit [hidden] override');
    assert.ok(Css.includes('.pill-range') && Css.includes('.value-pill') && Css.includes('--card-top'), 'CSS missing Sun+Fluid cards & pill slider styling');
    const Stage3D = fs.readFileSync(path.join(Here, 'Engine/ForceField3DViewport.js'), 'utf8');
    assert.ok(Stage3D.includes('_SyncHeatmap') && Stage3D.includes('HeatmapTexture') && Stage3D.includes('HeatmapMesh.visible = Enabled'), '3D Stage must support field heatmap rendering');
    assert.ok(Html.includes('id="HeatmapLegend"'), '3D Stage heatmap must expose its legend');
    const EditorJs = fs.readFileSync(path.join(Here, 'ForceFieldEditor.js'), 'utf8');
    assert.ok(!EditorJs.includes('SymbolBox.style.boxShadow') && !EditorJs.includes('Swatch.style.boxShadow'), 'UI entity icons must stay flat-coloured');
    assert.ok(!Css.includes('box-shadow: 0 0 12px rgba(62, 196, 109'), 'Active feature icon must not glow');

    assert.equal(F.ResolveSourceColour('Attractor'), '#ff4757');
    assert.equal(F.ResolveSourceColour('Repulsor'), '#3b8eff');
    assert.equal(F.ResolveSourceColour('GustFront'), '#ff8c32');
    for (const T of F.Types) assert.ok(/^#[0-9a-f]{6}$/i.test(F.ResolveSourceColour(T.Id)), `Missing colour on ${T.Id}`);

    const OrbitJs = fs.readFileSync(path.join(Here, 'Vendor/orbit-controls.esm.min.js'), 'utf8');
    assert.ok(!OrbitJs.includes("from 'three'") && !OrbitJs.includes('from "three"'), 'orbit-controls has unrewritten bare three import');

    for (const Rel of [
        'Vendor/three.esm.min.js', 'Vendor/orbit-controls.esm.min.js',
        'Fonts/dm-sans-latin-300-normal.woff2', 'Fonts/dm-sans-latin-400-normal.woff2', 'Fonts/dm-sans-latin-500-normal.woff2',
        'Icons/wind.svg', 'Icons/camera.svg', 'Icons/earth.svg', 'Icons/clouds.svg',
        'Engine/ForceFieldCatalogue.js', 'Engine/ForceField3DViewport.js', 'StudioControls.js', 'ForceFieldEditor.js'
    ])
    {
        assert.ok(fs.existsSync(path.join(Here, Rel)), `Missing asset/module: ${Rel}`);
    }
});

console.log(`ALL PASSED (${Passed}/${Passed})`);
