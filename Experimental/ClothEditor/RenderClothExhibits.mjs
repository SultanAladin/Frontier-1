/*============================================================================================================================================
                                                        RENDERCLOTHEXHIBITS.MJS
============================================================================================================================================*/
// 🧩 High-resolution Z-buffered 3D PBR + 2D Millimetre Pattern PNG Exhibit Renderer for Visual Verification.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

import { MannequinAvatar } from './Engine/MannequinAvatar.js';
import { cloneTextileMaterial } from './Engine/TextileCatalog.js';
import { buildGarmentPresetSpec } from './Engine/GarmentPresets.js';
import { compileGarmentNetwork } from './Engine/PatternDraftNetwork.js';
import { OrthotropicClothSolver } from './Engine/OrthotropicClothSolver.js';
import { evaluateHeatmapColorRGB } from './Engine/PatternCanvas2D.js';
import { computeSeamAllowancePolygonMm } from './Engine/GarmentPatternExport.js';

const Dir = path.dirname(fileURLToPath(import.meta.url));
const OutDir = path.resolve(Dir, '../../Exhibits/Images/ClothEditor');
fs.mkdirSync(OutDir, { recursive: true });

/*--------------------------------------------------------------------------------------------------------------------------------------------
  MINIMAL CRC32 + ZLIB PNG ENCODER & Z-BUFFERED SOFTWARE RASTERIZER
--------------------------------------------------------------------------------------------------------------------------------------------*/
const CRC_TABLE = new Uint32Array(256);
for (let n = 0; n < 256; ++n)
{
    let c = n;
    for (let k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    CRC_TABLE[n] = c >>> 0;
}
function crc32(buf)
{
    let c = 0xffffffff;
    for (let i = 0; i < buf.length; ++i) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}
function pngChunk(typeStr, dataBuf)
{
    const typeBuf = Buffer.from(typeStr, 'ascii');
    const lenBuf = Buffer.alloc(4);
    lenBuf.writeUInt32BE(dataBuf.length, 0);
    const crcBuf = Buffer.alloc(4);
    crcBuf.writeUInt32BE(crc32(Buffer.concat([typeBuf, dataBuf])), 0);
    return Buffer.concat([lenBuf, typeBuf, dataBuf, crcBuf]);
}
function savePNG(filePath, width, height, rgba)
{
    const stride = width * 4 + 1;
    const raw = Buffer.alloc(stride * height);
    for (let y = 0; y < height; ++y)
    {
        raw[y * stride] = 0;
        Buffer.from(rgba.buffer, y * width * 4, width * 4).copy(raw, y * stride + 1);
    }
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(width, 0);
    ihdr.writeUInt32BE(height, 4);
    ihdr[8] = 8; ihdr[9] = 6;
    const png = Buffer.concat([
        Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
        pngChunk('IHDR', ihdr),
        pngChunk('IDAT', zlib.deflateSync(raw, { level: 6 })),
        pngChunk('IEND', Buffer.alloc(0)),
    ]);
    fs.writeFileSync(filePath, png);
}

class FrameBuffer
{
    constructor(w, h)
    {
        this.w = w;
        this.h = h;
        this.rgba = new Uint8Array(w * h * 4);
        this.zbuf = new Float32Array(w * h);
        this.clear(20, 22, 27);
    }
    clear(r, g, b)
    {
        for (let i = 0; i < this.w * this.h; ++i)
        {
            const o = i * 4;
            this.rgba[o] = r; this.rgba[o + 1] = g; this.rgba[o + 2] = b; this.rgba[o + 3] = 255;
            this.zbuf[i] = 1e30;
        }
    }
    fillRect(x0, y0, rw, rh, r, g, b)
    {
        const sx = Math.max(0, x0 | 0), sy = Math.max(0, y0 | 0);
        const ex = Math.min(this.w, (x0 + rw) | 0), ey = Math.min(this.h, (y0 + rh) | 0);
        for (let y = sy; y < ey; ++y)
        {
            for (let x = sx; x < ex; ++x)
            {
                const o = (y * this.w + x) * 4;
                this.rgba[o] = r; this.rgba[o + 1] = g; this.rgba[o + 2] = b; this.rgba[o + 3] = 255;
                this.zbuf[y * this.w + x] = 1e30;
            }
        }
    }
    drawLine2D(x0, y0, x1, y1, r, g, b, alpha = 1.0)
    {
        const steps = Math.max(1, Math.ceil(Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0))));
        for (let s = 0; s <= steps; ++s)
        {
            const t = s / steps;
            const x = Math.round(x0 + (x1 - x0) * t);
            const y = Math.round(y0 + (y1 - y0) * t);
            if (x < 0 || x >= this.w || y < 0 || y >= this.h) continue;
            const o = (y * this.w + x) * 4;
            this.rgba[o]     = Math.round(this.rgba[o] * (1 - alpha) + r * alpha);
            this.rgba[o + 1] = Math.round(this.rgba[o + 1] * (1 - alpha) + g * alpha);
            this.rgba[o + 2] = Math.round(this.rgba[o + 2] * (1 - alpha) + b * alpha);
        }
    }
}

function projectPoint(px, py, pz, cam, vp)
{
    const dx = px - cam.tx, dy = py - cam.ty, dz = pz - cam.tz;
    const cy = Math.cos(cam.yaw), sy = Math.sin(cam.yaw);
    const cp = Math.cos(cam.pitch), sp = Math.sin(cam.pitch);
    const rx = dx * cy - dz * sy;
    const rz1 = dx * sy + dz * cy;
    const ry = dy * cp - rz1 * sp;
    const rz = dy * sp + rz1 * cp + cam.dist;
    if (rz <= 0.15) return null;
    const f = cam.focal * vp.h * 0.5;
    return {
        sx: vp.x + vp.w * 0.5 + (rx / rz) * f,
        sy: vp.y + vp.h * 0.5 - (ry / rz) * f,
        z: rz,
    };
}

function renderMesh(fb, vp, cam, positions, normals, indices, colorFn, zBias = 0.0, sheen = 0.25)
{
    const nVerts = positions.length / 3;
    const proj = new Array(nVerts);
    for (let i = 0; i < nVerts; ++i)
        proj[i] = projectPoint(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2], cam, vp);

    const lx = 0.48, ly = 0.72, lz = 0.50;
    const fx = -0.58, fy = 0.35, fz = 0.73;
    const triCount = indices.length / 3;
    const rgb = [0.75, 0.32, 0.25];

    for (let t = 0; t < triCount; ++t)
    {
        const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        const p0 = proj[i0], p1 = proj[i1], p2 = proj[i2];
        if (!p0 || !p1 || !p2) continue;

        const minX = Math.max(vp.x, Math.floor(Math.min(p0.sx, p1.sx, p2.sx)));
        const maxX = Math.min(vp.x + vp.w - 1, Math.ceil(Math.max(p0.sx, p1.sx, p2.sx)));
        const minY = Math.max(vp.y, Math.floor(Math.min(p0.sy, p1.sy, p2.sy)));
        const maxY = Math.min(vp.y + vp.h - 1, Math.ceil(Math.max(p0.sy, p1.sy, p2.sy)));
        if (minX > maxX || minY > maxY) continue;

        const area = (p1.sx - p0.sx) * (p2.sy - p0.sy) - (p1.sy - p0.sy) * (p2.sx - p0.sx);
        if (Math.abs(area) < 0.25) continue;
        const invA = 1.0 / area;

        colorFn(i0, i1, i2, rgb);
        const cr = rgb[0], cg = rgb[1], cb = rgb[2];

        for (let y = minY; y <= maxY; ++y)
        {
            const py = y + 0.5;
            for (let x = minX; x <= maxX; ++x)
            {
                const px = x + 0.5;
                const w0 = ((p1.sx - px) * (p2.sy - py) - (p1.sy - py) * (p2.sx - px)) * invA;
                const w1 = ((p2.sx - px) * (p0.sy - py) - (p2.sy - py) * (p0.sx - px)) * invA;
                const w2 = 1.0 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;

                const z = w0 * p0.z + w1 * p1.z + w2 * p2.z + zBias;
                const idx = y * fb.w + x;
                if (z >= fb.zbuf[idx]) continue;
                fb.zbuf[idx] = z;

                let nx = w0 * normals[i0 * 3]     + w1 * normals[i1 * 3]     + w2 * normals[i2 * 3];
                let ny = w0 * normals[i0 * 3 + 1] + w1 * normals[i1 * 3 + 1] + w2 * normals[i2 * 3 + 1];
                let nz = w0 * normals[i0 * 3 + 2] + w1 * normals[i1 * 3 + 2] + w2 * normals[i2 * 3 + 2];
                const nl = Math.sqrt(nx * nx + ny * ny + nz * nz) || 1.0;
                nx /= nl; ny /= nl; nz /= nl;
                if (area < 0) { nx = -nx; ny = -ny; nz = -nz; }

                const key = Math.max(0.0, nx * lx + ny * ly + nz * lz);
                const fill = Math.max(0.0, nx * fx + ny * fy + nz * fz);
                const rim = Math.pow(1.0 - Math.min(1.0, Math.abs(nz)), 2.2) * sheen;
                const shade = 0.24 + 0.68 * key + 0.24 * fill + rim;

                const o = idx * 4;
                fb.rgba[o]     = Math.min(255, Math.round(cr * shade * 255));
                fb.rgba[o + 1] = Math.min(255, Math.round(cg * shade * 255));
                fb.rgba[o + 2] = Math.min(255, Math.round(cb * shade * 255));
                fb.rgba[o + 3] = 255;
            }
        }
    }
}

function hexToRgb01(hex)
{
    const n = parseInt(hex.replace('#', ''), 16);
    return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

function renderDrapedViewport(fb, vp, avatar, solver, hex, mode = 'textile', yaw = -0.34)
{
    fb.fillRect(vp.x, vp.y, vp.w, vp.h, 24, 27, 34);
    const cam = { tx: 0.0, ty: 0.96, tz: 0.03, yaw, pitch: 0.08, dist: 1.92, focal: 2.35 };

    // 1. Tailor mannequin
    renderMesh(fb, vp, cam, avatar.positions, avatar.normals, avatar.faceIndices, (i0, i1, i2, out) =>
    {
        out[0] = 0.74; out[1] = 0.71; out[2] = 0.67;
    }, 0.0, 0.06);

    // 2. Draped cloth
    solver.recomputeNormalsAndStrain();
    const baseRgb = hexToRgb01(hex);
    const tmpA = [0, 0, 0], tmpB = [0, 0, 0], tmpC = [0, 0, 0];
    renderMesh(fb, vp, cam, solver.positions, solver.normals, solver.network.faceIndices, (i0, i1, i2, out) =>
    {
        if (mode === 'textile')
        {
            out[0] = baseRgb[0]; out[1] = baseRgb[1]; out[2] = baseRgb[2];
        }
        else
        {
            evaluateHeatmapColorRGB(mode, solver, i0, tmpA);
            evaluateHeatmapColorRGB(mode, solver, i1, tmpB);
            evaluateHeatmapColorRGB(mode, solver, i2, tmpC);
            out[0] = (tmpA[0] + tmpB[0] + tmpC[0]) / 3;
            out[1] = (tmpA[1] + tmpB[1] + tmpC[1]) / 3;
            out[2] = (tmpA[2] + tmpB[2] + tmpC[2]) / 3;
        }
    }, -0.003, solver.textile.sheen * 0.45);

    // 3. 3D Stitched Seam Overlay
    const sp = solver.network.seamIndices;
    const pos = solver.positions;
    for (let s = 0; s < solver.network.seamCount; ++s)
    {
        const a = sp[s * 2], b = sp[s * 2 + 1];
        const pa = projectPoint(pos[a * 3], pos[a * 3 + 1], pos[a * 3 + 2], cam, vp);
        const pb = projectPoint(pos[b * 3], pos[b * 3 + 1], pos[b * 3 + 2], cam, vp);
        if (pa && pb)
            fb.drawLine2D(pa.sx, pa.sy, pb.sx, pb.sy, 250, 224, 150, 0.55);
    }

    // Panel border
    fb.drawLine2D(vp.x, vp.y, vp.x + vp.w - 1, vp.y, 58, 65, 80, 1.0);
    fb.drawLine2D(vp.x, vp.y + vp.h - 1, vp.x + vp.w - 1, vp.y + vp.h - 1, 58, 65, 80, 1.0);
    fb.drawLine2D(vp.x, vp.y, vp.x, vp.y + vp.h - 1, 58, 65, 80, 1.0);
    fb.drawLine2D(vp.x + vp.w - 1, vp.y, vp.x + vp.w - 1, vp.y + vp.h - 1, 58, 65, 80, 1.0);
}

function renderPatternSheet2D(fb, vp, network, solver, mode = 'strain')
{
    fb.fillRect(vp.x, vp.y, vp.w, vp.h, 19, 21, 26);

    const uv = network.patternUV;
    let minU = Infinity, minV = Infinity, maxU = -Infinity, maxV = -Infinity;
    for (let i = 0; i < network.pointCount; ++i)
    {
        const u = uv[i * 2] * 1000.0;
        const v = uv[i * 2 + 1] * 1000.0;
        if (u < minU) minU = u; if (u > maxU) maxU = u;
        if (v < minV) minV = v; if (v > maxV) maxV = v;
    }
    const pad = 45.0;
    minU -= pad; minV -= pad; maxU += pad; maxV += pad;
    const scale = Math.min((vp.w - 36) / (maxU - minU), (vp.h - 36) / (maxV - minV));
    const sx = (u) => vp.x + 18 + (u - minU) * scale;
    const sy = (v) => vp.y + vp.h - 18 - (v - minV) * scale;

    // 100mm grid
    for (let gx = Math.floor(minU / 100) * 100; gx <= maxU; gx += 100)
        fb.drawLine2D(sx(gx), vp.y, sx(gx), vp.y + vp.h, 36, 41, 52, 0.7);
    for (let gy = Math.floor(minV / 100) * 100; gy <= maxV; gy += 100)
        fb.drawLine2D(vp.x, sy(gy), vp.x + vp.w, sy(gy), 36, 41, 52, 0.7);

    // Fill triangles with heatmap
    const rgb = [0, 0, 0];
    for (let f = 0; f < network.faceCount; ++f)
    {
        const i0 = network.faceIndices[f * 3], i1 = network.faceIndices[f * 3 + 1], i2 = network.faceIndices[f * 3 + 2];
        const x0 = sx(uv[i0 * 2] * 1000), y0 = sy(uv[i0 * 2 + 1] * 1000);
        const x1 = sx(uv[i1 * 2] * 1000), y1 = sy(uv[i1 * 2 + 1] * 1000);
        const x2 = sx(uv[i2 * 2] * 1000), y2 = sy(uv[i2 * 2 + 1] * 1000);
        evaluateHeatmapColorRGB(mode, solver, i0, rgb);
        const cr = Math.round(rgb[0] * 255), cg = Math.round(rgb[1] * 255), cb = Math.round(rgb[2] * 255);

        const bx0 = Math.max(vp.x, Math.floor(Math.min(x0, x1, x2)));
        const bx1 = Math.min(vp.x + vp.w - 1, Math.ceil(Math.max(x0, x1, x2)));
        const by0 = Math.max(vp.y, Math.floor(Math.min(y0, y1, y2)));
        const by1 = Math.min(vp.y + vp.h - 1, Math.ceil(Math.max(y0, y1, y2)));
        const area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (Math.abs(area) < 0.2) continue;
        const invA = 1.0 / area;
        for (let py = by0; py <= by1; ++py)
        {
            for (let px = bx0; px <= bx1; ++px)
            {
                const w0 = ((x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)) * invA;
                const w1 = ((x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)) * invA;
                const w2 = 1.0 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const o = (py * fb.w + px) * 4;
                fb.rgba[o] = cr; fb.rgba[o + 1] = cg; fb.rgba[o + 2] = cb;
            }
        }
        fb.drawLine2D(x0, y0, x1, y1, 255, 255, 255, 0.14);
        fb.drawLine2D(x1, y1, x2, y2, 255, 255, 255, 0.14);
    }

    for (const piece of network.pieces)
    {
        const boundaryMm = piece.contourIndices.map((vi) => [uv[vi * 2] * 1000.0, uv[vi * 2 + 1] * 1000.0]);
        const cutMm = computeSeamAllowancePolygonMm(boundaryMm, piece.seamAllowanceMm || 12.0);
        for (let k = 0; k < boundaryMm.length; ++k)
        {
            const a = boundaryMm[k], b = boundaryMm[(k + 1) % boundaryMm.length];
            fb.drawLine2D(sx(a[0]), sy(a[1]), sx(b[0]), sy(b[1]), 96, 165, 250, 0.95);
            const ca = cutMm[k], cb2 = cutMm[(k + 1) % cutMm.length];
            fb.drawLine2D(sx(ca[0]), sy(ca[1]), sx(cb2[0]), sy(cb2[1]), 148, 163, 184, 0.65);
        }
    }

    // M:N Sewing lines
    for (let s = 0; s < network.seamCount; s += 3)
    {
        const va = network.seamIndices[s * 2], vb = network.seamIndices[s * 2 + 1];
        const ax = sx(uv[va * 2] * 1000), ay = sy(uv[va * 2 + 1] * 1000);
        const bx = sx(uv[vb * 2] * 1000), by = sy(uv[vb * 2 + 1] * 1000);
        fb.drawLine2D(ax, ay, bx, by, 56, 189, 248, 0.35);
    }
}

/*--------------------------------------------------------------------------------------------------------------------------------------------
  SIMULATE & RENDER EXHIBITS
--------------------------------------------------------------------------------------------------------------------------------------------*/
const binBuf = fs.readFileSync(path.join(Dir, 'Assets/FrontierMannequin.bin'));
const avatar = MannequinAvatar.fromArrayBuffer(binBuf.buffer.slice(binBuf.byteOffset, binBuf.byteOffset + binBuf.byteLength));
avatar.ensureSignedDistanceVolume();

// Exhibit 1: 4-Garment Draped Gallery (1600 x 820)
{
    const fb = new FrameBuffer(1600, 820);
    const presets = [
        ['coutureDress',  '#c4513d', Math.PI - 0.30],
        ['gatheredSkirt', '#1f6f78', Math.PI - 0.28],
        ['tailoredTee',   '#3b7a62', Math.PI - 0.30],
        ['biasSilkTunic', '#a23b5c', Math.PI - 0.32],
    ];
    presets.forEach(([key, hex, yaw], idx) =>
    {
        const spec = buildGarmentPresetSpec(key, avatar);
        const net = compileGarmentNetwork(spec, avatar, { particleSpacingMm: 15.5 });
        const solver = new OrthotropicClothSolver(net, cloneTextileMaterial(spec.defaultTextile), avatar);
        for (let f = 0; f < 125; ++f) solver.stepFrame(1.0 / 60.0);
        renderDrapedViewport(fb, { x: 16 + idx * 394, y: 16, w: 382, h: 788 }, avatar, solver, hex, 'textile', yaw);
    });
    const p1 = path.join(OutDir, '01-FourGarmentDrapeGallery.png');
    savePNG(p1, fb.w, fb.h, fb.rgba);
    console.log('Saved', p1);
}

// Exhibit 2: Split 2D Millimetre Pattern + 3D Diagnostic Lenses (1600 x 820)
{
    const fb = new FrameBuffer(1600, 820);
    const specD = buildGarmentPresetSpec('coutureDress', avatar);
    const netD = compileGarmentNetwork(specD, avatar, { particleSpacingMm: 15.5 });
    const solverD = new OrthotropicClothSolver(netD, cloneTextileMaterial('cottonPoplin'), avatar);
    for (let f = 0; f < 125; ++f) solverD.stepFrame(1.0 / 60.0);

    const specB = buildGarmentPresetSpec('biasSilkTunic', avatar);
    const netB = compileGarmentNetwork(specB, avatar, { particleSpacingMm: 15.5 });
    const solverB = new OrthotropicClothSolver(netB, cloneTextileMaterial('mulberrySilk'), avatar);
    for (let f = 0; f < 125; ++f) solverB.stepFrame(1.0 / 60.0);

    renderPatternSheet2D(fb, { x: 16, y: 16, w: 600, h: 788 }, netD, solverD, 'strain');
    renderDrapedViewport(fb, { x: 632, y: 16, w: 312, h: 788 }, avatar, solverD, '#c4513d', 'strain', Math.PI - 0.28);
    renderDrapedViewport(fb, { x: 956, y: 16, w: 312, h: 788 }, avatar, solverB, '#a23b5c', 'shear', Math.PI - 0.30);
    renderDrapedViewport(fb, { x: 1280, y: 16, w: 304, h: 788 }, avatar, solverD, '#c4513d', 'batches', Math.PI + 0.32);

    const p2 = path.join(OutDir, '02-PatternAndHeatmapDiagnostics.png');
    savePNG(p2, fb.w, fb.h, fb.rgba);
    console.log('Saved', p2);
}
