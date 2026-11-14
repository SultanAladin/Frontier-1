/*============================================================================================================================================
                                                          PATTERNCANVAS2D.JS
============================================================================================================================================*/
// 🧩 Interactive 2D Millimetre Pattern Drafting & M:N Sewing Canvas (Project-Zero Dark Atelier Theme).
//    Renders 1:1 mm rulers, 10mm/100mm grid, Sew & Seam-Allowance Cut contours, Warp/Weft grainline arrows,
//    color-coded M:N sewing stitches, edge dimensions (mm), and synchronized 2D strain/shear/pressure heatmaps.

import { computeSeamAllowancePolygonMm } from './GarmentPatternExport.js?v=3';

const SEAM_PALETTE = [
    '#38bdf8', '#f472b6', '#34d399', '#fbbf24',
    '#a78bfa', '#fb7185', '#2dd4bf', '#60a5fa',
];

const BATCH_PALETTE = [
    '#3b82f6', '#10b981', '#f59e0b', '#ec4899',
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
        const colors = solver.solverMode === 'avbd'
            ? solver.network.pointBatches.colors
            : null;
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

        // View transform: pattern meters -> screen pixels (1 m = 1000 mm)
        this.pixelsPerMeter = 360.0;
        this.panX = 0.0;
        this.panY = 0.0;
        this._isPanning = false;
        this._lastMouse = [0, 0];
        this._rgbScratch = [0, 0, 0];

        this._bindEvents();
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
    }

    _precomputePieceContours()
    {
        if (!this.network) return;
        const uv = this.network.patternUV;
        this._pieceRenderCache = this.network.pieces.map((piece) =>
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
        }, { passive: false });

        this.canvas.addEventListener('pointerdown', (e) =>
        {
            const rect = this.canvas.getBoundingClientRect();
            const sx = e.clientX - rect.left, sy = e.clientY - rect.top;
            this._lastMouse = [sx, sy];

            if (e.button === 1 || e.button === 2 || e.altKey || e.shiftKey)
            {
                this._isPanning = true;
                this.canvas.setPointerCapture(e.pointerId);
                return;
            }

            // Hit-test pattern piece in 2D mm space
            if (this._pieceRenderCache)
            {
                const [wx, wy] = this._screenToWorld(sx, sy, rect.width, rect.height);
                const xMm = wx * 1000.0, yMm = wy * 1000.0;
                let hitIdx = -1;
                for (let p = 0; p < this._pieceRenderCache.length; ++p)
                {
                    if (this._pointInPolyMm(xMm, yMm, this._pieceRenderCache[p].cutMm))
                    {
                        hitIdx = p;
                        break;
                    }
                }
                if (hitIdx >= 0)
                {
                    this.selectedPieceIndex = hitIdx;
                    if (this.onSelectPiece) this.onSelectPiece(hitIdx);
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
            const rect = this.canvas.getBoundingClientRect();
            const sx = e.clientX - rect.left, sy = e.clientY - rect.top;
            this.panX += sx - this._lastMouse[0];
            this.panY += sy - this._lastMouse[1];
            this._lastMouse = [sx, sy];
        });

        const endPan = () => { this._isPanning = false; };
        this.canvas.addEventListener('pointerup', endPan);
        this.canvas.addEventListener('pointercancel', endPan);
        this.canvas.addEventListener('contextmenu', (e) => e.preventDefault());
    }

    _pointInPolyMm(x, y, poly)
    {
        let inside = false;
        for (let i = 0, j = poly.length - 1; i < poly.length; j = i++)
        {
            const xi = poly[i][0], yi = poly[i][1];
            const xj = poly[j][0], yj = poly[j][1];
            const intersect = ((yi > y) !== (yj > y)) && (x < ((xj - xi) * (y - yi)) / (yj - yi + 1e-12) + xi);
            if (intersect) inside = !inside;
        }
        return inside;
    }

    render()
    {
        const rect = this.canvas.getBoundingClientRect();
        const dpr = Math.min(2, window.devicePixelRatio || 1);
        const w = Math.max(100, Math.floor(rect.width));
        const h = Math.max(100, Math.floor(rect.height));
        if (this.canvas.width !== Math.floor(w * dpr) || this.canvas.height !== Math.floor(h * dpr))
        {
            this.canvas.width = Math.floor(w * dpr);
            this.canvas.height = Math.floor(h * dpr);
        }

        const ctx = this.ctx;
        ctx.save();
        ctx.scale(dpr, dpr);

        // 1. Deep Project-Zero CAD Sheet Background
        ctx.fillStyle = '#131519';
        ctx.fillRect(0, 0, w, h);

        this._drawMillimetreGrid(ctx, w, h);

        if (!this.network || !this._pieceRenderCache)
        {
            ctx.restore();
            return;
        }

        const net = this.network;
        const uv = net.patternUV;
        const fIdx = net.faceIndices;
        const rgb = this._rgbScratch;

        // 2. Render Pattern Pieces (Fill / Live Heatmap + Isotropic Honeycomb Mesh + Cut/Sew Contours)
        for (let pIdx = 0; pIdx < net.pieces.length; ++pIdx)
        {
            const piece = net.pieces[pIdx];
            const cache = this._pieceRenderCache[pIdx];
            const isSelected = pIdx === this.selectedPieceIndex;

            // Seam allowance outer silhouette
            if (this.showSeamAllowance)
            {
                ctx.beginPath();
                for (let k = 0; k < cache.cutMm.length; ++k)
                {
                    const [sx, sy] = this._worldToScreen(cache.cutMm[k][0] * 0.001, cache.cutMm[k][1] * 0.001, w, h);
                    if (k === 0) ctx.moveTo(sx, sy); else ctx.lineTo(sx, sy);
                }
                ctx.closePath();
                ctx.fillStyle = isSelected ? 'rgba(59, 130, 246, 0.09)' : 'rgba(255, 255, 255, 0.025)';
                ctx.fill();
                ctx.strokeStyle = isSelected ? 'rgba(96, 165, 250, 0.72)' : 'rgba(148, 163, 184, 0.34)';
                ctx.lineWidth = isSelected ? 1.35 : 0.9;
                ctx.stroke();
            }

            // Base sew-line fill
            ctx.beginPath();
            for (let k = 0; k < cache.sewMm.length; ++k)
            {
                const [sx, sy] = this._worldToScreen(cache.sewMm[k][0] * 0.001, cache.sewMm[k][1] * 0.001, w, h);
                if (k === 0) ctx.moveTo(sx, sy); else ctx.lineTo(sx, sy);
            }
            ctx.closePath();
            ctx.fillStyle = this.heatmapMode === 'textile' ? `${this.fabricHex}36` : '#1b1f27';
            ctx.fill();
        }

        // 3. Per-triangle Heatmap Fill + Optional Honeycomb Triangulation Wireframe
        const showHeat = this.heatmapMode !== 'textile' && this.solver;
        if (showHeat || this.showMeshWire)
        {
            ctx.lineWidth = 0.45;
            for (let f = 0; f < net.faceCount; ++f)
            {
                const ia = fIdx[f * 3], ib = fIdx[f * 3 + 1], ic = fIdx[f * 3 + 2];
                const [ax, ay] = this._worldToScreen(uv[ia * 2], uv[ia * 2 + 1], w, h);
                const [bx, by] = this._worldToScreen(uv[ib * 2], uv[ib * 2 + 1], w, h);
                const [cx, cy] = this._worldToScreen(uv[ic * 2], uv[ic * 2 + 1], w, h);

                ctx.beginPath();
                ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.lineTo(cx, cy);
                ctx.closePath();

                if (showHeat)
                {
                    evaluateHeatmapColorRGB(this.heatmapMode, this.solver, ia, rgb);
                    ctx.fillStyle = `rgba(${(rgb[0] * 255) | 0}, ${(rgb[1] * 255) | 0}, ${(rgb[2] * 255) | 0}, 0.62)`;
                    ctx.fill();
                }
                if (this.showMeshWire)
                {
                    ctx.strokeStyle = showHeat ? 'rgba(15, 23, 42, 0.26)' : 'rgba(226, 232, 240, 0.10)';
                    ctx.stroke();
                }
            }
        }

        // 4. Crisp Sew Contours, Grainline Arrows & Dimension Callouts
        for (let pIdx = 0; pIdx < net.pieces.length; ++pIdx)
        {
            const piece = net.pieces[pIdx];
            const cache = this._pieceRenderCache[pIdx];
            const isSelected = pIdx === this.selectedPieceIndex;

            ctx.save();
            ctx.beginPath();
            for (let k = 0; k < cache.sewMm.length; ++k)
            {
                const [sx, sy] = this._worldToScreen(cache.sewMm[k][0] * 0.001, cache.sewMm[k][1] * 0.001, w, h);
                if (k === 0) ctx.moveTo(sx, sy); else ctx.lineTo(sx, sy);
            }
            ctx.closePath();
            ctx.setLineDash([4, 3]);
            ctx.strokeStyle = isSelected ? '#60a5fa' : '#cbd5e1';
            ctx.lineWidth = isSelected ? 1.5 : 1.05;
            ctx.stroke();
            ctx.restore();

            // Grainline arrow (Warp direction)
            const [cx, cy] = this._worldToScreen(cache.centerM[0], cache.centerM[1], w, h);
            const grainRad = ((piece.grainlineAngleDeg || 0) * Math.PI) / 180.0;
            const halfLenPx = Math.min(48, Math.max(20, cache.heightMm * 0.001 * this.pixelsPerMeter * 0.28));
            const dx = Math.sin(grainRad) * halfLenPx;
            const dy = -Math.cos(grainRad) * halfLenPx;

            ctx.strokeStyle = '#f87171';
            ctx.lineWidth = 1.25;
            ctx.beginPath();
            ctx.moveTo(cx - dx, cy - dy);
            ctx.lineTo(cx + dx, cy + dy);
            ctx.stroke();

            // Arrowheads
            const ang = Math.atan2(dy, dx);
            for (const dir of [1, -1])
            {
                const tipX = cx + dir * dx, tipY = cy + dir * dy;
                const a = dir > 0 ? ang : ang + Math.PI;
                ctx.beginPath();
                ctx.moveTo(tipX, tipY);
                ctx.lineTo(tipX - Math.cos(a - 0.45) * 6, tipY - Math.sin(a - 0.45) * 6);
                ctx.moveTo(tipX, tipY);
                ctx.lineTo(tipX - Math.cos(a + 0.45) * 6, tipY - Math.sin(a + 0.45) * 6);
                ctx.stroke();
            }

            // Piece label pill
            ctx.font = '600 10.5px Inter, system-ui, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillStyle = isSelected ? '#f8fafc' : '#cbd5e1';
            ctx.fillText(piece.name, cx, cy - 5);

            ctx.font = '500 9.5px "JetBrains Mono", monospace';
            ctx.fillStyle = '#94a3b8';
            ctx.fillText(`${cache.widthMm.toFixed(0)}×${cache.heightMm.toFixed(0)} mm · ${(piece.grainlineAngleDeg || 0).toFixed(0)}°`, cx, cy + 9);
        }

        // 5. Color-Coded M:N Sewing Stitch Connections (decimated cleanly so gather ratios are legible)
        if (this.showSeamLines && net.seams)
        {
            for (let s = 0; s < net.seams.length; ++s)
            {
                const seam = net.seams[s];
                const col = SEAM_PALETTE[s % SEAM_PALETTE.length];
                ctx.strokeStyle = col;
                ctx.globalAlpha = 0.52;
                ctx.lineWidth = 0.95;

                const stride = Math.max(1, Math.floor(seam.pairCount / 6));
                for (let k = 0; k < seam.pairCount; k += stride)
                {
                    const pairIdx = (seam.pairStart + k) * 2;
                    const va = net.seamIndices[pairIdx];
                    const vb = net.seamIndices[pairIdx + 1];
                    const [ax, ay] = this._worldToScreen(uv[va * 2], uv[va * 2 + 1], w, h);
                    const [bx, by] = this._worldToScreen(uv[vb * 2], uv[vb * 2 + 1], w, h);
                    ctx.beginPath();
                    ctx.moveTo(ax, ay);
                    ctx.lineTo(bx, by);
                    ctx.stroke();
                }
                ctx.globalAlpha = 1.0;
            }
        }

        this._drawRulers(ctx, w, h);
        ctx.restore();
    }

    _drawMillimetreGrid(ctx, w, h)
    {
        const step100Px = 0.10 * this.pixelsPerMeter; // 100 mm major grid
        const step10Px = 0.01 * this.pixelsPerMeter;  // 10 mm minor grid
        const [wx0, wy0] = this._screenToWorld(0, h, w, h);
        const [wx1, wy1] = this._screenToWorld(w, 0, w, h);

        if (step10Px >= 5.5)
        {
            ctx.strokeStyle = 'rgba(255, 255, 255, 0.025)';
            ctx.lineWidth = 0.6;
            ctx.beginPath();
            for (let x = Math.floor(wx0 * 100) / 100; x <= wx1; x += 0.01)
            {
                const [sx] = this._worldToScreen(x, 0, w, h);
                ctx.moveTo(sx, 0); ctx.lineTo(sx, h);
            }
            for (let y = Math.floor(wy0 * 100) / 100; y <= wy1; y += 0.01)
            {
                const [, sy] = this._worldToScreen(0, y, w, h);
                ctx.moveTo(0, sy); ctx.lineTo(w, sy);
            }
            ctx.stroke();
        }

        if (step100Px >= 14.0)
        {
            ctx.strokeStyle = 'rgba(255, 255, 255, 0.065)';
            ctx.lineWidth = 0.85;
            ctx.beginPath();
            for (let x = Math.floor(wx0 * 10) / 10; x <= wx1; x += 0.10)
            {
                const [sx] = this._worldToScreen(x, 0, w, h);
                ctx.moveTo(sx, 0); ctx.lineTo(sx, h);
            }
            for (let y = Math.floor(wy0 * 10) / 10; y <= wy1; y += 0.10)
            {
                const [, sy] = this._worldToScreen(0, y, w, h);
                ctx.moveTo(0, sy); ctx.lineTo(w, sy);
            }
            ctx.stroke();
        }
    }

    _drawRulers(ctx, w, h)
    {
        ctx.fillStyle = 'rgba(22, 24, 29, 0.92)';
        ctx.fillRect(0, 0, w, 19);
        ctx.fillRect(0, 19, 24, h - 19);
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, 19.5); ctx.lineTo(w, 19.5);
        ctx.moveTo(24.5, 0); ctx.lineTo(24.5, h);
        ctx.stroke();

        ctx.font = '500 9px "JetBrains Mono", monospace';
        ctx.fillStyle = '#7c8594';
        const [wx0, wy0] = this._screenToWorld(24, h, w, h);
        const [wx1, wy1] = this._screenToWorld(w, 19, w, h);

        ctx.textAlign = 'center';
        for (let x = Math.floor(wx0 * 10) / 10; x <= wx1; x += 0.10)
        {
            const [sx] = this._worldToScreen(x, 0, w, h);
            if (sx < 34) continue;
            ctx.fillText(`${Math.round(x * 1000)}`, sx, 13);
        }

        ctx.save();
        for (let y = Math.floor(wy0 * 10) / 10; y <= wy1; y += 0.10)
        {
            const [, sy] = this._worldToScreen(0, y, w, h);
            if (sy < 30) continue;
            ctx.save();
            ctx.translate(14, sy);
            ctx.rotate(-Math.PI * 0.5);
            ctx.fillText(`${Math.round(y * 1000)}`, 0, 0);
            ctx.restore();
        }
        ctx.restore();

        ctx.fillStyle = '#60a5fa';
        ctx.textAlign = 'left';
        ctx.fillText('mm', 4, 13);
    }
}
