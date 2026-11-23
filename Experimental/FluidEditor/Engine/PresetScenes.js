//============================================================================================================================================
//                                                              PRESETSCENES.JS
//============================================================================================================================================
// 🧩 Scene presets as data (Fluid layer stack plan §4, phase 2). Each preset is a `frontier-fluid-preset` TOML document in Presets/,
// bundled into Engine/PresetLibrary.js by GeneratePresetLibrary.mjs. Behaviour and shading live in the gas layer stack; grid, emitters,
// force fields and colliders are the document's flat entries (force fields stay flat by design); each particle system entry carries its
// own particle layer stack (phase 3).

import { PresetDocuments, PresetLibraryHash } from './PresetLibrary.js';
import { createDomain, createEmitter, createForceGenerator, createCollider, createVisualizerSet } from './ForceGeneratorCatalog.js';
import { createParticleSystem } from './ParticleCatalog.js';
import { evaluateParticleStack, applyParticleEvaluation } from './LayerStack.js';
import { readGasStack, evaluateGasStack, applyGasEvaluation, SourceLayerCode, SpecificationHash, LayerSpecificationByCode } from './LayerStack.js';
import { canonicalJson, specificationHash } from './LayerSpecificationValidation.js';

export { PresetLibraryHash };
export { PresetDocumentFormat, PresetDocumentRevision, validatePresetDocument } from './PresetDocumentValidation.js';
import { PresetDocumentFormat, PresetDocumentRevision } from './PresetDocumentValidation.js';

// Volume Render settings that are renderer choices, not look: they stay outside the stack.
export const RenderKeys = Object.freeze(['Exposure', 'Visible', 'Steps', 'Filter']);

/** Summary rows for the scene browser, plus the flat entries the catalogue checks read. */
export const ScenePresets = Object.freeze(PresetDocuments.map((document) => Object.freeze({
    Id: document.Scene.Id,
    Name: document.Scene.Name,
    Family: document.Scene.Family,
    Icon: document.Scene.Icon,
    Summary: document.Scene.Summary,
    Boundary: document.Scene.Boundary,
    Grid: document.Grid,
    Emitters: document.Emitter || [],
    Forces: document.Force || [],
    Colliders: document.Collider || [],
    Particles: document.Particle || [],
    Document: document,
})));
export const ScenePresetById = Object.freeze(Object.fromEntries(ScenePresets.map((preset) => [preset.Id, preset])));

const strip = (entry, keys) => Object.fromEntries(Object.entries(entry).filter(([key]) => !keys.includes(key)));
const clone = (value) => JSON.parse(JSON.stringify(value));

/** A particle entry's `Track` list holds both kinds: `Weight` (the cloud's timing) and `layer-N/Parameter` (its stack). */
function particleOverrides(entry)
{
    const tracks = entry.Track || [];
    const overrides = { ...strip(entry, ['Template', 'Layer', 'Track']), Tracks: tracks.filter((track) => !String(track.Path).includes('/')) };
    if (entry.Layer) overrides.Stack = { Layer: entry.Layer, Track: tracks.filter((track) => String(track.Path).includes('/')) };
    return overrides;
}

/** Tracks as a document writes them (`Track`), only when there are any. */
const trackEntry = (tracks) => (tracks && tracks.length ? { Track: clone(tracks) } : {});

/** Builds a live scene from a preset document: entities get fresh ids, Source layers are relinked, the stack is evaluated. */
export function buildSceneFromDocument(document)
{
    const scene = {
        Name: document.Scene.Name,
        PresetId: document.Scene.Id,
        Domain: createDomain({ ...document.Grid, ...(document.Scene.Boundary ? { Boundary: document.Scene.Boundary } : {}) }),
        Emitters: [],
        Forces: (document.Force || []).map((force) => createForceGenerator(force.Type, force)),
        Colliders: (document.Collider || []).map((collider) => createCollider(collider)),
        // Particle entries carry their own layer stack (phase 3); entries without one (older documents) are template + flat overrides.
        Particles: (document.Particle || []).map((node) => createParticleSystem(node.Template, particleOverrides(node))),
        Visualizers: createVisualizerSet(),
    };
    const relink = new Map();
    for (const entry of document.Emitter || [])
    {
        const emitter = createEmitter(strip(entry, ['Token']));
        relink.set(entry.Token, emitter.Id);
        scene.Emitters.push(emitter);
    }
    Object.assign(scene.Visualizers.Volume, document.Render || {});
    Object.assign(scene.Visualizers.Debris, document.Debris || {});
    const stack = readGasStack(JSON.parse(JSON.stringify(document.Volume[0])));
    for (const layer of stack.Layers) if (layer.Category === SourceLayerCode) layer.Parameters.Emitter = relink.get(layer.Parameters.Emitter) || layer.Parameters.Emitter;
    scene.GasStack = stack;
    if (document.Timeline) scene.Timeline = { ...document.Timeline };
    applyGasEvaluation(scene, evaluateGasStack(stack, scene.Emitters, 0));
    return scene;
}

/**
 * Identity of a resolved scene for conversion checks: every simulated and rendered field, with runtime ids (`emitter-1f` …) replaced
 * by their order of appearance so two loads compare equal. The stacks themselves are excluded; what they evaluate to is included.
 */
export function sceneFingerprint(scene)
{
    // Keyed stacks fold their amount at the playhead into the flat fields; the identity is the static fold (what a load builds).
    const keyed = (stack) => Boolean(stack && stack.Tracks && stack.Tracks.length);
    if (keyed(scene.GasStack) || scene.Particles.some((node) => keyed(node.Stack))) scene = staticFold(scene);
    const { Name, Domain, Emitters, Forces, Colliders, Particles, Visualizers } = scene;
    const ids = new Map();
    // Keyed tracks change what is simulated over time, so they are part of the identity; empty track lists are not (scenes without keys
    // keep the fingerprints recorded at conversion). Stack track paths use the layer's position, since layer tokens are fresh per load.
    const stackTracks = (stack) => (stack && stack.Tracks && stack.Tracks.length
        ? stack.Tracks.map((track) => { const [token, ...rest] = track.Path.split('/'); return { ...track, Path: [`#${stack.Layers.findIndex((layer) => layer.Token === token)}`, ...rest].join('/') }; })
        : undefined);
    const gasTracks = stackTracks(scene.GasStack);
    const text = JSON.stringify({ Name, Domain, Emitters, Forces, Colliders, Particles, Visualizers, ...(gasTracks ? { GasTracks: gasTracks } : {}) }, (key, value) =>
    {
        if (key === 'Tracks' && Array.isArray(value) && value.length === 0) return undefined;
        if (key === 'Stack') return stackTracks(value) ? { Tracks: stackTracks(value) } : undefined;      // particle layer stacks: their evaluation (the node fields) is included
        if (typeof value === 'string' && /^(force|emitter|collider|particles)-[0-9a-z]+$/.test(value))
        {
            if (!ids.has(value)) ids.set(value, `node-${ids.size + 1}`);
            return ids.get(value);
        }
        return value;
    });
    return specificationHash(JSON.parse(text));
}

/** A copy of the scene with every keyed stack folded statically (no playhead), as `buildSceneFromDocument` would fold it. */
function staticFold(scene)
{
    const copy = { ...scene, Domain: { ...scene.Domain }, Visualizers: { ...scene.Visualizers, Volume: { ...scene.Visualizers.Volume } }, Particles: scene.Particles.map((node) => ({ ...node })) };
    if (copy.GasStack && copy.GasStack.Tracks && copy.GasStack.Tracks.length) applyGasEvaluation(copy, evaluateGasStack(copy.GasStack, copy.Emitters));
    for (const node of copy.Particles) if (node.Stack && node.Stack.Tracks && node.Stack.Tracks.length)
    {
        const evaluation = evaluateParticleStack(node.Stack);
        if (!evaluation.Stopped) applyParticleEvaluation(node, evaluation);
    }
    return copy;
}

/** Library hash over every document (canonical), so a changed preset is visible in the validation log. */
export function presetLibraryHash(documents = PresetDocuments) { return specificationHash(documents.map((document) => JSON.parse(canonicalJson(document)))); }

/** A particle system as a preset entry: identity and timing flat, everything the GPU reads in its layer stack. */
export function particleEntry(node)
{
    const entry = { Template: node.Template, Name: node.Name };
    if (!node.Enabled) entry.Enabled = false;
    if (!node.Visible) entry.Visible = false;
    for (const key of ['StartTime', 'Duration', 'Fade', 'Repeat']) entry[key] = node[key];
    const tokens = new Map(node.Stack.Layers.map((layer, index) => [layer.Token, `layer-${index + 1}`]));
    entry.Layer = node.Stack.Layers.map((layer) => ({ ...JSON.parse(JSON.stringify(layer)), Token: tokens.get(layer.Token) }));
    const layerTracks = (node.Stack.Tracks || []).map((track) => { const [token, ...rest] = track.Path.split('/'); return { ...clone(track), Path: [tokens.get(token) || token, ...rest].join('/') }; });
    Object.assign(entry, trackEntry([...(node.Tracks || []), ...layerTracks]));
    return entry;
}

/** Current scene → preset document (Save as preset). Inverse of buildSceneFromDocument for everything the scene owns. */
export function sceneToDocument(scene, summary = {})
{
    const tokens = new Map(scene.Emitters.map((emitter, index) => [emitter.Id, `emitter-${index + 1}`]));
    const grid = Object.fromEntries(['Resolution', 'Size', 'MaxCells', 'Grid', 'TileSize', 'TileResolution', 'TilePool', 'Tiles', 'Follow']
        .filter((key) => scene.Domain[key] !== undefined).map((key) => [key, scene.Domain[key]]));
    const stack = JSON.parse(JSON.stringify(scene.GasStack));
    for (const layer of stack.Layers) if (layer.Category === SourceLayerCode) layer.Parameters.Emitter = tokens.get(layer.Parameters.Emitter) || layer.Parameters.Emitter;
    const drop = ['Id', 'Kind', 'Tracks'];
    const document = {
        Document: { Format: PresetDocumentFormat, Revision: PresetDocumentRevision, SpecificationHash },
        Scene: { Id: summary.Id || scene.PresetId || 'preset-custom', Name: summary.Name || scene.Name, Family: summary.Family || 'Custom', Icon: summary.Icon || 'scene', Summary: summary.Summary || '', Boundary: scene.Domain.Boundary },
        Grid: grid,
        Render: Object.fromEntries(RenderKeys.map((key) => [key, scene.Visualizers.Volume[key]])),
        Debris: strip(scene.Visualizers.Debris, drop.concat(['Name'])),
        ...(scene.Timeline ? { Timeline: { ...scene.Timeline } } : {}),
        Volume: [{ Token: stack.Token, Category: stack.Category, Layer: stack.Layers, ...trackEntry(stack.Tracks) }],
        Emitter: scene.Emitters.map((emitter) => ({ Token: tokens.get(emitter.Id), ...strip(emitter, drop), ...trackEntry(emitter.Tracks) })),
        Force: scene.Forces.map((force) => ({ ...strip(force, drop), ...trackEntry(force.Tracks) })),
        Collider: scene.Colliders.map((collider) => strip(collider, drop)),
        Particle: scene.Particles.map((node) => particleEntry(node)),
    };
    return document;
}
