//============================================================================================================================================
//                                                        PREVIEWTHUMBNAILS.JS
//============================================================================================================================================
// 🧩 Procedural 2D canvas preview rasterization for Fluid Studio scene presets, analytical force fields, particle stacks, and shading orbs

import { ResolveObjectMesh, SampleObjectAnchors } from '../Engine/ObjectMeshCatalog.js';

const ThumbnailCache = new Map();

function seededFraction(seed)
{
    const raw = Math.sin(seed * 127.1 + 311.7) * 43758.5453123;
    return raw - Math.floor(raw);
}

function hashText(text)
{
    let hash = 2166136261;
    const source = String(text || '');
    for (let index = 0; index < source.length; index += 1)
    {
        hash ^= source.charCodeAt(index);
        hash = Math.imul(hash, 16777619);
    }
    return hash >>> 0;
}

function hexToRgba(hex, alpha)
{
    const clean = String(hex || '#888888').replace('#', '');
    const full = clean.length === 3 ? clean.split('').map((ch) => ch + ch).join('') : clean;
    const numeric = parseInt(full, 16);
    if (!Number.isFinite(numeric)) return `rgba(160, 165, 175, ${alpha})`;
    const r = (numeric >> 16) & 255;
    const g = (numeric >> 8) & 255;
    const b = numeric & 255;
    return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

function createCanvas(width, height, className = 'preview-canvas')
{
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    if (className) canvas.className = className;
    return canvas;
}

function paintBackdrop(ctx, width, height, accentHex = '#2a2e38')
{
    const bg = ctx.createLinearGradient(0, 0, 0, height);
    bg.addColorStop(0, '#17191e');
    bg.addColorStop(0.68, '#101114');
    bg.addColorStop(1, '#0b0c0e');
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, width, height);

    const glow = ctx.createRadialGradient(width * 0.5, height * 0.58, 4, width * 0.5, height * 0.58, width * 0.62);
    glow.addColorStop(0, hexToRgba(accentHex, 0.22));
    glow.addColorStop(1, 'rgba(0, 0, 0, 0)');
    ctx.fillStyle = glow;
    ctx.fillRect(0, 0, width, height);

    // Subtle perspective floor grid
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.045)';
    ctx.lineWidth = 1;
    const horizon = height * 0.76;
    ctx.beginPath();
    ctx.moveTo(0, horizon);
    ctx.lineTo(width, horizon);
    for (let index = -4; index <= 4; index += 1)
    {
        const xTop = width * 0.5 + index * (width * 0.08);
        const xBottom = width * 0.5 + index * (width * 0.19);
        ctx.moveTo(xTop, horizon);
        ctx.lineTo(xBottom, height);
    }
    ctx.stroke();
}

function drawArrowhead(ctx, x, y, dx, dy, size)
{
    const length = Math.hypot(dx, dy) || 1;
    const ux = dx / length;
    const uy = dy / length;
    const px = -uy;
    const py = ux;
    ctx.beginPath();
    ctx.moveTo(x + ux * size, y + uy * size);
    ctx.lineTo(x - ux * size * 0.8 + px * size * 0.62, y - uy * size * 0.8 + py * size * 0.62);
    ctx.lineTo(x - ux * size * 0.8 - px * size * 0.62, y - uy * size * 0.8 - py * size * 0.62);
    ctx.closePath();
    ctx.fill();
}

function sampleForceVector(typeId, nx, ny)
{
    const r = Math.hypot(nx, ny) + 1e-4;
    switch (typeId)
    {
        case 'DirectionalWind':
            return [1.0, Math.sin(nx * 3.2) * 0.16];
        case 'WindShear':
            return [ny * 1.35, Math.sin(nx * 2.2) * 0.14];
        case 'FlowStream':
            return [Math.exp(-ny * ny * 2.4) * 1.2, -ny * 0.28];
        case 'DirectionalBurst':
            return [nx * 0.35, -1.15 * Math.exp(-nx * nx * 1.8)];
        case 'PointAttractor':
            return [-nx / r * Math.min(1.2, 0.85 / (r + 0.25)), -ny / r * Math.min(1.2, 0.85 / (r + 0.25))];
        case 'PointRepulsor':
        case 'RadialBurst':
            return [nx / r * Math.min(1.25, 0.9 / (r + 0.22)), ny / r * Math.min(1.25, 0.9 / (r + 0.22))];
        case 'AxisAttractor':
            return [-nx * 1.2, -0.25];
        case 'Orbit':
            return [(-ny - nx * 0.28) / (r + 0.22), (nx - ny * 0.28) / (r + 0.22)];
        case 'Containment':
            return r > 0.58 ? [-nx * 1.25, -ny * 1.25] : [-ny * 0.25, nx * 0.25];
        case 'MagneticDipole':
        {
            const r3 = Math.pow(r * r + 0.16, 1.5);
            return [(3 * nx * ny) / r3 * 0.38, (2 * ny * ny - nx * nx) / r3 * 0.38];
        }
        case 'VortexColumn':
            return [(-ny - nx * 0.32) / (r + 0.2), (nx * 0.72 - 0.48) / (r + 0.25)];
        case 'VortexRing':
        {
            const left = Math.hypot(nx + 0.42, ny) + 0.16;
            const right = Math.hypot(nx - 0.42, ny) + 0.16;
            return [-(ny / left) * 0.45 + (ny / right) * 0.45, ((nx + 0.42) / left) * 0.45 - ((nx - 0.42) / right) * 0.45 - 0.35];
        }
        case 'Spin':
            return [-ny * 1.15, nx * 1.15];
        case 'CurlNoiseTurbulence':
            return [Math.sin(ny * 4.2 + nx * 1.7), Math.cos(nx * 4.2 - ny * 1.6)];
        case 'BrownianJitter':
            return [Math.sin(nx * 12.7 + ny * 7.3), Math.cos(ny * 11.3 - nx * 8.9)];
        case 'ShockwaveShell':
        {
            const ring = Math.exp(-Math.pow(r - 0.62, 2) * 18);
            return [(nx / r) * (0.25 + ring * 1.2), (ny / r) * (0.25 + ring * 1.2)];
        }
        case 'Implosion':
            return [-(nx / r) * (0.45 + r * 0.8), -(ny / r) * (0.45 + r * 0.8)];
        case 'DragBaffle':
            return [0.75 * (1 - Math.exp(-r * r * 2.2)), ny * 0.12];
        case 'ThermalUpdraft':
        default:
            return [-nx * 0.22, -Math.exp(-nx * nx * 2.4) * 1.25];
    }
}

export function renderForceThumbnail(forceType, width = 260, height = 148)
{
    const canvas = createCanvas(width, height);
    if (typeof canvas.getContext !== 'function') return canvas;
    const key = `force:${forceType.Id}:${width}x${height}`;
    const ctx = canvas.getContext('2d');
    if (!ctx) return canvas;

    paintBackdrop(ctx, width, height, forceType.Tint);
    const cx = width * 0.5;
    const cy = height * 0.52;
    const scale = Math.min(width, height) * 0.42;

    // Reach contour ring
    ctx.save();
    ctx.strokeStyle = hexToRgba(forceType.Tint, 0.32);
    ctx.setLineDash([4, 4]);
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.arc(cx, cy, scale * 0.82, 0, Math.PI * 2);
    ctx.stroke();
    if (forceType.Id === 'ShockwaveShell')
    {
        ctx.setLineDash([]);
        ctx.strokeStyle = hexToRgba(forceType.Tint, 0.72);
        ctx.lineWidth = 2.2;
        ctx.beginPath();
        ctx.arc(cx, cy, scale * 0.62, 0, Math.PI * 2);
        ctx.stroke();
    }
    ctx.restore();

    // Streamlines integrated through the analytical field
    const seeds = 22;
    for (let index = 0; index < seeds; index += 1)
    {
        const angle = (index / seeds) * Math.PI * 2;
        const radius = 0.22 + (index % 3) * 0.28;
        let nx = Math.cos(angle) * radius * 1.25;
        let ny = Math.sin(angle) * radius * 0.92;
        ctx.beginPath();
        ctx.moveTo(cx + nx * scale, cy + ny * scale);
        let vx = 0;
        let vy = 0;
        for (let step = 0; step < 14; step += 1)
        {
            [vx, vy] = sampleForceVector(forceType.Id, nx, ny);
            const mag = Math.hypot(vx, vy) || 1;
            nx += (vx / mag) * 0.085;
            ny += (vy / mag) * 0.085;
            ctx.lineTo(cx + nx * scale, cy + ny * scale);
        }
        ctx.strokeStyle = hexToRgba(forceType.Tint, index % 2 === 0 ? 0.68 : 0.36);
        ctx.lineWidth = index % 2 === 0 ? 1.5 : 1.0;
        ctx.stroke();
        if (index % 2 === 0)
        {
            ctx.fillStyle = hexToRgba(forceType.Tint, 0.92);
            drawArrowhead(ctx, cx + nx * scale, cy + ny * scale, vx, vy, 3.6);
        }
    }

    // Center source node badge
    ctx.fillStyle = hexToRgba(forceType.Tint, 0.95);
    ctx.beginPath();
    ctx.arc(cx, cy, 3.4, 0, Math.PI * 2);
    ctx.fill();
    ThumbnailCache.set(key, true);
    return canvas;
}

export function renderPresetThumbnail(preset, width = 260, height = 148)
{
    const canvas = createCanvas(width, height);
    if (typeof canvas.getContext !== 'function') return canvas;
    const ctx = canvas.getContext('2d');
    if (!ctx) return canvas;

    const doc = preset.Document || {};
    const layers = (doc.Volume && doc.Volume[0] && doc.Volume[0].Layer) || [];
    const smokeLayer = layers.find((layer) => layer.Category === 107);
    const fireLayer = layers.find((layer) => layer.Category === 108);
    const emitters = doc.Emitter || [];
    const forces = doc.Force || [];
    const particles = doc.Particle || [];
    const colliders = doc.Collider || [];

    const hasGas = emitters.length > 0;
    const hasFire = hasGas
        && Boolean(!fireLayer || (fireLayer.Enabled !== false && (fireLayer.Parameters?.FireIntensity ?? 1) > 0.1))
        && emitters.some((emitter) => (emitter.Fuel ?? 0) > 0.05 || (emitter.Temperature ?? 0) > 2.4);
    const isRing = forces.some((force) => force.Type === 'VortexRing') || /ring/i.test(preset.Name);
    const hasCollider = colliders.length > 0 || /collider/i.test(preset.Name);
    const isVortex = forces.some((force) => ['VortexColumn', 'Spin', 'Orbit'].includes(force.Type)) || /tornado|vortex/i.test(preset.Name);
    const isBurst = !hasCollider && (forces.some((force) => ['RadialBurst', 'ShockwaveShell', 'Implosion', 'DirectionalBurst'].includes(force.Type)) || /explosion|megaton|demolition|ordnance|airburst/i.test(preset.Name));
    const isHorizontal = /wind|drift|tunnel|gust|crossing/i.test(preset.Name);
    const isBlank = preset.Id === 'preset-blank';

    const flameCentre = fireLayer?.Parameters?.FireCore || '#fff3cf';
    const fireFlame = fireLayer?.Parameters?.FireFlame || '#ff7a18';
    const fireEmber = fireLayer?.Parameters?.FireEmber || '#c82808';
    const smokeHex = smokeLayer?.Parameters?.Albedo || '#8d929c';
    const accent = hasFire ? fireFlame : !hasGas && particles.length ? '#38bdf8' : smokeHex;

    paintBackdrop(ctx, width, height, accent);

    // Wireframe bounds box hint
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.09)';
    ctx.lineWidth = 1;
    ctx.strokeRect(width * 0.24, height * 0.14, width * 0.52, height * 0.66);

    if (isBlank)
    {
        ctx.fillStyle = 'rgba(255, 255, 255, 0.22)';
        ctx.beginPath();
        ctx.arc(width * 0.5, height * 0.5, 4, 0, Math.PI * 2);
        ctx.fill();
        return canvas;
    }

    const seedBase = hashText(preset.Id);

    if (hasGas && isRing)
    {
        for (let ringIdx = 0; ringIdx < 3; ringIdx += 1)
        {
            const ry = height * (0.7 - ringIdx * 0.2);
            const rx = 18 + ringIdx * 11;
            ctx.strokeStyle = hexToRgba(smokeHex, 0.62 - ringIdx * 0.14);
            ctx.lineWidth = 6 - ringIdx;
            ctx.beginPath();
            ctx.ellipse(width * 0.5, ry, rx, rx * 0.36, 0, 0, Math.PI * 2);
            ctx.stroke();
        }
    }
    else if (hasGas)
    {
        const puffCount = isBurst ? 34 : 28;
        for (let index = 0; index < puffCount; index += 1)
        {
            const u = index / Math.max(1, puffCount - 1);
            const jitterA = seededFraction(seedBase + index * 3.1) - 0.5;
            const jitterB = seededFraction(seedBase + index * 7.7) - 0.5;

            let px = width * 0.5;
            let py = height * 0.76 - u * height * 0.58;
            let radius = 8 + u * 24 + Math.abs(jitterA) * 9;

            if (hasCollider)
            {
                const side = index % 2 === 0 ? -1 : 1;
                const deflect = Math.exp(-Math.pow((u - 0.46) * 4.2, 2)) * 26;
                px = width * 0.5 + side * (6 + deflect) + jitterA * 8;
                radius = 7 + u * 16;
            }
            else if (isVortex)
            {
                const swirl = Math.sin(u * 10 + seedBase * 0.01) * (4 + u * 26);
                px = width * 0.5 + swirl;
                radius = 5 + u * 18;
            }
            else if (isBurst)
            {
                const theta = seededFraction(seedBase + index * 5.3) * Math.PI * 2;
                const dist = Math.pow(u, 0.65) * height * 0.34;
                px = width * 0.5 + Math.cos(theta) * dist * 1.25;
                py = height * 0.52 + Math.sin(theta) * dist * 0.88 - u * 8;
                radius = 10 + (1 - u) * 16;
            }
            else if (isHorizontal)
            {
                px = width * 0.2 + u * width * 0.62;
                py = height * 0.66 - u * height * 0.22 + jitterB * 14;
                radius = 7 + u * 20;
            }
            else
            {
                px += jitterA * (10 + u * 28);
                py += jitterB * 7;
            }

            const smokeGrad = ctx.createRadialGradient(px - radius * 0.22, py - radius * 0.22, 1, px, py, radius);
            smokeGrad.addColorStop(0, hexToRgba(smokeHex, hasFire ? 0.34 : 0.54));
            smokeGrad.addColorStop(0.6, hexToRgba(smokeHex, 0.18));
            smokeGrad.addColorStop(1, 'rgba(0,0,0,0)');
            ctx.fillStyle = smokeGrad;
            ctx.beginPath();
            ctx.arc(px, py, radius, 0, Math.PI * 2);
            ctx.fill();

            if (hasFire && u < (isBurst ? 0.78 : 0.62))
            {
                const heat = 1 - u / (isBurst ? 0.78 : 0.62);
                const fireR = radius * (0.55 + heat * 0.35);
                const fireGrad = ctx.createRadialGradient(px, py, 1, px, py, fireR);
                fireGrad.addColorStop(0, hexToRgba(flameCentre, 0.85 * heat));
                fireGrad.addColorStop(0.42, hexToRgba(fireFlame, 0.62 * heat));
                fireGrad.addColorStop(0.82, hexToRgba(fireEmber, 0.28 * heat));
                fireGrad.addColorStop(1, 'rgba(0,0,0,0)');
                ctx.globalCompositeOperation = 'lighter';
                ctx.fillStyle = fireGrad;
                ctx.beginPath();
                ctx.arc(px, py, fireR, 0, Math.PI * 2);
                ctx.fill();
                ctx.globalCompositeOperation = 'source-over';
            }
        }
    }

    if (hasCollider)
    {
        const cx = width * 0.5;
        const cy = height * 0.49;
        const cr = 16;
        const sph = ctx.createRadialGradient(cx - 5, cy - 5, 2, cx, cy, cr);
        sph.addColorStop(0, '#eef2f7');
        sph.addColorStop(0.55, '#6c7687');
        sph.addColorStop(1, '#1e222b');
        ctx.fillStyle = sph;
        ctx.beginPath();
        ctx.arc(cx, cy, cr, 0, Math.PI * 2);
        ctx.fill();
    }

    // Overlay preset particle clouds if present
    if (particles.length > 0)
    {
        const count = hasGas ? 42 : 96;
        ctx.globalCompositeOperation = 'lighter';
        for (let pIndex = 0; pIndex < count; pIndex += 1)
        {
            const a = seededFraction(seedBase + 900 + pIndex * 2.3) * Math.PI * 2;
            const r = (hasGas ? 8 : 4) + seededFraction(seedBase + 500 + pIndex * 4.1) * height * 0.36;
            const sx = width * 0.5 + Math.cos(a + (isVortex ? r * 0.05 : 0)) * r * 1.32;
            const sy = height * 0.48 + Math.sin(a + (isVortex ? r * 0.05 : 0)) * r * 0.84;
            const huePick = pIndex % 3 === 0 ? 'rgba(255, 214, 130, 0.9)' : pIndex % 3 === 1 ? 'rgba(96, 205, 255, 0.88)' : 'rgba(192, 132, 252, 0.84)';
            ctx.fillStyle = huePick;
            ctx.beginPath();
            ctx.arc(sx, sy, 1.5 + (pIndex % 3) * 0.8, 0, Math.PI * 2);
            ctx.fill();
        }
        ctx.globalCompositeOperation = 'source-over';
    }

    return canvas;
}

export function renderParticleThumbnail(template, width = 260, height = 148)
{
    const canvas = createCanvas(width, height);
    if (typeof canvas.getContext !== 'function') return canvas;
    const ctx = canvas.getContext('2d');
    if (!ctx) return canvas;

    const defaults = template.Defaults || {};
    const palette = (defaults.Palette && defaults.Palette.length ? defaults.Palette : [template.Tint || '#38bdf8', '#ffffff']);
    paintBackdrop(ctx, width, height, palette[0]);

    const cx = width * 0.5;
    const cy = height * 0.5;
    const seedBase = hashText(template.Id);
    const hasTrails = defaults.Trail && defaults.Trail !== 'Off';
    const shape = defaults.Shape || 'Sphere';
    const isCloner = defaults.Mode === 'Cloner';

    ctx.save();
    ctx.globalCompositeOperation = 'lighter';

    const objectName = defaults.CarrierMode === 'Object3D' ? defaults.CarrierObject : defaults.SpawnObject;
    if (defaults.CarrierMode === 'Object3D' || defaults.Shape === 'ObjectSurface' || ['Spiderweb', 'ClothGrid', 'FloraBed', 'FireworkBurst'].includes(shape))
    {
        // 3D object content: anchors from ObjectMeshCatalog (or the structured web / cloth / flora / shell shapes) drawn as a
        // depth-shaded point cloud, so the thumbnail shows the actual mesh silhouette instead of a generic blob.
        if (defaults.BackingSurface && defaults.BackingSurface !== 'Hidden')
        {
            const backing = ctx.createRadialGradient(cx, cy + 22, 6, cx, cy + 22, 70);
            backing.addColorStop(0, defaults.BackingSurface === 'BrushedChrome' ? 'rgba(214,222,235,0.42)' : 'rgba(60,78,102,0.42)');
            backing.addColorStop(1, 'rgba(0,0,0,0)');
            ctx.fillStyle = backing;
            ctx.beginPath();
            ctx.ellipse(cx, cy + 26, 74, 20, 0, 0, Math.PI * 2);
            ctx.fill();
        }
        let points = null;
        if (defaults.Shape === 'ObjectSurface' || defaults.CarrierMode === 'Object3D')
        {
            const mesh = ResolveObjectMesh(objectName || 'UtahTeapot', defaults.CustomObjectId || '');
            points = SampleObjectAnchors(mesh, 460, defaults.SpawnSampleMode || 'EvenSurface', defaults.ObjectYaw || 0).Anchors.map((anchor) => anchor.Position);
        }
        else if (shape === 'Spiderweb')
        {
            points = [];
            const spokes = 8, rings = 5;
            for (let s = 0; s < spokes; s += 1)
                for (let r = 1; r <= rings * 3; r += 1)
                {
                    const a = (s / spokes) * Math.PI * 2;
                    const radius = (r / (rings * 3));
                    points.push([Math.cos(a) * radius, Math.sin(a) * radius * 0.42, -Math.sin(radius * Math.PI) * 0.25]);
                }
            for (let ring = 1; ring <= rings; ring += 1)
                for (let step = 0; step < spokes * 3; step += 1)
                {
                    const a = (step / (spokes * 3)) * Math.PI * 2;
                    const radius = ring / rings;
                    points.push([Math.cos(a) * radius, Math.sin(a) * radius * 0.42, -Math.sin(radius * Math.PI) * 0.25]);
                }
        }
        else if (shape === 'ClothGrid')
        {
            points = [];
            for (let gx = 0; gx <= 18; gx += 1)
                for (let gy = 0; gy <= 13; gy += 1)
                {
                    const u = (gx / 18) * 2 - 1, v = gy / 13;
                    points.push([u, 0.55 - v * 1.1, Math.sin(u * Math.PI * 3) * (0.1 + v * 0.22)]);
                }
        }
        else if (shape === 'FloraBed')
        {
            points = [];
            for (let blade = 0; blade < 26; blade += 1)
            {
                const a = seededFraction(seedBase + blade * 3.1) * Math.PI * 2;
                const rootR = Math.sqrt(seededFraction(seedBase + blade * 7.7)) * 0.9;
                const height = 0.35 + seededFraction(seedBase + blade * 5.3) * 0.75;
                for (let step = 0; step < 6; step += 1)
                {
                    const h = step / 5;
                    const bend = Math.pow(h, 1.6) * 0.32;
                    points.push([Math.cos(a) * rootR + Math.sin(a) * bend, -0.45 + h * height, Math.sin(a) * rootR - Math.cos(a) * bend]);
                }
            }
        }
        else
        {
            points = [];
            for (let index = 0; index < 220; index += 1)
            {
                const y = 1 - (index / 220) * 2;
                const r = Math.sqrt(Math.max(0, 1 - y * y));
                const a = index * 2.39996323;
                const lobe = 0.78 + 0.22 * Math.abs(Math.cos(a * 3));
                points.push([Math.cos(a) * r * lobe, y * lobe, Math.sin(a) * r * lobe]);
            }
        }
        if (points)
        {
            const scale = Math.min(width, height) * 0.34;
            const cosY = 0.86, sinY = 0.51, cosP = 0.80, sinP = 0.60;
            for (let index = 0; index < points.length; index += 1)
            {
                const [px0, py0, pz0] = points[index];
                const x = px0 * cosY + pz0 * sinY;
                const z = -px0 * sinY + pz0 * cosY;
                const y = py0 * cosP - z * sinP;
                const depth = py0 * sinP + z * cosP;
                const px = cx + x * scale;
                const py = cy - y * scale;
                const col = palette[index % palette.length];
                const fade = 0.35 + 0.55 * Math.max(0, 1 - (depth + 1) * 0.5);
                ctx.fillStyle = hexToRgba(col, fade);
                ctx.beginPath();
                ctx.arc(px, py, 1.5, 0, Math.PI * 2);
                ctx.fill();
            }
        }
    }
    else if (defaults.DistortionMode === 'GravitationalLens')
    {
        // Relativistic accretion swirl + photon ring + dark event horizon
        for (let index = 0; index < 44; index += 1)
        {
            const a = (index / 44) * Math.PI * 2;
            const rx = 44 + (index % 4) * 7;
            const ry = rx * 0.34;
            ctx.strokeStyle = hexToRgba(palette[index % palette.length], 0.52);
            ctx.lineWidth = 2.0;
            ctx.beginPath();
            ctx.ellipse(cx, cy, rx, ry, -0.26, a, a + 0.55);
            ctx.stroke();
        }
        const ringGlow = ctx.createRadialGradient(cx, cy, 14, cx, cy, 34);
        ringGlow.addColorStop(0, 'rgba(255, 243, 199, 0.95)');
        ringGlow.addColorStop(0.45, hexToRgba(palette[1] || '#f59e0b', 0.72));
        ringGlow.addColorStop(1, 'rgba(0,0,0,0)');
        ctx.fillStyle = ringGlow;
        ctx.beginPath();
        ctx.arc(cx, cy, 34, 0, Math.PI * 2);
        ctx.fill();
        ctx.globalCompositeOperation = 'source-over';
        ctx.fillStyle = '#05060a';
        ctx.beginPath();
        ctx.arc(cx, cy, 14, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = 'rgba(255, 236, 179, 0.92)';
        ctx.lineWidth = 1.6;
        ctx.stroke();
    }
    else if (defaults.DerezMode && defaults.DerezMode !== 'Off')
    {
        // Isometric voxel formation + glowing derez wave front + released cubes / butterflies
        const edgeCol = defaults.DerezEdgeColour || '#00f6ff';
        const isFly = defaults.DerezMode === 'ButterflyMorph';
        for (let gx = -3; gx <= 3; gx += 1)
        {
            for (let gy = -3; gy <= 3; gy += 1)
            {
                const diag = (gx - gy) / 6;
                const released = diag > 0.05;
                const onEdge = Math.abs(diag - 0.05) < 0.28;
                let px = cx - 10 + gx * 8 + gy * 4;
                let py = cy + 8 + gy * 7 - gx * 3;
                if (released)
                {
                    const burst = (diag - 0.05) * 34;
                    px += burst * 1.1 + (seededFraction(seedBase + gx * 7 + gy * 13) - 0.3) * 14;
                    py -= burst * (isFly ? 1.25 : 0.65) + seededFraction(seedBase + gx * 3 + gy * 19) * 12;
                }
                const col = onEdge ? edgeCol : palette[Math.abs(gx + gy) % palette.length];
                ctx.fillStyle = hexToRgba(col, onEdge ? 0.98 : (released ? 0.78 : 0.88));
                if (released && isFly)
                {
                    ctx.beginPath();
                    ctx.ellipse(px - 3, py, 3.6, 2.2, -0.4, 0, Math.PI * 2);
                    ctx.ellipse(px + 3, py, 3.6, 2.2, 0.4, 0, Math.PI * 2);
                    ctx.fill();
                }
                else
                {
                    const sz = onEdge ? 5.6 : 4.6;
                    ctx.fillRect(px - sz * 0.5, py - sz * 0.5, sz, sz);
                }
            }
        }
    }
    else if (shape === 'MocapFigure' || (defaults.MocapClip && defaults.MocapClip !== 'Off'))
    {
        // Articulated 15-joint mocap performer + ribbons / hologram scanlines
        const joints = [
            [0, -36], [0, -24], [0, -2], [-15, -20], [-26, -6], [-18, 8],
            [15, -20], [26, -12], [32, -24], [-9, 0], [-16, 20], [-24, 38],
            [9, 0], [18, 16], [12, 36],
        ].map(([jx, jy]) => [cx + jx, cy + jy]);
        const bones = [[0, 1], [1, 2], [1, 3], [3, 4], [4, 5], [1, 6], [6, 7], [7, 8], [2, 9], [9, 10], [10, 11], [2, 12], [12, 13], [13, 14]];
        if (hasTrails)
        {
            for (const idx of [5, 8, 11, 14])
            {
                const [jx, jy] = joints[idx];
                ctx.strokeStyle = hexToRgba(palette[idx % palette.length], 0.65);
                ctx.lineWidth = 2.0;
                ctx.beginPath();
                ctx.moveTo(jx, jy);
                ctx.bezierCurveTo(jx - 18, jy + 4, jx - 34, jy - 8, jx - 48, jy + 6);
                ctx.stroke();
            }
        }
        for (let b = 0; b < bones.length; b += 1)
        {
            const [p0, p1] = bones[b];
            ctx.strokeStyle = hexToRgba(palette[b % palette.length], 0.85);
            ctx.lineWidth = 3.2;
            ctx.beginPath();
            ctx.moveTo(joints[p0][0], joints[p0][1]);
            ctx.lineTo(joints[p1][0], joints[p1][1]);
            ctx.stroke();
        }
        for (let j = 0; j < joints.length; j += 1)
        {
            ctx.fillStyle = hexToRgba(palette[j % palette.length], 0.96);
            ctx.beginPath();
            ctx.arc(joints[j][0], joints[j][1], j === 0 ? 6.2 : 2.8, 0, Math.PI * 2);
            ctx.fill();
        }
        if ((defaults.GlitchAmount || 0) > 0.1)
        {
            for (let s = -2; s <= 2; s += 1)
            {
                ctx.fillStyle = hexToRgba(palette[(s + 4) % palette.length], 0.5);
                ctx.fillRect(cx - 38 + s * 7, cy + s * 13, 76, 1.8);
            }
        }
    }
    else if (defaults.Sprite === 'HexTile')
    {
        // Hexagonal dome / torus energy shield lattice
        const hexR = 8.5;
        for (let row = -3; row <= 3; row += 1)
        {
            for (let col = -4; col <= 4; col += 1)
            {
                const hx = cx + col * hexR * 1.65 + (row % 2) * hexR * 0.82;
                const hy = cy + row * hexR * 1.42;
                const dist = Math.hypot((hx - cx) / 56, (hy - cy) / 42);
                if (dist > 1.0) continue;
                const ripple = 0.45 + 0.55 * Math.cos(dist * 7.5 - 1.4);
                ctx.strokeStyle = hexToRgba(palette[Math.abs(row + col) % palette.length], 0.35 + 0.6 * ripple);
                ctx.lineWidth = 1.3;
                ctx.beginPath();
                for (let k = 0; k < 6; k += 1)
                {
                    const ang = (k / 6) * Math.PI * 2 + Math.PI / 6;
                    const vx = hx + Math.cos(ang) * (hexR * 0.82);
                    const vy = hy + Math.sin(ang) * (hexR * 0.82);
                    if (k === 0) ctx.moveTo(vx, vy); else ctx.lineTo(vx, vy);
                }
                ctx.closePath();
                ctx.stroke();
            }
        }
    }
    else if (defaults.Sprite === 'Butterfly')
    {
        for (let index = 0; index < 22; index += 1)
        {
            const a = (index / 22) * Math.PI * 2;
            const r = 14 + (index % 5) * 9;
            const px = cx + Math.cos(a) * r * 1.35;
            const py = cy + Math.sin(a * 1.5) * r * 0.72;
            ctx.fillStyle = hexToRgba(palette[index % palette.length], 0.88);
            ctx.beginPath();
            ctx.ellipse(px - 3.2, py, 3.8, 2.3, -0.45, 0, Math.PI * 2);
            ctx.ellipse(px + 3.2, py, 3.8, 2.3, 0.45, 0, Math.PI * 2);
            ctx.fill();
        }
    }
    else if (isCloner && shape === 'Grid')
    {
        for (let gx = -4; gx <= 4; gx += 1)
        {
            for (let gy = -2; gy <= 2; gy += 1)
            {
                const wave = Math.sin(gx * 0.7 + gy * 0.9) * 5;
                const px = cx + gx * 18 + gy * 4;
                const py = cy + gy * 15 + wave;
                const col = palette[(Math.abs(gx + gy)) % palette.length];
                ctx.fillStyle = hexToRgba(col, 0.88);
                ctx.beginPath();
                ctx.arc(px, py, 2.5, 0, Math.PI * 2);
                ctx.fill();
            }
        }
    }
    else if (shape === 'Ring' || /ring|galaxy|pulse/i.test(template.Id))
    {
        const count = 56;
        for (let index = 0; index < count; index += 1)
        {
            const a = (index / count) * Math.PI * 2;
            const ringR = 34 + Math.sin(a * 3 + seedBase) * 5;
            const px = cx + Math.cos(a) * ringR * 1.35;
            const py = cy + Math.sin(a) * ringR * 0.68;
            const col = palette[index % palette.length];
            ctx.fillStyle = hexToRgba(col, 0.86);
            ctx.beginPath();
            ctx.arc(px, py, 2.2, 0, Math.PI * 2);
            ctx.fill();
        }
    }
    else if (hasTrails || /strand|ribbon|filament|stream|meteor|rain/i.test(template.Id))
    {
        const strands = 18;
        for (let index = 0; index < strands; index += 1)
        {
            const col = palette[index % palette.length];
            ctx.strokeStyle = hexToRgba(col, 0.72);
            ctx.lineWidth = 1.6;
            ctx.beginPath();
            let px = width * (0.16 + seededFraction(seedBase + index * 3.7) * 0.68);
            let py = height * (0.2 + seededFraction(seedBase + index * 5.9) * 0.6);
            ctx.moveTo(px, py);
            for (let step = 0; step < 10; step += 1)
            {
                const angle = Math.sin(px * 0.03 + index * 0.5) * 1.4;
                px += Math.cos(angle) * 7;
                py += Math.sin(angle) * 5;
                ctx.lineTo(px, py);
            }
            ctx.stroke();
            ctx.fillStyle = hexToRgba(col, 0.95);
            ctx.beginPath();
            ctx.arc(px, py, 2.1, 0, Math.PI * 2);
            ctx.fill();
        }
    }
    else
    {
        const count = 68;
        for (let index = 0; index < count; index += 1)
        {
            const a = seededFraction(seedBase + index * 4.3) * Math.PI * 2;
            const r = Math.pow(seededFraction(seedBase + index * 9.1), 0.62) * Math.min(width, height) * 0.36;
            const px = cx + Math.cos(a) * r * 1.25;
            const py = cy + Math.sin(a) * r * 0.88;
            const col = palette[index % palette.length];
            const size = 1.4 + seededFraction(seedBase + index * 2.1) * 2.8;
            const glow = ctx.createRadialGradient(px, py, 0.2, px, py, size * 2.4);
            glow.addColorStop(0, hexToRgba(col, 0.95));
            glow.addColorStop(1, 'rgba(0,0,0,0)');
            ctx.fillStyle = glow;
            ctx.beginPath();
            ctx.arc(px, py, size * 2.4, 0, Math.PI * 2);
            ctx.fill();
        }
    }
    ctx.restore();
    return canvas;
}

export function renderSourceThumbnail(shape = 'Sphere', width = 260, height = 148)
{
    const canvas = createCanvas(width, height);
    if (typeof canvas.getContext !== 'function') return canvas;
    const ctx = canvas.getContext('2d');
    if (!ctx) return canvas;

    paintBackdrop(ctx, width, height, '#f97316');
    const cx = width * 0.5;
    const cy = height * 0.64;

    // Rising plume above source
    for (let index = 0; index < 14; index += 1)
    {
        const u = index / 13;
        const py = cy - u * height * 0.44;
        const px = cx + Math.sin(u * 5.2) * 8;
        const r = 10 + u * 18;
        const g = ctx.createRadialGradient(px, py, 1, px, py, r);
        g.addColorStop(0, u < 0.45 ? 'rgba(255, 200, 110, 0.62)' : 'rgba(180, 186, 198, 0.32)');
        g.addColorStop(0.55, u < 0.45 ? 'rgba(249, 115, 22, 0.35)' : 'rgba(120, 126, 138, 0.16)');
        g.addColorStop(1, 'rgba(0,0,0,0)');
        ctx.fillStyle = g;
        ctx.beginPath();
        ctx.arc(px, py, r, 0, Math.PI * 2);
        ctx.fill();
    }

    // Emitter gizmo geometry
    ctx.strokeStyle = '#ff9f43';
    ctx.lineWidth = 1.6;
    if (shape === 'Sphere')
    {
        ctx.beginPath();
        ctx.arc(cx, cy, 18, 0, Math.PI * 2);
        ctx.stroke();
        ctx.beginPath();
        ctx.ellipse(cx, cy, 18, 6.5, 0, 0, Math.PI * 2);
        ctx.stroke();
    }
    else if (shape === 'Disc')
    {
        ctx.beginPath();
        ctx.ellipse(cx, cy + 6, 26, 8.5, 0, 0, Math.PI * 2);
        ctx.stroke();
        ctx.fillStyle = '#ff9f43';
        ctx.beginPath();
        ctx.moveTo(cx, cy + 6);
        ctx.lineTo(cx, cy - 18);
        ctx.stroke();
        drawArrowhead(ctx, cx, cy - 18, 0, -1, 4.5);
    }
    else
    {
        ctx.strokeRect(cx - 18, cy - 14, 36, 26);
        ctx.strokeStyle = 'rgba(255, 159, 67, 0.45)';
        ctx.strokeRect(cx - 12, cy - 20, 36, 26);
    }
    return canvas;
}

export function renderColliderThumbnail(width = 260, height = 148)
{
    const canvas = createCanvas(width, height);
    if (typeof canvas.getContext !== 'function') return canvas;
    const ctx = canvas.getContext('2d');
    if (!ctx) return canvas;

    paintBackdrop(ctx, width, height, '#94a3b8');
    const cx = width * 0.5;
    const cy = height * 0.52;
    const radius = 26;

    // Laminar flow lines deflecting around no-slip sphere
    for (let row = -3; row <= 3; row += 1)
    {
        if (row === 0) continue;
        ctx.beginPath();
        for (let step = 0; step <= 32; step += 1)
        {
            const x = width * 0.12 + (step / 32) * width * 0.76;
            const dx = x - cx;
            const baseY = cy + row * 12;
            const dy = baseY - cy;
            const dist2 = dx * dx + dy * dy + 60;
            const push = (radius * radius * 1.18) / dist2;
            const y = baseY + dy * push * 0.65;
            if (step === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.strokeStyle = 'rgba(148, 163, 184, 0.48)';
        ctx.lineWidth = 1.3;
        ctx.stroke();
    }

    // Studio-lit shader sphere
    const grad = ctx.createRadialGradient(cx - 9, cy - 10, 3, cx, cy, radius);
    grad.addColorStop(0, '#f1f5f9');
    grad.addColorStop(0.45, '#94a3b8');
    grad.addColorStop(1, '#1e293b');
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.arc(cx, cy, radius, 0, Math.PI * 2);
    ctx.fill();
    return canvas;
}

export function renderShadingOrb({ smokeHex = '#8e8f94', flameCentre = '#fff3cf', fireFlame = '#ff8a16', fireEmber = '#ff2a05', fireIntensity = 4.0, mode = 'combined' }, size = 96)
{
    const canvas = createCanvas(size, size, 'orb-canvas');
    if (typeof canvas.getContext !== 'function') return canvas;
    const ctx = canvas.getContext('2d');
    if (!ctx) return canvas;

    const c = size * 0.5;
    const r = size * 0.43;

    ctx.save();
    ctx.beginPath();
    ctx.arc(c, c, r, 0, Math.PI * 2);
    ctx.clip();

    const bg = ctx.createRadialGradient(c * 0.72, c * 0.68, 2, c, c, r);
    if (mode === 'smoke')
    {
        bg.addColorStop(0, '#ffffff');
        bg.addColorStop(0.28, smokeHex);
        bg.addColorStop(0.85, hexToRgba(smokeHex, 0.45));
        bg.addColorStop(1, '#0d0e11');
        ctx.fillStyle = bg;
        ctx.fillRect(0, 0, size, size);
    }
    else if (mode === 'fire')
    {
        bg.addColorStop(0, flameCentre);
        bg.addColorStop(0.42, fireFlame);
        bg.addColorStop(0.78, fireEmber);
        bg.addColorStop(1, '#120807');
        ctx.fillStyle = bg;
        ctx.fillRect(0, 0, size, size);
    }
    else
    {
        ctx.fillStyle = '#101216';
        ctx.fillRect(0, 0, size, size);
        for (let index = 0; index < 14; index += 1)
        {
            const u = index / 13;
            const px = c + Math.sin(u * 6.0) * (r * 0.22);
            const py = c + r * 0.55 - u * r * 1.05;
            const pr = r * (0.28 + u * 0.34);
            const sg = ctx.createRadialGradient(px - pr * 0.2, py - pr * 0.2, 1, px, py, pr);
            sg.addColorStop(0, hexToRgba(smokeHex, 0.55));
            sg.addColorStop(1, 'rgba(0,0,0,0)');
            ctx.fillStyle = sg;
            ctx.beginPath();
            ctx.arc(px, py, pr, 0, Math.PI * 2);
            ctx.fill();

            if (fireIntensity > 0.05 && u < 0.68)
            {
                const heat = (1 - u / 0.68) * Math.min(1, fireIntensity / 5);
                const fg = ctx.createRadialGradient(px, py, 1, px, py, pr * 0.78);
                fg.addColorStop(0, hexToRgba(flameCentre, 0.9 * heat));
                fg.addColorStop(0.45, hexToRgba(fireFlame, 0.68 * heat));
                fg.addColorStop(0.85, hexToRgba(fireEmber, 0.3 * heat));
                fg.addColorStop(1, 'rgba(0,0,0,0)');
                ctx.globalCompositeOperation = 'lighter';
                ctx.fillStyle = fg;
                ctx.beginPath();
                ctx.arc(px, py, pr * 0.78, 0, Math.PI * 2);
                ctx.fill();
                ctx.globalCompositeOperation = 'source-over';
            }
        }
    }

    // Subtle rim specular highlight matching Project Zero shader balls
    const rim = ctx.createRadialGradient(c * 0.65, c * 0.55, r * 0.1, c, c, r);
    rim.addColorStop(0, 'rgba(255,255,255,0.16)');
    rim.addColorStop(0.86, 'rgba(255,255,255,0)');
    rim.addColorStop(1, 'rgba(255,255,255,0.14)');
    ctx.fillStyle = rim;
    ctx.fillRect(0, 0, size, size);
    ctx.restore();

    ctx.strokeStyle = 'rgba(255, 255, 255, 0.12)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(c, c, r, 0, Math.PI * 2);
    ctx.stroke();
    return canvas;
}
