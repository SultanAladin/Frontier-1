//============================================================================================================================================
//                                                              TOMLSUBSET.JS
//============================================================================================================================================
// 🧩 The TOML the layer stack needs, read and written without a dependency (Fluid layer stack plan, phase 1): comments, [Table],
// [[ArrayOfTables]] with dotted names, and key = string / integer / float / boolean / array / inline table. Dates, multi-line strings,
// literal strings and dotted keys are refused with the line number, so a file never half-loads. The C++ side reads the same files with
// toml++.

export class TomlSubsetError extends Error {}

function fail(line, message) { throw new TomlSubsetError(`TOML line ${line}: ${message}`); }

/** Splits on `separator` at depth 0 (outside strings, arrays and inline tables). */
function splitTop(text, separator, line)
{
    const parts = [];
    let depth = 0, quoted = false, start = 0;
    for (let index = 0; index < text.length; index += 1)
    {
        const character = text[index];
        if (quoted) { if (character === '\\') index += 1; else if (character === '"') quoted = false; continue; }
        if (character === '"') quoted = true;
        else if (character === '[' || character === '{') depth += 1;
        else if (character === ']' || character === '}') depth -= 1;
        else if (character === separator && depth === 0) { parts.push(text.slice(start, index)); start = index + 1; }
        if (depth < 0) fail(line, 'unbalanced bracket');
    }
    if (quoted || depth !== 0) fail(line, 'unterminated string or bracket');
    parts.push(text.slice(start));
    return parts;
}

function stripComment(text)
{
    let quoted = false;
    for (let index = 0; index < text.length; index += 1)
    {
        const character = text[index];
        if (quoted) { if (character === '\\') index += 1; else if (character === '"') quoted = false; }
        else if (character === '"') quoted = true;
        else if (character === '#') return text.slice(0, index);
    }
    return text;
}

const Escapes = { b: '\b', t: '\t', n: '\n', f: '\f', r: '\r', '"': '"', '\\': '\\' };

function parseValue(raw, line)
{
    const text = raw.trim();
    if (!text) fail(line, 'missing value');
    if (text.startsWith('"'))
    {
        if (text.startsWith('"""')) fail(line, 'multi-line strings are not supported');
        if (!text.endsWith('"') || text.length < 2) fail(line, 'unterminated string');
        return text.slice(1, -1).replace(/\\(u[0-9a-fA-F]{4}|.)/g, (match, code) =>
        {
            if (code[0] === 'u') return String.fromCharCode(parseInt(code.slice(1), 16));
            if (!(code in Escapes)) fail(line, `unknown escape \\${code}`);
            return Escapes[code];
        });
    }
    if (text.startsWith("'")) fail(line, 'literal strings are not supported');
    if (text === 'true') return true;
    if (text === 'false') return false;
    if (text.startsWith('['))
    {
        if (!text.endsWith(']')) fail(line, 'unterminated array');
        const inner = text.slice(1, -1).trim();
        if (!inner) return [];
        return splitTop(inner, ',', line).filter((part, index, all) => part.trim() || index < all.length - 1).map((part) => parseValue(part, line));
    }
    if (text.startsWith('{'))
    {
        if (!text.endsWith('}')) fail(line, 'unterminated inline table');
        const table = {};
        const inner = text.slice(1, -1).trim();
        if (!inner) return table;
        for (const pair of splitTop(inner, ',', line)) assignPair(table, pair, line);
        return table;
    }
    if (/^[+-]?(\d[\d_]*)$/.test(text)) return Number(text.replace(/_/g, ''));
    if (/^[+-]?(\d[\d_]*)?(\.\d[\d_]*)?([eE][+-]?\d+)?$/.test(text) && /\d/.test(text)) return Number(text.replace(/_/g, ''));
    if (text === 'inf' || text === '+inf' || text === '-inf' || text === 'nan') fail(line, 'inf and nan are not allowed in layer files');
    fail(line, `unsupported value "${text}"`);
}

function assignPair(table, pair, line)
{
    const parts = splitTop(pair, '=', line);
    if (parts.length < 2) fail(line, 'expected key = value');
    const key = parts[0].trim();
    if (!/^[A-Za-z0-9_-]+$/.test(key)) fail(line, `unsupported key "${key}" (bare keys only)`);
    if (Object.prototype.hasOwnProperty.call(table, key)) fail(line, `duplicate key "${key}"`);
    table[key] = parseValue(parts.slice(1).join('='), line);
}

/** Parses TOML text into plain objects. Integer and float are both JS numbers; the specification types say which one is meant. */
export function parseToml(text)
{
    const root = {};
    let current = root;
    const lines = String(text).replace(/\r\n?/g, '\n').split('\n');
    for (let index = 0; index < lines.length; index += 1)
    {
        const line = index + 1;
        const content = stripComment(lines[index]).trim();
        if (!content) continue;
        const arrayHeader = content.match(/^\[\[\s*([A-Za-z0-9_.-]+)\s*\]\]$/);
        const tableHeader = !arrayHeader && content.match(/^\[\s*([A-Za-z0-9_.-]+)\s*\]$/);
        if (arrayHeader || tableHeader)
        {
            const names = (arrayHeader || tableHeader)[1].split('.');
            let table = root;
            for (const name of names.slice(0, -1))
            {
                let next = table[name];
                if (Array.isArray(next)) next = next[next.length - 1];
                if (next === undefined) next = table[name] = {};
                if (typeof next !== 'object' || next === null) fail(line, `"${name}" is not a table`);
                table = next;
            }
            const last = names[names.length - 1];
            if (arrayHeader)
            {
                if (table[last] === undefined) table[last] = [];
                if (!Array.isArray(table[last])) fail(line, `"${last}" is not an array of tables`);
                current = {};
                table[last].push(current);
            }
            else
            {
                if (table[last] !== undefined) fail(line, `table "${last}" defined twice`);
                current = table[last] = {};
            }
            continue;
        }
        assignPair(current, content, line);
    }
    return root;
}

function formatScalar(value, floatKeys = new Set(), key = '')
{
    if (typeof value === 'string') return JSON.stringify(value);
    if (typeof value === 'boolean') return value ? 'true' : 'false';
    if (typeof value === 'number')
    {
        if (!Number.isFinite(value)) throw new TomlSubsetError('inf and nan cannot be written');
        if (floatKeys.has(key) && Number.isInteger(value) && Math.abs(value) < 1e15) return `${value}.0`;
        // Round-trip exact: shortest JS representation, with ".0" so integers written as floats stay floats.
        const text = String(value);
        return Number.isInteger(value) && !/e/i.test(text) ? text : text.replace(/^(-?\d+)e/i, '$1.0e');
    }
    if (Array.isArray(value)) return `[${value.map((item) => formatScalar(item, floatKeys, key)).join(', ')}]`;
    if (value && typeof value === 'object') return `{ ${Object.entries(value).map(([name, item]) => `${name} = ${formatScalar(item, floatKeys, name)}`).join(', ')} }`;
    throw new TomlSubsetError(`cannot write ${typeof value}`);
}

/**
 * Writes `{ Table: {...}, ArrayName: [{...}] }` in a fixed order: scalars first, then sub-tables, then arrays of tables, keys in
 * insertion order. `floatKeys` names keys always written with a decimal point (Float32 parameters), so 1 stays 1.0 in the file.
 */
export function writeToml(document, floatKeys = new Set())
{
    const lines = [];
    const scalarText = (key, value) => formatScalar(value, floatKeys, key);
    const isTableArray = (value) => Array.isArray(value) && value.length > 0 && value.every((item) => item && typeof item === 'object' && !Array.isArray(item));
    function emit(table, path, indent)
    {
        const entries = Object.entries(table).filter(([, value]) => value !== undefined);
        for (const [key, value] of entries) if (!isTableArray(value)) lines.push(`${indent}${key} = ${scalarText(key, value)}`);
        for (const [key, value] of entries)
        {
            if (!isTableArray(value)) continue;
            for (const item of value)
            {
                lines.push('', `${indent}[[${[...path, key].join('.')}]]`);
                emit(item, [...path, key], indent + '  ');
            }
        }
    }
    for (const [key, value] of Object.entries(document))
    {
        if (value && typeof value === 'object' && !Array.isArray(value))
        {
            if (lines.length) lines.push('');
            lines.push(`[${key}]`);
            emit(value, [key], '');
        }
    }
    for (const [key, value] of Object.entries(document))
    {
        if (!isTableArray(value)) continue;
        for (const item of value)
        {
            lines.push('', `[[${key}]]`);
            emit(item, [key], '  ');
        }
    }
    return lines.join('\n') + '\n';
}
