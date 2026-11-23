//============================================================================================================================================
//                                                       PRESETDOCUMENTVALIDATION.JS
//============================================================================================================================================
// 🧩 Rules for `frontier-fluid-preset` and `frontier-particle-template` documents. Kept apart from PresetScenes.js so the generator can check documents before the bundle
// (Engine/PresetLibrary.js) exists.

import { SourceLayerCode, SpawnLayerCode, LayerSpecificationByCode } from './LayerStack.js';

export const PresetDocumentFormat = 'frontier-fluid-preset';
// Revision 2 (phase 4) adds keyed tracks (`Track` on Volume, Emitter, Force and Particle entries) and an optional [Timeline].
export const PresetDocumentRevision = 2;

/** Rules a preset document must follow; an empty list means it loads. */
export function validatePresetDocument(document)
{
    const problems = [];
    const label = (document.Scene && document.Scene.Id) || 'preset';
    if (!document.Document || document.Document.Format !== PresetDocumentFormat) problems.push(`${label}: Document.Format must be "${PresetDocumentFormat}"`);
    if (document.Document && document.Document.Revision > PresetDocumentRevision) problems.push(`${label}: revision ${document.Document.Revision} is newer than this editor`);
    if (!document.Scene || !document.Scene.Id || !document.Scene.Name) problems.push(`${label}: Scene needs Id and Name`);
    if (!document.Grid || !Array.isArray(document.Grid.Resolution)) problems.push(`${label}: Grid.Resolution is required`);
    const volumes = document.Volume || [];
    if (volumes.length !== 1) problems.push(`${label}: exactly one gas volume expected (found ${volumes.length})`);
    const emitterTokens = new Set((document.Emitter || []).map((emitter) => emitter.Token));
    if (emitterTokens.size !== (document.Emitter || []).length) problems.push(`${label}: every emitter needs a unique Token`);
    for (const volume of volumes) problems.push(...validateTracks(`${label} volume`, volume.Track, volume.Layer || []));
    (document.Emitter || []).forEach((entry) => problems.push(...validateTracks(`${label} emitter ${entry.Token}`, entry.Track, null)));
    (document.Force || []).forEach((entry, index) => problems.push(...validateTracks(`${label} force ${index + 1}`, entry.Track, null)));
    (document.Particle || []).forEach((entry, index) => problems.push(...validateTracks(`${label} particle ${index + 1}`, entry.Track, entry.Layer || [])));
    if (document.Timeline && !(document.Timeline.End > 0)) problems.push(`${label}: Timeline.End must be above 0`);
    for (const volume of volumes)
        for (const layer of volume.Layer || [])
        {
            if (layer.Category === SourceLayerCode && !emitterTokens.has(layer.Parameters.Emitter)) problems.push(`${label}: Source layer ${layer.Token} links a missing emitter`);
            if (!LayerSpecificationByCode[layer.Category]) problems.push(`${label}: layer ${layer.Token} has unknown category ${layer.Category}`);
        }
    (document.Particle || []).forEach((entry, index) =>
    {
        if (!entry.Template) problems.push(`${label}: particle ${index + 1} needs a Template`);
        if (entry.Layer) problems.push(...validateParticleLayers(`${label} particle ${index + 1}`, entry.Layer));
    });
    for (const emitter of document.Emitter || [])
        if (!volumes.some((volume) => (volume.Layer || []).some((layer) => layer.Category === SourceLayerCode && layer.Parameters.Emitter === emitter.Token))) problems.push(`${label}: emitter ${emitter.Token} has no Source layer`);
    return problems;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE TEMPLATES (phase 3)
//------------------------------------------------------------------------------------------------------------------------

export const ParticleTemplateFormat = 'frontier-particle-template';
export const ParticleTemplateRevision = 2;

/** Rules a `frontier-particle-template` document must follow; an empty list means it loads. */
export function validateParticleTemplateDocument(document)
{
    const problems = [];
    const label = (document.Template && document.Template.Id) || 'template';
    if (!document.Document || document.Document.Format !== ParticleTemplateFormat) problems.push(`${label}: Document.Format must be "${ParticleTemplateFormat}"`);
    if (document.Document && document.Document.Revision > ParticleTemplateRevision) problems.push(`${label}: revision ${document.Document.Revision} is newer than this editor`);
    if (!document.Template || !document.Template.Id || !document.Template.Name || !document.Template.Category) problems.push(`${label}: Template needs Id, Name and Category`);
    problems.push(...validateParticleLayers(label, document.Layer));
    problems.push(...validateTracks(label, document.Track, document.Layer || []));
    return problems;
}

/** Layer rules shared by templates and the particle entries of preset documents. */
export function validateParticleLayers(label, layers = [])
{
    const problems = [];
    if (!layers.length || layers[0].Category !== SpawnLayerCode) problems.push(`${label}: the first layer must be Spawn (${SpawnLayerCode})`);
    const tokens = new Set();
    for (const layer of layers)
    {
        const specification = LayerSpecificationByCode[layer.Category];
        if (!specification) problems.push(`${label}: layer ${layer.Token} has unknown category ${layer.Category}`);
        else if (specification.Stack !== 'ParticleCloud') problems.push(`${label}: layer ${layer.Token} (${specification.Name}) belongs to a ${specification.Stack}`);
        if (tokens.has(layer.Token)) problems.push(`${label}: layer token ${layer.Token} is used twice`);
        tokens.add(layer.Token);
    }
    return problems;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  KEYED TRACKS (phase 4)
//------------------------------------------------------------------------------------------------------------------------

const KeyableTypes = ['Float32', 'Int32', 'UInt32', 'Vector3', 'ColourLinear', 'Bool', 'Choice'];
const TimingFields = ['StartTime', 'Duration', 'Fade', 'Repeat'];

/**
 * Track rules. `layers` given: paths are `Weight` (the owner's timing) or `<layer token>/<Parameter | Weight>` naming a keyable parameter.
 * `layers` null (emitter, force): paths name a field of the entry. Keys need finite, strictly increasing times and an Ease code 0–3.
 */
export function validateTracks(label, tracks, layers)
{
    const problems = [];
    if (tracks === undefined) return problems;
    if (!Array.isArray(tracks)) return [`${label}: Track must be a list`];
    const paths = new Set();
    for (const track of tracks)
    {
        const path = track && track.Path;
        if (typeof path !== 'string' || !path) { problems.push(`${label}: a track has no Path`); continue; }
        if (paths.has(path)) problems.push(`${label}: track ${path} appears twice`);
        paths.add(path);
        if (track.Extrapolation !== undefined && ![0, 1, 2].includes(track.Extrapolation)) problems.push(`${label}: track ${path} Extrapolation must be 0 Hold, 1 Cycle or 2 Ping-pong`);
        const slash = path.indexOf('/');
        if (slash >= 0)
        {
            if (!layers) problems.push(`${label}: track ${path} names a layer, but this entry has no layer stack`);
            else
            {
                const layer = layers.find((candidate) => candidate.Token === path.slice(0, slash));
                const name = path.slice(slash + 1);
                const specification = layer && LayerSpecificationByCode[layer.Category];
                if (!layer) problems.push(`${label}: track ${path} names a missing layer`);
                else if (specification && name === 'Weight' && (specification.Required || specification.Linked)) problems.push(`${label}: track ${path}: ${specification.Name} has no Weight`);
                else if (specification && name !== 'Weight')
                {
                    const parameter = specification.Parameter.find((candidate) => candidate.Name === name);
                    if (!parameter) problems.push(`${label}: track ${path}: ${specification.Name} has no parameter ${name}`);
                    else if (!KeyableTypes.includes(parameter.Type)) problems.push(`${label}: track ${path}: ${parameter.Type} parameters are not keyable`);
                }
            }
        }
        else if (TimingFields.includes(path) || !/^[A-Z][A-Za-z0-9]*$/.test(path)) problems.push(`${label}: track ${path} is not a keyable field`);
        const keys = track.Keys;
        if (!Array.isArray(keys) || keys.length === 0) { problems.push(`${label}: track ${path} has no keys`); continue; }
        keys.forEach((key, index) =>
        {
            if (!key || !Number.isFinite(key.Time)) problems.push(`${label}: track ${path} key ${index + 1} needs a Time`);
            else if (index > 0 && !(key.Time > keys[index - 1].Time)) problems.push(`${label}: track ${path} keys must be in increasing Time`);
            if (!key || key.Amount === undefined) problems.push(`${label}: track ${path} key ${index + 1} needs an Amount`);
            if (key && key.Ease !== undefined && ![0, 1, 2, 3].includes(key.Ease)) problems.push(`${label}: track ${path} key ${index + 1} Ease must be 0–3`);
        });
    }
    return problems;
}
