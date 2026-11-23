//============================================================================================================================================
//                                                              KEYTRACKS.JS
//============================================================================================================================================
// 🧩 Keyed tracks (Fluid layer stack plan §5, phase 4). Portable: no DOM, no WebGL; the C++ port implements the same sampling rules.
//
// A track animates one amount of one owner:
//   - a layer parameter or layer Weight inside a stack  → stack.Tracks[]  Path = "<layer token>/<Parameter or Weight>"
//   - a field of a flat node (force, emitter, particle) → node.Tracks[]   Path = "<Field>"; Path "Weight" is the node's timing
// Track = { Path, Extrapolation, Period, Keys: [{ Time, Amount, Ease, In, Out }] }, keys sorted by Time (seconds).
//   Ease (of the key that starts a segment): 0 Constant · 1 Linear · 2 Smooth (monotone cubic Hermite, auto tangents) ·
//                                            3 Bezier (cubic Hermite with explicit tangents: Out of this key, In of the next, amount/s)
//   Extrapolation after the last key: 0 Hold · 1 Cycle · 2 Ping-pong. Before the first key the first amount always holds.
//   Period (Cycle / Ping-pong) defaults to last − first key time; the timing envelope sets it to Repeat.
// Amount types: Float32 / Int32 (Int rounds after easing), Vector3 (per component), ColourLinear (#rrggbb, eased in linear light),
// Bool / Choice / Ramp8 / Token (stepped: always Constant).
// Precedence (plan §5.2): a keyed amount replaces the static parameter; layer Weight multiplies after (the stack blend).

export const EaseCodes = Object.freeze([
    Object.freeze({ Code: 0, Token: 'Constant', Label: 'Constant' }),
    Object.freeze({ Code: 1, Token: 'Linear', Label: 'Linear' }),
    Object.freeze({ Code: 2, Token: 'Smooth', Label: 'Smooth' }),
    Object.freeze({ Code: 3, Token: 'Bezier', Label: 'Bezier' }),
]);
export const ExtrapolationCodes = Object.freeze([
    Object.freeze({ Code: 0, Token: 'Hold', Label: 'Hold' }),
    Object.freeze({ Code: 1, Token: 'Cycle', Label: 'Cycle' }),
    Object.freeze({ Code: 2, Token: 'PingPong', Label: 'Ping-pong' }),
]);
export const KeyableTypes = Object.freeze(['Float32', 'Int32', 'UInt32', 'Vector3', 'ColourLinear', 'Bool', 'Choice']);
export const SteppedTypes = Object.freeze(['Bool', 'Choice', 'Ramp8', 'Token']);
export const MaximumKeysPerTrack = 256;
/** Two keys closer than this (seconds) are the same key. Half a 240 Hz step. */
export const KeyTimeEpsilon = 1 / 480;

const clone = (value) => (value === undefined ? undefined : JSON.parse(JSON.stringify(value)));

//------------------------------------------------------------------------------------------------------------------------
//                                                  COLOUR
//------------------------------------------------------------------------------------------------------------------------

const toLinear = (channel) => (channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4);
const toSrgb = (channel) => (channel <= 0.0031308 ? channel * 12.92 : 1.055 * channel ** (1 / 2.4) - 0.055);
function hexToLinear(hex)
{
    const value = parseInt(String(hex).slice(1, 7), 16) || 0;
    return [(value >> 16) & 255, (value >> 8) & 255, value & 255].map((channel) => toLinear(channel / 255));
}
function linearToHex(rgb)
{
    return '#' + rgb.map((channel) => Math.round(Math.min(1, Math.max(0, toSrgb(Math.min(1, Math.max(0, channel))))) * 255).toString(16).padStart(2, '0')).join('');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SAMPLING
//------------------------------------------------------------------------------------------------------------------------

/** Kind of amount a track carries, from its first key (no specification needed to sample). */
function amountKind(amount)
{
    if (typeof amount === 'number') return 'Number';
    if (Array.isArray(amount) && amount.length === 3 && amount.every((component) => typeof component === 'number')) return 'Vector';
    if (typeof amount === 'string' && /^#[0-9a-f]{6}$/i.test(amount)) return 'Colour';
    return 'Step';
}

/** Monotone (Fritsch–Carlson) tangent at key `index` for one scalar channel: no overshoot between keys. */
function autoTangent(keys, index, channel)
{
    const value = (key) => channel(key.Amount);
    const previous = keys[index - 1], current = keys[index], next = keys[index + 1];
    if (!previous || !next) return 0;
    const left = (value(current) - value(previous)) / Math.max(1e-9, current.Time - previous.Time);
    const right = (value(next) - value(current)) / Math.max(1e-9, next.Time - current.Time);
    if (left * right <= 0) return 0;
    const leftSpan = current.Time - previous.Time, rightSpan = next.Time - current.Time;
    const leftWeight = 2 * rightSpan + leftSpan, rightWeight = rightSpan + 2 * leftSpan;
    return (leftWeight + rightWeight) / (leftWeight / left + rightWeight / right);
}

function hermite(p0, p1, m0, m1, span, u)
{
    const u2 = u * u, u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * span * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * span * m1;
}

/** Eases one scalar channel across segment [index, index + 1] at fraction u. */
function easeChannel(keys, index, u, channel)
{
    const a = keys[index], b = keys[index + 1];
    const p0 = channel(a.Amount), p1 = channel(b.Amount);
    switch (a.Ease)
    {
        case 0: return p0;
        case 2: return hermite(p0, p1, autoTangent(keys, index, channel), autoTangent(keys, index + 1, channel), b.Time - a.Time, u);
        case 3: return hermite(p0, p1, channelOf(a.Out, channel), channelOf(b.In, channel), b.Time - a.Time, u);
        default: return p0 + (p1 - p0) * u;
    }
}
/** Bezier tangents are a number (scalar tracks) or a 3-array (Vector3 tracks); missing = 0 (flat). */
const channelOf = (tangent, channel) => (tangent === undefined ? 0 : Array.isArray(tangent) ? channel(tangent) : (typeof tangent === 'number' ? tangent : 0));

/** Local time after extrapolation: Hold clamps, Cycle wraps, Ping-pong reflects. Before the first key always holds. */
export function trackLocalTime(track, time)
{
    const keys = track.Keys;
    const first = keys[0].Time, last = keys[keys.length - 1].Time;
    if (time <= first) return first;
    const period = track.Period > 0 ? track.Period : last - first;
    if (!track.Extrapolation || !(period > 0)) return Math.min(time, last);
    // Same arithmetic as the legacy envelope ((time − start) % repeat), so a converted envelope matches at every instant.
    const offset = time - first;
    if (track.Extrapolation === 1) return first + (offset % period);
    const cycle = offset % (2 * period);
    return first + (cycle <= period ? cycle : 2 * period - cycle);
}

/** Amount of `track` at `time` (seconds). Exact at key times; linear segments are exact to float rounding. `stepped` holds each key (Bool, Choice). */
export function sampleTrack(track, time, stepped = false)
{
    const keys = track && track.Keys;
    if (!keys || keys.length === 0) return undefined;
    if (keys.length === 1) return clone(keys[0].Amount);
    const local = trackLocalTime(track, time);
    // Binary search: last key with Time ≤ local.
    let low = 0, high = keys.length - 1;
    if (local >= keys[high].Time) return clone(keys[high].Amount);
    while (high - low > 1)
    {
        const middle = (low + high) >> 1;
        if (keys[middle].Time <= local) low = middle; else high = middle;
    }
    const a = keys[low], b = keys[low + 1];
    const u = (local - a.Time) / Math.max(1e-12, b.Time - a.Time);
    const kind = amountKind(a.Amount);
    if (stepped || kind === 'Step' || amountKind(b.Amount) !== kind) return clone(a.Amount);
    if (kind === 'Number') return easeChannel(keys, low, u, (amount) => amount);
    if (kind === 'Vector') return [0, 1, 2].map((axis) => easeChannel(keys, low, u, (amount) => amount[axis]));
    const linear = keys.map((key) => ({ ...key, Amount: hexToLinear(key.Amount) }));
    return linearToHex([0, 1, 2].map((axis) => easeChannel(linear, low, u, (amount) => amount[axis])));
}

/** Samples and coerces to the parameter type (Int rounds, Bool/Choice step). */
export function sampleTrackAs(track, time, parameter)
{
    const amount = sampleTrack(track, time, Boolean(parameter && SteppedTypes.includes(parameter.Type)));
    if (amount === undefined || !parameter) return amount;
    if ((parameter.Type === 'Int32' || parameter.Type === 'UInt32') && typeof amount === 'number') return Math.round(amount);
    if (parameter.Type === 'Float32' && typeof amount === 'number' && Number.isFinite(parameter.Minimum)) return Math.min(parameter.Maximum, Math.max(parameter.Minimum, amount));
    return amount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  EDITING
//------------------------------------------------------------------------------------------------------------------------

export function findTrack(tracks, path) { return (tracks || []).find((track) => track.Path === path) || null; }
export function keyIndexAt(track, time) { return track ? track.Keys.findIndex((key) => Math.abs(key.Time - time) <= KeyTimeEpsilon) : -1; }

/** Writes a key at `time` (replacing one within KeyTimeEpsilon); creates the track. Stepped amounts (and `stepped` types) get Constant ease. */
export function setKey(tracks, path, time, amount, ease = 2, steppedType = false)
{
    let track = findTrack(tracks, path);
    if (!track) { track = { Path: path, Extrapolation: 0, Keys: [] }; tracks.push(track); }
    const stepped = steppedType || amountKind(amount) === 'Step';
    const index = keyIndexAt(track, time);
    if (index >= 0) { track.Keys[index].Amount = clone(amount); if (stepped) track.Keys[index].Ease = 0; return track.Keys[index]; }
    if (track.Keys.length >= MaximumKeysPerTrack) return null;
    const key = { Time: roundTime(time), Amount: clone(amount), Ease: stepped ? 0 : ease };
    track.Keys.push(key);
    track.Keys.sort((left, right) => left.Time - right.Time);
    return key;
}

/** Removes the key at `time`; drops the track when it has no keys left. Returns true when a key was removed. */
export function removeKey(tracks, path, time)
{
    const track = findTrack(tracks, path);
    const index = keyIndexAt(track, time);
    if (index < 0) return false;
    track.Keys.splice(index, 1);
    if (!track.Keys.length) tracks.splice(tracks.indexOf(track), 1);
    return true;
}

export function removeTrack(tracks, path)
{
    const index = (tracks || []).findIndex((track) => track.Path === path);
    if (index >= 0) tracks.splice(index, 1);
    return index >= 0;
}

/** Moves (offset) and/or scales (about `pivot`) the given keys of one track, keeping the order valid and times ≥ 0. */
export function transformKeys(track, indices, { offset = 0, scale = 1, pivot = 0 } = {})
{
    const chosen = new Set(indices);
    for (const [index, key] of track.Keys.entries()) if (chosen.has(index)) key.Time = roundTime(Math.max(0, pivot + (key.Time - pivot) * scale + offset));
    track.Keys.sort((left, right) => left.Time - right.Time);
    // Collapse keys that landed on the same time: the moved one wins.
    for (let index = track.Keys.length - 1; index > 0; index -= 1) if (Math.abs(track.Keys[index].Time - track.Keys[index - 1].Time) <= KeyTimeEpsilon) track.Keys.splice(index - 1, 1);
}

/** Microsecond grid keeps saved files short and comparisons stable. */
export function roundTime(time) { return Math.round(time * 1e6) / 1e6; }

/** Gives a stack's tracks the new layer tokens after a fresh read (duplicate / template instance). */
export function renameTrackLayers(tracks, renamed)
{
    for (const track of tracks || [])
    {
        const [layer, ...rest] = track.Path.split('/');
        if (renamed.has(layer)) track.Path = [renamed.get(layer), ...rest].join('/');
    }
    return tracks;
}

/** Reads tracks from a file: drops malformed keys, sorts, coerces codes. Unknown fields on a track are kept. */
export function readTracks(source)
{
    return (source || []).filter((track) => track && typeof track.Path === 'string' && Array.isArray(track.Keys)).map((track) =>
    {
        const keys = track.Keys.filter((key) => key && Number.isFinite(key.Time) && key.Amount !== undefined).slice(0, MaximumKeysPerTrack)
            .map((key) => ({ ...clone(key), Ease: [0, 1, 2, 3].includes(key.Ease) ? key.Ease : 1 }))
            .sort((left, right) => left.Time - right.Time);
        return { ...clone(track), Extrapolation: [0, 1, 2].includes(track.Extrapolation) ? track.Extrapolation : 0, Keys: keys };
    }).filter((track) => track.Keys.length > 0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TIMING ENVELOPE → WEIGHT TRACK (plan §5.3)
//------------------------------------------------------------------------------------------------------------------------

/** The legacy envelope without the repeat wrap (continuous in `local`). */
function envelopeAt(local, duration, fade)
{
    if (local < 0) return 0;
    const rise = Math.min(1, local / fade);
    if (duration <= 0) return rise;
    if (local >= duration) return 0;
    return Math.min(rise, Math.min(1, (duration - local) / fade));
}

/**
 * The exact Weight track for a node's StartTime / Duration / Fade / Repeat envelope (the formula ForceGeneratorCatalog.timingEnvelope used before phase 4; FluidValidation §9 keeps it as the reference).
 * Keys at every breakpoint of the piecewise-linear envelope inside one cycle, linear ease; Repeat → Cycle with Period = Repeat.
 * Returns null when the envelope is constant 1 (no start, no duration).
 */
export function envelopeToTrack(node)
{
    const start = node.StartTime || 0;
    const duration = node.Duration || 0;
    const fade = Math.max(0.001, node.Fade ?? 0.15);
    const repeat = node.Repeat || 0;
    if (start <= 0 && duration <= 0) return null;
    const end = repeat > 0 ? repeat : Infinity;
    const breakpoints = [0, fade, duration > 0 ? duration - fade : NaN, duration > 0 ? duration / 2 : NaN, duration > 0 ? duration : NaN]
        .filter((local) => Number.isFinite(local) && local >= 0 && local < end);
    if (Number.isFinite(end)) breakpoints.push(end);
    const times = [...new Set(breakpoints)].sort((left, right) => left - right);
    const all = times.map((local) => ({ Time: start + local, Amount: envelopeAt(local, duration, fade), Ease: 1 }));
    // Drop keys that sit on the straight line between their neighbours (the plan's 3- or 4-key shapes remain).
    const keys = all.filter((key, index) =>
    {
        if (index === 0 || index === all.length - 1) return true;
        const previous = all[index - 1], next = all[index + 1];
        const line = previous.Amount + (next.Amount - previous.Amount) * (key.Time - previous.Time) / (next.Time - previous.Time);
        return Math.abs(line - key.Amount) > 1e-12;
    });
    return repeat > 0 ? { Path: 'Weight', Extrapolation: 1, Period: repeat, Keys: keys } : { Path: 'Weight', Extrapolation: 0, Keys: keys };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  NODE WEIGHT AND KEYED FIELDS
//------------------------------------------------------------------------------------------------------------------------

/** Timing fields of a force, emitter or particle cloud: the generator of its Weight track until Weight is keyed by hand. */
export const TimingFields = Object.freeze(['StartTime', 'Duration', 'Fade', 'Repeat']);
const EnvelopeTracks = new WeakMap();

/** The track that drives a node's Weight: an explicit `Weight` track wins; otherwise the one its timing fields generate (cached). */
export function weightTrackOf(node)
{
    const explicit = findTrack(node.Tracks, 'Weight');
    if (explicit) return explicit;
    const signature = `${node.StartTime}|${node.Duration}|${node.Fade}|${node.Repeat}`;
    const cached = EnvelopeTracks.get(node);
    if (cached && cached.Signature === signature) return cached.Track;
    const track = envelopeToTrack(node);
    if (track) track.Envelope = true;
    EnvelopeTracks.set(node, { Signature: signature, Track: track });
    return track;
}

/** Weight (0..1) of a force, emitter or particle cloud at `time`: scales its strength or emission. 1 when nothing is keyed. */
export function nodeWeight(node, time)
{
    const track = weightTrackOf(node);
    return track ? Math.min(1, Math.max(0, sampleTrack(track, time))) : 1;
}

/** True when the node's Weight changes over time (timing fields or an explicit Weight track). */
export function hasWeightTrack(node) { return Boolean(weightTrackOf(node)); }

/** Fields of a flat node that cannot be keyed: identity, the Weight generator, and the GPU fields a particle cloud's layers own. */
const UnkeyableFields = new Set(['Id', 'Kind', 'Name', 'Type', 'Template', 'Tracks', 'Stack', 'Visible', ...TimingFields]);
export function isKeyableField(node, name)
{
    if (name === 'Weight') return node.Kind === 'Force' || node.Kind === 'Emitter' || node.Kind === 'Particles';
    if (node.Kind === 'Particles' || UnkeyableFields.has(name) || !(name in node)) return false;
    const value = node[name];
    return typeof value === 'number' || typeof value === 'boolean' || (typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value))
        || (Array.isArray(value) && value.length === 3 && value.every((component) => typeof component === 'number'));
}

/** Amount type of a flat field, for coercion and stepping: numbers ease, booleans and words step. */
export function fieldParameter(node, name)
{
    const value = name === 'Weight' ? 1 : node[name];
    if (typeof value === 'boolean') return { Type: 'Bool' };
    if (typeof value === 'number') return { Type: 'Float32' };
    if (Array.isArray(value)) return { Type: 'Vector3' };
    if (typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value)) return { Type: 'ColourLinear' };
    return { Type: 'Token' };
}

/**
 * A force or emitter as the solver sees it at `time`: keyed fields replace the static ones (the node is returned unchanged when only
 * Weight is keyed, so the common case allocates nothing). Weight itself is applied by the solver through nodeWeight().
 */
export function keyedNode(node, time)
{
    const tracks = node.Tracks;
    if (!tracks || !tracks.length || tracks.every((track) => track.Path === 'Weight')) return node;
    const copy = { ...node };
    for (const track of tracks)
    {
        if (track.Path === 'Weight' || !(track.Path in node)) continue;
        const amount = sampleTrackAs(track, time, fieldParameter(node, track.Path));
        if (amount !== undefined) copy[track.Path] = amount;
    }
    return copy;
}

/** Layer `Token/Name` paths of a stack track, or null for a node-level path. */
export function splitLayerPath(path)
{
    const slash = path.indexOf('/');
    return slash < 0 ? null : { Token: path.slice(0, slash), Name: path.slice(slash + 1) };
}

/** Earliest time a key edit can change the result: Smooth tangents reach two keys back. Checkpoints before it stay valid. */
export function affectedFrom(track, time)
{
    if (!track || !track.Keys.length) return 0;
    let index = track.Keys.findIndex((key) => key.Time >= time - KeyTimeEpsilon);
    if (index < 0) index = track.Keys.length;
    if (index === 0) return 0;                                   // the first amount holds back to t = 0
    if (track.Extrapolation) return Math.max(0, Math.min(track.Keys[0].Time, time));
    const anchor = track.Keys[Math.max(0, index - 2)];
    return Math.max(0, Math.min(anchor ? anchor.Time : 0, time));
}
