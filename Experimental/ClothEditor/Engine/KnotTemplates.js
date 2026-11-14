/*============================================================================================================================================
                                                        KNOTTEMPLATES.JS
============================================================================================================================================*/
// Guo et al. (SIGGRAPH 2025), "Fast Physics-Based Modeling of Knots and Ties Using Templates".
//
// The parts of that pipeline that can exist without a knot-authoring UI, implemented here:
//   Sec. 4.1  pipe-like template: a cubic Bezier medial axis with a radius stored per node and a
//             global orientation parameter o that rotates the cross-section around the axis.
//   Sec. 4.2  tessellation into a triangle pipe. Orientation along the pipe is carried by parallel
//             transport of a frame (n, b) so consecutive rings do not twist against each other;
//             each ring is cut by the bisector plane idea in discrete form (rings stay normal to
//             the transported tangent at their sample point).
//   Sec. 4.3  collisions between non-adjacent pipe parts are removed by shrinking the radii where
//             they overlap (adaptive, per-node), never by moving the medial axis.
//   Sec. 5.1  mapping: a rectangular cloth region wraps around the pipe (u -> angle, v -> arc
//             length); a circular region maps sector-by-sector onto the same surface.
//
// Templates are user-authored objects in the paper (there is no table of coordinates in it), so the
// control polygons below are examples. defineKnotTemplate() accepts any control polygon + radii.

function bezierPoint(c, t, out)
{
    const u = 1 - t;
    const w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
    out[0] = w0 * c[0][0] + w1 * c[1][0] + w2 * c[2][0] + w3 * c[3][0];
    out[1] = w0 * c[0][1] + w1 * c[1][1] + w2 * c[2][1] + w3 * c[3][1];
    out[2] = w0 * c[0][2] + w1 * c[1][2] + w2 * c[2][2] + w3 * c[3][2];
    return out;
}

function bezierTangent(c, t, out)
{
    const u = 1 - t;
    for (let a = 0; a < 3; ++a)
        out[a] = 3 * u * u * (c[1][a] - c[0][a]) + 6 * u * t * (c[2][a] - c[1][a]) + 3 * t * t * (c[3][a] - c[2][a]);
    const l = Math.hypot(out[0], out[1], out[2]) || 1;
    out[0] /= l; out[1] /= l; out[2] /= l;
    return out;
}

export function defineKnotTemplate({ id = 'custom', label = 'Custom knot', spans, orientation = 0 })
{
    if (!Array.isArray(spans) || spans.length === 0) throw new Error('knot template needs at least one Bezier span');
    for (const s of spans)
    {
        if (!Array.isArray(s.controls) || s.controls.length !== 4) throw new Error('each span needs 4 control points');
        if (!Array.isArray(s.radii) || s.radii.length !== 4) throw new Error('each span needs a radius per control point');
    }
    return { id, label, spans, orientation };
}

// Example templates (control polygons authored for this app, in metres).
export const KNOT_TEMPLATES = {
    bow: defineKnotTemplate({
        id: 'bow', label: 'Bow knot', orientation: 0,
        spans: [
            { controls: [[-0.09, 0, 0], [-0.06, 0.05, 0.02], [0.02, 0.06, -0.02], [0.05, 0.01, 0.02]], radii: [0.012, 0.016, 0.016, 0.013] },
            { controls: [[0.05, 0.01, 0.02], [0.08, -0.03, 0.05], [0.03, -0.06, 0.03], [-0.02, -0.04, -0.02]], radii: [0.013, 0.017, 0.015, 0.012] },
            { controls: [[-0.02, -0.04, -0.02], [-0.06, -0.02, -0.04], [-0.04, 0.03, -0.01], [0.0, 0.04, 0.01]], radii: [0.012, 0.015, 0.014, 0.012] },
        ],
    }),
    pretzel: defineKnotTemplate({
        id: 'pretzel', label: 'Pretzel knot', orientation: Math.PI / 2,
        spans: [
            { controls: [[-0.08, -0.03, 0], [-0.05, 0.05, 0.03], [0.04, 0.06, 0.03], [0.08, 0.0, 0]], radii: [0.011, 0.015, 0.015, 0.012] },
            { controls: [[0.08, 0.0, 0], [0.05, -0.05, -0.03], [-0.03, -0.06, -0.02], [-0.07, -0.01, 0.02]], radii: [0.012, 0.016, 0.014, 0.011] },
        ],
    }),
    fourInHand: defineKnotTemplate({
        id: 'fourInHand', label: 'Four-in-hand tie knot', orientation: 0,
        spans: [
            { controls: [[0, 0.09, 0], [0.03, 0.05, 0.02], [0.035, 0.0, 0.03], [0.01, -0.03, 0.01]], radii: [0.010, 0.015, 0.018, 0.015] },
            { controls: [[0.01, -0.03, 0.01], [-0.02, -0.055, -0.01], [-0.01, -0.09, 0.0], [0.005, -0.12, 0.0]], radii: [0.015, 0.013, 0.011, 0.010] },
        ],
    }),
};

// Sample the medial axis. Returns flat arrays: points (3n), tangents (3n), radii (n), arc length.
export function sampleKnotMedialAxis(template, samplesPerSpan = 16)
{
    const points = [], tangents = [], radii = [];
    const p = [0, 0, 0], t = [0, 0, 0];
    for (let si = 0; si < template.spans.length; ++si)
    {
        const span = template.spans[si];
        const startK = si === 0 ? 0 : 1; // do not duplicate the shared node between spans
        for (let k = startK; k <= samplesPerSpan; ++k)
        {
            const u = k / samplesPerSpan;
            bezierPoint(span.controls, u, p);
            bezierTangent(span.controls, u, t);
            points.push(p[0], p[1], p[2]);
            tangents.push(t[0], t[1], t[2]);
            radii.push(span.radii[0] * (1 - u) ** 3 + span.radii[1] * 3 * (1 - u) ** 2 * u
                     + span.radii[2] * 3 * (1 - u) * u * u + span.radii[3] * u ** 3);
        }
    }
    const count = radii.length;
    const cumulative = new Float64Array(count);
    for (let i = 1; i < count; ++i)
        cumulative[i] = cumulative[i - 1] + Math.hypot(points[i * 3] - points[(i - 1) * 3], points[i * 3 + 1] - points[(i - 1) * 3 + 1], points[i * 3 + 2] - points[(i - 1) * 3 + 2]);
    return { points: new Float64Array(points), tangents: new Float64Array(tangents), radii: new Float64Array(radii), cumulative, count, length: cumulative[count - 1] || 0 };
}

// Parallel-transport frames along a sampled axis: frames are [n, b] per sample (6n floats).
// Transport = project the previous normal onto the plane normal to the new tangent and renormalise,
// which is the discrete parallel transport used for pipe/rod frames (no twist is introduced).
export function parallelTransportFrames(points, tangents, count)
{
    const frames = new Float64Array(count * 6);
    // First normal: any vector not parallel to the first tangent.
    let nx = 0, ny = 1, nz = 0;
    const t0x = tangents[0], t0y = tangents[1], t0z = tangents[2];
    if (Math.abs(t0y) > 0.9) { nx = 1; ny = 0; }
    let d = nx * t0x + ny * t0y + nz * t0z;
    nx -= d * t0x; ny -= d * t0y; nz -= d * t0z;
    let l = Math.hypot(nx, ny, nz) || 1; nx /= l; ny /= l; nz /= l;
    for (let i = 0; i < count; ++i)
    {
        const tx = tangents[i * 3], ty = tangents[i * 3 + 1], tz = tangents[i * 3 + 2];
        d = nx * tx + ny * ty + nz * tz;
        nx -= d * tx; ny -= d * ty; nz -= d * tz;
        l = Math.hypot(nx, ny, nz);
        if (l < 1e-8) { // tangent turned 180 degrees in one step: pick a fresh perpendicular
            nx = Math.abs(tx) < 0.9 ? 1 : 0; ny = Math.abs(tx) < 0.9 ? 0 : 1; nz = 0;
            d = nx * tx + ny * ty + nz * tz; nx -= d * tx; ny -= d * ty; nz -= d * tz;
            l = Math.hypot(nx, ny, nz) || 1;
        }
        nx /= l; ny /= l; nz /= l;
        frames[i * 6] = nx; frames[i * 6 + 1] = ny; frames[i * 6 + 2] = nz;
        frames[i * 6 + 3] = ty * nz - tz * ny; frames[i * 6 + 4] = tz * nx - tx * nz; frames[i * 6 + 5] = tx * ny - ty * nx;
        void points;
    }
    return frames;
}

// Sec. 4.3: shrink radii (only where needed) until non-adjacent pipe samples no longer overlap.
// Returns the number of reduction rounds applied. Medial samples themselves never move.
export function reduceRadiiForPipeCollisions(medial, minGap = 0.001, maxRounds = 24)
{
    const { points, radii, count } = medial;
    let rounds = 0;
    for (; rounds < maxRounds; ++rounds)
    {
        let collided = false;
        for (let i = 0; i < count; ++i)
        {
            for (let j = i + 3; j < count; ++j)
            {
                const dx = points[j * 3] - points[i * 3], dy = points[j * 3 + 1] - points[i * 3 + 1], dz = points[j * 3 + 2] - points[i * 3 + 2];
                const dist = Math.hypot(dx, dy, dz);
                const allowed = radii[i] + radii[j] + minGap;
                if (dist < allowed && dist > 1e-9)
                {
                    collided = true;
                    const scale = Math.max(0.55, (dist - minGap) / allowed);
                    radii[i] *= scale; radii[j] *= scale;
                }
            }
        }
        if (!collided) break;
    }
    return rounds;
}

// Sec. 4.2 tessellation -> triangle soup. positions (3m), indices, plus the medial data so the pipe
// can also be used as capsule colliders (centre_i, radius_i) by solver Option D.
export function tessellateKnotTemplate(template, { samplesPerSpan = 16, radialSegments = 10, reduceCollisions = true } = {})
{
    const medial = sampleKnotMedialAxis(template, samplesPerSpan);
    if (reduceCollisions) reduceRadiiForPipeCollisions(medial);
    const frames = parallelTransportFrames(medial.points, medial.tangents, medial.count);
    const { points, radii, count } = medial;
    const o = template.orientation || 0;
    const positions = [];
    for (let i = 0; i < count; ++i)
    {
        const cx = points[i * 3], cy = points[i * 3 + 1], cz = points[i * 3 + 2];
        const nx = frames[i * 6], ny = frames[i * 6 + 1], nz = frames[i * 6 + 2];
        const bx = frames[i * 6 + 3], by = frames[i * 6 + 4], bz = frames[i * 6 + 5];
        for (let j = 0; j < radialSegments; ++j)
        {
            const a = o + (2 * Math.PI * j) / radialSegments;
            const ca = Math.cos(a) * radii[i], sa = Math.sin(a) * radii[i];
            positions.push(cx + ca * nx + sa * bx, cy + ca * ny + sa * by, cz + ca * nz + sa * bz);
        }
    }
    // End-cap centres.
    const startCap = positions.length / 3;
    positions.push(points[0], points[1], points[2]);
    const endCap = positions.length / 3;
    positions.push(points[(count - 1) * 3], points[(count - 1) * 3 + 1], points[(count - 1) * 3 + 2]);

    const indices = [];
    for (let i = 0; i < count - 1; ++i)
        for (let j = 0; j < radialSegments; ++j)
        {
            const j1 = (j + 1) % radialSegments;
            const a = i * radialSegments + j, b = i * radialSegments + j1;
            const c = (i + 1) * radialSegments + j, d2 = (i + 1) * radialSegments + j1;
            indices.push(a, c, b, b, c, d2);
        }
    for (let j = 0; j < radialSegments; ++j)
    {
        const j1 = (j + 1) % radialSegments;
        indices.push(startCap, j1, j);
        const last = (count - 1) * radialSegments;
        indices.push(endCap, last + j, last + j1);
    }
    return {
        id: template.id, label: template.label, orientation: template.orientation || 0,
        positions: new Float64Array(positions), indices: new Uint32Array(indices),
        medial, frames, radialSegments,
    };
}

function medialAt(medial, frames, arc01, out)
{
    const target = Math.min(1, Math.max(0, arc01)) * medial.length;
    const cum = medial.cumulative;
    let i = 0;
    while (i < medial.count - 2 && cum[i + 1] < target) ++i;
    const spanLen = Math.max(1e-9, cum[i + 1] - cum[i]);
    const f = Math.min(1, Math.max(0, (target - cum[i]) / spanLen));
    for (let a = 0; a < 3; ++a)
        out.center[a] = medial.points[i * 3 + a] * (1 - f) + medial.points[(i + 1) * 3 + a] * f;
    for (let a = 0; a < 6; ++a)
        out.frame[a] = frames[i * 6 + a] * (1 - f) + frames[(i + 1) * 6 + a] * f;
    out.radius = medial.radii[i] * (1 - f) + medial.radii[i + 1] * f;
    // Renormalise the interpolated frame.
    const nl = Math.hypot(out.frame[0], out.frame[1], out.frame[2]) || 1;
    out.frame[0] /= nl; out.frame[1] /= nl; out.frame[2] /= nl;
    const bl = Math.hypot(out.frame[3], out.frame[4], out.frame[5]) || 1;
    out.frame[3] /= bl; out.frame[4] /= bl; out.frame[5] /= bl;
    return out;
}

// Sec. 5.1 rectangular mapping: (u along the piece width, v along its length) -> pipe surface.
// uNorm in [0,1] wraps once around the circumference; vNorm in [0,1] runs along the axis.
export function mapRectangleOntoPipe(pipe, uNorm, vNorm, out, scratch = { center: [0, 0, 0], frame: new Float64Array(6), radius: 0 })
{
    medialAt(pipe.medial, pipe.frames, vNorm, scratch);
    const a = (pipeMedialOrientation(pipe) + uNorm * 2 * Math.PI);
    const ca = Math.cos(a) * scratch.radius, sa = Math.sin(a) * scratch.radius;
    out[0] = scratch.center[0] + ca * scratch.frame[0] + sa * scratch.frame[3];
    out[1] = scratch.center[1] + ca * scratch.frame[1] + sa * scratch.frame[4];
    out[2] = scratch.center[2] + ca * scratch.frame[2] + sa * scratch.frame[5];
    return out;
}

// Sec. 5.1 circular mapping: a disc (rNorm, theta) maps sector-wise: angle -> around the pipe,
// radius -> along the axis, so a pulled-through fabric tip becomes a wrapped knot end.
export function mapDiscOntoPipe(pipe, rNorm, theta, out, scratch = { center: [0, 0, 0], frame: new Float64Array(6), radius: 0 })
{
    return mapRectangleOntoPipe(pipe, theta / (2 * Math.PI), Math.min(1, Math.max(0, rNorm)), out, scratch);
}

function pipeMedialOrientation(pipe)
{
    return pipe.orientation || 0;
}
