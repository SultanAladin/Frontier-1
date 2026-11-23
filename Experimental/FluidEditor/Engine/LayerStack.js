//============================================================================================================================================
//                                                               LAYERSTACK.JS
//============================================================================================================================================
// 🧩 Gas volume layer stack (Fluid layer stack plan, phases 1–2). A gas volume's behaviour is an ordered list of layers, each an instance of
// a LayerSpecifications.toml category. Evaluation folds the enabled layers, in order, into the flat blocks the solver and volume renderer
// already read (scene.Domain and scene.Visualizers.Volume), so the GPU code is unchanged and a converted preset simulates bit-identically.
//
// Unknown categories and unknown parameters are kept verbatim (inert) and written back unchanged, so newer files never lose data here.

import { LayerSpecificationDocument, SpecificationHash } from './LayerSpecifications.js';
import { parseToml, writeToml } from './TomlSubset.js';

function deepFreeze(value)
{
    if (value && typeof value === 'object') { Object.values(value).forEach(deepFreeze); Object.freeze(value); }
    return value;
}

export { SpecificationHash };
import { readTracks, renameTrackLayers, sampleTrackAs, splitLayerPath } from './KeyTracks.js';
export const LayerDocumentFormat = 'frontier-effect-stack';
export const LayerDocumentRevision = 1;
export const MaximumLayers = LayerSpecificationDocument.Document.MaximumLayers;
export const LayerSpecifications = deepFreeze(JSON.parse(JSON.stringify(LayerSpecificationDocument.Layer)));
export const LayerSpecificationByCode = Object.freeze(Object.fromEntries(LayerSpecifications.map((layer) => [layer.Code, layer])));
export const GasLayerSpecifications = Object.freeze(LayerSpecifications.filter((layer) => layer.Stack === 'GasVolume'));
export const ParticleLayerSpecifications = Object.freeze(LayerSpecifications.filter((layer) => layer.Stack === 'ParticleCloud'));
export const GasVolumeCategory = 1;
export const ParticleCloudCategory = 2;
export const SpawnLayerCode = 201;
export const PhaseOrder = Object.freeze(['Inject', 'Advance', 'Shade', 'Overlay']);
export const BlendModes = Object.freeze([
    Object.freeze({ Code: 0, Token: 'Replace', Label: 'Replace' }),
    Object.freeze({ Code: 1, Token: 'Add', Label: 'Add' }),
    Object.freeze({ Code: 2, Token: 'Multiply', Label: 'Multiply' }),
    Object.freeze({ Code: 3, Token: 'Maximum', Label: 'Maximum' }),
]);
export const DomainLayerCode = 101;
export const SourceLayerCode = 102;

// Every parameter that writes into an evaluated block, by block and field name → { Layer, Parameter }.
const ownersOf = (specifications, targets) => Object.freeze(Object.fromEntries(targets.map((target) => [target, Object.freeze(Object.fromEntries(
    specifications.flatMap((layer) => layer.Parameter.filter((parameter) => parameter.Target === target).map((parameter) => [parameter.Name, Object.freeze({ Layer: layer, Parameter: parameter })])),
))])));
export const ParameterOwners = ownersOf(GasLayerSpecifications, ['Domain', 'Volume']);
export const ParticleParameterOwners = ownersOf(ParticleLayerSpecifications, ['Particles']);
/** Per stack kind: its category code, layer specifications, owned blocks and default token. */
export const StackKinds = Object.freeze({
    GasVolume: Object.freeze({ Category: GasVolumeCategory, Specifications: GasLayerSpecifications, Owners: ParameterOwners, Token: 'gas-volume' }),
    ParticleCloud: Object.freeze({ Category: ParticleCloudCategory, Specifications: ParticleLayerSpecifications, Owners: ParticleParameterOwners, Token: 'particle-cloud' }),
});
const kindOfCategory = (category) => Object.values(StackKinds).find((kind) => kind.Category === category) || StackKinds.GasVolume;
const stackNameOf = (kind) => Object.keys(StackKinds).find((name) => StackKinds[name] === kind);

let LayerSequence = 0;
function allocateLayerToken(specification) { LayerSequence += 1; return `layer-${specification ? specification.Token : 'unknown'}-${LayerSequence}`; }

const clone = (value) => (value === undefined ? undefined : JSON.parse(JSON.stringify(value)));
const choiceToken = (parameter, code) => ((parameter.Options || []).find((option) => option.Code === code) || parameter.Options[0]).Token;
const choiceCode = (parameter, token) => { const option = (parameter.Options || []).find((entry) => entry.Token === token || entry.Code === token); return option ? option.Code : parameter.Default; };

/** Clamps / coerces an amount to its parameter type, or returns the Default when the amount is unusable. */
export function coerceAmount(parameter, value)
{
    switch (parameter.Type)
    {
        case 'Float32': return Number.isFinite(value) ? Math.min(parameter.Maximum, Math.max(parameter.Minimum, value)) : parameter.Default;
        case 'Int32': case 'UInt32': return Number.isFinite(value) ? Math.round(Math.min(parameter.Maximum, Math.max(parameter.Minimum, value))) : parameter.Default;
        case 'Bool': return typeof value === 'boolean' ? value : parameter.Default;
        case 'ColourLinear': return typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value) ? value.toLowerCase() : parameter.Default;
        case 'Choice': return (parameter.Options || []).some((option) => option.Code === value) ? value : choiceCode(parameter, value);
        case 'Token': return typeof value === 'string' ? value : parameter.Default;
        case 'Vector3': return Array.isArray(value) && value.length === 3 && value.every(Number.isFinite) ? [...value] : clone(parameter.Default);
        default: return clone(value);
    }
}

export function createLayer(categoryCode, overrides = {})
{
    const specification = LayerSpecificationByCode[categoryCode];
    if (!specification) throw new Error(`Unknown layer category ${categoryCode}`);
    const parameters = {};
    for (const parameter of specification.Parameter)
        parameters[parameter.Name] = coerceAmount(parameter, overrides.Parameters && parameter.Name in overrides.Parameters ? overrides.Parameters[parameter.Name] : clone(parameter.Default));
    return {
        Token: overrides.Token || allocateLayerToken(specification),
        Category: categoryCode,
        Enabled: overrides.Enabled ?? true,
        Solo: overrides.Solo ?? false,
        Weight: Number.isFinite(overrides.Weight) ? Math.min(1, Math.max(0, overrides.Weight)) : 1,
        Blend: BlendModes.some((mode) => mode.Code === overrides.Blend) ? overrides.Blend : 0,
        Parameters: parameters,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PRESET CONVERSION
//------------------------------------------------------------------------------------------------------------------------

/** The amount an evaluated block holds for a parameter, in specification units (Choice → code). */
function blockAmount(block, parameter)
{
    const value = block[parameter.Name];
    return parameter.Type === 'Choice' ? choiceCode(parameter, value) : value;
}

/**
 * Builds the gas stack that reproduces an already-resolved scene exactly: the Domain layer always, a Source layer per emitter, and each
 * behaviour or shading layer whenever any of its parameters differs from the Neutral amount. Amounts are copied, never rounded or
 * clamped, so evaluate(convert(scene)) equals the scene field for field (FluidValidation checks all 36 presets).
 */
export function convertSceneToGasStack(scene)
{
    const layers = [];
    for (const specification of GasLayerSpecifications)
    {
        if (specification.Code === SourceLayerCode)
        {
            for (const emitter of scene.Emitters || []) layers.push(createLinkedSource(emitter));
            continue;
        }
        const block = (specification.Parameter[0].Target === 'Volume') ? scene.Visualizers.Volume : scene.Domain;
        const amounts = Object.fromEntries(specification.Parameter.map((parameter) => [parameter.Name, blockAmount(block, parameter)]));
        const differs = specification.Parameter.some((parameter) => JSON.stringify(amounts[parameter.Name]) !== JSON.stringify(parameter.Neutral));
        if (!specification.Required && !differs) continue;
        const layer = createLayer(specification.Code);
        layer.Parameters = amounts;
        layers.push(layer);
    }
    return { Token: 'gas-volume', Category: GasVolumeCategory, Layers: layers, Tracks: [] };
}

export function createLinkedSource(emitter)
{
    return createLayer(SourceLayerCode, { Enabled: emitter.Enabled !== false, Parameters: { Emitter: emitter.Id } });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  EVALUATION
//------------------------------------------------------------------------------------------------------------------------

function mixColour(from, to, amount)
{
    if (amount >= 1) return to;
    if (amount <= 0) return from;
    // Mix in linear light (plan §3.1 rule 7), then back to sRGB hex.
    const toLinear = (channel) => { const c = channel / 255; return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; };
    const toSrgb = (linear) => Math.round(255 * Math.min(1, Math.max(0, linear <= 0.0031308 ? linear * 12.92 : 1.055 * linear ** (1 / 2.4) - 0.055)));
    const parse = (hex) => [1, 3, 5].map((offset) => parseInt(hex.slice(offset, offset + 2), 16));
    const a = parse(from), b = parse(to);
    return '#' + a.map((channel, index) => toSrgb(toLinear(channel) + (toLinear(b[index]) - toLinear(channel)) * amount).toString(16).padStart(2, '0')).join('');
}

function blendAmount(parameter, current, amount, weight, blend)
{
    if (parameter.Type === 'ColourLinear') return mixColour(current, amount, weight);
    if (parameter.Type === 'Float32' || parameter.Type === 'Int32' || parameter.Type === 'UInt32')
    {
        // Weight 1 + Replace copies the amount exactly (no floating-point drift on converted presets).
        let next;
        if (blend === 1) next = current + (amount - parameter.Neutral) * weight;
        else if (blend === 2) next = current * (1 + (amount - 1) * weight);
        else if (blend === 3) next = Math.max(current, weight >= 1 ? amount : current + (amount - current) * weight);
        else next = weight >= 1 ? amount : current + (amount - current) * weight;
        return parameter.Type === 'Float32' ? next : Math.round(next);
    }
    // Vector3 Replace / Add blend per component (weight 1 Replace copies exactly).
    if (parameter.Type === 'Vector3' && Array.isArray(current) && Array.isArray(amount))
    {
        if (blend === 1) return current.map((component, axis) => component + (amount[axis] - parameter.Neutral[axis]) * weight);
        if (blend === 0 && weight < 1) return current.map((component, axis) => component + (amount[axis] - component) * weight);
        return [...amount];
    }
    // Bool, Choice, Ramp8, Token: a contributing layer (Weight > 0) replaces.
    return Array.isArray(amount) ? [...amount] : amount;
}

/**
 * Folds the stack into evaluated blocks. Returns { Domain, Volume, Links, Problems, Stopped, Contributing }. `Stopped` is a reason when the
 * volume must not simulate (missing or misplaced required layer, capacity exceeded); the rest of the scene keeps running.
 */
export function evaluateLayerStack(stack, emitters = [], time = undefined)
{
    // Keyed amounts (phase 4): at `time`, every keyed parameter or Weight replaces the layer's static amount before blending.
    const keyed = Number.isFinite(time) && stack && stack.Tracks && stack.Tracks.length ? keyedLayers(stack, time) : null;
    const kind = kindOfCategory(stack && stack.Category);
    const kindName = stackNameOf(kind);
    const blocks = Object.fromEntries(Object.keys(kind.Owners).map((target) => [target, {}]));
    const neutral = {};
    for (const [target, owners] of Object.entries(kind.Owners))
        for (const [name, { Parameter }] of Object.entries(owners)) { blocks[target][name] = clone(Parameter.Neutral); neutral[name] = Parameter.Neutral; }
    const problems = [];
    let stopped = null;
    const layers = (stack && stack.Layers) || [];
    if (layers.length > MaximumLayers) { stopped = `More than ${MaximumLayers} layers`; problems.push({ Severity: 'Stop', Text: stopped }); }
    const required = kind.Specifications.filter((specification) => specification.Required);
    for (const specification of required)
    {
        const index = layers.findIndex((layer) => layer.Category === specification.Code);
        if (index < 0) { stopped = stopped || `${specification.Name} layer missing`; problems.push({ Severity: 'Stop', Text: `${specification.Name} layer is required` }); }
        else if (index !== 0) { stopped = stopped || `${specification.Name} must be the first layer`; problems.push({ Severity: 'Stop', Token: layers[index].Token, Text: `${specification.Name} must be the first layer` }); }
    }
    const counts = {};
    for (const layer of layers)
    {
        const specification = LayerSpecificationByCode[layer.Category];
        if (!specification) { problems.push({ Severity: 'Info', Token: layer.Token, Text: `Unknown category ${layer.Category}: kept, not evaluated` }); continue; }
        if (specification.Stack !== kindName) { problems.push({ Severity: 'Stop', Token: layer.Token, Text: `${specification.Name} belongs to a ${specification.Stack}` }); stopped = stopped || `${specification.Name} does not belong in this stack`; continue; }
        counts[layer.Category] = (counts[layer.Category] || 0) + 1;
        if (counts[layer.Category] > specification.Capacity) { problems.push({ Severity: 'Stop', Token: layer.Token, Text: `${specification.Name}: at most ${specification.Capacity}` }); stopped = stopped || `Too many ${specification.Name} layers`; }
    }
    const soloing = layers.some((layer) => layer.Solo && layer.Enabled);
    const contributing = new Set();
    const links = {};
    for (const staticLayer of layers)
    {
        const layer = (keyed && keyed.get(staticLayer.Token)) || staticLayer;
        const specification = LayerSpecificationByCode[layer.Category];
        if (!specification || specification.Stack !== kindName) continue;
        const live = layer.Enabled && (!soloing || layer.Solo || specification.Required) && (specification.Required || layer.Weight > 0);
        if (specification.Linked === 'Emitter')
        {
            const token = layer.Parameters.Emitter;
            if (!emitters.some((emitter) => emitter.Id === token)) problems.push({ Severity: 'Warning', Token: layer.Token, Text: 'Linked emitter not found' });
            links[token] = Boolean(live);
            if (live) contributing.add(layer.Token);
            continue;
        }
        if (!live) continue;
        contributing.add(layer.Token);
        // The required layer always evaluates at full weight: a half-weighted Domain has no meaning.
        const weight = specification.Required ? 1 : layer.Weight;
        for (const parameter of specification.Parameter)
        {
            if (!(parameter.Target in blocks)) continue;
            const amount = layer.Parameters[parameter.Name];
            if (amount === undefined) continue;
            blocks[parameter.Target][parameter.Name] = blendAmount(parameter, blocks[parameter.Target][parameter.Name], amount, weight, layer.Blend);
        }
    }
    // Choice codes become the tokens the solver reads.
    for (const [target, owners] of Object.entries(kind.Owners))
        for (const [name, { Parameter }] of Object.entries(owners)) if (Parameter.Type === 'Choice') blocks[target][name] = choiceToken(Parameter, blocks[target][name]);
    return { ...blocks, Links: links, Problems: problems, Stopped: stopped, Contributing: contributing };
}

export function evaluateGasStack(stack, emitters = [], time = undefined) { return evaluateLayerStack(stack, emitters, time); }
export function evaluateParticleStack(stack, time = undefined) { return evaluateLayerStack(stack, [], time); }

/** The parameter a stack track path points at: { Layer, Specification, Parameter (null for Weight), Name }, or null. */
export function resolveTrackPath(stack, path)
{
    const parts = splitLayerPath(path);
    if (!parts) return null;
    const layer = stack.Layers.find((candidate) => candidate.Token === parts.Token);
    const specification = layer && LayerSpecificationByCode[layer.Category];
    if (!specification) return null;
    if (parts.Name === 'Weight') return specification.Required || specification.Linked ? null : { Layer: layer, Specification: specification, Parameter: null, Name: 'Weight' };
    const parameter = specification.Parameter.find((candidate) => candidate.Name === parts.Name);
    return parameter ? { Layer: layer, Specification: specification, Parameter: parameter, Name: parts.Name } : null;
}

/** Keyable layer parameters: numbers, vectors, colours, Bool and Choice (stepped). Ramps, tokens and links are not keyable. */
export function isKeyableParameter(parameter) { return ['Float32', 'Int32', 'UInt32', 'Vector3', 'ColourLinear', 'Bool', 'Choice'].includes(parameter.Type); }

/** WeightParameter: the Weight setting of a layer as a keyable amount. */
export const WeightParameter = Object.freeze({ Name: 'Weight', Type: 'Float32', Minimum: 0, Maximum: 1, Default: 1, Neutral: 1 });

/** Layer copies holding their keyed amounts at `time` (only layers with tracks), keyed by token. */
export function keyedLayers(stack, time)
{
    const result = new Map();
    for (const track of stack.Tracks)
    {
        const target = resolveTrackPath(stack, track.Path);
        if (!target) continue;
        let layer = result.get(target.Layer.Token);
        if (!layer) { layer = { ...target.Layer, Parameters: { ...target.Layer.Parameters } }; result.set(target.Layer.Token, layer); }
        const parameter = target.Parameter || WeightParameter;
        const amount = coerceAmount(parameter, sampleTrackAs(track, time, parameter));
        if (target.Parameter) layer.Parameters[target.Name] = amount; else layer.Weight = amount;
    }
    return result;
}

/** The amount a layer setting or parameter holds at `time`: keyed when a track exists, else static. */
export function layerAmountAt(stack, layer, name, time)
{
    const track = (stack.Tracks || []).find((candidate) => candidate.Path === `${layer.Token}/${name}`);
    const specification = LayerSpecificationByCode[layer.Category];
    const parameter = name === 'Weight' ? WeightParameter : specification && specification.Parameter.find((candidate) => candidate.Name === name);
    if (!track || !parameter || !Number.isFinite(time)) return name === 'Weight' ? layer.Weight : layer.Parameters[name];
    return coerceAmount(parameter, sampleTrackAs(track, time, parameter));
}

/** Writes a particle evaluation into the node the GPU reads. */
export function applyParticleEvaluation(node, evaluation)
{
    for (const [name, amount] of Object.entries(evaluation.Particles)) node[name] = Array.isArray(amount) ? [...amount] : amount;
}

/** Writes an evaluation into the scene blocks the solver and renderer read. */
export function applyGasEvaluation(scene, evaluation)
{
    Object.assign(scene.Domain, evaluation.Domain);
    Object.assign(scene.Visualizers.Volume, evaluation.Volume);
    for (const emitter of scene.Emitters || []) if (emitter.Id in evaluation.Links) emitter.Enabled = evaluation.Links[emitter.Id];
}

/** Which layer an inspector edit of a flat field belongs to: the last live layer of the owning category (it wins), else null. */
export function owningLayer(stack, target, name)
{
    const owners = kindOfCategory(stack.Category).Owners;
    const owner = owners[target] && owners[target][name];
    if (!owner) return null;
    const candidates = stack.Layers.filter((layer) => layer.Category === owner.Layer.Code);
    const live = candidates.filter((layer) => layer.Enabled);
    return { Owner: owner, Layer: (live.length ? live : candidates).at(-1) || null };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DOCUMENTS
//------------------------------------------------------------------------------------------------------------------------

/** Reads one stack from a document (JSON object or parsed TOML). Unknown categories / parameters stay verbatim. */
/** Reads a stack from a file or scene; `fresh` gives every layer a new token (duplicates, template instances). */
export function readLayerStack(source, category = GasVolumeCategory, fresh = false)
{
    const layers = [];
    const renamed = new Map();
    for (const raw of (source && (source.Layers || source.Layer)) || [])
    {
        const specification = LayerSpecificationByCode[raw.Category];
        if (!specification)
        {
            layers.push(clone(raw));
            continue;
        }
        const layer = createLayer(raw.Category, fresh ? { ...raw, Token: undefined } : raw);
        if (fresh && raw.Token) renamed.set(raw.Token, layer.Token);
        // Keep unknown parameter names (newer files) next to the known, coerced ones.
        for (const [name, value] of Object.entries(raw.Parameters || {})) if (!(name in layer.Parameters)) layer.Parameters[name] = clone(value);
        layers.push(layer);
    }
    const kind = kindOfCategory((source && source.Category) || category);
    // Tracks (phase 4) follow their layers to fresh tokens; a file holds them as `Track`, a live stack as `Tracks`.
    const tracks = renameTrackLayers(readTracks(source && (source.Tracks || source.Track)), renamed);
    return { Token: (source && source.Token) || kind.Token, Category: kind.Category, Layers: layers, Tracks: tracks };
}

export function readGasStack(source) { return readLayerStack(source, GasVolumeCategory); }
export function readParticleStack(source, fresh = false) { return readLayerStack(source, ParticleCloudCategory, fresh); }

/** Inserts a layer after the last layer of the same or an earlier phase; returns null at capacity or at the layer limit. */
export function insertLayerInPhaseOrder(stack, categoryCode, overrides = {})
{
    const specification = LayerSpecificationByCode[categoryCode];
    if (!specification || stack.Layers.length >= MaximumLayers) return null;
    if (stack.Layers.filter((layer) => layer.Category === categoryCode).length >= specification.Capacity) return null;
    const rank = PhaseOrder.indexOf(specification.Phase);
    let at = stack.Layers.length;
    for (let index = stack.Layers.length - 1; index >= 0; index -= 1)
    {
        const other = LayerSpecificationByCode[stack.Layers[index].Category];
        if (other && PhaseOrder.indexOf(other.Phase) <= rank) { at = index + 1; break; }
        at = index;
    }
    const layer = createLayer(categoryCode, overrides);
    stack.Layers.splice(at, 0, layer);
    return layer;
}

/**
 * A template instance: the template's own layers (fresh tokens), with every field of `node` that differs from the template written
 * into its owning layer. A missing layer is inserted in phase order holding the node's current amounts, so nothing else moves.
 * Returns null when the result would not evaluate to `node` exactly (the caller then converts the node instead).
 */
export function instantiateParticleStack(templateStack, node)
{
    const stack = readParticleStack(templateStack, true);
    const base = evaluateParticleStack(stack);
    if (base.Stopped) return null;
    for (const [name, { Layer: specification, Parameter }] of Object.entries(ParticleParameterOwners.Particles))
    {
        const wanted = blockAmount(node, Parameter);
        if (JSON.stringify(wanted) === JSON.stringify(blockAmount(base.Particles, Parameter))) continue;
        let { Layer: layer } = owningLayer(stack, 'Particles', name);
        if (!layer)
        {
            const current = Object.fromEntries(specification.Parameter.map((parameter) => [parameter.Name, blockAmount(base.Particles, parameter)]));
            layer = insertLayerInPhaseOrder(stack, specification.Code, { Parameters: current });
            if (!layer) return null;
        }
        layer.Parameters[name] = clone(wanted);
    }
    const evaluation = evaluateParticleStack(stack);
    const copy = clone(node);
    if (evaluation.Stopped) return null;
    applyParticleEvaluation(copy, evaluation);
    return JSON.stringify(copy) === JSON.stringify(node) ? stack : null;
}

/**
 * Builds the particle stack that reproduces a resolved particle node exactly: Spawn always, every other layer whenever any of its
 * parameters differs from Neutral. Amounts are copied unrounded, so evaluate(convert(node)) equals the node field for field.
 */
export function convertParticleSystemToStack(node)
{
    const layers = [];
    for (const specification of ParticleLayerSpecifications)
    {
        const amounts = Object.fromEntries(specification.Parameter.map((parameter) => [parameter.Name, blockAmount(node, parameter)]));
        const differs = specification.Parameter.some((parameter) => JSON.stringify(amounts[parameter.Name]) !== JSON.stringify(parameter.Neutral));
        if (!specification.Required && !differs) continue;
        const layer = createLayer(specification.Code);
        layer.Parameters = clone(amounts);
        layers.push(layer);
    }
    return { Token: 'particle-cloud', Category: ParticleCloudCategory, Layers: layers, Tracks: [] };
}

export function writeStackDocument(stacks)
{
    return {
        Document: { Format: LayerDocumentFormat, Revision: LayerDocumentRevision, SpecificationHash },
        Volume: stacks.map((stack) => ({ Token: stack.Token, Category: stack.Category, Layer: stack.Layers.map((layer) => clone(layer)), ...(stack.Tracks && stack.Tracks.length ? { Track: clone(stack.Tracks) } : {}) })),
    };
}

// Keys always written with a decimal point: layer Weight and every Float32 parameter (toml++ then reads them as floating point).
// Track keys (phase 4): Time, Amount, In, Out and Period are floating point too.
export const FloatKeys = Object.freeze(new Set(['Weight', 'Time', 'Amount', 'In', 'Out', 'Period', ...LayerSpecifications.flatMap((layer) => layer.Parameter.filter((parameter) => parameter.Type === 'Float32').map((parameter) => parameter.Name))]));
export function writeStackToml(stacks) { return writeToml(writeStackDocument(stacks), FloatKeys); }

export function readStackToml(text)
{
    const document = parseToml(text);
    if (!document.Document || document.Document.Format !== LayerDocumentFormat) throw new Error(`Not a ${LayerDocumentFormat} document`);
    if (document.Document.Revision > LayerDocumentRevision) throw new Error(`Revision ${document.Document.Revision} is newer than this editor (${LayerDocumentRevision})`);
    return { Hash: document.Document.SpecificationHash, Stacks: (document.Volume || []).map((volume) => readGasStack(volume)) };
}
