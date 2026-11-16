/*============================================================================================================================================
                                                          PATTERNCANVAS2D.JS
============================================================================================================================================*/
// Interactive 2D Millimetre Pattern Drafting & M:N Sewing Canvas (Project-Zero Dark Charcoal Atelier Theme).

import { computeSeamAllowancePolygonMm } from './GarmentPatternExport.js';

const SEAM_PALETTE = [
    '#38bdf8', '#f472b6', '#34d399', '#fbbf24',
    '#a78bfa', '#fb7185', '#2dd4bf', '#60a5fa',
];

const BATCH_PALETTE = [
    '#3b82f6', '#34c759', '#f59e0b', '#ec4899',
    '#8b5cf6', '#06b6d4', '#ef4444', '#84cc16',
];

export function evaluateHeatmapColorRGB(mode, solver, vertexIndex, outRGB)
{
    if (mode === 'strain')
    {
        const crimp = Math.max(solver.textile.warpCrimpPct || 2.0, solver.textile.weftCrimpPct || 2.5) * 0.01;
        const yarnS = Math.max(solver.pointWarpStretch[vertexIndex], solver.pointWeftStretch[vertexIndex]) - 1.0;
        if (yarnS < 0.0)
        {
            const t = Math.min(1.0, -yarnS / 0.06);
            outRGB[0] = 0.16 * (1 - t) + 0.14 * t;
            outRGB[1] = 0.52 * (1 - t) + 0.36 * t;
            outRGB[2] = 0.78 * (1 - t) + 0.92 * t;
        }
        else if (yarnS <= crimp)
        {
            const t = yarnS / Math.max(0.005, crimp);
            outRGB[0] = 0.15 + 0.12 * t;
            outRGB[1] = 0.68 + 0.14 * t;
            outRGB[2] = 0.52 - 0.18 * t;
        }
        else
        {
            const t = Math.min(1.0, (yarnS - crimp) / 0.045);
            outRGB[0] = 0.92 * t + 0.35 * (1 - t);
            outRGB[1] = 0.72 * (1 - t) + 0.18 * t;
            outRGB[2] = 0.16 * (1 - t) + 0.16 * t;
        }
        return;
    }
    if (mode === 'shear')
    {
        const deg = solver.pointShearDeg ? solver.pointShearDeg[vertexIndex] : 0.0;
        const t = Math.min(1.0, deg / 18.0);
        outRGB[0] = 0.16 + 0.80 * Math.pow(t, 0.85);
        outRGB[1] = 0.24 + 0.52 * Math.sin(t * Math.PI);
        outRGB[2] = 0.76 * (1.0 - 0.75 * t) + 0.15;
        return;
    }
    if (mode === 'pressure')
    {
        const kpa = solver.contactPressureKpa[vertexIndex];
        const t = Math.min(1.0, kpa / 5.0);
        outRGB[0] = 0.15 + 0.82 * t;
        outRGB[1] = 0.20 + 0.58 * Math.sin(t * Math.PI * 0.85);
        outRGB[2] = 0.32 + 0.55 * (1 - t);
        return;
    }
    if (mode === 'batches')
    {
        const colors = solver.network.pointBatches.colors;
        const cIdx = colors ? (colors[vertexIndex] % BATCH_PALETTE.length) : (vertexIndex % BATCH_PALETTE.length);
        const hex = BATCH_PALETTE[cIdx];
        outRGB[0] = parseInt(hex.slice(1, 3), 16) / 255;
        outRGB[1] = parseInt(hex.slice(3, 5), 16) / 255;
        outRGB[2] = parseInt(hex.slice(5, 7), 16) / 255;
        return;
    }
    outRGB[0] = 0.75; outRGB[1] = 0.32; outRGB[2] = 0.25;
}

export class PatternCanvas2D
{
    constructor(canvas, options = {})
    {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
        this.onSelectPiece = options.onSelectPiece || null;

        this.network = null;
        this.solver = null;
        this.selectedPieceIndex = 0;
        this.heatmapMode = 'textile';
        this.fabricHex = '#c4513d';
        this.showSeamLines = true;
        this.showMeshWire = true;
        this.showSeamAllowance = true;

        this.pixelsPerMeter = 360.0;
        this.panX = 0.0;
        this.panY = 0.0;
        this._isPanning = false;
        this._lastMouse = [0, 0];
        this._rgbScratch = [0, 0, 0];
        this.dirty = true;

        this._bindEvents();
    }

    markDirty()
    {
        this.dirty = true;
    }

    needsResize()
    {
        const dpr = Math.min(2, (typeof window !== 'undefined' ? window.devicePixelRatio : 1) || 1);
        const w = this.canvas.clientWidth || 500;
        const h = this.canvas.clientHeight || 500;
        return this.canvas.width !== Math.round(w * dpr) || this.canvas.height !== Math.round(h * dpr);
    }

    bindSimulation(network, solver)
    {
        const firstBind = !this.network || this.network.key !== network.key;
        this.network = network;
        this.solver = solver;
        if (this.selectedPieceIndex >= network.pieces.length)
            this.selectedPieceIndex = 0;
        this._precomputePieceContours();
        if (firstBind)
            this.fitToView();
        this.dirty = true;
    }

    _precomputePieceContours()
    {
        if (!this.network) return;
        const net = this.network;
        const uv = net.patternUV;
        const faces = net.faceIndices;
        const pIds = net.pieceIds;
        const nPieces = net.pieces.length;

        const pieceFaces = Array.from({ length: nPieces }, () => []);
        const pieceEdgeSets = Array.from({ length: nPieces }, () => new Set());
        const pieceEdges = Array.from({ length: nPieces }, () => []);
        for (let f = 0; f < net.faceCount; ++f)
        {
            const ia = faces[f * 3], ib = faces[f * 3 + 1], ic = faces[f * 3 + 2];
            const p = pIds[ia];
            if (p < 0 || p >= nPieces) continue;
            pieceFaces[p].push(ia, ib, ic);
            const es = pieceEdgeSets[p];
            const pe = pieceEdges[p];
            for (let k = 0; k < 3; ++k)
            {
                const u = k === 0 ? ia : (k === 1 ? ib : ic);
                const v = k === 0 ? ib : (k === 1 ? ic : ia);
                const a = u < v ? u : v, b = u < v ? v : u;
                const key = a * 65536 + b;
                if (!es.has(key))
                {
                    es.add(key);
                    pe.push(a, b);
                }
            }
        }

        this._pieceRenderCache = net.pieces.map((piece, pIdx) =>
        {
            const sewMm = piece.contourIndices.map((vid) => [uv[vid * 2] * 1000.0, uv[vid * 2 + 1] * 1000.0]);
            const cutMm = computeSeamAllowancePolygonMm(sewMm, piece.seamAllowanceMm || 12);
            let cx = 0, cy = 0, minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
            for (const [x, y] of sewMm)
            {
                cx += x; cy += y;
                if (x < minX) minX = x; if (x > maxX) maxX = x;
                if (y < minY) minY = y; if (y > maxY) maxY = y;
            }
            cx /= Math.max(1, sewMm.length);
            cy /= Math.max(1, sewMm.length);
            return {
                sewMm,
                cutMm,
                centerM: [cx * 0.001, cy * 0.001],
                widthMm: maxX - minX,
                heightMm: maxY - minY,
                faceVerts: new Int32Array(pieceFaces[pIdx]),
                wireEdges: new Int32Array(pieceEdges[pIdx]),
            };
        });
    }

    fitToView()
    {
        if (!this.network || this.network.pointCount === 0) return;
        const rect = this.canvas.getBoundingClientRect();
        const w = Math.max(240, rect.width || this.canvas.width || 520);
        const h = Math.max(240, rect.height || this.canvas.height || 520);

        const uv = this.network.patternUV;
        let minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (let i = 0; i < this.network.pointCount; ++i)
        {
            const x = uv[i * 2], y = uv[i * 2 + 1];
            if (x < minX) minX = x; if (x > maxX) maxX = x;
            if (y < minY) minY = y; if (y > maxY) maxY = y;
        }
        const spanX = Math.max(0.35, (maxX - minX) + 0.14);
        const spanY = Math.max(0.35, (maxY - minY) + 0.16);
        this.pixelsPerMeter = Math.min((w - 56) / spanX, (h - 64) / spanY);
        this.panX = -0.5 * (minX + maxX) * this.pixelsPerMeter;
        this.panY = 0.5 * (minY + maxY) * this.pixelsPerMeter + 8;
        this.dirty = true;
    }

    _worldToScreen(mx, my, w, h)
    {
        return [
            0.5 * w + this.panX + mx * this.pixelsPerMeter,
            0.5 * h + this.panY - my * this.pixelsPerMeter,
        ];
    }

    _screenToWorld(sx, sy, w, h)
    {
        return [
            (sx - 0.5 * w - this.panX) / this.pixelsPerMeter,
            -(sy - 0.5 * h - this.panY) / this.pixelsPerMeter,
        ];
    }

    _bindEvents()
    {
        this.canvas.addEventListener('wheel', (e) =>
        {
            e.preventDefault();
            const rect = this.canvas.getBoundingClientRect();
            const sx = e.clientX - rect.left, sy = e.clientY - rect.top;
            const [wx, wy] = this._screenToWorld(sx, sy, rect.width, rect.height);
            const factor = e.deltaY < 0 ? 1.12 : (1.0 / 1.12);
            this.pixelsPerMeter = Math.max(110.0, Math.min(2200.0, this.pixelsPerMeter * factor));
            this.panX = sx - 0.5 * rect.width - wx * this.pixelsPerMeter;
            this.panY = sy - 0.5 * rect.height + wy * this.pixelsPerMeter;
            this.dirty = true;
        }, { passive: false });

        this.canvas.addEventListener('pointerdown', (e) =>
        {
            const rect = this.canvas.getBoundingClientRect();
            const sx = e.clientX - rect.left, sy = e.clientY - rect.top;
            this._lastMouse = [e.clientX, e.clientY];

            if (e.button === 1 || e.button === 2 || e.shiftKey)
            {
                this._isPanning = true;
                this.canvas.setPointerCapture(e.pointerId);
                return;
            }

            if (e.button === 0 && this.network && this._pieceRenderCache)
            {
                const [wx, wy] = this._screenToWorld(sx, sy, rect.width, rect.height);
                const wxMm = wx * 1000.0, wyMm = wy * 1000.0;
                let hitIdx = -1;
                for (let p = 0; p < this._pieceRenderCache.length; ++p)
                {
                    if (this._pointInPoly(wxMm, wyMm, this._pieceRenderCache[p].cutMm))
                    {
                        hitIdx = p;
                        break;
                    }
                }
                if (hitIdx >= 0)
                {
                    this.selectedPieceIndex = hitIdx;
                    this.dirty = true;
                    if (this.onSelectPiece) this.onSelectPiece(hitIdx, this.network.pieces[hitIdx]);
                }
                else
                {
                    this._isPanning = true;
                    this.canvas.setPointerCapture(e.pointerId);
                }
            }
        });

        this.canvas.addEventListener('pointermove', (e) =>
        {
            if (!this._isPanning) return;
            this.panX += e.clientX - this._lastMouse[0];
            this.panY += e.clientY - this._lastMouse[1];
            this._lastMouse = [e.clientX, e.clientY];
            this.dirty = true;
        });

        this.canvas.addEventListener('pointerup', (e) =>
        {
            if (this._isPanning)
            {
                this._isPanning = false;
                try { this.canvas.releasePointerCapture(e.pointerId); } catch {}
            }
        });
        this.canvas.addEventListener('contextmenu', (e) => e.preventDefault());
    }

    _pointInPoly(px, py, poly)
    {
        let inside = false;
        for (let i = 0, j = poly.length - 1; i < poly.length; j = i++)
        {
            const xi = poly[i][0], yi = poly[i][1];
            const xj = poly[j][0], yj = poly[j][1];
            if (((yi > py) !== (yj > py)) && (px < ((xj - xi) * (py - yi)) / (yj - yi + 1e-12) + xi))
                inside = !inside;
        }
        return inside;
    }

    render()
    {
        const canvas = this.canvas;
        const dpr = Math.min(2, (typeof window !== 'undefined' ? window.devicePixelRatio : 1) || 1);
        const w = canvas.clientWidth || 500;
        const h = canvas.clientHeight || 500;
        if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr))
        {
            canvas.width = Math.round(w * dpr);
            canvas.height = Math.round(h * dpr);
        }

        const ctx = this.ctx;
        ctx.save();
        ctx.scale(dpr, dpr);

        ctx.fillStyle = '#121214';
        ctx.fillRect(0, 0, w, h);

        // 100mm major + 25mm minor Project-Zero CAD grid
        const stepMinor = 0.025;
        const stepMajor = 0.100;
        const [minX, maxY] = this._screenToWorld(0, 0, w, h);
        const [maxX, minY] = this._screenToWorld(w, h, w, h);

        ctx.lineWidth = 1;
        ctx.strokeStyle = 'rgba(255,255,255,0.03)';
        ctx.beginPath();
        for (let gx = Math.floor(minX / stepMinor) * stepMinor; gx <= maxX; gx += stepMinor)
        {
            const [sx] = this._worldToScreen(gx, 0, w, h);
            ctx.moveTo(sx, 0); ctx.lineTo(sx, h);
        }
        for (let gy = Math.floor(minY / stepMinor) * stepMinor; gy <= maxY; gy += stepMinor)
        {
            const [, sy] = this._worldToScreen(0, gy, w, h);
            ctx.moveTo(0, sy); ctx.lineTo(w, sy);
        }
        ctx.stroke();

        ctx.strokeStyle = 'rgba(255,255,255,0.075)';
        ctx.beginPath();
        for (let gx = Math.floor(minX / stepMajor) * stepMajor; gx <= maxX; gx += stepMajor)
        {
            const [sx] = this._worldToScreen(gx, 0, w, h);
            ctx.moveTo(sx, 0); ctx.lineTo(sx, h);
        }
        for (let gy = Math.floor(minY / stepMajor) * stepMajor; gy <= maxY; gy += stepMajor)
        {
            const [, sy] = this._worldToScreen(0, gy, w, h);
            ctx.moveTo(0, sy); ctx.lineTo(w, sy);
        }
        ctx.stroke();

        if (!this.network || !this._pieceRenderCache)
        {
            ctx.restore();
            this.dirty = false;
            return;
        }

        const net = this.network;
        const uv = net.patternUV;
        const ox = 0.5 * w + this.panX;
        const oy = 0.5 * h + this.panY;
        const scale = this.pixelsPerMeter;

        // Draw piece bodies & synchronized live heatmaps
        for (let p = 0; p < net.pieces.length; ++p)
        {
            const piece = net.pieces[p];
            const cache = this._pieceRenderCache[p];
            const isSel = p === this.selectedPieceIndex;

            // Seam allowance cut line
            if (this.showSeamAllowance)
            {
                ctx.beginPath();
                for (let idx = 0; idx < cache.cutMm.length; ++idx)
                {
                    const xMm = cache.cutMm[idx][0], yMm = cache.cutMm[idx][1];
                    const sx = ox + (xMm * 0.001) * scale;
                    const sy = oy - (yMm * 0.001) * scale;
                    if (idx === 0) ctx.moveTo(sx, sy);
                    else ctx.lineTo(sx, sy);
                }
                ctx.closePath();
                ctx.fillStyle = isSel ? 'rgba(255, 180, 84, 0.08)' : 'rgba(255, 255, 255, 0.025)';
                ctx.fill();
                ctx.lineWidth = isSel ? 1.5 : 1.0;
                ctx.setLineDash([4, 3]);
                ctx.strokeStyle = isSel ? '#ffb454' : 'rgba(255, 255, 255, 0.25)';
                ctx.stroke();
                ctx.setLineDash([]);
            }

            // Fill interior: single panel polygon in static textile mode; triangle heatmap when inspecting live fields
            if (this.heatmapMode === 'textile' || !this.solver)
            {
                ctx.beginPath();
                for (let idx = 0; idx < cache.sewMm.length; ++idx)
                {
                    const xMm = cache.sewMm[idx][0], yMm = cache.sewMm[idx][1];
                    const sx = ox + (xMm * 0.001) * scale;
                    const sy = oy - (yMm * 0.001) * scale;
                    if (idx === 0) ctx.moveTo(sx, sy);
                    else ctx.lineTo(sx, sy);
                }
                ctx.closePath();
                ctx.fillStyle = isSel ? `${this.fabricHex}55` : `${this.fabricHex}30`;
                ctx.fill();
            }
            else
            {
                const fVerts = cache.faceVerts;
                for (let t = 0; t < fVerts.length; t += 3)
                {
                    const ia = fVerts[t], ib = fVerts[t + 1], ic = fVerts[t + 2];
                    const ax = ox + uv[ia * 2] * scale, ay = oy - uv[ia * 2 + 1] * scale;
                    const bx = ox + uv[ib * 2] * scale, by = oy - uv[ib * 2 + 1] * scale;
                    const cx = ox + uv[ic * 2] * scale, cy = oy - uv[ic * 2 + 1] * scale;
                    evaluateHeatmapColorRGB(this.heatmapMode, this.solver, ia, this._rgbScratch);
                    const r = (this._rgbScratch[0] * 255) | 0;
                    const g = (this._rgbScratch[1] * 255) | 0;
                    const b = (this._rgbScratch[2] * 255) | 0;
                    ctx.fillStyle = `rgba(${r},${g},${b},0.65)`;
                    ctx.beginPath();
                    ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.lineTo(cx, cy);
                    ctx.closePath();
                    ctx.fill();
                }
            }

            // Single-batch unique-edge mesh wireframe per panel (replaces 8,400+ individual Canvas2D draws)
            if (this.showMeshWire && cache.wireEdges && cache.wireEdges.length > 0)
            {
                const edges = cache.wireEdges;
                ctx.beginPath();
                for (let e = 0; e < edges.length; e += 2)
                {
                    const a = edges[e] * 2, b = edges[e + 1] * 2;
                    ctx.moveTo(ox + uv[a] * scale, oy - uv[a + 1] * scale);
                    ctx.lineTo(ox + uv[b] * scale, oy - uv[b + 1] * scale);
                }
                ctx.strokeStyle = isSel ? 'rgba(255,255,255,0.10)' : 'rgba(255,255,255,0.045)';
                ctx.lineWidth = 0.6;
                ctx.stroke();
            }

            // Sew contour outline
            ctx.beginPath();
            for (let idx = 0; idx < cache.sewMm.length; ++idx)
            {
                const xMm = cache.sewMm[idx][0], yMm = cache.sewMm[idx][1];
                const sx = ox + (xMm * 0.001) * scale;
                const sy = oy - (yMm * 0.001) * scale;
                if (idx === 0) ctx.moveTo(sx, sy);
                else ctx.lineTo(sx, sy);
            }
            ctx.closePath();
            ctx.lineWidth = isSel ? 2.0 : 1.2;
            ctx.strokeStyle = isSel ? '#ffb454' : '#a8b0be';
            ctx.stroke();

            // Grainline directional arrow
            const [gcx, gcy] = this._worldToScreen(cache.centerM[0], cache.centerM[1], w, h);
            const gLenPx = Math.min(48, Math.max(18, cache.heightMm * 0.00022 * this.pixelsPerMeter));
            const rad = ((piece.grainlineAngleDeg || 0) * Math.PI) / 180.0;
            const dx = Math.sin(rad) * gLenPx, dy = -Math.cos(rad) * gLenPx;
            ctx.strokeStyle = isSel ? '#f472b6' : 'rgba(244,114,182,0.55)';
            ctx.lineWidth = 1.2;
            ctx.beginPath();
            ctx.moveTo(gcx - dx, gcy - dy);
            ctx.lineTo(gcx + dx, gcy + dy);
            ctx.stroke();

            // Piece label + mm dimensions
            ctx.fillStyle = isSel ? '#f4f4f6' : '#a1a1aa';
            ctx.font = '500 10.5px "DM Sans", sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(piece.name, gcx, gcy - 6);
            ctx.fillStyle = '#71717a';
            ctx.font = '400 9px "DM Sans", sans-serif';
            ctx.fillText(
                `${Math.round(cache.widthMm)} × ${Math.round(cache.heightMm)} mm · Grain ${(piece.grainlineAngleDeg || 0).toFixed(0)}°`,
                gcx, gcy + 14
            );
        }

        // M:N Sewing stitch connections between pattern pieces
        if (this.showSeamLines && net.seams)
        {
            const sIds = net.seamIndices;
            for (let sIdx = 0; sIdx < net.seams.length; ++sIdx)
            {
                const seam = net.seams[sIdx];
                const col = SEAM_PALETTE[sIdx % SEAM_PALETTE.length];
                ctx.strokeStyle = `${col}88`;
                ctx.lineWidth = 1.0;
                const step = Math.max(1, Math.floor(seam.pairCount / 7));
                ctx.beginPath();
                for (let k = 0; k < seam.pairCount; k += step)
                {
                    const pairIdx = seam.pairStart + k;
                    const va = sIds[pairIdx * 2], vb = sIds[pairIdx * 2 + 1];
                    const [ax, ay] = this._worldToScreen(uv[va * 2], uv[va * 2 + 1], w, h);
                    const [bx, by] = this._worldToScreen(uv[vb * 2], uv[vb * 2 + 1], w, h);
                    ctx.moveTo(ax, ay); ctx.lineTo(bx, by);
                }
                ctx.stroke();
            }
        }

        // Bottom-left ruler bar (100 mm = 10 cm)
        const barPx = 0.100 * this.pixelsPerMeter;
        ctx.strokeStyle = '#9ca3af';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(16, h - 18); ctx.lineTo(16 + barPx, h - 18);
        ctx.moveTo(16, h - 22); ctx.lineTo(16, h - 14);
        ctx.moveTo(16 + barPx, h - 22); ctx.lineTo(16 + barPx, h - 14);
        ctx.stroke();
        ctx.fillStyle = '#9ca3af';
        ctx.font = '400 9.5px "DM Sans", sans-serif';
        ctx.textAlign = 'left';
        ctx.fillText('100 mm (10 cm)', 16, h - 26);

        ctx.restore();
        this.dirty = false;
    }
}
