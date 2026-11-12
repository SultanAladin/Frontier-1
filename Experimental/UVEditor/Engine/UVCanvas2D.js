/*==========================================================================================================================================
                                                            UVCANVAS2D.JS
==========================================================================================================================================*/
// High-DPI 2D UV & UDIM Editor canvas renderer and hit-tester:
//   - Multi-UDIM coordinate system (1001 + u + 10*v) with live checker/color-grid/UV-grid & custom image textures
//   - Overlay lenses: Clean, Importance Zones, Texel Density, Angle Stretch, Area Stretch, Overlap & Flipped
//   - Blender UV HUD: 2D Cursor, Pivot indicator, Proportional Falloff ring, Box/Circle/Lasso marquees, G/R/S constraint lines

import { EdgeKey, UdimOrigin } from './TopologySurface.js';

function CreatePatternCanvas(Mode)
{
    const Size = 512;
    const Canvas = typeof document !== 'undefined' ? document.createElement('canvas') : null;
    if (!Canvas) return null;
    Canvas.width = Size;
    Canvas.height = Size;
    const Ctx = Canvas.getContext('2d');

    if (Mode === 'blank')
    {
        Ctx.fillStyle = '#0f1114';
        Ctx.fillRect(0, 0, Size, Size);
        return Canvas;
    }

    if (Mode === 'checker')
    {
        const Cells = 16;
        const Step = Size / Cells;
        for (let y = 0; y < Cells; y++)
        {
            for (let x = 0; x < Cells; x++)
            {
                Ctx.fillStyle = (x + y) % 2 === 0 ? '#181b1e' : '#111316';
                Ctx.fillRect(x * Step, y * Step, Step, Step);
            }
        }
        return Canvas;
    }

    if (Mode === 'uv')
    {
        const Cells = 8;
        const Step = Size / Cells;
        for (let y = 0; y < Cells; y++)
        {
            for (let x = 0; x < Cells; x++)
            {
                const Hue = ((x + y * 2) / 16) * 360;
                Ctx.fillStyle = (x + y) % 2 === 0 ? '#1c2024' : '#14171b';
                Ctx.fillRect(x * Step, y * Step, Step, Step);
                Ctx.strokeStyle = `hsla(${Hue}, 45%, 60%, 0.25)`;
                Ctx.lineWidth = 1;
                Ctx.strokeRect(x * Step + 0.5, y * Step + 0.5, Step - 1, Step - 1);
                Ctx.fillStyle = 'rgba(242,242,240,0.34)';
                Ctx.font = '500 10px ui-monospace, monospace';
                Ctx.fillText(`${String.fromCharCode(65 + x)}${8 - y}`, x * Step + 6, y * Step + 15);
            }
        }
        // Center cross & diagonal orientation arrow
        Ctx.strokeStyle = 'rgba(239,244,207,0.22)';
        Ctx.beginPath();
        Ctx.moveTo(0, Size / 2); Ctx.lineTo(Size, Size / 2);
        Ctx.moveTo(Size / 2, 0); Ctx.lineTo(Size / 2, Size);
        Ctx.stroke();
        return Canvas;
    }

    // Default 'color' grid (Blender-inspired dark muted calibration grid matching Frontier theme)
    const Cells = 8;
    const Step = Size / Cells;
    const Palette = [
        '#1d262b', '#222b28', '#29261d', '#24202b',
        '#1b242e', '#262922', '#2b2221', '#1e2827'
    ];
    for (let y = 0; y < Cells; y++)
    {
        for (let x = 0; x < Cells; x++)
        {
            const Base = Palette[(x * 3 + y * 5) % Palette.length];
            Ctx.fillStyle = Base;
            Ctx.fillRect(x * Step, y * Step, Step, Step);

            // Subtle 2x2 sub-checker
            const Half = Step * 0.5;
            Ctx.fillStyle = 'rgba(255,255,255,0.035)';
            Ctx.fillRect(x * Step, y * Step, Half, Half);
            Ctx.fillRect(x * Step + Half, y * Step + Half, Half, Half);

            Ctx.fillStyle = 'rgba(242,242,240,0.28)';
            Ctx.font = '500 11px ui-monospace, monospace';
            Ctx.fillText(`${String.fromCharCode(65 + x)}${Cells - y}`, x * Step + 5, y * Step + 15);
        }
    }
    return Canvas;
}

function HeatColor(T, Alpha = 0.42)
{
    // T in [0..1]: 0=cool blue -> 0.33=mint -> 0.66=amber -> 1=coral red
    const Clamped = Math.max(0, Math.min(1, T));
    let R, G, B;
    if (Clamped < 0.35)
    {
        const K = Clamped / 0.35;
        R = 68 + K * (105 - 68);
        G = 148 + K * (212 - 148);
        B = 235 + K * (170 - 235);
    }
    else if (Clamped < 0.7)
    {
        const K = (Clamped - 0.35) / 0.35;
        R = 105 + K * (242 - 105);
        G = 212 + K * (194 - 212);
        B = 170 + K * (96 - 170);
    }
    else
    {
        const K = (Clamped - 0.7) / 0.3;
        R = 242 + K * (232 - 242);
        G = 194 + K * (95 - 194);
        B = 96 + K * (82 - 96);
    }
    return `rgba(${Math.round(R)}, ${Math.round(G)}, ${Math.round(B)}, ${Alpha})`;
}

export class UVCanvas2D
{
    constructor(Canvas)
    {
        this.canvas = Canvas;
        this.ctx = Canvas.getContext('2d');
        this.panU = 0.5;
        this.panV = 0.5;
        this.scalePx = 460; // pixels per 1.0 UV unit
        this.width = 600;
        this.height = 500;
        this.dpr = 1;
        this.patternCache = new Map();
        this.customTileImages = new Map();
        this._baseLayerCanvas = typeof document !== 'undefined' ? document.createElement('canvas') : null;
        this._baseLayerCtx = this._baseLayerCanvas ? this._baseLayerCanvas.getContext('2d') : null;
        this._baseLayerSig = null;
        this._uvEdgeTopologyCache = null;
    }

    _EnsureUvEdgeTopology(surface, islands)
    {
        const Sig = typeof surface.ComputeDataSignature === 'function'
            ? surface.ComputeDataSignature('2dEdge')
            : `${surface.faces.length}:${surface.uvCoords.length}:${surface.seams.size}:${surface.hiddenIslands.size}`;
        if (this._uvEdgeTopologyCache && this._uvEdgeTopologyCache.sig === Sig) return this._uvEdgeTopologyCache;

        const UvEdgeCount = new Map();
        const UniqueEdges = []; // { uvA, uvB, uvKey, k3, faceIndices, islId, isSeam, isBoundary }
        const EdgeByKey = new Map();
        for (const Isl of islands)
        {
            if (Isl.hidden) continue;
            for (const Fi of Isl.faceIndices)
            {
                const C = surface.faces[Fi].corners;
                for (let k = 0; k < C.length; k++)
                {
                    const A = C[k], B = C[(k + 1) % C.length];
                    const UvKey = EdgeKey(A.uv, B.uv);
                    UvEdgeCount.set(UvKey, (UvEdgeCount.get(UvKey) || 0) + 1);
                    let Entry = EdgeByKey.get(UvKey);
                    if (!Entry)
                    {
                        const K3 = EdgeKey(A.v, B.v);
                        Entry = {
                            uvA: A.uv, uvB: B.uv, uvKey: UvKey, k3: K3,
                            faces: [Fi], islId: Isl.id, isSeam: surface.seams.has(K3), isBoundary: false
                        };
                        EdgeByKey.set(UvKey, Entry);
                        UniqueEdges.push(Entry);
                    }
                    else
                    {
                        Entry.faces.push(Fi);
                    }
                }
            }
        }
        for (let i = 0; i < UniqueEdges.length; i++)
        {
            const E = UniqueEdges[i];
            E.isBoundary = UvEdgeCount.get(E.uvKey) === 1;
        }
        this._uvEdgeTopologyCache = { sig: Sig, uniqueEdges: UniqueEdges, edgeByKey: EdgeByKey };
        return this._uvEdgeTopologyCache;
    }

    Resize()
    {
        const Rect = this.canvas.getBoundingClientRect();
        if (!Rect.width || !Rect.height) return false;
        this.dpr = Math.min(2, window.devicePixelRatio || 1);
        const W = Math.round(Rect.width * this.dpr);
        const H = Math.round(Rect.height * this.dpr);
        if (this.canvas.width !== W || this.canvas.height !== H)
        {
            this.canvas.width = W;
            this.canvas.height = H;
        }
        this.width = Rect.width;
        this.height = Rect.height;
        return true;
    }

    UvToScreen(U, V)
    {
        return [
            this.width * 0.5 + (U - this.panU) * this.scalePx,
            this.height * 0.5 - (V - this.panV) * this.scalePx
        ];
    }

    ScreenToUv(Sx, Sy)
    {
        return [
            this.panU + (Sx - this.width * 0.5) / this.scalePx,
            this.panV - (Sy - this.height * 0.5) / this.scalePx
        ];
    }

    FrameBounds(MinU, MinV, MaxU, MaxV, PaddingRatio = 0.14)
    {
        const SpanU = Math.max(0.15, MaxU - MinU);
        const SpanV = Math.max(0.15, MaxV - MinV);
        const W = Math.max(200, this.width);
        const H = Math.max(200, this.height);
        const UsableW = W * (1 - PaddingRatio * 2);
        const UsableH = H * (1 - PaddingRatio * 2);
        this.scalePx = Math.max(40, Math.min(4000, Math.min(UsableW / SpanU, UsableH / SpanV)));
        this.panU = 0.5 * (MinU + MaxU);
        this.panV = 0.5 * (MinV + MaxV);
    }

    FrameUdimTiles(UdimTiles)
    {
        if (!UdimTiles.length)
        {
            this.FrameBounds(0, 0, 1, 1);
            return;
        }
        let MinU = Infinity, MinV = Infinity, MaxU = -Infinity, MaxV = -Infinity;
        for (const T of UdimTiles)
        {
            const O = UdimOrigin(T.id);
            if (O.u < MinU) MinU = O.u;
            if (O.v < MinV) MinV = O.v;
            if (O.u + 1 > MaxU) MaxU = O.u + 1;
            if (O.v + 1 > MaxV) MaxV = O.v + 1;
        }
        this.FrameBounds(MinU, MinV, MaxU, MaxV, 0.10);
    }

    GetPattern(Mode)
    {
        if (!this.patternCache.has(Mode)) this.patternCache.set(Mode, CreatePatternCanvas(Mode));
        return this.patternCache.get(Mode);
    }

    SetTileCustomImage(TileId, ImageEl)
    {
        this.customTileImages.set(TileId, ImageEl);
    }

    Render(State)
    {
        this.Resize();
        const Ctx = this.ctx;
        Ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);

        const W = this.width, H = this.height;
        const {
            surface,
            islands,
            udimTiles,
            activeUdim,
            importanceGroups,
            selectionMode,
            activeTool,
            overlayLens,
            showChecker,
            showSubGrid,
            showSeams,
            showBoundaries,
            showPins,
            showIslandBadges,
            targetTexelDensity,
            diagnostics,
            hoverHit,
            cursorUV,
            pivotPoint,
            proportional,
            modalTransform,
            marqueeBox,
            circleSelect,
            lassoPoints
        } = State;

        const SurfSig = typeof surface.ComputeDataSignature === 'function'
            ? surface.ComputeDataSignature()
            : `${surface.faces.length}:${surface.uvCoords.length}:${surface.seams.size}`;
        const UdimSig = udimTiles.map(T => `${T.id}:${T.checkerMode}:${T.resolution}:${T.label}`).join(',');
        const BaseSig = [
            W, H, this.dpr,
            this.panU.toFixed(6), this.panV.toFixed(6), this.scalePx.toFixed(3),
            SurfSig, UdimSig, activeUdim,
            overlayLens, showChecker ? 1 : 0, showSubGrid ? 1 : 0,
            showSeams ? 1 : 0, showBoundaries !== false ? 1 : 0,
            targetTexelDensity, importanceGroups.length
        ].join('|');

        const GroupColors = new Map(importanceGroups.map(G => [G.id, G.color]));
        const EdgeTopo = this._EnsureUvEdgeTopology(surface, islands);

        // 1..4a. Static Base Layer (UDIM grid, tiles, batched face fills, batched wireframe, seams) -> cached offscreen!
        let BlitBaseOk = false;
        if (this._baseLayerCanvas && this._baseLayerCtx)
        {
            const PixW = Math.max(1, Math.round(W * this.dpr));
            const PixH = Math.max(1, Math.round(H * this.dpr));
            if (this._baseLayerCanvas.width !== PixW || this._baseLayerCanvas.height !== PixH)
            {
                this._baseLayerCanvas.width = PixW;
                this._baseLayerCanvas.height = PixH;
                this._baseLayerSig = null;
            }
            if (this._baseLayerSig !== BaseSig)
            {
                this._RenderBaseLayer(this._baseLayerCtx, W, H, State, GroupColors, EdgeTopo);
                this._baseLayerSig = BaseSig;
            }
            Ctx.save();
            Ctx.setTransform(1, 0, 0, 1, 0, 0);
            Ctx.drawImage(this._baseLayerCanvas, 0, 0);
            Ctx.restore();
            BlitBaseOk = true;
        }
        if (!BlitBaseOk)
        {
            this._RenderBaseLayer(Ctx, W, H, State, GroupColors, EdgeTopo);
        }

        // 4b. Dynamic Batched Selection Overlay Layer (selected faces, island edges, active face, seams/selected edges)
        const HalfW = W * 0.5, HalfH = H * 0.5;
        const S = this.scalePx, Pu = this.panU, Pv = this.panV;

        if (surface.selectedIslands.size || surface.selectedFaces.size)
        {
            let HasIslFill = false;
            Ctx.beginPath();
            for (const Isl of islands)
            {
                if (Isl.hidden || !surface.selectedIslands.has(Isl.id)) continue;
                for (const Fi of Isl.faceIndices)
                {
                    if (surface.selectedFaces.has(Fi)) continue;
                    const Cr = surface.faces[Fi].corners;
                    const U0 = surface.uvCoords[Cr[0].uv];
                    Ctx.moveTo(HalfW + (U0[0] - Pu) * S, HalfH - (U0[1] - Pv) * S);
                    for (let k = 1; k < Cr.length; k++)
                    {
                        const Uk = surface.uvCoords[Cr[k].uv];
                        Ctx.lineTo(HalfW + (Uk[0] - Pu) * S, HalfH - (Uk[1] - Pv) * S);
                    }
                    Ctx.closePath();
                    HasIslFill = true;
                }
            }
            if (HasIslFill)
            {
                Ctx.fillStyle = 'rgba(239, 244, 207, 0.16)';
                Ctx.fill();
            }

            if (surface.selectedFaces.size)
            {
                Ctx.beginPath();
                let HasFaceFill = false;
                for (const Fi of surface.selectedFaces)
                {
                    const F = surface.faces[Fi];
                    if (!F) continue;
                    const Cr = F.corners;
                    const U0 = surface.uvCoords[Cr[0].uv];
                    Ctx.moveTo(HalfW + (U0[0] - Pu) * S, HalfH - (U0[1] - Pv) * S);
                    for (let k = 1; k < Cr.length; k++)
                    {
                        const Uk = surface.uvCoords[Cr[k].uv];
                        Ctx.lineTo(HalfW + (Uk[0] - Pu) * S, HalfH - (Uk[1] - Pv) * S);
                    }
                    Ctx.closePath();
                    HasFaceFill = true;
                }
                if (HasFaceFill)
                {
                    Ctx.fillStyle = 'rgba(245, 184, 76, 0.30)';
                    Ctx.fill();
                }
            }
        }

        if (surface.activeFace !== null && surface.activeFace !== undefined && surface.faces[surface.activeFace])
        {
            const Cr = surface.faces[surface.activeFace].corners;
            const U0 = surface.uvCoords[Cr[0].uv];
            Ctx.beginPath();
            Ctx.moveTo(HalfW + (U0[0] - Pu) * S, HalfH - (U0[1] - Pv) * S);
            for (let k = 1; k < Cr.length; k++)
            {
                const Uk = surface.uvCoords[Cr[k].uv];
                Ctx.lineTo(HalfW + (Uk[0] - Pu) * S, HalfH - (Uk[1] - Pv) * S);
            }
            Ctx.closePath();
            Ctx.strokeStyle = '#ff9d3b';
            Ctx.lineWidth = 2.0;
            Ctx.stroke();
        }

        // Batched selected edges + active edge
        const IsRegionSelMode = selectionMode === 'face' || selectionMode === 'island';
        const SelEdgeList = [];
        let ActiveEdgeItem = null;
        for (let i = 0; i < EdgeTopo.uniqueEdges.length; i++)
        {
            const E = EdgeTopo.uniqueEdges[i];
            let IsSelEdge = false;
            if (IsRegionSelMode)
            {
                let SelCnt = 0;
                for (let j = 0; j < E.faces.length; j++)
                {
                    const Fi = E.faces[j];
                    if (surface.selectedFaces.has(Fi) || surface.selectedIslands.has(E.islId)) SelCnt++;
                }
                IsSelEdge = SelCnt === 1;
            }
            else
            {
                IsSelEdge = surface.selectedUVEdges.has(E.uvKey);
            }
            if (!IsSelEdge) continue;
            const Pa = surface.uvCoords[E.uvA], Pb = surface.uvCoords[E.uvB];
            const X0 = HalfW + (Pa[0] - Pu) * S, Y0 = HalfH - (Pa[1] - Pv) * S;
            const X1 = HalfW + (Pb[0] - Pu) * S, Y1 = HalfH - (Pb[1] - Pv) * S;
            if (Math.max(X0, X1) < -8 || Math.min(X0, X1) > W + 8 || Math.max(Y0, Y1) < -8 || Math.min(Y0, Y1) > H + 8) continue;
            if (!IsRegionSelMode && surface.activeEdgeUV === E.uvKey) ActiveEdgeItem = [X0, Y0, X1, Y1];
            else SelEdgeList.push(X0, Y0, X1, Y1);
        }

        if (SelEdgeList.length || ActiveEdgeItem)
        {
            Ctx.save();
            Ctx.lineCap = 'round';
            if (SelEdgeList.length)
            {
                Ctx.beginPath();
                for (let i = 0; i < SelEdgeList.length; i += 4)
                {
                    Ctx.moveTo(SelEdgeList[i], SelEdgeList[i + 1]);
                    Ctx.lineTo(SelEdgeList[i + 2], SelEdgeList[i + 3]);
                }
                Ctx.strokeStyle = 'rgba(8, 9, 11, 0.88)';
                Ctx.lineWidth = 5.0;
                Ctx.stroke();
                Ctx.strokeStyle = '#eff4cf';
                Ctx.lineWidth = 2.9;
                Ctx.stroke();
            }
            if (ActiveEdgeItem)
            {
                Ctx.beginPath();
                Ctx.moveTo(ActiveEdgeItem[0], ActiveEdgeItem[1]);
                Ctx.lineTo(ActiveEdgeItem[2], ActiveEdgeItem[3]);
                Ctx.strokeStyle = 'rgba(8, 9, 11, 0.88)';
                Ctx.lineWidth = 5.8;
                Ctx.stroke();
                Ctx.strokeStyle = '#ff9d3b';
                Ctx.lineWidth = 3.4;
                Ctx.stroke();
            }
            Ctx.restore();
        }

        // Hover edge / vertex preview in 2D
        if (hoverHit?.kind === 'edge')
        {
            const Pa = surface.uvCoords[hoverHit.uvA];
            const Pb = surface.uvCoords[hoverHit.uvB];
            if (Pa && Pb)
            {
                const [X0, Y0] = this.UvToScreen(Pa[0], Pa[1]);
                const [X1, Y1] = this.UvToScreen(Pb[0], Pb[1]);
                Ctx.save();
                Ctx.lineCap = 'round';
                Ctx.beginPath();
                Ctx.moveTo(X0, Y0);
                Ctx.lineTo(X1, Y1);
                Ctx.strokeStyle = 'rgba(8, 9, 11, 0.85)';
                Ctx.lineWidth = 6.0;
                Ctx.stroke();
                Ctx.strokeStyle = activeTool === 'seam' ? '#ff8b78' : '#7dd3fc';
                Ctx.lineWidth = 3.6;
                Ctx.stroke();
                Ctx.restore();
            }
        }
        else if (hoverHit?.kind === 'vertex')
        {
            const P = surface.uvCoords[hoverHit.uvIndex];
            if (P)
            {
                const [Sx, Sy] = this.UvToScreen(P[0], P[1]);
                Ctx.beginPath();
                Ctx.arc(Sx, Sy, 6.2, 0, Math.PI * 2);
                Ctx.strokeStyle = '#7dd3fc';
                Ctx.lineWidth = 2.2;
                Ctx.stroke();
            }
        }

        // 5. Draw Island Priority Labels when reasonably sized on screen
        if (showIslandBadges && this.scalePx > 140)
        {
            Ctx.font = '500 9px "DM Sans", sans-serif';
            for (const Isl of islands)
            {
                if (Isl.hidden) continue;
                const BoxW = Isl.bounds.width * this.scalePx;
                const BoxH = Isl.bounds.height * this.scalePx;
                if (BoxW < 42 || BoxH < 20) continue;
                const [Cx, Cy] = this.UvToScreen(Isl.bounds.centerU, Isl.bounds.centerV);
                if (Cx < -80 || Cx > W + 80 || Cy < -40 || Cy > H + 40) continue;
                const Label = Isl.displayName.length > 14 ? Isl.displayName.slice(0, 13) + '…' : Isl.displayName;
                const Tw = Ctx.measureText(Label).width + 10;
                Ctx.fillStyle = 'rgba(11, 12, 14, 0.76)';
                Ctx.beginPath();
                Ctx.roundRect(Cx - Tw * 0.5, Cy - 8, Tw, 16, 8);
                Ctx.fill();
                Ctx.fillStyle = surface.selectedIslands.has(Isl.id) ? '#eff4cf' : 'rgba(242,242,240,0.78)';
                Ctx.textAlign = 'center';
                Ctx.fillText(Label, Cx, Cy + 3);
                Ctx.textAlign = 'left';
            }
        }

        // 6. Batched Vertices & Pins (2 path strokes/fills instead of 4000 separate arcs!)
        const ShowAllVerts = selectionMode === 'vertex' || this.scalePx > 260;
        const DrawnUVs = new Uint8Array(surface.uvCoords.length);
        const PinPts = [], UnselPts = [], SelPts = [];
        for (const Isl of islands)
        {
            if (Isl.hidden) continue;
            for (let i = 0; i < Isl.uvIndices.length; i++)
            {
                const UvIdx = Isl.uvIndices[i];
                if (DrawnUVs[UvIdx]) continue;
                DrawnUVs[UvIdx] = 1;

                const IsPin = showPins && surface.pinned.has(UvIdx);
                const IsSel = surface.selectedUVs.has(UvIdx);
                if (!ShowAllVerts && !IsPin && !IsSel) continue;

                const P = surface.uvCoords[UvIdx];
                const Sx = HalfW + (P[0] - Pu) * S;
                const Sy = HalfH - (P[1] - Pv) * S;
                if (Sx < -10 || Sx > W + 10 || Sy < -10 || Sy > H + 10) continue;

                if (IsPin) PinPts.push(Sx, Sy);
                if (IsSel) SelPts.push(Sx, Sy);
                else if (ShowAllVerts) UnselPts.push(Sx, Sy);
            }
        }

        if (UnselPts.length)
        {
            Ctx.beginPath();
            for (let i = 0; i < UnselPts.length; i += 2)
            {
                const x = UnselPts[i], y = UnselPts[i + 1];
                Ctx.moveTo(x + 2.1, y);
                Ctx.arc(x, y, 2.1, 0, Math.PI * 2);
            }
            Ctx.fillStyle = 'rgba(255, 255, 255, 0.45)';
            Ctx.fill();
        }
        if (PinPts.length)
        {
            Ctx.beginPath();
            for (let i = 0; i < PinPts.length; i += 2)
            {
                const x = PinPts[i], y = PinPts[i + 1];
                Ctx.moveTo(x + 5.2, y);
                Ctx.arc(x, y, 5.2, 0, Math.PI * 2);
            }
            Ctx.strokeStyle = '#ff645a';
            Ctx.lineWidth = 1.6;
            Ctx.stroke();
        }
        if (SelPts.length)
        {
            Ctx.beginPath();
            for (let i = 0; i < SelPts.length; i += 2)
            {
                const x = SelPts[i], y = SelPts[i + 1];
                Ctx.moveTo(x + 4.3, y);
                Ctx.arc(x, y, 4.3, 0, Math.PI * 2);
            }
            Ctx.fillStyle = '#eff4cf';
            Ctx.fill();
            Ctx.lineWidth = 1.5;
            Ctx.strokeStyle = '#0b0b0b';
            Ctx.stroke();
        }

        // 7. 2D Cursor (Blender-style crosshair)
        if (cursorUV)
        {
            const [Cx, Cy] = this.UvToScreen(cursorUV[0], cursorUV[1]);
            Ctx.save();
            Ctx.beginPath();
            Ctx.arc(Cx, Cy, 8, 0, Math.PI * 2);
            Ctx.strokeStyle = '#ff6d5a';
            Ctx.lineWidth = 1.5;
            Ctx.setLineDash([4, 3]);
            Ctx.stroke();
            Ctx.setLineDash([]);
            Ctx.strokeStyle = 'rgba(255,255,255,0.8)';
            Ctx.lineWidth = 1;
            Ctx.beginPath();
            Ctx.moveTo(Cx - 12, Cy); Ctx.lineTo(Cx + 12, Cy);
            Ctx.moveTo(Cx, Cy - 12); Ctx.lineTo(Cx, Cy + 12);
            Ctx.stroke();
            Ctx.restore();
        }

        // 8. Pivot Indicator & Proportional Editing Radius
        if (pivotPoint && surface.selectedUVs.size)
        {
            const [Px, Py] = this.UvToScreen(pivotPoint[0], pivotPoint[1]);
            Ctx.beginPath();
            Ctx.arc(Px, Py, 4.5, 0, Math.PI * 2);
            Ctx.fillStyle = '#e8c27a';
            Ctx.fill();
            Ctx.strokeStyle = '#0b0b0b';
            Ctx.lineWidth = 1.2;
            Ctx.stroke();

            if (proportional?.enabled)
            {
                const RadPx = proportional.radius * this.scalePx;
                Ctx.save();
                Ctx.beginPath();
                Ctx.arc(Px, Py, RadPx, 0, Math.PI * 2);
                Ctx.strokeStyle = 'rgba(239, 244, 207, 0.48)';
                Ctx.lineWidth = 1.2;
                Ctx.setLineDash([5, 5]);
                Ctx.stroke();
                Ctx.restore();
            }
        }

        // 9. Modal Transform Axis Lock Lines & Live HUD
        if (modalTransform)
        {
            const [Px, Py] = this.UvToScreen(modalTransform.pivot[0], modalTransform.pivot[1]);
            if (modalTransform.axis === 'x')
            {
                Ctx.strokeStyle = 'rgba(255, 108, 94, 0.72)';
                Ctx.lineWidth = 1.2;
                Ctx.beginPath(); Ctx.moveTo(0, Py); Ctx.lineTo(W, Py); Ctx.stroke();
            }
            else if (modalTransform.axis === 'y')
            {
                Ctx.strokeStyle = 'rgba(120, 214, 150, 0.72)';
                Ctx.lineWidth = 1.2;
                Ctx.beginPath(); Ctx.moveTo(Px, 0); Ctx.lineTo(Px, H); Ctx.stroke();
            }
            if (modalTransform.mode === 'rotate' || modalTransform.mode === 'scale')
            {
                Ctx.save();
                Ctx.setLineDash([4, 4]);
                Ctx.strokeStyle = 'rgba(239, 244, 207, 0.45)';
                Ctx.beginPath();
                Ctx.moveTo(Px, Py);
                Ctx.lineTo(modalTransform.curScreenX, modalTransform.curScreenY);
                Ctx.stroke();
                Ctx.restore();
            }
        }

        // 10. Box / Circle / Lasso Selection Overlays
        if (marqueeBox)
        {
            const Bx = Math.min(marqueeBox.x0, marqueeBox.x1);
            const By = Math.min(marqueeBox.y0, marqueeBox.y1);
            const Bw = Math.abs(marqueeBox.x1 - marqueeBox.x0);
            const Bh = Math.abs(marqueeBox.y1 - marqueeBox.y0);
            Ctx.fillStyle = 'rgba(239, 244, 207, 0.10)';
            Ctx.strokeStyle = 'rgba(239, 244, 207, 0.85)';
            Ctx.lineWidth = 1.2;
            Ctx.setLineDash([4, 3]);
            Ctx.fillRect(Bx, By, Bw, Bh);
            Ctx.strokeRect(Bx, By, Bw, Bh);
            Ctx.setLineDash([]);
        }
        if (circleSelect?.active)
        {
            Ctx.beginPath();
            Ctx.arc(circleSelect.x, circleSelect.y, circleSelect.radiusPx, 0, Math.PI * 2);
            Ctx.fillStyle = 'rgba(239, 244, 207, 0.08)';
            Ctx.strokeStyle = '#eff4cf';
            Ctx.lineWidth = 1.3;
            Ctx.fill();
            Ctx.stroke();
        }
        if (lassoPoints && lassoPoints.length > 1)
        {
            Ctx.beginPath();
            Ctx.moveTo(lassoPoints[0][0], lassoPoints[0][1]);
            for (let i = 1; i < lassoPoints.length; i++) Ctx.lineTo(lassoPoints[i][0], lassoPoints[i][1]);
            Ctx.closePath();
            Ctx.fillStyle = 'rgba(239, 244, 207, 0.10)';
            Ctx.strokeStyle = '#eff4cf';
            Ctx.lineWidth = 1.3;
            Ctx.setLineDash([4, 3]);
            Ctx.fill();
            Ctx.stroke();
            Ctx.setLineDash([]);
        }
    }

    _RenderBaseLayer(Ctx, W, H, State, GroupColors, EdgeTopo)
    {
        const {
            surface,
            islands,
            udimTiles,
            activeUdim,
            overlayLens,
            showChecker,
            showSubGrid,
            showSeams,
            showBoundaries,
            targetTexelDensity,
            diagnostics
        } = State;

        Ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
        Ctx.fillStyle = '#090a0c';
        Ctx.fillRect(0, 0, W, H);

        const HalfW = W * 0.5, HalfH = H * 0.5;
        const S = this.scalePx, Pu = this.panU, Pv = this.panV;

        // 1. Batched Background UDIM coordinate workspace lines
        const LeftUV = Pu - HalfW / S;
        const RightUV = Pu + HalfW / S;
        const BottomUV = Pv - HalfH / S;
        const TopUV = Pv + HalfH / S;

        const MinTileU = Math.max(-2, Math.floor(LeftUV));
        const MaxTileU = Math.min(10, Math.ceil(RightUV));
        const MinTileV = Math.max(-1, Math.floor(BottomUV));
        const MaxTileV = Math.min(5, Math.ceil(TopUV));

        Ctx.lineWidth = 1;
        Ctx.strokeStyle = 'rgba(255,255,255,0.04)';
        Ctx.beginPath();
        for (let tu = MinTileU; tu <= MaxTileU; tu++)
        {
            const Sx = HalfW + (tu - Pu) * S;
            Ctx.moveTo(Sx, 0); Ctx.lineTo(Sx, H);
        }
        for (let tv = MinTileV; tv <= MaxTileV; tv++)
        {
            const Sy = HalfH - (tv - Pv) * S;
            Ctx.moveTo(0, Sy); Ctx.lineTo(W, Sy);
        }
        Ctx.stroke();

        if (S > 90)
        {
            Ctx.font = '500 9.5px ui-monospace, monospace';
            Ctx.fillStyle = 'rgba(255,255,255,0.14)';
            for (let tv = 0; tv <= 2; tv++)
            {
                for (let tu = 0; tu <= 4; tu++)
                {
                    const TileId = 1001 + tu + tv * 10;
                    if (udimTiles.some(T => T.id === TileId)) continue;
                    const Sx = HalfW + (tu + 0.04 - Pu) * S;
                    const Sy = HalfH - (tv + 0.94 - Pv) * S;
                    Ctx.fillText(`${TileId}`, Sx, Sy);
                }
            }
        }

        // 2. Active Configured UDIM Tiles
        const TileAreaSums = new Map();
        for (const Isl of islands)
        {
            if (Isl.hidden) continue;
            TileAreaSums.set(Isl.udimTile, (TileAreaSums.get(Isl.udimTile) || 0) + Isl.areaUV);
        }

        for (const Tile of udimTiles)
        {
            const O = UdimOrigin(Tile.id);
            const X0 = HalfW + (O.u - Pu) * S, Y1 = HalfH - (O.v - Pv) * S;
            const X1 = HalfW + (O.u + 1 - Pu) * S, Y0 = HalfH - (O.v + 1 - Pv) * S;
            const TileW = X1 - X0, TileH = Y1 - Y0;
            if (X1 < 0 || X0 > W || Y1 < 0 || Y0 > H) continue;

            const IsActive = Tile.id === activeUdim;
            Ctx.save();
            Ctx.beginPath();
            Ctx.rect(X0, Y0, TileW, TileH);
            Ctx.clip();

            const CustomImg = this.customTileImages.get(Tile.id);
            if (CustomImg && CustomImg.complete)
            {
                Ctx.globalAlpha = 0.72;
                Ctx.drawImage(CustomImg, X0, Y0, TileW, TileH);
                Ctx.globalAlpha = 1;
            }
            else if (showChecker)
            {
                const PatCanvas = this.GetPattern(Tile.checkerMode || 'color');
                if (PatCanvas)
                {
                    Ctx.globalAlpha = IsActive ? 0.92 : 0.64;
                    Ctx.drawImage(PatCanvas, X0, Y0, TileW, TileH);
                    Ctx.globalAlpha = 1;
                }
            }
            else
            {
                Ctx.fillStyle = IsActive ? '#111417' : '#0c0e11';
                Ctx.fillRect(X0, Y0, TileW, TileH);
            }

            if (showSubGrid && TileW > 64)
            {
                const Divs = TileW > 420 ? 16 : 8;
                Ctx.strokeStyle = 'rgba(255,255,255,0.055)';
                Ctx.lineWidth = 1;
                Ctx.beginPath();
                for (let k = 1; k < Divs; k++)
                {
                    const Gx = X0 + (TileW * k) / Divs;
                    const Gy = Y0 + (TileH * k) / Divs;
                    Ctx.moveTo(Gx, Y0); Ctx.lineTo(Gx, Y1);
                    Ctx.moveTo(X0, Gy); Ctx.lineTo(X1, Gy);
                }
                Ctx.stroke();
            }

            Ctx.restore();
            Ctx.strokeStyle = IsActive ? 'rgba(239, 244, 207, 0.55)' : 'rgba(255, 255, 255, 0.16)';
            Ctx.lineWidth = IsActive ? 1.5 : 1;
            Ctx.strokeRect(X0 + 0.5, Y0 + 0.5, TileW - 1, TileH - 1);

            const OccPct = Math.min(100, (TileAreaSums.get(Tile.id) || 0) * 100);
            const ResLabel = Tile.resolution >= 1024 ? `${Tile.resolution / 1024}K` : `${Tile.resolution}`;
            const BadgeText = `${Tile.id} · ${ResLabel} · ${OccPct.toFixed(1)}%${Tile.label ? ' · ' + Tile.label : ''}`;
            Ctx.font = '500 10px ui-monospace, monospace';
            const Tw = Math.min(Math.max(90, TileW - 8), Ctx.measureText(BadgeText).width + 16);
            const PillY = Math.max(6, Y0 - 22);
            Ctx.fillStyle = IsActive ? 'rgba(239, 244, 207, 0.94)' : 'rgba(22, 23, 26, 0.90)';
            Ctx.strokeStyle = IsActive ? 'rgba(239, 244, 207, 1)' : 'rgba(255, 255, 255, 0.12)';
            Ctx.beginPath();
            Ctx.roundRect(X0 + 2, PillY, Tw, 18, 9);
            Ctx.fill();
            Ctx.stroke();
            Ctx.fillStyle = IsActive ? '#0b0b0b' : '#d8dad6';
            Ctx.fillText(BadgeText, X0 + 10, PillY + 12.5, Tw - 14);
        }

        // 3. Color-Batched UV Faces (1 fill() per distinct fill color instead of 6000 separate fill() calls!)
        const FaceBuckets = new Map();
        const AddFaceToBucket = (Color, Fi) =>
        {
            let List = FaceBuckets.get(Color);
            if (!List) { List = []; FaceBuckets.set(Color, List); }
            List.push(Fi);
        };
        const HasFastDist = typeof surface.GetFaceDistortionFast === 'function';

        for (const Isl of islands)
        {
            if (Isl.hidden) continue;
            if (Isl.bounds.maxU < LeftUV - 0.01 || Isl.bounds.minU > RightUV + 0.01 ||
                Isl.bounds.maxV < BottomUV - 0.01 || Isl.bounds.minV > TopUV + 0.01) continue;

            const GrpColor = Isl.importanceGroupId ? GroupColors.get(Isl.importanceGroupId) : null;
            const BaseColor = overlayLens === 'importance'
                ? (GrpColor ? this.HexToRgba(GrpColor, 0.25) : 'rgba(145, 152, 162, 0.16)')
                : overlayLens === 'texel'
                    ? HeatColor(Math.round(Math.max(0, Math.min(1, 0.35 + Math.log2(Math.max(0.125, Isl.texelDensity / Math.max(0.1, targetTexelDensity || 10))) * 0.28)) * 31) / 31, 0.36)
                    : (GrpColor && overlayLens === 'clean' ? this.HexToRgba(GrpColor, 0.10) : 'rgba(255,255,255,0.065)');

            for (let i = 0; i < Isl.faceIndices.length; i++)
            {
                const Fi = Isl.faceIndices[i];
                if (overlayLens === 'stretch-angle')
                {
                    const D = HasFastDist ? surface.GetFaceDistortionFast(Fi) : surface.FaceTrianglesDistortion(Fi);
                    const Q = Math.round(Math.min(1, D.angleStretch * 2.2) * 31) / 31;
                    AddFaceToBucket(HeatColor(Q, 0.42), Fi);
                }
                else if (overlayLens === 'stretch-area')
                {
                    const D = HasFastDist ? surface.GetFaceDistortionFast(Fi) : surface.FaceTrianglesDistortion(Fi);
                    const Q = Math.round(Math.max(0, Math.min(1, 0.35 + Math.log2(Math.max(0.1, D.areaScale)) * 0.22)) * 31) / 31;
                    AddFaceToBucket(HeatColor(Q, 0.40), Fi);
                }
                else if (overlayLens === 'overlap')
                {
                    if (diagnostics?.OverlappingFaces?.has(Fi)) AddFaceToBucket('rgba(255, 84, 84, 0.54)', Fi);
                    else if (diagnostics?.FlippedFaces?.has(Fi)) AddFaceToBucket('rgba(243, 196, 108, 0.46)', Fi);
                    else AddFaceToBucket('rgba(120, 199, 150, 0.12)', Fi);
                }
                else
                {
                    AddFaceToBucket(BaseColor, Fi);
                }
            }
        }

        for (const [Color, FaceList] of FaceBuckets)
        {
            Ctx.beginPath();
            for (let i = 0; i < FaceList.length; i++)
            {
                const Cr = surface.faces[FaceList[i]].corners;
                const U0 = surface.uvCoords[Cr[0].uv];
                Ctx.moveTo(HalfW + (U0[0] - Pu) * S, HalfH - (U0[1] - Pv) * S);
                for (let k = 1; k < Cr.length; k++)
                {
                    const Uk = surface.uvCoords[Cr[k].uv];
                    Ctx.lineTo(HalfW + (Uk[0] - Pu) * S, HalfH - (Uk[1] - Pv) * S);
                }
                Ctx.closePath();
            }
            Ctx.fillStyle = Color;
            Ctx.fill();
        }

        // 4a. Style-Batched Wireframe Edges, Boundaries & Seams (1 stroke() per color bucket!)
        const EdgeBuckets = new Map();
        const SeamCoords = [];
        const IslandMap = new Map(islands.map(Isl => [Isl.id, Isl]));

        for (let i = 0; i < EdgeTopo.uniqueEdges.length; i++)
        {
            const E = EdgeTopo.uniqueEdges[i];
            const Pa = surface.uvCoords[E.uvA], Pb = surface.uvCoords[E.uvB];
            if (Math.max(Pa[0], Pb[0]) < LeftUV - 0.01 || Math.min(Pa[0], Pb[0]) > RightUV + 0.01 ||
                Math.max(Pa[1], Pb[1]) < BottomUV - 0.01 || Math.min(Pa[1], Pb[1]) > TopUV + 0.01) continue;

            const X0 = HalfW + (Pa[0] - Pu) * S, Y0 = HalfH - (Pa[1] - Pv) * S;
            const X1 = HalfW + (Pb[0] - Pu) * S, Y1 = HalfH - (Pb[1] - Pv) * S;
            if (showSeams && E.isSeam)
            {
                SeamCoords.push(X0, Y0, X1, Y1);
                continue;
            }
            const Isl = IslandMap.get(E.islId);
            const GrpColor = Isl?.importanceGroupId ? GroupColors.get(Isl.importanceGroupId) : null;
            const IsBoundary = showBoundaries !== false && E.isBoundary;
            const Key = IsBoundary
                ? `1.25|${GrpColor ? this.HexToRgba(GrpColor, 0.72) : 'rgba(123, 212, 184, 0.54)'}`
                : `0.9|${GrpColor ? this.HexToRgba(GrpColor, 0.40) : 'rgba(242, 242, 240, 0.30)'}`;
            let Coords = EdgeBuckets.get(Key);
            if (!Coords) { Coords = []; EdgeBuckets.set(Key, Coords); }
            Coords.push(X0, Y0, X1, Y1);
        }

        for (const [Key, Coords] of EdgeBuckets)
        {
            const Sep = Key.indexOf('|');
            Ctx.lineWidth = parseFloat(Key.slice(0, Sep));
            Ctx.strokeStyle = Key.slice(Sep + 1);
            Ctx.beginPath();
            for (let i = 0; i < Coords.length; i += 4)
            {
                Ctx.moveTo(Coords[i], Coords[i + 1]);
                Ctx.lineTo(Coords[i + 2], Coords[i + 3]);
            }
            Ctx.stroke();
        }
        if (SeamCoords.length)
        {
            Ctx.strokeStyle = '#ff6d5a';
            Ctx.lineWidth = 1.95;
            Ctx.beginPath();
            for (let i = 0; i < SeamCoords.length; i += 4)
            {
                Ctx.moveTo(SeamCoords[i], SeamCoords[i + 1]);
                Ctx.lineTo(SeamCoords[i + 2], SeamCoords[i + 3]);
            }
            Ctx.stroke();
        }
    }

    HexToRgba(Hex, Alpha)
    {
        const Clean = (Hex || '#eff4cf').replace('#', '');
        const Num = parseInt(Clean.length === 3 ? Clean.split('').map(C => C + C).join('') : Clean, 16);
        const R = (Num >> 16) & 255, G = (Num >> 8) & 255, B = Num & 255;
        return `rgba(${R}, ${G}, ${B}, ${Alpha})`;
    }

    HitTest(Sx, Sy, { surface, islands, mode, activeTool, udimTiles })
    {
        const [U, V] = this.ScreenToUv(Sx, Sy);
        const ThresholdUV = 12 / this.scalePx;

        // 1. Check UDIM header pill click
        for (const T of udimTiles)
        {
            const O = UdimOrigin(T.id);
            const [X0] = this.UvToScreen(O.u, O.v);
            const [X1, Y0] = this.UvToScreen(O.u + 1, O.v + 1);
            const PillY = Math.max(6, Y0 - 22);
            if (Sx >= X0 && Sx <= Math.min(X1, X0 + 220) && Sy >= PillY && Sy <= PillY + 18)
            {
                return { kind: 'udim', tileId: T.id };
            }
        }

        // Spatial-grid O(1) lookup of candidate faces near (U, V)
        const UseSpatial = typeof surface.QueryUvSpatialFaces === 'function';
        const Candidates = UseSpatial
            ? surface.QueryUvSpatialFaces(U - ThresholdUV, V - ThresholdUV, U + ThresholdUV, V + ThresholdUV)
            : null;
        const HiddenFaces = UseSpatial && surface.hiddenIslands.size ? new Set() : null;
        const IslandByFace = new Map();
        for (const Isl of islands)
        {
            if (Isl.hidden)
            {
                if (HiddenFaces) for (const Fi of Isl.faceIndices) HiddenFaces.add(Fi);
                continue;
            }
            for (const Fi of Isl.faceIndices) IslandByFace.set(Fi, Isl);
        }

        if (mode === 'vertex' && activeTool !== 'seam')
        {
            let BestUV = null, BestDist = ThresholdUV;
            if (Candidates)
            {
                for (let i = 0; i < Candidates.length; i++)
                {
                    const Fi = Candidates[i];
                    if (HiddenFaces?.has(Fi)) continue;
                    const Cr = surface.faces[Fi].corners;
                    for (let k = 0; k < Cr.length; k++)
                    {
                        const UvIdx = Cr[k].uv;
                        const P = surface.uvCoords[UvIdx];
                        const D = Math.hypot(P[0] - U, P[1] - V);
                        if (D < BestDist) { BestDist = D; BestUV = UvIdx; }
                    }
                }
            }
            else
            {
                for (const Isl of islands)
                {
                    if (Isl.hidden) continue;
                    for (const UvIdx of Isl.uvIndices)
                    {
                        const P = surface.uvCoords[UvIdx];
                        const D = Math.hypot(P[0] - U, P[1] - V);
                        if (D < BestDist) { BestDist = D; BestUV = UvIdx; }
                    }
                }
            }
            if (BestUV !== null) return { kind: 'vertex', uvIndex: BestUV };
        }

        if (mode === 'edge' || activeTool === 'seam')
        {
            let BestEdge = null, BestDist = ThresholdUV, BestA = -1, BestB = -1, BestVA = -1, BestVB = -1;
            if (Candidates)
            {
                for (let i = 0; i < Candidates.length; i++)
                {
                    const Fi = Candidates[i];
                    if (HiddenFaces?.has(Fi)) continue;
                    const C = surface.faces[Fi].corners;
                    for (let k = 0; k < C.length; k++)
                    {
                        const C0 = C[k], C1 = C[(k + 1) % C.length];
                        const Ua = C0.uv, Ub = C1.uv;
                        const A = surface.uvCoords[Ua], B = surface.uvCoords[Ub];
                        const D = PointSegmentDist(U, V, A[0], A[1], B[0], B[1]);
                        if (D < BestDist)
                        {
                            BestDist = D;
                            BestEdge = EdgeKey(Ua, Ub);
                            BestA = Ua; BestB = Ub;
                            BestVA = C0.v; BestVB = C1.v;
                        }
                    }
                }
            }
            else
            {
                for (const Isl of islands)
                {
                    if (Isl.hidden) continue;
                    for (const Fi of Isl.faceIndices)
                    {
                        const C = surface.faces[Fi].corners;
                        for (let k = 0; k < C.length; k++)
                        {
                            const C0 = C[k], C1 = C[(k + 1) % C.length];
                            const Ua = C0.uv, Ub = C1.uv;
                            const A = surface.uvCoords[Ua], B = surface.uvCoords[Ub];
                            const D = PointSegmentDist(U, V, A[0], A[1], B[0], B[1]);
                            if (D < BestDist)
                            {
                                BestDist = D;
                                BestEdge = EdgeKey(Ua, Ub);
                                BestA = Ua; BestB = Ub;
                                BestVA = C0.v; BestVB = C1.v;
                            }
                        }
                    }
                }
            }
            if (BestEdge)
            {
                return {
                    kind: 'edge',
                    edgeKey: BestEdge,
                    uvA: BestA,
                    uvB: BestB,
                    vA: BestVA,
                    vB: BestVB,
                    key3d: EdgeKey(BestVA, BestVB)
                };
            }
        }

        // Face / Island hit test via spatial candidate list
        const CheckFaceHit = (Fi, Isl) =>
        {
            const Corners = surface.faces[Fi].corners;
            const Poly = Corners.map(C => surface.uvCoords[C.uv]);
            if (!PointInPoly(U, V, Poly)) return null;
            let BestEdgeDist = Infinity;
            let NearVa = Corners[0].v, NearVb = Corners[1].v;
            let NearUa = Corners[0].uv, NearUb = Corners[1].uv;
            for (let k = 0; k < Corners.length; k++)
            {
                const C0 = Corners[k], C1 = Corners[(k + 1) % Corners.length];
                const A = surface.uvCoords[C0.uv], B = surface.uvCoords[C1.uv];
                const D = PointSegmentDist(U, V, A[0], A[1], B[0], B[1]);
                if (D < BestEdgeDist)
                {
                    BestEdgeDist = D;
                    NearVa = C0.v; NearVb = C1.v;
                    NearUa = C0.uv; NearUb = C1.uv;
                }
            }
            return {
                kind: mode === 'face' ? 'face' : 'island',
                faceIndex: Fi,
                islandId: Isl?.id ?? null,
                edgeVa: NearVa,
                edgeVb: NearVb,
                edgeKey3d: EdgeKey(NearVa, NearVb),
                uvA: NearUa,
                uvB: NearUb
            };
        };

        if (Candidates)
        {
            for (let i = 0; i < Candidates.length; i++)
            {
                const Fi = Candidates[i];
                if (HiddenFaces?.has(Fi)) continue;
                const Isl = IslandByFace.get(Fi);
                if (!Isl) continue;
                const Res = CheckFaceHit(Fi, Isl);
                if (Res) return Res;
            }
            return null;
        }

        for (const Isl of islands)
        {
            if (Isl.hidden) continue;
            if (U < Isl.bounds.minU - 0.01 || U > Isl.bounds.maxU + 0.01 ||
                V < Isl.bounds.minV - 0.01 || V > Isl.bounds.maxV + 0.01) continue;
            for (const Fi of Isl.faceIndices)
            {
                const Res = CheckFaceHit(Fi, Isl);
                if (Res) return Res;
            }
        }
        return null;
    }
}

function PointSegmentDist(Px, Py, Ax, Ay, Bx, By)
{
    const Dx = Bx - Ax, Dy = By - Ay;
    const Len2 = Dx * Dx + Dy * Dy;
    if (Len2 < 1e-12) return Math.hypot(Px - Ax, Py - Ay);
    const T = Math.max(0, Math.min(1, ((Px - Ax) * Dx + (Py - Ay) * Dy) / Len2));
    return Math.hypot(Px - (Ax + T * Dx), Py - (Ay + T * Dy));
}

function PointInPoly(U, V, Poly)
{
    let Inside = false;
    for (let i = 0, j = Poly.length - 1; i < Poly.length; j = i++)
    {
        const Xi = Poly[i][0], Yi = Poly[i][1];
        const Xj = Poly[j][0], Yj = Poly[j][1];
        const Intersect = ((Yi > V) !== (Yj > V)) &&
            (U < ((Xj - Xi) * (V - Yi)) / ((Yj - Yi) || 1e-12) + Xi);
        if (Intersect) Inside = !Inside;
    }
    return Inside;
}
