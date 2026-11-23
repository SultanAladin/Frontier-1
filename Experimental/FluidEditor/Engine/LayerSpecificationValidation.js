//============================================================================================================================================
//                                                     LAYERSPECIFICATIONVALIDATION.JS
//============================================================================================================================================
// 🧩 Checks a parsed LayerSpecifications.toml against the plan's rules (§3.1) and computes its hash. Used by the generator (refuses to
// write a bad specification), by FluidValidation.mjs (the generated file must match the TOML) and later by the C++ generator.

export const ParameterTypes = Object.freeze(['Float32', 'Int32', 'UInt32', 'Bool', 'Vector3', 'ColourLinear', 'Choice', 'Ramp8', 'Token']);
export const LayerPhases = Object.freeze(['Inject', 'Advance', 'Shade', 'Overlay']);
export const LayerStacks = Object.freeze(['GasVolume', 'ParticleCloud']);
export const ParameterTargets = Object.freeze(['Domain', 'Volume', 'Link', 'Particles']);
// Which evaluated blocks each stack may write.
export const StackTargets = Object.freeze({ GasVolume: ['Domain', 'Volume', 'Link'], ParticleCloud: ['Particles'] });

const isHexColour = (value) => typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value);

function checkAmount(type, value, parameter, label, problems)
{
    const where = `${label} ${parameter.Name}`;
    if (type === 'Float32' || type === 'Int32' || type === 'UInt32')
    {
        if (typeof value !== 'number' || !Number.isFinite(value)) { problems.push(`${where}: not a number`); return; }
        if (type !== 'Float32' && !Number.isInteger(value)) problems.push(`${where}: ${type} must be a whole number`);
        if (type === 'UInt32' && value < 0) problems.push(`${where}: UInt32 cannot be negative`);
        if (value < parameter.Minimum || value > parameter.Maximum) problems.push(`${where}: ${value} outside ${parameter.Minimum}–${parameter.Maximum}`);
    }
    else if (type === 'Bool') { if (typeof value !== 'boolean') problems.push(`${where}: not true/false`); }
    else if (type === 'ColourLinear') { if (!isHexColour(value)) problems.push(`${where}: colour must be #rrggbb`); }
    else if (type === 'Vector3') { if (!Array.isArray(value) || value.length !== 3 || !value.every(Number.isFinite)) problems.push(`${where}: Vector3 needs three numbers`); }
    else if (type === 'Choice') { if (!(parameter.Options || []).some((option) => option.Code === value)) problems.push(`${where}: ${value} is not an option code`); }
    else if (type === 'Token') { if (typeof value !== 'string') problems.push(`${where}: token must be a string`); }
    else if (type === 'Ramp8') { if (!Array.isArray(value) || value.length > 8) problems.push(`${where}: Ramp8 holds at most 8 stops`); }
}

/** Returns a list of human-readable problems; an empty list means the specification can be generated. */
export function validateLayerSpecifications(document)
{
    const problems = [];
    const head = document.Document || {};
    if (head.Format !== 'frontier-layer-specifications') problems.push('Document.Format must be "frontier-layer-specifications"');
    if (!Number.isInteger(head.Revision) || head.Revision < 1) problems.push('Document.Revision must be a whole number ≥ 1');
    if (!Number.isInteger(head.MaximumLayers) || head.MaximumLayers < 1) problems.push('Document.MaximumLayers must be a whole number ≥ 1');
    const layers = Array.isArray(document.Layer) ? document.Layer : [];
    if (!layers.length) problems.push('No [[Layer]] entries');
    const layerCodes = new Set(), layerTokens = new Set();
    for (const layer of layers)
    {
        const label = `Layer ${layer.Token || layer.Code}`;
        if (!Number.isInteger(layer.Code) || layer.Code < 100 || layer.Code > 299) problems.push(`${label}: Code must be 100–299`);
        if (layerCodes.has(layer.Code)) problems.push(`${label}: duplicate Code ${layer.Code}`);
        if (layerTokens.has(layer.Token)) problems.push(`${label}: duplicate Token`);
        layerCodes.add(layer.Code); layerTokens.add(layer.Token);
        if (!/^[a-z][a-z0-9-]*$/.test(layer.Token || '')) problems.push(`${label}: Token must be lower-case words joined by hyphens`);
        if (!LayerStacks.includes(layer.Stack)) problems.push(`${label}: Stack must be one of ${LayerStacks.join(', ')}`);
        if (!LayerPhases.includes(layer.Phase)) problems.push(`${label}: Phase must be one of ${LayerPhases.join(', ')}`);
        if (typeof layer.Required !== 'boolean') problems.push(`${label}: Required must be true/false`);
        if (!Number.isInteger(layer.Capacity) || layer.Capacity < 1) problems.push(`${label}: Capacity must be ≥ 1`);
        if (layer.Required && layer.Capacity !== 1) problems.push(`${label}: a required layer has Capacity 1`);
        const parameters = Array.isArray(layer.Parameter) ? layer.Parameter : [];
        if (!parameters.length) problems.push(`${label}: no parameters`);
        const codes = new Set(), names = new Set();
        for (const parameter of parameters)
        {
            const where = `${label} ${parameter.Name}`;
            if (!Number.isInteger(parameter.Code) || parameter.Code < 1) problems.push(`${where}: Code must be ≥ 1`);
            if (codes.has(parameter.Code)) problems.push(`${where}: duplicate parameter Code ${parameter.Code}`);
            if (names.has(parameter.Name)) problems.push(`${where}: duplicate parameter Name`);
            codes.add(parameter.Code); names.add(parameter.Name);
            if (!ParameterTypes.includes(parameter.Type)) { problems.push(`${where}: unknown Type ${parameter.Type}`); continue; }
            if (!ParameterTargets.includes(parameter.Target)) problems.push(`${where}: Target must be one of ${ParameterTargets.join(', ')}`);
            else if (StackTargets[layer.Stack] && !StackTargets[layer.Stack].includes(parameter.Target)) problems.push(`${where}: a ${layer.Stack} layer cannot write ${parameter.Target}`);
            if (typeof parameter.Keyable !== 'boolean') problems.push(`${where}: Keyable must be true/false`);
            if (['Float32', 'Int32', 'UInt32'].includes(parameter.Type) && !(parameter.Minimum < parameter.Maximum)) problems.push(`${where}: Minimum must be below Maximum`);
            if (parameter.Type === 'Choice')
            {
                const options = parameter.Options || [];
                if (!options.length) problems.push(`${where}: Choice needs Options`);
                const optionCodes = new Set();
                for (const option of options)
                {
                    if (!Number.isInteger(option.Code)) problems.push(`${where}: every option needs an explicit integer Code`);
                    if (optionCodes.has(option.Code)) problems.push(`${where}: duplicate option Code ${option.Code}`);
                    if (typeof option.Token !== 'string' || typeof option.Label !== 'string') problems.push(`${where}: options need Token and Label`);
                    optionCodes.add(option.Code);
                }
            }
            checkAmount(parameter.Type, parameter.Default, parameter, `${label} Default`, problems);
            checkAmount(parameter.Type, parameter.Neutral, parameter, `${label} Neutral`, problems);
        }
    }
    return problems;
}

/** Canonical JSON (sorted keys) so the hash only changes when content changes, never with key order or whitespace. */
export function canonicalJson(value)
{
    if (Array.isArray(value)) return `[${value.map(canonicalJson).join(',')}]`;
    if (value && typeof value === 'object') return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${canonicalJson(value[key])}`).join(',')}}`;
    return JSON.stringify(value);
}

/** 64-bit FNV-1a over the UTF-8 canonical JSON, as 16 hex digits. The C++ generator computes the same value. */
export function specificationHash(document)
{
    const bytes = new TextEncoder().encode(canonicalJson(document));
    let hash = 0xcbf29ce484222325n;
    const prime = 0x100000001b3n, mask = 0xffffffffffffffffn;
    for (const byte of bytes) hash = ((hash ^ BigInt(byte)) * prime) & mask;
    return hash.toString(16).padStart(16, '0');
}
