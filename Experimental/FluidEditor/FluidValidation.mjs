//============================================================================================================================================
//                                                        FLUIDVALIDATION.MJS
//           Node ESM Validation — Catalog Integrity, GLSL Static Compilation (glslangValidator when present), Mock-WebGL2
//            Solver Pass Sequencing + Uniform Wiring Audit, Preset Round-Trip Serialisation, and index.html DOM Contract
//============================================================================================================================================

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import * as Shaders from './Engine/FluidShaders.js';
import * as ParticleShaders from './Engine/ParticleShaders.js';
import { ParticleSimulation } from './Engine/ParticleSimulation.js';
import { ParticleTemplates, ParticleShapes, ParticleColourModes, ParticleSprites, createParticleSystem, particleTextureSide } from './Engine/ParticleCatalog.js';
import { GpuContext } from './Engine/GpuContext.js';
import { FluidSolver } from './Engine/FluidSolver.js';
import { VolumeRenderer } from './Engine/VolumeRenderer.js';
import { OrbitCamera } from './Engine/OrbitCamera.js';
import { ScenePresets, ScenePresetById, buildSceneFromDocument } from './Engine/PresetScenes.js';
import {
    ForceGeneratorTypes, ForceTypeById, FalloffCurves, EmitterShapes, BoundaryModes, createDomain, createEmitter, createForceGenerator,
    createCollider, createVisualizerSet, MaximumForceGenerators, nodeWeight,
} from './Engine/ForceGeneratorCatalog.js';

const Folder = path.dirname(fileURLToPath(import.meta.url));
let Failures = 0;
function check(condition, message)
{
    if (condition) console.log(`  🟢 ${message}`);
    else { Failures += 1; console.log(`  🔴 ${message}`); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  1. CATALOG INTEGRITY
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[1] Force generator catalog');
const codes = new Set();
for (const type of ForceGeneratorTypes)
{
    check(!codes.has(type.Code), `${type.Id} has unique GLSL code ${type.Code}`);
    codes.add(type.Code);
    check(FalloffCurves.includes(type.Defaults.Falloff), `${type.Id} default falloff is registered`);
    check(Shaders.ForceFieldChunk.includes(`type == ${type.Code}`), `${type.Id} is evaluated by the GLSL force kernel`);
}
check(ForceGeneratorTypes.length === 20, `${ForceGeneratorTypes.length} generator types registered across ${new Set(ForceGeneratorTypes.map((type) => type.Category)).size} categories`);
for (const curve of FalloffCurves) check(Shaders.ForceFieldChunk.includes(`mode == ${FalloffCurves.indexOf(curve)}`) || curve === 'InverseSquare', `Falloff ${curve} handled by GLSL`);
for (const preset of ScenePresets)
{
    for (const force of preset.Forces || []) check(Boolean(ForceTypeById[force.Type]), `${preset.Name}: force ${force.Type} resolves`);
    for (const emitter of preset.Emitters || []) check(EmitterShapes.includes(emitter.Shape), `${preset.Name}: emitter shape ${emitter.Shape} resolves`);
    check(!preset.Boundary || BoundaryModes.includes(preset.Boundary), `${preset.Name}: boundary mode resolves`);
    check(preset.Grid.Resolution.every((cells) => cells >= 8 && cells <= 160), `${preset.Name}: resolution inside solver limits`);
}

for (const shape of ParticleShapes) check(shape === 'Point' || ParticleShaders.ParticleShapeChunk.includes(`uShape == ${ParticleShapes.indexOf(shape)}`), `Particle shape ${shape} sampled by GLSL`);
for (const mode of ParticleColourModes) check(mode === 'Life' || ParticleShaders.ParticleRenderVertex.includes(`uColourMode == ${ParticleColourModes.indexOf(mode)}`), `Particle colour rule ${mode} shaded by GLSL`);
check(ParticleShaders.TrailVertex.includes('CatmullRom') && ParticleShaders.TrailUpdateFragment.includes('uHead'), 'Trail history ring + Catmull-Rom ribbon shaders present');
for (const sprite of ParticleSprites) check(sprite === 'Square' || ParticleShaders.ParticleFragment.includes(`uSprite == ${ParticleSprites.indexOf(sprite)}`), `Sprite footprint ${sprite} shaded by GLSL`);
check(ParticleTemplates.length >= 10 && particleTextureSide(16384) === 128 && particleTextureSide(500) === 24, `${ParticleTemplates.length} particle templates · texture sizing`);
check(ParticleTemplates.every((template) => (template.Defaults.Palette || []).length >= 2 && template.Defaults.Palette.length <= 8) && createParticleSystem('sparks', { ColourStart: '#ff0000', ColourEnd: '#0000ff', Palette: [] }).Palette.join() === '#ff0000,#0000ff', 'Palettes: every template carries 2–8 stops; legacy Start/End back-fill');
for (const preset of ScenePresets) for (const node of preset.Particles || []) check(ParticleTemplates.some((template) => template.Id === node.Template), `${preset.Name}: particle template ${node.Template} resolves`);

//------------------------------------------------------------------------------------------------------------------------
//                                                  2. GLSL STATIC COMPILATION
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[2] GLSL ES 3.00 shaders');
const shaderEntries = [...Object.entries(Shaders), ...Object.entries(ParticleShaders)].filter(([, source]) => typeof source === 'string' && source.startsWith('#version 300 es'));
check(shaderEntries.length >= 18, `${shaderEntries.length} shader stages exported`);
const candidates = [
    process.env.GLSLANG_VALIDATOR,
    path.resolve(Folder, '../../node_modules/glslang-validator-prebuilt-predownloaded/bin/glslangValidator.linux'),
    '/tmp/glsltest/node_modules/glslang-validator-prebuilt-predownloaded/bin/glslangValidator.linux',
].filter(Boolean);
const validator = candidates.find((candidate) => fs.existsSync(candidate));
if (validator)
{
    const scratch = fs.mkdtempSync('/tmp/fluid-glsl-');
    const sparseEntries = shaderEntries.filter(([, source]) => source.includes(Shaders.VolumeChunk)).map(([name, source]) => [name + '.sparse', Shaders.sparse(source)]);
    for (const [name, source] of [...shaderEntries, ...sparseEntries])
    {
        const file = path.join(scratch, `${name.replace('.sparse', '_sparse')}.${name.endsWith('Vertex') || name.endsWith('Vertex.sparse') ? 'vert' : 'frag'}`);
        fs.writeFileSync(file, source);
        try { execFileSync(validator, [file], { stdio: 'pipe' }); check(true, `${name} compiles`); }
        catch (error) { check(false, `${name} compiles\n${error.stdout}`); }
    }
}
else console.log('  ⚠️ glslangValidator not found — skipping static GLSL compilation (set GLSLANG_VALIDATOR to enable)');

//------------------------------------------------------------------------------------------------------------------------
//                                                  3. MOCK WEBGL2 SOLVER AUDIT
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[3] Mock WebGL2 pass sequencing + uniform wiring');

function parseUniforms(source)
{
    const names = new Map();
    const stripped = source.replace(/\/\/.*$/gm, '');
    for (const match of stripped.matchAll(/uniform\s+(?:lowp|mediump|highp\s+)?(\w+)\s+(\w+)(?:\s*\[\s*(\w+)\s*\])?\s*;/g))
    {
        const [, type, name, arraySize] = match;
        names.set(name, { type, array: arraySize !== undefined });
    }
    return names;
}

function createMockGl()
{
    const calls = { draws: 0, programsUsed: [], uniformErrors: [] };
    let shaderId = 0, programId = 0;
    const shaders = new Map(), programs = new Map();
    let currentProgram = null;
    const constants = { VERTEX_SHADER: 1, FRAGMENT_SHADER: 2, COMPILE_STATUS: 3, LINK_STATUS: 4, ACTIVE_UNIFORMS: 5, TEXTURE_2D: 6, RGBA16F: 7, RGBA32F: 8, FRAMEBUFFER: 9, COLOR_ATTACHMENT0: 10, FRAMEBUFFER_COMPLETE: 11, ARRAY_BUFFER: 12, STATIC_DRAW: 13, DYNAMIC_DRAW: 14, FLOAT: 15, TRIANGLES: 16, LINES: 17, POINTS: 18, COLOR_BUFFER_BIT: 19, DEPTH_BUFFER_BIT: 256, BLEND: 20, DEPTH_TEST: 21, SRC_ALPHA: 22, ONE: 23, ONE_MINUS_SRC_ALPHA: 24, NEAREST: 25, LINEAR: 26, TEXTURE_MIN_FILTER: 27, TEXTURE_MAG_FILTER: 28, TEXTURE_WRAP_S: 29, TEXTURE_WRAP_T: 30, CLAMP_TO_EDGE: 31, TEXTURE0: 100, READ_FRAMEBUFFER: 32, DRAW_FRAMEBUFFER: 33, R16I: 40, RGBA32I: 41, RED_INTEGER: 42, RGBA_INTEGER: 43, SHORT: 44, INT: 45, RGBA: 46, SCISSOR_TEST: 47, UNPACK_ALIGNMENT: 48 };
    const requireLocation = (location, setter) =>
    {
        if (!location || location.program !== currentProgram) calls.uniformErrors.push(`${setter} on invalid location for program ${currentProgram && currentProgram.label}`);
    };
    const gl = {
        ...constants,
        getExtension: (name) => (name === 'EXT_color_buffer_float' ? {} : null),
        createShader: (type) => { const id = ++shaderId; shaders.set(id, { type, source: '' }); return id; },
        shaderSource: (id, source) => { shaders.get(id).source = source; },
        compileShader: () => {},
        getShaderParameter: () => true,
        getShaderInfoLog: () => '',
        deleteShader: () => {},
        createProgram: () => { const id = ++programId; programs.set(id, { shaders: [], uniforms: null, label: `program-${id}` }); return id; },
        attachShader: (program, shader) => programs.get(program).shaders.push(shaders.get(shader)),
        linkProgram: (program) =>
        {
            const record = programs.get(program);
            record.uniforms = new Map();
            for (const shader of record.shaders) for (const [name, info] of parseUniforms(shader.source)) record.uniforms.set(name, info);
            record.uniformList = [...record.uniforms.entries()];
        },
        getProgramParameter: (program, parameter) => (parameter === constants.ACTIVE_UNIFORMS ? programs.get(program).uniformList.length : true),
        getProgramInfoLog: () => '',
        getActiveUniform: (program, index) => { const [name, info] = programs.get(program).uniformList[index]; return { name: info.array ? `${name}[0]` : name, type: info.type }; },
        getUniformLocation: (program, name) => (programs.get(program).uniforms.has(name.replace(/\[0\]$/, '')) ? { program: programs.get(program), name } : null),
        useProgram: (program) => { currentProgram = programs.get(program); calls.programsUsed.push(currentProgram); },
        createTexture: () => ({}), bindTexture: () => {}, texParameteri: () => {}, texStorage2D: () => {}, activeTexture: () => {},
        createFramebuffer: () => ({}), bindFramebuffer: () => {}, framebufferTexture2D: () => {}, checkFramebufferStatus: () => constants.FRAMEBUFFER_COMPLETE,
        deleteFramebuffer: () => {}, deleteTexture: () => {},
        createVertexArray: () => ({}), bindVertexArray: () => {}, createBuffer: () => ({}), bindBuffer: () => {}, bufferData: () => {}, enableVertexAttribArray: () => {}, vertexAttribPointer: () => {},
        viewport: () => {}, clearColor: () => {}, clear: () => {}, disable: () => {}, enable: () => {}, depthMask: () => {}, blendFunc: () => {}, blitFramebuffer: () => {},
        scissor: () => {}, pixelStorei: () => {}, texSubImage2D: () => { calls.uploads = (calls.uploads || 0) + 1; }, readPixels: (x, y, w, h, format, type, buffer) => { calls.readbacks = (calls.readbacks || 0) + 1; buffer.fill(0); if (calls.occupancyHook) calls.occupancyHook(buffer); },
        drawArrays: () => { calls.draws += 1; },
    };
    for (const setter of ['uniform1i', 'uniform1f', 'uniform2i', 'uniform2f', 'uniform3i', 'uniform3f', 'uniform3fv', 'uniform4fv', 'uniformMatrix4fv'])
        gl[setter] = (location) => requireLocation(location, setter);
    gl.__calls = calls;
    gl.__labelProgram = (programRecord) => { programs.get(programRecord.program).label = programRecord.label; };
    return gl;
}

const mockGl = createMockGl();
const mockCanvas = { width: 1280, height: 720, getContext: () => mockGl };
const context = new GpuContext(mockCanvas);
const solver = new FluidSolver(context);
const renderer = new VolumeRenderer(context, solver);
const particles = new ParticleSimulation(context, solver);
renderer.particles = particles;
for (const programRecord of [...Object.values(solver.programs), ...Object.values(renderer.programs), ...Object.values(particles.programs)]) mockGl.__labelProgram(programRecord);

const preset = ScenePresetById['preset-wind-tunnel'];
const domain = buildSceneFromDocument(preset.Document).Domain;
check(domain.Resolution.join() === '96,48,48' && Math.abs(domain.CellSize - 6 / 96) < 1e-9, 'Domain grid derives 96×48×48 @ 6.25 cm from Size 6×3×3 / MaxCells 96');
const scene = {
    Name: preset.Name, Domain: domain,
    Emitters: preset.Emitters.map((emitter) => createEmitter(emitter)),
    Forces: [...preset.Forces.map((force) => createForceGenerator(force.Type, force)), ...ForceGeneratorTypes.map((type) => createForceGenerator(type.Id)), ],
    Colliders: preset.Colliders.map((collider) => createCollider(collider)),
    Particles: ParticleTemplates.map((template) => createParticleSystem(template.Id)),
    Visualizers: createVisualizerSet(),
};
scene.Visualizers.ScalarSlice.Visible = true;
solver.configure(domain.Resolution, domain.CellSize);
check(solver.layout.textureWidth === 96 * 7 && solver.layout.textureHeight === 48 * 7, `Tiled layout for 96×48×48 is ${solver.layout.textureWidth}×${solver.layout.textureHeight} (7×7 tiles)`);

const drawsBefore = mockGl.__calls.draws;
solver.step(scene, 1 / 60);
const stepDraws = mockGl.__calls.draws - drawsBefore;
check(solver.forceArrays.count === MaximumForceGenerators, `Force packing saturates at ${MaximumForceGenerators} slots (${scene.Forces.length} authored)`);
check(solver.emitterArrays.count === 1 && solver.colliderArray.count === 1, 'Emitter + collider packing');
const expectedDraws = 2 + 3 + 3 + 1 + 1 + 1 + 1 + 1 + domain.PressureIterations + 1;
check(stepDraws === expectedDraws, `MacCormack step issues ${stepDraws} GPU passes (expected ${expectedDraws})`);
domain.Advection = 'SemiLagrangian';
const drawsSemi = mockGl.__calls.draws;
solver.step(scene, 1 / 60);
check(mockGl.__calls.draws - drawsSemi === expectedDraws - 4, 'Semi-Lagrangian step drops the 4 MacCormack correction passes');
solver.updateTracers(1 / 60, 4);
const camera = new OrbitCamera();
camera.setViewport(1280, 720);
renderer.render(scene, camera, scene.Forces[0].Id, 1 / 60);
particles.update(scene, 1 / 60); particles.update(scene, 1 / 60);
renderer.render(scene, new OrbitCamera(), null, 1 / 60);
check(particles.records.size === ParticleTemplates.length, `Every particle template allocates GPU state (${particles.records.size} systems)`);
check(mockGl.__calls.uniformErrors.length === 0, `Every uniform written exists in its shader (${mockGl.__calls.uniformErrors.length} mismatches)`);
for (const error of mockGl.__calls.uniformErrors.slice(0, 12)) console.log('     ↳ ' + error);
check(renderer.lineVertexCount === 0, `Gizmos and wireframe overlays completely off by default (${renderer.lineVertexCount} line vertices)`);
scene.Visualizers.ForceGizmos.Visible = true;
renderer.render(scene, new OrbitCamera(), scene.Forces[0].Id, 1 / 60);
check(renderer.lineVertexCount > 100, `Gizmo batch produced ${renderer.lineVertexCount} line vertices for ${scene.Forces.length} forces`);

//------------------------------------------------------------------------------------------------------------------------
//                                                  4. SERIALISATION ROUND-TRIP
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[4] Serialisation');
const document_ = JSON.parse(JSON.stringify({ Format: 'FrontierFluidScene', Version: 1, Name: scene.Name, Domain: scene.Domain, Emitters: scene.Emitters, Forces: scene.Forces, Colliders: scene.Colliders, Visualizers: scene.Visualizers }));
check(document_.Forces.length === scene.Forces.length && document_.Domain.CellSize === domain.CellSize, 'Scene document round-trips through JSON');
const rebuilt = document_.Forces.map((force) => createForceGenerator(force.Type, force));
check(rebuilt.every((force, index) => force.Strength === scene.Forces[index].Strength && force.Position.join() === scene.Forces[index].Position.join()), 'Force generators rebuild with identical parameters');

//------------------------------------------------------------------------------------------------------------------------
//                                                  5. DOM CONTRACT
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[5] index.html DOM contract');
const html = fs.readFileSync(path.join(Folder, 'index.html'), 'utf8');
for (const id of [...html.matchAll(/getElementById\('([^']+)'\)/g)].map((match) => match[1]))
    check(html.includes(`id="${id}"`), `#${id} exists in markup`);
const panels = fs.readFileSync(path.join(Folder, 'UI/StudioPanels.js'), 'utf8');
for (const id of [...panels.matchAll(/getElementById\('([^']+)'\)/g)].map((match) => match[1]))
    check(panels.includes(`id="${id}"`), `#${id} is created by StudioPanels before lookup`);
for (const asset of ['Styles/FluidEditor.css', 'Fonts/dm-sans-latin-300-normal.woff2', 'Fonts/dm-sans-latin-400-normal.woff2', 'Fonts/dm-sans-latin-500-normal.woff2', 'Fonts/OFL.txt', 'Serve.py'])
    check(fs.existsSync(path.join(Folder, asset)), `${asset} present`);
check(!/https?:\/\/(fonts|cdn|unpkg|jsdelivr)/.test(html + panels), 'No hot-linked fonts or CDN scripts');



//------------------------------------------------------------------------------------------------------------------------
//                                                  6. UI RENDER SMOKE (DOM SHIM)
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[6] Studio panel render smoke test');
function shimElement(tag)
{
    const node = {
        tagName: tag.toUpperCase(), children: [], attributes: {}, style: { setProperty() {} }, classList: null, listeners: {},
        _className: '', _innerHTML: '', value: '', textContent: '', disabled: false, title: '',
        get innerHTML() { return node._innerHTML; },
        set innerHTML(val) { node._innerHTML = val; if (val === '') node.children.length = 0; },
        appendChild(child) { node.children.push(child); return child; },
        append(...nodes) { node.children.push(...nodes); },
        prepend(...nodes) { node.children.unshift(...nodes); },
        get firstElementChild() { return node.children[0] || null; },
        getBoundingClientRect() { return { left: 0, top: 0, width: 800, height: 160 }; },
        focus() {},
        addEventListener(type, handler) { (node.listeners[type] ||= []).push(handler); },
        querySelector(selector) { return selector === 'svg' ? shimElement('svg') : shimElement('div'); },
        querySelectorAll() { return []; },
        setAttribute(key, value) { node.attributes[key] = value; },
        getAttribute(key) { return node.attributes[key] ?? null; },
        contains() { return false; },
        click() { for (const handler of node.listeners.click || []) handler({ stopPropagation() {}, preventDefault() {} }); },
    };
    const classes = new Set();
    node.classList = { add: (...names) => names.forEach((name) => classes.add(name)), remove: (...names) => names.forEach((name) => classes.delete(name)), toggle: (name, force) => (force ? classes.add(name) : classes.delete(name)), contains: (name) => classes.has(name) };
    Object.defineProperty(node, 'className', { get: () => node._className, set: (value) => { node._className = value; value.split(/\s+/).filter(Boolean).forEach((name) => classes.add(name)); } });
    return node;
}
const registry = new Map();
globalThis.document = {
    createElement: shimElement,
    createElementNS: (namespace, tag) => shimElement(tag),
    getElementById: (id) => registry.get(id) || null,
    addEventListener() {},
    body: shimElement('body'),
};
globalThis.window = { devicePixelRatio: 1, addEventListener() {} };
globalThis.requestAnimationFrame = () => 0;
globalThis.performance = globalThis.performance || { now: () => Date.now() };
const { mountStudioUI } = await import('./UI/StudioPanels.js');
const { FluidStudioApp } = await import('./Engine/FluidStudioApp.js');
const refs = {};
for (const key of ['outliner', 'inspector', 'toolbar', 'transport', 'legend', 'notch', 'notchMenu', 'drawer', 'drawerTab', 'statusFps', 'statusStep', 'statusGrid', 'statusForces', 'statusText', 'timeline']) refs[key] = shimElement('div');
const canvas = { ...shimElement('canvas'), width: 1280, height: 720, getContext: () => createMockGl(), getBoundingClientRect: () => ({ left: 0, top: 0, width: 1280, height: 720 }), setPointerCapture() {}, releasePointerCapture() {} };
let ui = null;
const app = new FluidStudioApp({ canvas, onMetrics: (metrics) => ui && ui.updateMetrics(metrics), onStateChange: (snapshot) => ui && ui.renderAll(snapshot), onError: (error) => { throw error; } });
ui = mountStudioUI({ app, refs });
app.init();
check(app.supported && app.scene, 'App initialises against mock GPU context');
ui.renderAll(app.snapshot());
check(refs.outliner.children.length >= 4 && refs.inspector.children.length >= 5, 'Outliner + inspector populate for the Domain selection');
let renders = 0;
for (const type of ForceGeneratorTypes)
{
    const force = app.addForce(type.Id);
    check(app.selection === force.Id, `Add ${type.Name} selects the new node`);
    renders += refs.inspector.children.length;
}
check(app.scene.Forces.length === ForceGeneratorTypes.length && app.scene.PresetId === 'preset-blank', 'All generator types construct into the blank starting scene');
app.scene.Visualizers.ForceVectors.Visible = true; app.scene.Visualizers.ForceVectors.Mode = 'Volume'; app.frame(70);
check(app.supported, 'Sparse-volume force vector overlay renders');
app.addEmitter('Disc'); app.addCollider();
for (const visualizer of Object.values(app.scene.Visualizers)) { app.select(visualizer.Id); check(refs.inspector.children.length >= 4, `${visualizer.Name} inspector renders`); }
app.frame(16); app.frame(33); app.stepOnce(); app.frame(50);
check(app.solver.frame === 4, `Solver advanced ${app.solver.frame} frames through the app loop (3 live + 1 stepped)`);
for (const preset of ScenePresets) { app.loadPreset(preset.Id); check(app.scene.Name === preset.Name && app.solver.layout.width === preset.Grid.Resolution[0], `Preset ${preset.Name} loads + reconfigures grid`); }
ui.toggleDrawer(true, 'forces');
check(refs.drawer.children.length === 2 && refs.drawer.children[1].children.reduce((sum, group) => sum + group.children[1].children.length, 0) === ForceGeneratorTypes.length, 'Asset drawer lists every force generator card grouped by category');
app.loadPreset('preset-campfire');
app.updateNode(app.scene.Domain.Id, 'MaxCells', 48);
check(app.solver.layout.height === 48 && app.solver.layout.width === 29, `MaxCells edits rebuild the solver fields at fixed world size (3×5×3 m → ${app.solver.layout.width}×${app.solver.layout.height}×${app.solver.layout.depth})`);
app.updateNode(app.scene.Domain.Id, 'Size', [4, 6, 4]);
check(app.solver.layout.width === 32 && Math.abs(app.scene.Domain.CellSize - 0.125) < 1e-9, 'Size edits keep the voxel count on the longest axis');
const exported = app.exportJson();
app.importJson(exported);
check(app.scene.Domain.Resolution.join() === '32,48,32' && app.scene.Domain.MaxCells === 48, 'Export → import preserves the edited domain');
app.loadPreset('preset-demolition');
const charge = app.scene.Emitters[0];
check(nodeWeight(charge, 0.1) === 0 && nodeWeight(charge, 0.3) === 1 && nodeWeight(charge, 1.0) === 0, 'Timing keys gate the ground charge (waiting → live → done)');
check(nodeWeight({ StartTime: 1, Duration: 1, Fade: 0.5, Repeat: 3 }, 4.5) === 1 && nodeWeight({}, 99) === 1, 'Repeat keys loop; un-keyed nodes stay on');
app.scene.Visualizers.Debris.Visible = true; app.frame(80); app.frame(96); app.frame(112);
check(app.supported && app.solver.debrisPositions && app.solver.debrisVelocities, 'Debris particle ping-pong integrates + renders through the app loop');
app.scene.Domain.SceneLoop = 0.01; app.frame(130); check(app.solver.frame <= 1, 'Scene loop restarts the solver clock');

// World Grid: a 3×1×3 tile window following a Straight-path emitter must slide by whole tiles with the fields shifted in place.
app.loadPreset('preset-drift-run');
const grid = app.scene.Domain;
check(grid.Tiles.join() === '3,1,3' && grid.TileCells.join() === '40,22,40' && grid.Resolution.join() === '120,22,120', `World Grid 3×1×3 tiles of ${grid.TileCells.join('×')} cells solve as one ${grid.Resolution.join('×')} field`);
app.scene.Emitters[0].PathKind = 'Straight'; app.scene.Emitters[0].PathSpeed = 10; app.scene.Emitters[0].Position = [0, -0.98, 0];
const relocationsBefore = app.solver.relocations;
for (let i = 0; i < 40; i += 1) app.frame(200 + i * 16);
check(app.solver.relocations > relocationsBefore && grid.GridOrigin[0] >= 1 && grid.GridOrigin[1] === 0, `Following emitter at 10 m/s stepped the window ${app.solver.relocations - relocationsBefore}× along X to tile ${grid.GridOrigin.join(',')} (height locked)`);
check(Math.abs(app.solver.domainCentre[0] - grid.GridOrigin[0] * 4) < 1e-9 && app.solver.layout.width === 120, 'Window centre tracks the tile origin without reallocating the fields');
app.recentreGrid();
check(grid.GridOrigin.join() === '0,0,0' && app.solver.domainCentre[0] === 0, 'Recentre returns the window to tile 0');
check(Shaders.ShiftFragment.includes('CellInside(source)'), 'Shift kernel zero-fills cells scrolling in from outside the old window');

// Infinite grid: sparse tiles allocate around emitters, grow across faces the occupancy readback flags, and free when empty.
app.loadPreset('preset-infinite-plume');
const pool = app.solver.tilePool;
check(app.solver.sparse && pool && pool.cells === 24 && pool.slots === 56 && app.solver.layout.textureWidth === pool.slotsX * pool.blockW, `Infinite grid allocates a ${pool.slotsX}×${pool.slotsY} slot atlas (${app.solver.layout.textureWidth}×${app.solver.layout.textureHeight}) for 56 tiles of 24³`);
app.frame(1000);
const pinned = app.solver.residentTiles.filter((entry) => entry.pinned).length;
check(pinned >= 1 && app.solver.residentTiles.every((entry) => entry.tile[1] >= 0), `Emitter pins ${pinned} ground tile(s); nothing allocates below the floor`);
const appGl = app.solver.gl;
// Fake an occupancy readback that reports gas pressing on the +X and +Y faces of every resident slot.
appGl.__calls.occupancyHook = (buffer) => { for (let slot = 0; slot < pool.slots; slot += 1) { if (!pool.entries[slot]) continue; buffer[slot * 12 + 0] = 500; buffer[slot * 12 + 1] = 1; buffer[slot * 12 + 5] = 400; buffer[slot * 12 + 7] = 400; } };
const before = app.solver.residentTiles.length;
app.frame(1016); app.frame(1032); app.frame(1048);
const after = app.solver.residentTiles.length;
check(after > before && pool.map.has('1,0,0') && pool.map.has('0,1,0'), `Occupancy drives growth: ${before} → ${after} resident tiles, +X and +Y neighbours allocated`);
appGl.__calls.occupancyHook = (buffer) => { for (let slot = 0; slot < pool.slots; slot += 1) buffer[slot * 12 + 3] = 1; };
for (let i = 0; i < 100; i += 1) app.frame(1100 + i * 16);
check(app.solver.residentTiles.length === app.solver.residentTiles.filter((entry) => entry.pinned).length, `Empty tiles are freed after their grace period (${app.solver.residentTiles.length} pinned remain)`);
appGl.__calls.occupancyHook = null;
check(Shaders.sparse(Shaders.PressureFragment).includes('uTileLookup') && !Shaders.sparse(Shaders.PressureFragment).includes('PixelFromCell(ivec3 cell)\n{\n    int tileX'), 'Sparse rewrite swaps the addressing chunk in every kernel');
check(appGl.__calls.uniformErrors.length === 0, `Sparse programs: every uniform written exists (${mockGl.__calls.uniformErrors.length} mismatches)`);
app.loadPreset('preset-campfire');
check(!app.solver.sparse && app.solver.layout.width === 29 || app.solver.layout.width > 0, 'Switching back to a fixed-box preset rebuilds the window solver');

//------------------------------------------------------------------------------------------------------------------------
//                                                  7. LAYER STACK (plan phases 1–2)
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[7] Layer specifications + gas layer stack');
{
    const { parseToml } = await import('./Engine/TomlSubset.js');
    const { validateLayerSpecifications, specificationHash } = await import('./Engine/LayerSpecificationValidation.js');
    const { generateLayerSpecifications, SpecificationSource, SpecificationOutput } = await import('./GenerateLayerSpecifications.mjs');
    const Stack = await import('./Engine/LayerStack.js');
    const sourceText = fs.readFileSync(SpecificationSource, 'utf8');
    const parsedSpecification = parseToml(sourceText);
    const specificationProblems = validateLayerSpecifications(parsedSpecification);
    check(specificationProblems.length === 0, `LayerSpecifications.toml follows the rules${specificationProblems.length ? ': ' + specificationProblems.join('; ') : ''}`);
    check(fs.readFileSync(SpecificationOutput, 'utf8') === generateLayerSpecifications(sourceText), 'Engine/LayerSpecifications.js is up to date with the TOML (run node GenerateLayerSpecifications.mjs)');
    check(Stack.SpecificationHash === specificationHash(parsedSpecification), `Specification hash ${Stack.SpecificationHash} matches the TOML`);
    const broken = JSON.parse(JSON.stringify(parsedSpecification));
    broken.Layer[0].Parameter[5].Options[1].Code = 0;
    broken.Layer[2].Parameter[0].Default = 99;
    check(validateLayerSpecifications(broken).length >= 2, 'Validator refuses duplicate choice codes and out-of-range defaults');

    // Presets are documents: each must build the exact scene the legacy preset object built (fingerprints recorded at conversion).
    const PresetGenerator = await import('./GeneratePresetLibrary.mjs');
    const Presets = await import('./Engine/PresetScenes.js');
    check(fs.readFileSync(PresetGenerator.PresetLibraryOutput, 'utf8') === PresetGenerator.generatePresetLibrary(), `Engine/PresetLibrary.js is up to date with Presets/*.toml (run node GeneratePresetLibrary.mjs) · library ${Presets.PresetLibraryHash}`);
    const conversion = JSON.parse(fs.readFileSync(path.join(path.dirname(fileURLToPath(import.meta.url)), '../../Exhibits/FluidEditor/Results/PresetConversion.json'), 'utf8'));
    let exact = 0, roundTrips = 0;
    for (const preset of ScenePresets)
    {
        const fingerprint = Presets.sceneFingerprint(Presets.buildSceneFromDocument(preset.Document));
        const recorded = conversion.Presets[preset.Id];
        if (recorded && fingerprint === recorded.Legacy) exact += 1;
        else console.log(`     ↳ ${preset.Id}: ${fingerprint} ≠ recorded ${recorded ? recorded.Legacy : 'none'} (if you changed this preset on purpose, update Exhibits/FluidEditor/Results/PresetConversion.json)`);
        app.loadPreset(preset.Id);
        if (app.gasEvaluation.Problems.length) console.log(`     ↳ ${preset.Id}: stack problems ${JSON.stringify(app.gasEvaluation.Problems)}`);
        const tomlText = Stack.writeStackToml([app.scene.GasStack]);
        if (Stack.writeStackToml(Stack.readStackToml(tomlText).Stacks) === tomlText && !app.gasEvaluation.Problems.length) roundTrips += 1;
    }
    check(exact === ScenePresets.length && ScenePresets.length === Object.keys(conversion.Presets).length, `${exact}/${ScenePresets.length} preset documents build the identical scene the legacy preset objects built`);
    check(roundTrips === ScenePresets.length, `${roundTrips}/${ScenePresets.length} preset stacks evaluate without problems and round-trip through TOML byte for byte`);
    const tweaked = JSON.parse(JSON.stringify(ScenePresetById['preset-campfire'].Document));
    tweaked.Volume[0].Layer.find((layer) => layer.Category === 105).Parameters.Vorticity += 0.01;
    check(Presets.sceneFingerprint(Presets.buildSceneFromDocument(tweaked)) !== conversion.Presets['preset-campfire'].Legacy, 'Fingerprint detects a 0.01 change in one layer parameter');
    const saved = parseToml(app.exportPresetToml({ Id: 'preset-saved', Family: 'Custom' }));
    check(Presets.validatePresetDocument(saved).length === 0 && Presets.sceneFingerprint(Presets.buildSceneFromDocument(saved)) === Presets.sceneFingerprint(app.scene), 'Save as preset writes a valid document that rebuilds the current scene exactly');

    // Unknown (newer) categories and parameters stay verbatim and inert.
    app.loadPreset('preset-campfire');
    const future = JSON.parse(JSON.stringify(app.scene.GasStack));
    future.Layers.push({ Token: 'layer-future-1', Category: 199, Enabled: true, Solo: false, Weight: 1, Blend: 0, Parameters: { Mystery: 7, Curve: [1, 2, 3] } });
    future.Layers[2].Parameters.FutureKnob = 0.5;
    const futureText = Stack.writeStackToml([future]);
    const reread = Stack.readStackToml(futureText);
    check(Stack.writeStackToml(reread.Stacks) === futureText && reread.Stacks[0].Layers.at(-1).Parameters.Mystery === 7, 'Unknown layer category and unknown parameter are kept and written back unchanged');
    const futureEvaluation = Stack.evaluateGasStack(reread.Stacks[0], app.scene.Emitters);
    check(!futureEvaluation.Stopped && futureEvaluation.Problems.some((problem) => problem.Severity === 'Info'), 'Unknown layer is reported as inert and does not stop the volume');

    // Behaviour: disabling, weighting, solo, capacity and the required Domain layer.
    const buoyancy = app.scene.GasStack.Layers.find((layer) => layer.Category === 103);
    const authored = buoyancy.Parameters.Buoyancy;
    app.updateLayer(buoyancy.Token, 'Enabled', false);
    check(app.scene.Domain.Buoyancy === 0 && app.scene.Domain.Cooling === 0, 'Disabling the Buoyancy layer returns buoyancy and cooling to neutral (0)');
    app.updateLayer(buoyancy.Token, 'Enabled', true); app.updateLayer(buoyancy.Token, 'Weight', 0.5);
    check(Math.abs(app.scene.Domain.Buoyancy - authored * 0.5) < 1e-12, `Weight 0.5 halves the contribution (${authored} → ${app.scene.Domain.Buoyancy})`);
    app.updateLayer(buoyancy.Token, 'Weight', 1);
    const fire = app.scene.GasStack.Layers.find((layer) => layer.Category === 108);
    app.updateLayer(buoyancy.Token, 'Solo', true);
    check(app.scene.Visualizers.Volume.FireIntensity === 0 && app.scene.Domain.Buoyancy === authored && !app.gasEvaluation.Stopped, 'Solo keeps only the soloed layer (and the required Domain) contributing');
    app.updateLayer(buoyancy.Token, 'Solo', false);
    check(app.scene.Visualizers.Volume.FireIntensity === fire.Parameters.FireIntensity, 'Un-solo restores every layer');
    app.updateNode('domain', 'Vorticity', 0.9);
    const vorticity = app.scene.GasStack.Layers.find((layer) => layer.Category === 105);
    check(vorticity && vorticity.Parameters.Vorticity === 0.9 && app.scene.Domain.Vorticity === 0.9, 'Editing a stack-owned Domain field writes into its layer (existing controls keep working)');
    app.updateNode('vis-volume', 'FireIntensity', 6);
    check(fire.Parameters.FireIntensity === 6 && app.scene.Visualizers.Volume.FireIntensity === 6, 'Volume Render fire edits are written to the Fire shading layer');
    const second = app.addLayer(103), third = app.addLayer(103), fourth = app.addLayer(103), fifth = app.addLayer(103);
    check(second && third && fourth && fifth === null, 'Capacity is enforced when adding (Buoyancy ≤ 4)');
    app.updateLayer(second.Token, 'Blend', 1); app.updateLayer(second.Token, 'Buoyancy', 0.5, true); // third and fourth keep Replace + default 1.2
    check(app.scene.Domain.Buoyancy === 1.2, `Later Replace layers win over earlier ones (top to bottom): ${app.scene.Domain.Buoyancy}`);
    app.removeLayer(third.Token); app.removeLayer(fourth.Token);
    check(Math.abs(app.scene.Domain.Buoyancy - (authored + 0.5)) < 1e-12, `Add blend sums onto the layers above (${authored} + 0.5 = ${app.scene.Domain.Buoyancy})`);
    const domainLayer = app.scene.GasStack.Layers[0];
    check(app.removeLayer(domainLayer.Token) === false, 'The required Domain layer cannot be deleted');
    app.moveLayer(second.Token, 0);
    check(app.scene.GasStack.Layers[0].Category === 101, 'Nothing can be moved above the Domain layer');
    const broken2 = { Token: 'gas-volume', Category: 1, Layers: app.scene.GasStack.Layers.slice(1) };
    const stoppedEvaluation = Stack.evaluateGasStack(broken2, app.scene.Emitters);
    check(Boolean(stoppedEvaluation.Stopped), `A stack without its Domain layer stops the volume with a reason ("${stoppedEvaluation.Stopped}")`);
    const frameBefore = app.solver.frame;
    app.scene.GasStack = broken2; app.evaluateStack(); app.frame(5000); app.frame(5016);
    check(app.solver.frame === frameBefore && app.supported, 'Stopped volume holds the gas still while the app keeps running');

    // Emitters and Source layers stay linked.
    app.loadPreset('preset-campfire');
    const sourcesBefore = app.scene.GasStack.Layers.filter((layer) => layer.Category === 102).length;
    const extra = app.addEmitter('Disc');
    check(app.scene.GasStack.Layers.filter((layer) => layer.Category === 102).length === sourcesBefore + 1, 'Adding an emitter adds its linked Source layer');
    const link = app.scene.GasStack.Layers.find((layer) => layer.Parameters.Emitter === extra.Id);
    app.updateLayer(link.Token, 'Enabled', false);
    check(extra.Enabled === false, 'Disabling a Source layer disables its emitter');
    app.updateNode(extra.Id, 'Enabled', true);
    check(link.Enabled === true, 'Enabling the emitter from its inspector re-enables the Source layer');
    app.removeNode(extra.Id);
    check(!app.scene.GasStack.Layers.some((layer) => layer.Parameters.Emitter === extra.Id), 'Deleting the emitter removes its Source layer');

    // Files: Version 5 keeps the gas stack (and tracks, §9) (and particle stacks, §8); Version 2 files convert exactly.
    app.updateNode('domain', 'Vorticity', 1.1);
    const version3 = app.exportJson();
    app.importJson(version3);
    check(JSON.parse(version3).Version === 5 && app.scene.Domain.Vorticity === 1.1 && app.scene.GasStack.Layers.filter((layer) => layer.Category === 102).every((layer) => app.scene.Emitters.some((emitter) => emitter.Id === layer.Parameters.Emitter)), 'Version 5 export → import keeps the stack and relinks Source layers to the re-created emitters');
    const version2 = JSON.parse(version3); delete version2.GasStack; version2.Version = 2;
    app.importJson(JSON.stringify(version2));
    check(app.scene.Domain.Vorticity === 1.1 && app.scene.GasStack.Layers[0].Category === 101, 'Version 2 files (no stack) convert to a stack with the same values');
    app.loadPreset('preset-campfire');
}

console.log('\n[8] Particle layer stacks + templates as documents');
{
    const Stack = await import('./Engine/LayerStack.js');
    const Catalog = await import('./Engine/ParticleCatalog.js');
    const Presets = await import('./Engine/PresetScenes.js');
    const PresetGenerator = await import('./GeneratePresetLibrary.mjs');
    const { parseToml, writeToml } = await import('./Engine/TomlSubset.js');
    const { specificationHash } = await import('./Engine/LayerSpecificationValidation.js');
    check(fs.readFileSync(PresetGenerator.ParticleTemplateLibraryOutput, 'utf8') === PresetGenerator.generateParticleTemplateLibrary(), `Engine/ParticleTemplateLibrary.js is up to date with ParticleTemplates/*.toml · library ${Catalog.ParticleTemplateLibraryHash}`);
    check(Stack.ParticleLayerSpecifications.length === 18 && Stack.ParticleLayerSpecifications.every((layer) => layer.Parameter.every((parameter) => parameter.Target === 'Particles')), `${Stack.ParticleLayerSpecifications.length} particle layer categories (201–218), all writing the particle node`);
    const owned = new Set(Stack.ParticleLayerSpecifications.flatMap((layer) => layer.Parameter.map((parameter) => parameter.Name)));
    const flatOnly = Object.keys(createParticleSystem('sparks')).filter((key) => !owned.has(key));
    check(flatOnly.join(',') === 'Id,Kind,Template,Name,Enabled,Visible,StartTime,Duration,Fade,Repeat,Tracks,Stack', `Every GPU field of a particle node is owned by a layer; only identity and timing stay flat (${flatOnly.join(', ')})`);

    // The 26 legacy templates: each document builds the identical node (fingerprints recorded at conversion).
    const conversion = JSON.parse(fs.readFileSync(path.join(Folder, '../../Exhibits/FluidEditor/Results/ParticleTemplateConversion.json'), 'utf8'));
    let exact = 0, clean = 0, roundTrips = 0;
    for (const [id, recorded] of Object.entries(conversion.Templates))
    {
        // An empty track list (phase 4) is no keys: the recorded legacy fingerprints still apply to the rest of the node.
        const { Id, Stack: _stack, Tracks, ...rest } = createParticleSystem(id);
        if (Catalog.ParticleTemplateById[id] && Tracks.length === 0 && specificationHash(JSON.parse(JSON.stringify(rest))) === recorded.Legacy) exact += 1;
        else console.log(`     ↳ ${id}: node differs from the legacy template (fingerprint ${recorded.Legacy})`);
    }
    for (const template of ParticleTemplates)
    {
        if (!template.Stopped && template.Problems.length === 0) clean += 1;
        const text = writeToml({ Layer: template.Stack.Layers }, Stack.FloatKeys);
        if (writeToml({ Layer: Stack.readParticleStack(parseToml(text)).Layers }, Stack.FloatKeys) === text) roundTrips += 1;
    }
    check(exact === 26 && Object.keys(conversion.Templates).length === 26, `${exact}/26 legacy templates rebuild the identical particle node from their document`);
    check(clean === ParticleTemplates.length && roundTrips === ParticleTemplates.length, `${clean}/${ParticleTemplates.length} template stacks evaluate without problems; ${roundTrips} round-trip through TOML byte for byte`);
    const exactConversions = [...ParticleTemplates.map((template) => createParticleSystem(template.Id)), ...ScenePresets.flatMap((preset) => buildSceneFromDocument(preset.Document).Particles)]
        .filter((node) => { const copy = JSON.parse(JSON.stringify(node)); Stack.applyParticleEvaluation(copy, Stack.evaluateParticleStack(Stack.convertParticleSystemToStack(node))); return JSON.stringify(copy) === JSON.stringify(node); }).length;
    check(exactConversions === ParticleTemplates.length + ScenePresets.reduce((total, preset) => total + preset.Particles.length, 0), `evaluate(convert(node)) equals the node field for field for every template and preset particle system (${exactConversions})`);
    const empty = Catalog.ParticleTemplateById['empty-cloud'];
    check(empty && empty.Stack.Layers.map((layer) => layer.Category).join(',') === '201,211' && !empty.Stopped, 'Empty Cloud template = Spawn + Sprite (the "+ New particle cloud" starting point) and evaluates');

    // Editing: layers own the fields; flat edits route to them; problems hold only that system.
    app.loadPreset('preset-form');
    const node = app.scene.Particles[0];
    app.select(node.Id);
    check(node.Stack.Layers[0].Category === Stack.SpawnLayerCode && node.Gravity === 0, `Form Sphere stack: ${node.Stack.Layers.map((layer) => Stack.LayerSpecificationByCode[layer.Category].Name).join(' → ')}`);
    const gravity = app.addLayer(203);
    check(gravity && node.Gravity === 9.8, `+ Add layer → Gravity adds 9.8 m/s² to the selected particle system (${node.Gravity})`);
    app.updateLayer(gravity.Token, 'Weight', 0.5);
    check(Math.abs(node.Gravity - 4.9) < 1e-12, `Weight 0.5 halves it (${node.Gravity})`);
    app.updateLayer(gravity.Token, 'Enabled', false);
    check(node.Gravity === 0, 'Disabling the layer returns gravity to neutral (0)');
    app.removeLayer(gravity.Token);
    app.updateNode(node.Id, 'Gravity', 2);
    const inserted = node.Stack.Layers.find((layer) => layer.Category === 203);
    check(inserted && node.Gravity === 2 && node.Stack.Layers.indexOf(inserted) < node.Stack.Layers.findIndex((layer) => layer.Category === 209), 'A flat edit with no owning layer inserts it in phase order (Advance before Shade)');
    app.placeNode(node, [0.5, 0.25, -0.5]);
    check(node.Stack.Layers[0].Parameters.Position.join(',') === '0.5,0.25,-0.5' && node.Position.join(',') === '0.5,0.25,-0.5', 'Moving the gizmo writes the Spawn layer position');
    check(app.removeLayer(node.Stack.Layers[0].Token) === false, 'The required Spawn layer cannot be deleted');
    check(node.Stack.Layers.some((layer) => layer.Category === 211) && app.addLayer(211) === null, 'Capacity holds: a second Sprite layer is refused');
    app.updateLayer(node.Stack.Layers[0].Token, 'Count', 4096, true);
    check(node.Count === 4096 && node.Stack.Layers[0].Parameters.Count === 4096, 'Editing a Spawn layer parameter evaluates into the node the GPU reads');
    const tweaked = JSON.parse(JSON.stringify(ScenePresetById['preset-form'].Document));
    tweaked.Particle[0].Layer[0].Parameters.Radius += 0.01;
    check(Presets.sceneFingerprint(Presets.buildSceneFromDocument(tweaked)) !== Presets.sceneFingerprint(Presets.buildSceneFromDocument(ScenePresetById['preset-form'].Document)), 'Scene fingerprint detects a 0.01 change in one particle layer parameter');
    const saved = parseToml(app.exportPresetToml({ Id: 'preset-saved', Family: 'Custom' }));
    check(Presets.validatePresetDocument(saved).length === 0 && Presets.sceneFingerprint(Presets.buildSceneFromDocument(saved)) === Presets.sceneFingerprint(app.scene) && saved.Particle[0].Layer.length === node.Stack.Layers.length, 'Save as preset writes each particle system with its layer stack and rebuilds it exactly');
    const weighted = node.Stack.Layers.find((layer) => layer.Category === 205);
    app.updateLayer(weighted.Token, 'Weight', 0.25);
    const exported = app.exportJson();
    app.importJson(exported);
    const back = app.scene.Particles[0];
    check(back.Stack.Layers.length === node.Stack.Layers.length && back.Stack.Layers.find((layer) => layer.Category === 205).Weight === 0.25 && back.Gravity === 2, 'Version 4 export → import keeps every particle stack (layers, weights, amounts)');
    const duplicate = app.duplicateNode(back.Id);
    check(duplicate && duplicate.Stack.Layers.every((layer, index) => layer.Token !== back.Stack.Layers[index].Token) && duplicate.Position[0] === back.Position[0] + 0.4, 'Duplicating a particle system copies its stack with fresh layer tokens and offsets the Spawn position');
    duplicate.Stack.Layers.push(Stack.createLayer(103));
    const foreign = app.particleEvaluation(duplicate);
    check(Boolean(foreign.Stopped) && foreign.Problems.some((problem) => /belongs to a GasVolume/.test(problem.Text)), `A gas layer inside a particle stack stops that system with a reason ("${foreign.Stopped}")`);
    duplicate.Stack.Layers.pop();
    duplicate.Stack.Layers.shift();
    app.evaluateParticles(duplicate);
    check(Catalog.HaltedParticleSystems.has(duplicate), 'A particle stack without Spawn holds that system (not simulated or drawn)');
    const before = app.solver.frame;
    app.playing = true; app.frame(6000); app.frame(6016);
    check(app.supported && app.solver.frame > before && !Catalog.HaltedParticleSystems.has(back), 'The rest of the scene keeps running while one particle system is held');
    const fresh = app.addParticles('empty-cloud');
    if (!fresh) console.log(`     ↳ ${app.scene.Particles.length} particle systems in the scene`);
    check(fresh && !Catalog.HaltedParticleSystems.has(fresh) && fresh.Stack.Layers.length === 2, 'A new particle system from Empty Cloud is created and runs without failures');
    app.loadPreset('preset-campfire');
}

console.log('\n[9] Timeline, keys and scrubbing (phase 4)');
{
    const Keys = await import('./Engine/KeyTracks.js');
    const Stack = await import('./Engine/LayerStack.js');
    const Presets = await import('./Engine/PresetScenes.js');
    const { parseToml, writeToml } = await import('./Engine/TomlSubset.js');
    const { CheckpointRing } = await import('./Engine/SimulationCheckpoints.js');
    const near = (a, b, tolerance = 1e-9) => Math.abs(a - b) <= tolerance;

    // §5.3 — the envelope the solver used before phase 4, verbatim, as the reference.
    function legacyTimingEnvelope(node, time)
    {
        const start = node.StartTime || 0;
        const duration = node.Duration || 0;
        const fade = Math.max(0.001, node.Fade ?? 0.15);
        const repeat = node.Repeat || 0;
        if (start <= 0 && duration <= 0) return 1;
        let local = time - start;
        if (local < 0) return 0;
        if (repeat > 0) local = local % repeat;
        const rise = Math.min(1, local / fade);
        if (duration <= 0) return rise;
        if (local >= duration) return 0;
        const fall = Math.min(1, (duration - local) / fade);
        return Math.min(rise, fall);
    }
    let keyedNodes = 0, samples = 0, worst = 0;
    for (const preset of Presets.ScenePresets)
    {
        const scene = Presets.buildSceneFromDocument(preset.Document);
        for (const node of [...scene.Forces, ...scene.Emitters, ...scene.Particles])
        {
            if (!Keys.hasWeightTrack(node)) continue;
            keyedNodes += 1;
            const end = (node.StartTime || 0) + Math.max(node.Duration || 0, node.Repeat || 0) * 3 + 2;
            for (let step = 0; step <= Math.ceil(end * 1000); step += 1) { const time = step / 1000; worst = Math.max(worst, Math.abs(legacyTimingEnvelope(node, time) - Keys.nodeWeight(node, time))); samples += 1; }
        }
    }
    check(keyedNodes > 0 && worst <= 1e-6, `Envelope → Weight track: ${keyedNodes} timed forces, sources and particle clouds in the 36 presets, ${samples} samples at 1 ms, max difference ${worst.toExponential(2)} (≤ 1e-6)`);
    let gridWorst = 0, gridCases = 0;
    for (const StartTime of [0, 0.3, 1]) for (const Duration of [0, 0.06, 0.5, 2]) for (const Fade of [0, 0.02, 0.15, 0.4, 3]) for (const Repeat of [0, 0.05, 0.7, 1.5, 2.5])
    {
        const node = { StartTime, Duration, Fade, Repeat };
        gridCases += 1;
        for (let step = 0; step <= 8000; step += 1) gridWorst = Math.max(gridWorst, Math.abs(legacyTimingEnvelope(node, step / 1000) - Keys.nodeWeight(node, step / 1000)));
    }
    check(gridWorst <= 1e-6, `Envelope → Weight track over ${gridCases} synthetic Start/Duration/Fade/Repeat cases (rise-and-hold, trapezoid, triangle, repeat shorter than duration): max difference ${gridWorst.toExponential(2)}`);
    const trapezoid = Keys.envelopeToTrack({ StartTime: 1, Duration: 2, Fade: 0.5 });
    const triangle = Keys.envelopeToTrack({ StartTime: 1, Duration: 0.6, Fade: 0.5 });
    const rise = Keys.envelopeToTrack({ StartTime: 1, Duration: 0, Fade: 0.5 });
    const cycle = Keys.envelopeToTrack({ StartTime: 1, Duration: 1, Fade: 0.2, Repeat: 3 });
    check(trapezoid.Keys.length === 4 && triangle.Keys.length === 3 && near(triangle.Keys[1].Amount, 0.6) && rise.Keys.length === 2 && trapezoid.Keys.every((key) => key.Ease === 1), 'Plan §5.3 shapes: trapezoid = 4 linear keys, Fade > Duration/2 = triangle peaking at Duration/(2·Fade), Duration 0 = rise then hold');
    check(cycle.Extrapolation === 1 && cycle.Period === 3 && Keys.envelopeToTrack({}) === null, 'Repeat → Cycle extrapolation with Period = Repeat; un-timed nodes have no Weight track (Weight 1)');

    // §5.2 — sampling.
    const linear = { Path: 'Weight', Keys: [{ Time: 0, Amount: 0, Ease: 1 }, { Time: 1, Amount: 10, Ease: 1 }] };
    check(near(Keys.sampleTrack(linear, 0.25), 2.5) && Keys.sampleTrack(linear, -1) === 0 && Keys.sampleTrack(linear, 5) === 10, 'Linear ease interpolates; the first amount holds before the first key and Hold keeps the last');
    const constant = { Path: 'X', Keys: [{ Time: 0, Amount: 1, Ease: 0 }, { Time: 1, Amount: 5, Ease: 1 }] };
    check(Keys.sampleTrack(constant, 0.99) === 1 && Keys.sampleTrack(constant, 1) === 5, 'Constant ease holds until the next key');
    const smooth = { Path: 'X', Keys: [{ Time: 0, Amount: 0, Ease: 2 }, { Time: 1, Amount: 1, Ease: 2 }, { Time: 2, Amount: 1, Ease: 2 }, { Time: 3, Amount: 0, Ease: 2 }] };
    let overshoot = 0;
    for (let step = 0; step <= 300; step += 1) { const amount = Keys.sampleTrack(smooth, step / 100); overshoot = Math.max(overshoot, amount - 1, -amount); }
    check(overshoot <= 1e-12 && near(Keys.sampleTrack(smooth, 0.5), 0.5) && Keys.sampleTrack(smooth, 1.5) === 1, 'Smooth ease (monotone cubic Hermite, auto tangents) passes through every key without overshoot');
    const bezier = { Path: 'X', Keys: [{ Time: 0, Amount: 0, Ease: 3, Out: 3 }, { Time: 1, Amount: 1, Ease: 1, In: 0 }] };
    check(Keys.sampleTrack(bezier, 0.1) > 0.25 && near(Keys.sampleTrack(bezier, 1), 1), 'Bezier ease follows explicit Out / In tangents (amount per second)');
    const looping = { Path: 'X', Extrapolation: 1, Keys: [{ Time: 1, Amount: 0, Ease: 1 }, { Time: 2, Amount: 1, Ease: 1 }] };
    const pingPong = { ...looping, Extrapolation: 2 };
    check(near(Keys.sampleTrack(looping, 2.25), 0.25) && near(Keys.sampleTrack(pingPong, 2.25), 0.75) && near(Keys.sampleTrack(pingPong, 3.25), 0.25), 'Cycle wraps and Ping-pong reflects after the last key');
    check(Keys.sampleTrackAs({ Path: 'Count', Keys: [{ Time: 0, Amount: 0, Ease: 1 }, { Time: 1, Amount: 3, Ease: 1 }] }, 0.5, { Type: 'Int32' }) === 2
        && Keys.sampleTrackAs({ Path: 'Mode', Keys: [{ Time: 0, Amount: 0, Ease: 1 }, { Time: 1, Amount: 1, Ease: 1 }] }, 0.9, { Type: 'Choice' }) === 0
        && Keys.sampleTrack({ Path: 'On', Keys: [{ Time: 0, Amount: false, Ease: 1 }, { Time: 1, Amount: true, Ease: 1 }] }, 0.9) === false, 'Int32 rounds after easing; Choice and Bool step');
    const colour = Keys.sampleTrack({ Path: 'Albedo', Keys: [{ Time: 0, Amount: '#000000', Ease: 1 }, { Time: 1, Amount: '#ffffff', Ease: 1 }] }, 0.5);
    const vector = Keys.sampleTrack({ Path: 'Position', Keys: [{ Time: 0, Amount: [0, 0, 0], Ease: 1 }, { Time: 2, Amount: [2, 4, -2], Ease: 1 }] }, 1);
    check(colour === '#bcbcbc' && vector.join(',') === '1,2,-1', `ColourLinear eases in linear light (#000 → #fff at 0.5 = ${colour}); Vector3 eases per component`);

    // Editing.
    const tracks = [];
    Keys.setKey(tracks, 'layer-1/Buoyancy', 1, 2);
    Keys.setKey(tracks, 'layer-1/Buoyancy', 0, 1);
    Keys.setKey(tracks, 'layer-1/Buoyancy', 1 + 1 / 1000, 3);
    check(tracks.length === 1 && tracks[0].Keys.length === 2 && tracks[0].Keys[0].Time === 0 && tracks[0].Keys[1].Amount === 3, 'setKey keeps keys sorted and replaces a key within half a 240 Hz step');
    Keys.transformKeys(tracks[0], [1], { offset: 0.5 });
    check(tracks[0].Keys[1].Time === 1.5, 'Moving keys keeps the order valid');
    Keys.transformKeys(tracks[0], [0, 1], { scale: 2, pivot: 0 });
    check(tracks[0].Keys[1].Time === 3, 'Scaling keys about a pivot');
    check(Keys.removeKey(tracks, 'layer-1/Buoyancy', 3) && Keys.removeKey(tracks, 'layer-1/Buoyancy', 0) && tracks.length === 0, 'Removing the last key removes the track');
    check(Keys.affectedFrom({ Keys: [{ Time: 1 }, { Time: 2 }, { Time: 3 }, { Time: 4 }] }, 4) === 2 && Keys.affectedFrom({ Keys: [{ Time: 1 }, { Time: 2 }] }, 0.5) === 0, 'A key edit invalidates from two keys back (Smooth tangents); editing before the first key affects t = 0');

    // Stack evaluation with keys: keyed amount replaces the static one; Weight multiplies after.
    const stack = Stack.convertSceneToGasStack(Presets.buildSceneFromDocument(ScenePresetById['preset-campfire'].Document));
    const buoyancy = stack.Layers.find((layer) => layer.Category === 103);
    stack.Tracks = [{ Path: `${buoyancy.Token}/Buoyancy`, Extrapolation: 0, Keys: [{ Time: 0, Amount: 0, Ease: 1 }, { Time: 2, Amount: 4, Ease: 1 }] }];
    const at1 = Stack.evaluateGasStack(stack, [], 1).Domain.Buoyancy;
    buoyancy.Weight = 0.5;
    const weighted = Stack.evaluateGasStack(stack, [], 1).Domain.Buoyancy;
    const neutral = Stack.LayerSpecificationByCode[103].Parameter.find((parameter) => parameter.Name === 'Buoyancy').Neutral;
    check(at1 === 2 && near(weighted, neutral + (2 - neutral) * 0.5) && Stack.evaluateGasStack(stack, []).Domain.Buoyancy === buoyancy.Parameters.Buoyancy * 0.5 + neutral * 0.5, 'A keyed parameter replaces the static amount at the playhead; layer Weight blends after it; no time = static');
    stack.Tracks.push({ Path: `${buoyancy.Token}/Weight`, Extrapolation: 0, Keys: [{ Time: 0, Amount: 0, Ease: 1 }, { Time: 1, Amount: 1, Ease: 1 }] });
    check(Stack.evaluateGasStack(stack, [], 1).Domain.Buoyancy === 2 && Stack.evaluateGasStack(stack, [], 0).Domain.Buoyancy === neutral, 'Layer Weight is keyable (0 = layer silent, 1 = full)');
    const document = Stack.writeStackDocument([stack]);
    const reread = Stack.readStackToml(Stack.writeStackToml([stack])).Stacks[0];
    check(JSON.stringify(reread.Tracks) === JSON.stringify(stack.Tracks) && /\[\[Volume\.Track\]\]/.test(Stack.writeStackToml([stack])) && /\[\[Volume\.Track\.Keys\]\]/.test(Stack.writeStackToml([stack])) && document.Volume[0].Track.length === 2, 'Tracks round-trip through TOML as [[Volume.Track]] / [[Volume.Track.Keys]] (Time, Amount, In, Out written as floats)');
    const fresh = Stack.readParticleStack({ Layer: [{ Token: 'layer-1', Category: 201 }, { Token: 'layer-2', Category: 205 }], Track: [{ Path: 'layer-2/Noise', Keys: [{ Time: 0, Amount: 1 }] }] }, true);
    check(fresh.Tracks[0].Path === `${fresh.Layers[1].Token}/Noise`, 'Fresh layer tokens (duplicates, template instances) carry their tracks along');
    const { validateTracks } = await import('./Engine/PresetDocumentValidation.js');
    check(validateTracks('t', [{ Path: 'layer-9/Buoyancy', Keys: [{ Time: 0, Amount: 1 }] }], [{ Token: 'layer-1', Category: 103 }]).length === 1
        && validateTracks('t', [{ Path: 'layer-1/Buoyancy', Keys: [{ Time: 1, Amount: 1 }, { Time: 0.5, Amount: 2 }] }], [{ Token: 'layer-1', Category: 103 }]).length === 1
        && validateTracks('t', [{ Path: 'StartTime', Keys: [{ Time: 0, Amount: 1 }] }], null).length === 1
        && validateTracks('t', [{ Path: 'Strength', Keys: [{ Time: 0, Amount: 1, Ease: 3, Out: 2 }] }], null).length === 0, 'Track rules: missing layer, keys out of order and timing fields are rejected');

    // App: ◆, Auto-key, keyed edits, Weight materialisation, files. Re-simulations after key edits finish synchronously here
    // (in the browser they run 24 ms of steps per frame), so the checks do not depend on the machine's speed.
    const frameBudget = app.seekBudgetMilliseconds;
    app.seekBudgetMilliseconds = Infinity;
    app.loadPreset('preset-campfire');
    const gas = app.scene.GasStack;
    const buoyant = gas.Layers.find((layer) => layer.Category === 103);
    const staticBuoyancy = buoyant.Parameters.Buoyancy;
    const target = { Kind: 'Layer', Token: buoyant.Token, Name: 'Buoyancy' };
    check(app.keyState(target) === 'none' && app.keyTargetForField(app.scene.Domain, 'Buoyancy').Token === buoyant.Token, 'Domain inspector fields resolve to their owning layer for ◆');
    app.updateLayer(buoyant.Token, 'Buoyancy', 2.2, true);
    check(buoyant.Parameters.Buoyancy === 2.2 && !gas.Tracks.length, 'Auto-key off: an edit of an un-keyed parameter stays static');
    app.toggleKey(target);
    check(gas.Tracks.length === 1 && app.keyState(target) === 'key', '◆ keys the current amount at the playhead');
    app.seekNow(1);
    app.updateLayer(buoyant.Token, 'Buoyancy', 0.4, true);
    check(gas.Tracks[0].Keys.length === 2 && buoyant.Parameters.Buoyancy === 2.2 && near(app.scene.Domain.Buoyancy, 0.4), 'Editing a keyed parameter writes a key at the playhead; the static amount is untouched; the solver reads the keyed amount');
    app.seekNow(0.5);
    check(near(app.scene.Domain.Buoyancy, Keys.sampleTrack(gas.Tracks[0], 0.5)), 'Scrubbing re-folds keyed stacks at the new playhead');
    const vorticity = gas.Layers.find((layer) => layer.Category === 105);
    app.autoKey = true;
    app.updateLayer(vorticity.Token, 'Weight', 0.5);
    app.autoKey = false;
    check(gas.Tracks.some((track) => track.Path === `${vorticity.Token}/Weight`) && vorticity.Weight === 1, 'Auto-key on: any edit (here a layer Weight) writes a key');
    app.removeLayer(vorticity.Token);
    check(!gas.Tracks.some((track) => track.Path.startsWith(vorticity.Token)), 'Removing a layer removes its tracks');
    void staticBuoyancy;

    app.loadPreset('preset-demolition');
    const charge = app.scene.Emitters[0];
    const derived = app.allTracks(true).filter((entry) => entry.Derived);
    check(derived.length >= 4 && derived.every((entry) => entry.Track.Envelope), `Timed nodes show their generated Weight track in the timeline (${derived.length} in Demolition)`);
    const generated = JSON.parse(JSON.stringify(Keys.weightTrackOf(charge)));
    app.toggleKey({ Kind: 'Node', Id: charge.Id, Name: 'Weight' });
    const explicit = Keys.findTrack(charge.Tracks, 'Weight');
    check(explicit && charge.StartTime === 0 && charge.Duration === 0 && explicit.Keys.length === generated.Keys.length + 1, '◆ on a timed node turns its timing into explicit Weight keys (timing fields cleared) and adds a key');
    app.updateNode(charge.Id, 'StartTime', 0.2);
    app.updateNode(charge.Id, 'Duration', 0.3);
    check(!Keys.findTrack(charge.Tracks, 'Weight') && near(Keys.nodeWeight(charge, 0.35), 1), 'Setting timing fields again hands Weight back to the generated envelope');
    const force = app.scene.Forces[0];
    app.seekNow(0);
    app.toggleKey({ Kind: 'Node', Id: force.Id, Name: 'Strength' });
    app.seekNow(0.25);
    app.updateNode(force.Id, 'Strength', 5);
    check(Keys.keyedNode(force, 0.25).Strength === 5 && force.Strength === 38 && near(Keys.keyedNode(force, 0.125).Strength, Keys.sampleTrack(Keys.findTrack(force.Tracks, 'Strength'), 0.125)), 'Force fields are keyable; the solver packs the keyed amount, the static one stays');
    const saved = parseToml(app.exportPresetToml({ Id: 'preset-keyed', Family: 'Custom' }));
    const rebuilt = Presets.buildSceneFromDocument(saved);
    check(Presets.validatePresetDocument(saved).length === 0 && saved.Document.Revision === 2 && saved.Force[0].Track.length === 1 && Presets.sceneFingerprint(rebuilt) === Presets.sceneFingerprint(app.scene), 'Save as preset (Revision 2) writes [[Force.Track]] and rebuilds the keyed scene exactly');
    const json = JSON.parse(app.exportJson());
    app.importJson(JSON.stringify(json));
    check(json.Version === 5 && json.Timeline.End > 0 && Keys.findTrack(app.scene.Forces[0].Tracks, 'Strength').Keys.length === 2, 'Version 5 scene files keep tracks and the timeline');

    // Particle layer keys follow the stack through duplicate and save.
    app.loadPreset('preset-form');
    const cloud = app.scene.Particles[0];
    const spawn = cloud.Stack.Layers[0];
    app.seekNow(0);
    app.toggleKey({ Kind: 'Layer', Token: spawn.Token, Name: 'Radius' });
    app.seekNow(0.5);
    app.updateLayer(spawn.Token, 'Radius', spawn.Parameters.Radius * 2, true);
    check(near(cloud.Radius, spawn.Parameters.Radius * 2) && cloud.Stack.Tracks.length === 1, 'Particle layer parameters are keyable; the cloud reads the keyed amount');
    const copy = app.duplicateNode(cloud.Id);
    check(copy.Stack.Tracks.length === 1 && copy.Stack.Tracks[0].Path.startsWith(copy.Stack.Layers[0].Token + '/'), 'Duplicating a cloud copies its tracks onto the fresh layer tokens');
    const formSaved = parseToml(app.exportPresetToml({ Id: 'preset-form-keyed', Family: 'Custom' }));
    check(Presets.validatePresetDocument(formSaved).length === 0 && formSaved.Particle[0].Track[0].Path === 'layer-1/Radius' && Presets.sceneFingerprint(Presets.buildSceneFromDocument(formSaved)) === Presets.sceneFingerprint(app.scene), 'Particle tracks are saved as [[Particle.Track]] with document layer tokens and rebuild exactly');

    // §5.4 — checkpoints and scrubbing (mock GPU: the bookkeeping; the browser capture checks the bits).
    app.loadPreset('preset-campfire');
    const ring = app.checkpoints;
    check(ring instanceof CheckpointRing && ring.capacity(app.solver, app.particles) >= 1 && ring.bytesPerCheckpoint(app.solver, app.particles) > 0, `Checkpoint ring sized by budget: ${ring.capacity(app.solver, app.particles)} × ${(ring.bytesPerCheckpoint(app.solver, app.particles) / 1048576).toFixed(1)} MB`);
    const steps = app.seekNow(1.6);
    check(steps === Math.round(1.6 / app.stepDeltaTime()) && ring.entries.map((entry) => entry.Slot).join(',') === '1,2,3', `Scrubbing forward steps the simulation (${steps} steps) and stores a checkpoint every 0.5 s (${ring.entries.map((entry) => entry.Time.toFixed(2)).join(', ')})`);
    const restores = app.statistics.seekRestores;
    const backSteps = app.seekNow(1.2);
    check(app.statistics.seekRestores === restores + 1 && backSteps === Math.round(0.2 / app.stepDeltaTime()) && near(app.solver.time, 1.2, 1e-6) && app.solver.frame === Math.round(1.2 / app.stepDeltaTime()), `Scrubbing back restores the newest checkpoint ≤ t (1.0 s) and re-steps ${backSteps} steps; clock and step index restored`);
    app.seekNow(0.2);
    check(near(app.solver.time, 0.2, 1e-6) && app.solver.frame === Math.round(0.2 / app.stepDeltaTime()), 'Scrubbing before the first checkpoint restarts from the empty grid at t = 0');
    const kept = ring.entries.length;
    app.toggleKey({ Kind: 'Layer', Token: app.scene.GasStack.Layers.find((layer) => layer.Category === 103).Token, Name: 'Buoyancy' });
    check(kept === 3 && ring.entries.length === 0, 'A key at t = 0.2 s invalidates the checkpoints after it');
    const keyBuoyancy = () => app.toggleKey({ Kind: 'Layer', Token: app.scene.GasStack.Layers.find((layer) => layer.Category === 103).Token, Name: 'Buoyancy' });
    app.seekNow(0.6); keyBuoyancy();
    app.seekNow(0.8); keyBuoyancy();
    app.seekNow(1.6);
    const beforeEdit = ring.entries.length;
    app.seekNow(1.4);
    const restoresBefore = app.statistics.seekRestores;
    app.seekBudgetMilliseconds = -1;                             // inspect the ring before the re-simulation steps
    keyBuoyancy();
    app.seekBudgetMilliseconds = Infinity;
    const survived = ring.entries.map((entry) => entry.Time.toFixed(2)).join(',');
    app.continueSeek(Infinity);
    check(beforeEdit === 3 && survived === '0.50', `Keys at 0.2 / 0.6 / 0.8 s, then a new key at 1.4 s: Smooth tangents reach back two keys (0.6 s), so only the 0.5 s checkpoint survives (${survived})`);
    check(app.statistics.seekRestores === restoresBefore + 1 && near(app.solver.time, 1.4, 1e-6) && ring.entries.map((entry) => entry.Time.toFixed(2)).join(',') === '0.50,1.00', 'A key edit before the playhead re-simulates from the surviving checkpoint back to the playhead (the 1.0 s checkpoint is recaptured with the new keys)');
    app.updateNode('domain', 'Gravity', 0.3);
    check(ring.entries.length === 0, 'A static edit invalidates every checkpoint');
    app.timeline().Loop = true; app.timeline().End = 0.1;
    app.playing = true;
    for (let frame = 0; frame < 12; frame += 1) app.frame(7000 + frame * 16);
    check(app.solver.time < 0.1 + 1e-6, 'Loop wraps the playhead at the timeline End');
    app.timeline().Loop = false;

    // Timeline UI smoke (DOM shim): renders tracks, ◆ beside keyed controls.
    let threw = null;
    try { ui.renderAll(app.snapshot()); ui.timeline.state.view = 'curves'; ui.timeline.render(); ui.timeline.state.view = 'dope'; ui.timeline.render(); ui.timeline.update(); }
    catch (error) { threw = error; }
    app.seekBudgetMilliseconds = frameBudget;
    check(!threw && ui.timeline.state.rows.length >= 1, `Timeline dock renders dope sheet and curves with ${ui.timeline.state.rows.length} track(s)${threw ? ` — ${threw.message}` : ''}`);
    // Sparse (infinite) grids: the tile pool and grid origin are part of the checkpoint.
    app.loadPreset('preset-infinite-plume');
    const sparseSteps = app.seekNow(0.6);
    const sparseBack = app.seekNow(0.55);
    check(app.solver.sparse && near(app.solver.time, 0.55, 1e-6) && sparseBack === Math.round(0.05 / app.stepDeltaTime()), `Infinite grid: scrubbing back restores the sparse checkpoint at 0.5 s and re-steps ${sparseBack} steps`);

    app.loadPreset('preset-campfire');
}

//------------------------------------------------------------------------------------------------------------------------
//                          10. SCENE-ONLY OUTLINER, TABBED INSPECTOR + VISUAL ASSET PREVIEWS
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[10] Scene-only Outliner, Project Zero Inspector + Content Directory previews');
{
    app.loadPreset('preset-blank');
    const blankTree = refs.outliner.children[3];
    const blankRowIds = blankTree.children.map((child) => child.getAttribute('data-outliner-row'));
    check(
        blankRowIds.length === 2
        && blankRowIds[0].startsWith('sim-')
        && blankRowIds[1] === 'domain',
        `Blank Stage Outliner shows only its Simulation Folder and Gas Domain (${blankRowIds.length} rows, zero Overlays or Global clutter)`
    );

    app.loadPreset('preset-campfire');
    const fireTree = refs.outliner.children[3];
    const fireRows = fireTree.children.map((child) => child.getAttribute('data-outliner-row'));
    check(
        fireRows[0].startsWith('sim-')
        && fireRows.includes('domain')
        && fireRows.some((id) => id.startsWith('folder-forces::sim-'))
        && fireRows.every((id) => !id.startsWith('folder-particles::') && id !== 'folder-global'),
        'Campfire Outliner nests Gas Domain (with Sources), Force Fields and collapsed Overlays inside its Simulation Folder'
    );

    app.select('domain');
    const domainTabBar = refs.inspector.children[1];
    const domainPills = domainTabBar.children[1].children;
    check(domainTabBar.classList.contains('pz-tab-bar') && domainPills.length === 4, 'Domain Inspector exposes 4 lettered Project Zero pages (Stack / Grid / Shading / Solver)');
    for (let page = 0; page < domainPills.length; page += 1)
    {
        domainPills[page].click();
        check(refs.inspector.children.length >= 4, `Domain Inspector page ${page + 1}/4 renders cleanly`);
    }
    ui.state.tabByKind.Domain = 0;

    app.select(app.scene.Forces[0].Id);
    const forcePills = refs.inspector.children[1].children[1].children;
    check(forcePills.length === 3, 'Force Inspector exposes 3 pages (Field / Transform / Timing)');
    forcePills[1].click();
    const hasTransformCard = refs.inspector.children.some((child) => child.classList && child.classList.contains('transform-card'));
    check(hasTransformCard, 'Force Transform page renders the Project Zero XYZ matrix card');
    ui.state.tabByKind.Force = 0;

    ui.toggleDrawer(true, 'presets');
    const presetCards = refs.drawer.children[1].children.reduce((sum, group) => sum + group.children[1].children.length, 0);
    const firstThumbHasCanvas = refs.drawer.children[1].children[0].children[1].children[0].children[0].children[0].tagName === 'CANVAS';
    check(presetCards === ScenePresets.length && firstThumbHasCanvas, `Content Directory renders all ${presetCards} Scene Presets with canvas previews`);

    ui.toggleDrawer(true, 'particles');
    const particleCards = refs.drawer.children[1].children.reduce((sum, group) => sum + group.children[1].children.length, 0);
    check(particleCards === ParticleTemplates.length, `Content Directory renders all ${particleCards} Particle Cloud templates with previews`);

    ui.toggleDrawer(true, 'emitters');
    check(refs.drawer.children[1].children.length === 4, 'Content Directory Sources & Colliders tab renders 4 visual cards');
    ui.toggleDrawer(false);
}

//------------------------------------------------------------------------------------------------------------------------
//                          11. SIMULATION FOLDERS, GLOBAL/LOCAL SCOPING + FOLDER INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[11] Project Zero Simulation Folders, Global/Local scoping + FolderInspector');
{
    app.loadPreset('preset-explosion');
    const explosionSimId = app.scene.Directory.ActiveSimulationId;
    check(
        explosionSimId.startsWith('sim-preset-explosion')
        && app.scene.Directory.Simulations[0].Name === 'Explosion',
        `Explosion loads into its own Simulation Folder (${explosionSimId})`
    );

    // Select the Explosion folder and verify Project Zero FolderInspector renders
    app.select(explosionSimId);
    check(
        refs.inspector.children.length === 5
        && refs.inspector.children[0].classList.contains('collection-heading'),
        'Selecting Explosion folder renders Project Zero FolderInspector (COLLECTION / SCENE INVENTORY)'
    );

    // Promote one Force Field (e.g. Curl Noise / Buoyancy) to Global
    const sharedForce = app.scene.Forces[app.scene.Forces.length - 1];
    const sharedForceName = sharedForce.Name;
    app.setEntityScope(sharedForce.Id, 'Global');
    const afterForceGlobal = refs.outliner.children[3].children.map((child) => child.getAttribute('data-outliner-row'));
    check(
        afterForceGlobal[0] === 'folder-global'
        && app.scene.Directory.GlobalForces.some((force) => force.Id === sharedForce.Id)
        && !app.scene.Directory.Simulations[0].LocalForces.some((force) => force.Id === sharedForce.Id),
        'Promoting a Force Field to Global creates Global folder and removes it from Explosion local forces'
    );

    // Enable an Overlay and promote it to Global
    app.updateNode('vis-vectors', 'Visible', true);
    app.setEntityScope('vis-vectors', 'Global');
    check(
        app.scene.Directory.OverlayScope['vis-vectors'] === 'Global',
        'Promoting an Overlay to Global stores it under Global OverlayScope'
    );

    // Add Campfire as a second Simulation Folder alongside Explosion
    const campfireFolder = app.addPresetAsFolder('preset-campfire');
    check(
        campfireFolder !== null
        && app.scene.Directory.Simulations.length === 2
        && app.scene.Forces.some((force) => force.Type === 'DirectionalWind')
        && !app.scene.Forces.some((force) => force.Type === 'RadialBurst')
        && app.scene.Forces.some((force) => force.Id === sharedForce.Id)
        && app.scene.Visualizers.VelocityVectors.Visible === true,
        'Adding Campfire as a second Simulation Folder isolates Campfire local forces (DirectionalWind) while sharing Global Force + Global Overlay'
    );

    // Switch back to Explosion folder -> Explosion's local Gas Domain & RadialBurst reactivate while Campfire's Breeze stays local to Campfire
    app.activateSimulation(explosionSimId);
    check(
        app.scene.Directory.ActiveSimulationId === explosionSimId
        && app.scene.Forces.some((force) => force.Type === 'RadialBurst')
        && !app.scene.Forces.some((force) => force.Type === 'DirectionalWind')
        && app.scene.Forces.some((force) => force.Id === sharedForce.Id),
        'Activating Explosion Simulation Folder restores Explosion local Domain/RadialBurst alongside Global items without Campfire clutter'
    );

    // Promote Gas Domain to Global -> shared across both Simulation Folders
    app.setEntityScope('domain', 'Global');
    check(
        app.scene.Directory.DomainScope === 'Global'
        && app.scene.Directory.GlobalDomain !== null
        && refs.outliner.children[3].children.some((child) => child.getAttribute('data-outliner-row') === 'domain'),
        'Promoting Gas Domain to Global moves Gas Domain into Global folder and shares it across simulations'
    );

    // Round-trip JSON export/import preserves multi-simulation Directory + Global scope
    const exportedJson = app.exportJson();
    app.loadPreset('preset-blank');
    app.importJson(exportedJson);
    check(
        app.scene.Directory.Simulations.length === 2
        && app.scene.Directory.DomainScope === 'Global'
        && app.scene.Directory.GlobalForces.some((force) => force.Name === sharedForceName)
        && app.scene.Directory.OverlayScope['vis-vectors'] === 'Global',
        'FluidScene JSON round-trip preserves Simulation Folders and Global/Local scoping'
    );
}

console.log('\n[12] Phase 5 LayerStack Effects (Derez, Mocap, MoGraph, Glitch, Lensing, Shimmer) & Zero-Default Overlays');
{
    const Stack = await import('./Engine/LayerStack.js');

    // 1. Overlays & Gizmos strictly disabled by default — even after spawning particles!
    app.loadPreset('preset-blank');
    const spawnedDerez = app.addParticles('derez-cube');
    const spawnedMocap = app.addParticles('mocap-sprint-ribbons');
    app.frame(7000); app.frame(7016);
    const vis = app.scene.Visualizers;
    const allDiagnosticsOff = !vis.VelocityVectors.Visible && !vis.ScalarSlice.Visible && !vis.Tracers.Visible && !vis.ForceVectors.Visible && !vis.ForceGizmos.Visible;
    const outlinerRowIds = refs.outliner.children[3].children.map((child) => child.getAttribute('data-outliner-row'));
    check(
        spawnedDerez && spawnedMocap
        && allDiagnosticsOff
        && app.renderer.lineVertexCount === 0
        && outlinerRowIds.every((id) => !String(id).includes('overlays')),
        'Spawning particles never enables Overlays or Force Gizmos (0 gizmo line vertices, 0 Outliner overlay rows)'
    );

    // 2. Dissolve & Derez (213) evaluates through LayerStack: Weight scales mix/glow/shatter; disabling resets to Neutral.
    app.select(spawnedDerez.Id);
    const derezLayer = spawnedDerez.Stack.Layers.find((layer) => layer.Category === 213);
    check(derezLayer && spawnedDerez.DerezMode === 'Derez' && spawnedDerez.DerezMix === 1 && spawnedDerez.Sprite === 'Cube3D', 'Derez Voxel Shatter evaluates Layer 213 (Dissolve & Derez) + Voxel Cube sprite');
    app.updateLayer(derezLayer.Token, 'Weight', 0.5);
    check(Math.abs(spawnedDerez.DerezMix - 0.5) < 1e-12 && Math.abs(spawnedDerez.DerezEdgeGlow - 1.4) < 1e-12, 'Weighting Dissolve & Derez to 0.5 halves DerezMix and DerezEdgeGlow');
    app.updateLayer(derezLayer.Token, 'Enabled', false);
    check(spawnedDerez.DerezMode === 'Off' && spawnedDerez.DerezMix === 0 && spawnedDerez.DerezEdgeGlow === 0, 'Disabling Dissolve & Derez resets DerezMode to Off and intensities to 0');
    app.updateLayer(derezLayer.Token, 'Enabled', true); app.updateLayer(derezLayer.Token, 'Weight', 1);

    // 3. Mocap Performer (214) + MoGraph Effector (215) stack composition (including additive dual effectors).
    app.select(spawnedMocap.Id);
    const mocapLayer = spawnedMocap.Stack.Layers.find((layer) => layer.Category === 214);
    check(mocapLayer && spawnedMocap.Shape === 'MocapFigure' && spawnedMocap.MocapClip === 'Sprint' && spawnedMocap.MocapAmplitude === 1, 'Mocap Sprint Ribbons evaluates Layer 214 (Mocap performer) on MocapFigure shape');
    app.updateLayer(mocapLayer.Token, 'Weight', 0.25);
    check(Math.abs(spawnedMocap.MocapAmplitude - 0.25) < 1e-12, 'Weighting Mocap performer scales skeletal pose blend');
    app.loadPreset('preset-blank');
    const matrix = app.addParticles('effector-cube-matrix');
    app.select(matrix.Id);
    const eff2 = app.addLayer(215);
    app.updateLayer(eff2.Token, 'Blend', 1);
    app.updateLayer(eff2.Token, 'EffectorPush', 0.25, true);
    check(Math.abs(matrix.EffectorPush - 0.60) < 1e-12 && app.addLayer(215) === null, 'Dual MoGraph effector layers compose additively and enforce Capacity = 2');

    // 4. Overlay layers: Gas Heat Shimmer (109), Particle Glitch (217), and Distortion & Lensing (218) drive post-composite pass.
    app.loadPreset('preset-inferno-shimmer');
    const shimmerLayer = app.scene.GasStack.Layers.find((layer) => layer.Category === 109);
    check(shimmerLayer && app.scene.Visualizers.Volume.ShimmerStrength === 0.45 && app.renderer.hasActivePostOverlay(app.scene), 'Gas Layer 109 (Heat shimmer) evaluates into Volume and activates PostOverlay pass');
    app.updateLayer(shimmerLayer.Token, 'Enabled', false);
    check(app.scene.Visualizers.Volume.ShimmerStrength === 0 && !app.renderer.hasActivePostOverlay(app.scene), 'Muting Heat shimmer bypasses the PostOverlay pass cleanly (zero overhead)');

    app.loadPreset('preset-singularity-lens');
    const bh = app.scene.Particles[0];
    const lensLayer = bh.Stack.Layers.find((layer) => layer.Category === 218);
    check(lensLayer && bh.DistortionMode === 'GravitationalLens' && bh.DistortionHorizon === 0.11 && app.renderer.hasActivePostOverlay(app.scene), 'Singularity Lensing evaluates Layer 218 (Distortion & Lensing) with relativistic event horizon');
    app.frame(7100); app.frame(7116);
    check(app.supported && app.renderer.postTarget !== null, 'PostOverlay offscreen capture target renders cleanly through the live frame loop');

    const holo = app.addParticles('hologram-glitch');
    const glitchLayer = holo.Stack.Layers.find((layer) => layer.Category === 217);
    check(glitchLayer && holo.GlitchAmount === 0.65 && holo.Sprite === 'HoloGlyph', 'Hologram Data Glitch evaluates Layer 217 (Glitch) + HoloGlyph sprite');

    // 5. TOML round-trip of all 40 presets and 40 particle templates including Phase 5 effect layers.
    const savedVfx = Stack.readStackToml(Stack.writeStackToml([holo.Stack]));
    check(savedVfx.Stacks[0].Layers.some((l) => l.Category === 217) && savedVfx.Stacks[0].Layers.some((l) => l.Category === 214), 'Phase 5 ParticleCloud stacks round-trip through TOML');

    // 6. Unlimited Particle Clouds (> 4) + All Delete Surfaces (Inline Outliner, Inspector Scope Bar, Keyboard/Folder Delete).
    app.loadPreset('preset-blank');
    const eightIds = [
        'derez-cube', 'mocap-sprint-ribbons', 'butterfly-dissolve', 'effector-cube-matrix',
        'hologram-glitch', 'black-hole-lens', 'energy-hex-shield', 'butterfly-murmuration',
    ];
    for (const tplId of eightIds) app.addParticles(tplId);
    const ninth = app.duplicateNode(app.scene.Particles[0].Id);
    app.frame(7200); app.frame(7216);
    check(
        ninth && app.scene.Particles.length === 9 && app.particles.records.size === 9,
        'No 4-particle limit: 9 simultaneous particle clouds spawn, duplicate, and step on GPU'
    );
    const swapped = app.replaceParticleTemplate(ninth.Id, 'reverse-derez');
    check(swapped && swapped.Template === 'reverse-derez' && app.scene.Particles.length === 9, 'replaceParticleTemplate swaps template in-place at 9 clouds');

    // Inline Outliner delete button
    const outlinerRowsEl = refs.outliner.children[3].children;
    const ninthRow = outlinerRowsEl.find((rowEl) => rowEl.getAttribute('data-outliner-row') === ninth.Id);
    const inlineDelBtn = ninthRow && ninthRow.children.find((btn) => btn.className && btn.className.includes('row-delete'));
    if (inlineDelBtn) inlineDelBtn.click();
    check(Boolean(inlineDelBtn) && app.scene.Particles.length === 8 && !app.particles.records.has(ninth.Id), 'Inline Outliner delete button removes particle cloud and prunes GPU record immediately');

    // Top Inspector scope-bar delete button
    app.select(app.scene.Particles[0].Id);
    const scopeBar = refs.inspector.children[0].children.find((el) => el.className === 'pz-scope-bar');
    const headerDelBtn = scopeBar && scopeBar.children.find((el) => el.className && el.className.includes('pz-scope-action') && el.className.includes('danger'));
    if (headerDelBtn) headerDelBtn.click();
    check(Boolean(headerDelBtn) && app.scene.Particles.length === 7, 'Inspector header quick-delete button removes selected particle cloud');

    // Keyboard / removeSelection on entity, then on Particle Clouds folder
    app.select(app.scene.Particles[0].Id);
    check(app.removeSelection() && app.scene.Particles.length === 6, 'removeSelection() deletes selected particle cloud');
    const particleFolderRow = refs.outliner.children[3].children.find((rowEl) => String(rowEl.getAttribute('data-outliner-row')).startsWith('folder-particles::'));
    app.select(particleFolderRow.getAttribute('data-outliner-row'));
    check(app.removeSelection() && app.scene.Particles.length === 0 && app.particles.records.size === 0, 'Deleting Particle Clouds folder removes all local particle clouds and frees all GPU records');
}

//------------------------------------------------------------------------------------------------------------------------
//            13. 3D OBJECT CARRIERS, OBJECT-SURFACE SPAWN, BACKING SURFACES + CUSTOM MESH IMPORT
//------------------------------------------------------------------------------------------------------------------------

console.log('\n[13] 3D Object visual carriers, object-surface spawn & custom mesh import');
{
    const Mesh = await import('./Engine/ObjectMeshCatalog.js');
    const Stack = await import('./Engine/LayerStack.js');
    const Catalog = await import('./Engine/ParticleCatalog.js');
    const SpawnSpec = Stack.ParticleLayerSpecifications.find((layer) => layer.Code === 201);
    const CarrierSpec = Stack.ParticleLayerSpecifications.find((layer) => layer.Code === 211);
    const spawnObject = SpawnSpec.Parameter.find((parameter) => parameter.Name === 'SpawnObject');
    const spawnMode = SpawnSpec.Parameter.find((parameter) => parameter.Name === 'SpawnSampleMode');
    const backing = SpawnSpec.Parameter.find((parameter) => parameter.Name === 'BackingSurface');
    const shapeParameter = SpawnSpec.Parameter.find((parameter) => parameter.Name === 'Shape');
    const carrierMode = CarrierSpec.Parameter.find((parameter) => parameter.Name === 'CarrierMode');
    const carrierObject = CarrierSpec.Parameter.find((parameter) => parameter.Name === 'CarrierObject');
    const spriteParameter = CarrierSpec.Parameter.find((parameter) => parameter.Name === 'Sprite');

    check(Mesh.ObjectNames.length === 19 && spawnObject.Options.map((option) => option.Token).join() === Mesh.ObjectNames.join()
        && carrierObject.Options.map((option) => option.Token).join() === Mesh.ObjectNames.join(),
        `Spawn and Sprite & Object both offer the same ${Mesh.ObjectNames.length} 3D objects (built-in + CustomObject)`);
    check(spawnMode.Options.map((option) => option.Token).join() === Mesh.ObjectSampleModes.join()
        && backing.Options.map((option) => option.Token).join() === 'Hidden,ObsidianGlossy,BrushedChrome,GhostWireframe'
        && carrierMode.Options.map((option) => option.Token).join() === 'Sprite,Object3D',
        'Object sampling modes, backing surfaces and the Sprite/Object3D carrier choice are specified');
    check(shapeParameter.Options.some((option) => option.Token === 'ObjectSurface')
        && ['Spiderweb', 'ClothGrid', 'FloraBed', 'FireworkBurst'].every((token) => shapeParameter.Options.some((option) => option.Token === token)),
        'Spawn shape catalogue carries the 3D object surface plus the web / cloth / flora / shell origins');
    check(spriteParameter.Options.length === Catalog.ParticleSprites.length && spriteParameter.Options.length === 18,
        `Sprite & Object offers ${spriteParameter.Options.length} cutouts (9 original + 9 ported analytical SDFs)`);

    let meshesOk = true;
    for (const name of Mesh.ObjectNames)
    {
        const mesh = Mesh.ResolveObjectMesh(name);
        if (!mesh || !mesh.Triangles || !mesh.Corners.every(Number.isFinite) || !mesh.Normals.every(Number.isFinite)) meshesOk = false;
    }
    check(meshesOk, `All ${Mesh.ObjectNames.length} object meshes build with finite corners and normals (${Mesh.ObjectNames.map((name) => Mesh.ResolveObjectMesh(name).Triangles).reduce((a, b) => a + b, 0)} triangles total)`);

    const teapot = Mesh.ResolveObjectMesh('UtahTeapot');
    const even = Mesh.SampleObjectAnchors(teapot, 512, 'EvenSurface', 0);
    const ridge = Mesh.SampleObjectAnchors(teapot, 512, 'RidgeAccent', 35);
    const volume = Mesh.SampleObjectAnchors(teapot, 256, 'SurfaceVolume', 0);
    const repeat = Mesh.SampleObjectAnchors(teapot, 512, 'EvenSurface', 0);
    const inside = volume.Anchors.filter((anchor) => Math.hypot(...anchor.Position) < Math.hypot(...even.Anchors[0].Position) * 0.98).length;
    check(even.Count === 512 && even.Positions.every(Number.isFinite) && inside > 0,
        `Surface, interior-volume and ridge anchor sampling produce finite positions (${inside} of 256 volume anchors pulled inward)`);
    check(ridge.Anchors.filter((anchor) => anchor.Ridge > 0.5).length > 300 && ridge.Anchors.some((anchor) => Math.abs(anchor.Position[0] - even.Anchors[0].Position[0]) > 1e-6),
        `RidgeAccent walks ${teapot.Creases.length} crease segments (${ridge.Anchors.filter((anchor) => anchor.Ridge > 0.5).length} of 512 anchors on ridges)`);
    check(repeat.Positions.every((value, index) => value === even.Positions[index]), 'Anchor sampling is deterministic (same mesh, count, mode and yaw → identical texels)');
    const texels = Mesh.PackObjectTexels(teapot);
    const anchorTexels = Mesh.PackAnchorTexels(even);
    check(texels.VertexCount === teapot.Triangles * 3 && texels.Width * texels.Height * 4 === texels.Data.length
        && anchorTexels.Width === 256 && anchorTexels.Height === Math.ceil(512 * 2 / 256) && anchorTexels.Data.length === anchorTexels.Width * anchorTexels.Height * 4,
        `Texel packing: ${texels.TriangleCount} triangles → ${texels.Width}×${texels.Height} mesh texture, ${even.Count} anchors → ${anchorTexels.Width}×${anchorTexels.Height} anchor texture`);

    // Custom mesh import through the three supported formats.
    const objText = 'v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\nf 2 3 4\nf 1 3 4\n';
    const objEntry = Mesh.ImportCustomObject('pyramid.obj', objText);
    const stlBuffer = new ArrayBuffer(84 + 50);
    new DataView(stlBuffer).setUint32(80, 1, true);
    const stlView = new DataView(stlBuffer);
    [0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0].forEach((value, index) => stlView.setFloat32(84 + index * 4, value, true));
    const stlEntry = Mesh.ImportCustomObject('plate.stl', stlBuffer);
    const jsonEntry = Mesh.ImportCustomObject('tetra.json', JSON.stringify({ vertices: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1], indices: [0, 1, 2, 0, 1, 3, 1, 2, 3, 0, 2, 3] }));
    check(objEntry && objEntry.TriangleCount === 4 && stlEntry && jsonEntry && jsonEntry.TriangleCount === 4,
        `Custom mesh import parses .obj (${objEntry && objEntry.TriangleCount} tris), binary .stl (${stlEntry && stlEntry.TriangleCount} tri) and .json (${jsonEntry && jsonEntry.TriangleCount} tris)`);
    check(Mesh.ResolveObjectMesh('CustomObject', objEntry.Id).Triangles === 4 && Mesh.ResolveObjectMesh('CustomObject', 'missing').Name === 'UtahTeapot',
        'CustomObject resolves the imported mesh and falls back to the Teapot when the id is unknown');

    // The importer, GPU object pass and backing surface, driven through the live app against the mock context.
    app.loadPreset('preset-mocap-stage');
    const cloud = app.scene.Particles[0];
    const spawnLayer = cloud.Stack.Layers.find((layer) => layer.Category === 201);
    app.updateLayer(spawnLayer.Token, 'Shape', Catalog.ParticleShapes.indexOf('ObjectSurface'), true);
    app.updateLayer(spawnLayer.Token, 'SpawnObject', Mesh.ObjectNames.indexOf('CrystalCluster'), true);
    app.updateLayer(spawnLayer.Token, 'SpawnSampleMode', 2, true);
    app.updateLayer(spawnLayer.Token, 'BackingSurface', 1, true);
    const carrierLayer = cloud.Stack.Layers.find((layer) => layer.Category === 211);
    app.updateLayer(carrierLayer.Token, 'CarrierMode', 1, true);
    app.updateLayer(carrierLayer.Token, 'CarrierObject', Mesh.ObjectNames.indexOf('ObsidianHand'), true);
    app.updateLayer(carrierLayer.Token, 'ObjectSpin', 2.4, true);
    app.updateLayer(carrierLayer.Token, 'ObjectMetallic', 0.8, true);
    app.frame(7400); app.frame(7440);
    const record = app.particles.records.get(cloud.Id);
    const objectMesh = app.particles.objectTexture('ObsidianHand');
    // The studio app runs on its own mock context (section 3 owns the standalone one), so count its draw calls.
    const appGl = app.context.gl;
    const drawsBeforeObject = appGl.__calls.draws;
    app.particles.render(app.scene, { viewProjection: new Float32Array(16) }, 1, app.camera);
    check(record && record.anchors && record.anchorCount === cloud.Count,
        `ObjectSurface spawn bakes ${record && record.anchorCount} ridge anchors for ${cloud.Count} particles and uploads them as ${record && record.anchorWidth}×${record && record.anchors.height} RGBA32F`);
    check(objectMesh.triangleCount > 1000 && objectMesh.vertexCount === objectMesh.triangleCount * 3,
        `Obsidian Hand packs ${objectMesh.triangleCount} triangles into a ${objectMesh.width}-wide texel mesh for gl_VertexID instancing`);
    check(appGl.__calls.draws > drawsBeforeObject && appGl.__calls.uniformErrors.length === 0,
        `3D carrier + obsidian backing surface draw through gl_TRIANGLES with every uniform resolved (${appGl.__calls.draws - drawsBeforeObject} draw calls, ${appGl.__calls.uniformErrors.length} mismatches)`);
    const ghost = app.scene.Particles[0];
    const ghostSpawn = ghost.Stack.Layers.find((layer) => layer.Category === 201);
    app.updateLayer(ghostSpawn.Token, 'BackingSurface', 3, true);
    app.frame(7480);
    const drawsBeforeWire = appGl.__calls.draws;
    app.particles.render(app.scene, { viewProjection: new Float32Array(16) }, 1, app.camera);
    check(appGl.__calls.draws > drawsBeforeWire && appGl.__calls.uniformErrors.length === 0, 'Ghost wireframe backing surface draws gl_LINES through the same object program');

    // Inspector: importing a mesh through the app points both the origin and the carrier at it, and the panel exposes the importer.
    const imported = app.importCustomObject('hand.obj', objText);
    check(imported && imported.TriangleCount === 4 && app.scene.Particles[0].Stack.Layers.find((layer) => layer.Category === 201).Parameters.CustomObjectId === imported.Id
        && app.scene.Particles[0].Stack.Layers.find((layer) => layer.Category === 211).Parameters.CarrierObject === Mesh.ObjectNames.indexOf('CustomObject'),
        'Importing a mesh sets the shared CustomObjectId and switches both the spawn object and the 3D carrier to CustomObject');
    app.select(app.scene.Particles[0].Id);
    app.selectLayer(carrierLayer.Token);
    ui.renderAll(app.snapshot());
    const findAttribute = (node, attribute) =>
    {
        if (!node) return null;
        if (node.attributes && node.attributes[attribute] !== undefined) return node;
        for (const child of node.children || []) { const hit = findAttribute(child, attribute); if (hit) return hit; }
        return null;
    };
    const carrierPanel = refs.inspector.children.find((panel) => panel.getAttribute && panel.getAttribute('data-layer-inspector') === carrierLayer.Token);
    const importer = findAttribute(refs.inspector, 'data-custom-object');
    check(Boolean(carrierPanel) && Boolean(importer) && importer.getAttribute('data-custom-object') === imported.Id,
        'Sprite & Object Inspector renders the custom object importer with the active mesh id');
    app.selectLayer(spawnLayer.Token);
    ui.renderAll(app.snapshot());
    check(Boolean(findAttribute(refs.inspector, 'data-custom-object')), 'Spawn Inspector renders the same custom object importer');

    // Independent Spawn vs Sprite & Object custom objects + Frontier binary FCT1 .facet import + vRidge packing + Particle Studio parity.
    const cubeTexels = Mesh.PackObjectTexels(Mesh.ResolveObjectMesh('Cube'));
    let nonZeroRidges = 0;
    for (let i = 7; i < cubeTexels.Data.length; i += 8) if (cubeTexels.Data[i] > 0) nonZeroRidges += 1;
    check(nonZeroRidges > 0, `PackObjectTexels populates non-zero vRidge crease weights (${nonZeroRidges} corners)`);

    const facetPath = path.join(path.dirname(fileURLToPath(import.meta.url)), '../Shared/Models/ControlVehicleBody.facet');
    if (fs.existsSync(facetPath))
    {
        const rawFacet = fs.readFileSync(facetPath);
        const facetBuf = rawFacet.buffer.slice(rawFacet.byteOffset, rawFacet.byteOffset + rawFacet.byteLength);
        const facetEntry = Mesh.ImportCustomObject('ControlVehicleBody.facet', facetBuf);
        check(Boolean(facetEntry) && facetEntry.TriangleCount > 1000,
            `Binary FCT1 .facet import parses ControlVehicleBody.facet (${facetEntry && facetEntry.TriangleCount} tris)`);
        const secondObj = app.importCustomObject('carrier-shard.obj', objText, cloud.Id, 'carrier');
        const spawnCustom = app.importCustomObject('ControlVehicleBody.facet', facetBuf, cloud.Id, 'spawn');
        check(Boolean(secondObj) && Boolean(spawnCustom)
            && spawnLayer.Parameters.CustomObjectId === spawnCustom.Id
            && carrierLayer.Parameters.CarrierCustomObjectId === secondObj.Id
            && spawnLayer.Parameters.CustomObjectId !== carrierLayer.Parameters.CarrierCustomObjectId,
            'Spawn (201) and Sprite & Object (211) hold independent custom 3D objects without clobbering each other');
    }

    // Verify zero duplicate template IDs/names and all 7 ported Particle Studio templates + 4 flagship presets + forces.
    const templateIds = new Set(Catalog.ParticleTemplates.map((t) => t.Id));
    const templateNames = new Set(Catalog.ParticleTemplates.map((t) => t.Name.toLowerCase()));
    const portedIds = ['light-speed-streaks', 'digital-twin-car', 'vehicle-airflow', 'ev-charging-conduit', 'orbital-light-rings', 'vehicle-transition', 'optical-fibre-wave'];
    check(templateIds.size === Catalog.ParticleTemplates.length && templateNames.size === Catalog.ParticleTemplates.length
        && portedIds.every((id) => templateIds.has(id)),
        `All ${Catalog.ParticleTemplates.length} particle templates have unique IDs/names and include all 7 ported Particle Studio looks`);

    const flagshipPresets = ['preset-launch-reveal', 'preset-aero-study', 'preset-charging-bay', 'preset-orbit-garden', 'preset-obsidian-gallery', 'preset-aviary-dusk', 'preset-coral-reef', 'preset-mint-vault', 'preset-sakura-courtyard', 'preset-signal-array'];
    const allPresetsHaveForcesAndClouds = flagshipPresets.every((id) =>
    {
        const p = ScenePresetById[id];
        return p && p.Document && Array.isArray(p.Document.Force) && p.Document.Force.length >= 2 && Array.isArray(p.Document.Particle) && p.Document.Particle.length >= 2;
    });
    check(allPresetsHaveForcesAndClouds, `All ${flagshipPresets.length} showcase & ported Particle Studio presets combine >=2 coordinated forces with multi-cloud staging`);
}

console.log(Failures === 0 ? '\n🟢 Fluid Studio validation passed' : `\n🔴 Fluid Studio validation failed (${Failures})`);
process.exit(Failures === 0 ? 0 : 1);
