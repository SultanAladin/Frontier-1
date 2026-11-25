/*==========================================================================================================================================
                                                        WORLDMAP2DCANVAS.JS
==========================================================================================================================================*/
// Infinite Sparse-Tile 2D World Mask & Independent Terrain Bounds Box Editor
// Supports: Infinite Pan/Zoom, Multi-Scale Metric Grid, Cached 256x256 Sparse Mask Tiles, Shaded Topographic Relief Preview,
// Brush / Erase / Smooth / Polygon Landmass Drawing, Interactive Draw/Move/8-Handle Resize Terrain Bounds Boxes, Multi-Select, & 2D Splines.

import { MASK_TILE_RES, TILE_WORLD_METERS } from './WorldMask2D.js';
import { SampleCatmullRomPolyline, CreateTerrainLayer } from './TerrainLayerStack.js';

const HANDLE_NAMES = ['nw', 'n', 'ne', 'e', 'se', 's', 'sw', 'w'];

function HexToRgb(Hex)
{
    const Clean = (Hex || '#58b982').replace('#', '');
    const Val = parseInt(Clean.length === 3 ? Clean.split('').map(C => C + C).join('') : Clean, 16) || 0x58b982;
    return [(Val >> 16) & 255, (Val >> 8) & 255, Val & 255];
}

export class WorldMap2DCanvas
{
    constructor(Canvas, World, Callbacks = {})
    {
        this.canvas = Canvas;
        this.ctx = Canvas.getContext('2d', { alpha: false });
        this.world = World;
        this.cb = Callbacks;

        // Infinite world camera: center in world meters (camX, camZ) & zoom in CSS pixels per meter
        this.camX = -120.0;
        this.camZ = -80.0;
        this.pxPerMeter = 0.27; // ~3.7 km across a 1000px viewport

        // Active 2D tool: 'brush' | 'erase' | 'smooth' | 'polygon' | 'bounds' | 'select' | 'spline' | 'pan'
        this.tool = 'brush';
        this.brushRadiusM = 145.0;
        this.brushStrength = 1.0;
        this.coastRoughness = 0.28;
        this.brushSeed = 4029;
        this.showReliefIn2D = true;
        this.showFalloffIn2D = false;
        this.showGrid = true;

        this.activeMaskLayerId = World.maskLayers[0]?.id || null;
        this.selectedRegionIds = new Set(World.regions[0] ? [World.regions[0].id] : []);
        this.activeRegionId = World.regions[0]?.id || null;
        this.activeSplineLayerId = null;

        // Cached offscreen tile bitmaps: key = `${layerId}:${tileKey}`
        this.tileCanvases = new Map();
        // Cached offscreen relief bitmaps per region
        this.regionCanvases = new Map();

        // Pointer & gesture state
        this.pointerWorld = [0, 0];
        this.pointerScreen = [0, 0];
        this.pointerInside = false;
        this.spaceDown = false;
        this.dragState = null;
        this.polygonDraft = []; // [[wx, wz], ...]
        this.hoverHandle = null;
        this.needsRedraw = true;

        this._bindEvents();
    }

    /*======================================================================================================================================
                                                COORDINATE TRANSFORMS (SCREEN <-> WORLD METERS)
    ======================================================================================================================================*/
    worldToScreen(Wx, Wz)
    {
        const Rect = this.canvas.getBoundingClientRect();
        return [
            Rect.width * 0.5 + (Wx - this.camX) * this.pxPerMeter,
            Rect.height * 0.5 + (Wz - this.camZ) * this.pxPerMeter
        ];
    }

    screenToWorld(Sx, Sy)
    {
        const Rect = this.canvas.getBoundingClientRect();
        return [
            this.camX + (Sx - Rect.width * 0.5) / this.pxPerMeter,
            this.camZ + (Sy - Rect.height * 0.5) / this.pxPerMeter
        ];
    }

    frameBounds(Bounds, PaddingFactor = 1.28)
    {
        if (!Bounds) return;
        const Rect = this.canvas.getBoundingClientRect();
        const W = Math.max(320, Rect.width), H = Math.max(240, Rect.height);
        this.camX = Bounds.x + Bounds.width * 0.5;
        this.camZ = Bounds.z + Bounds.depth * 0.5;
        const ZoomX = W / Math.max(80.0, Bounds.width * PaddingFactor);
        const ZoomZ = H / Math.max(80.0, Bounds.depth * PaddingFactor);
        this.pxPerMeter = Math.max(0.03, Math.min(5.0, Math.min(ZoomX, ZoomZ)));
        this.requestRedraw();
    }

    frameAllRegions()
    {
        const Enabled = this.world.regions.filter(R => R.enabled);
        const List = Enabled.length ? Enabled : this.world.regions;
        if (!List.length)
        {
            this.camX = 0;
            this.camZ = 0;
            this.pxPerMeter = 0.25;
            this.requestRedraw();
            return;
        }
        let MinX = Infinity, MinZ = Infinity, MaxX = -Infinity, MaxZ = -Infinity;
        for (const R of List)
        {
            MinX = Math.min(MinX, R.bounds.x);
            MinZ = Math.min(MinZ, R.bounds.z);
            MaxX = Math.max(MaxX, R.bounds.x + R.bounds.width);
            MaxZ = Math.max(MaxZ, R.bounds.z + R.bounds.depth);
        }
        this.frameBounds({ x: MinX, z: MinZ, width: MaxX - MinX, depth: MaxZ - MinZ }, 1.32);
    }

    invalidateRegionPreview(RegionId = null)
    {
        if (RegionId) this.regionCanvases.delete(RegionId);
        else this.regionCanvases.clear();
        this.requestRedraw();
    }

    invalidateMaskCache()
    {
        this.tileCanvases.clear();
        this.regionCanvases.clear();
        this.requestRedraw();
    }

    requestRedraw()
    {
        this.needsRedraw = true;
    }

    /*======================================================================================================================================
                                                INTERACTIVE POINTER & KEYBOARD EVENTS
    ======================================================================================================================================*/
    _bindEvents()
    {
        const El = this.canvas;
        El.addEventListener('contextmenu', E => E.preventDefault());

        El.addEventListener('pointerdown', E =>
        {
            const Rect = El.getBoundingClientRect();
            const Sx = E.clientX - Rect.left, Sy = E.clientY - Rect.top;
            const [Wx, Wz] = this.screenToWorld(Sx, Sy);
            this.pointerScreen = [Sx, Sy];
            this.pointerWorld = [Wx, Wz];
            this.pointerInside = true;

            // Pan via Middle Button, Space+Drag, Alt+Drag, or explicit 'pan' tool
            if (E.button === 1 || this.spaceDown || E.altKey || this.tool === 'pan')
            {
                El.setPointerCapture(E.pointerId);
                this.dragState = {
                    kind: 'pan',
                    pointerId: E.pointerId,
                    startSx: Sx, startSy: Sy,
                    startCamX: this.camX, startCamZ: this.camZ
                };
                return;
            }

            // Polygon tool: click adds vertex; right-click or close-to-start finishes!
            if (this.tool === 'polygon')
            {
                if (E.button === 2)
                {
                    if (this.polygonDraft.length >= 3) this.commitPolygonDraft(true);
                    else this.polygonDraft = [];
                    this.requestRedraw();
                    return;
                }
                if (this.polygonDraft.length >= 3)
                {
                    const [Fsx, Fsy] = this.worldToScreen(this.polygonDraft[0][0], this.polygonDraft[0][1]);
                    if (Math.hypot(Sx - Fsx, Sy - Fsy) < 13)
                    {
                        this.commitPolygonDraft(E.shiftKey);
                        return;
                    }
                }
                this.polygonDraft.push([Wx, Wz]);
                this.requestRedraw();
                return;
            }

            // Spline Landform editing in 2D
            if (this.tool === 'spline')
            {
                El.setPointerCapture(E.pointerId);
                this._handleSplinePointerDown(Wx, Wz, Sx, Sy, E);
                return;
            }

            // Draw New Terrain Bounds Box
            if (this.tool === 'bounds')
            {
                El.setPointerCapture(E.pointerId);
                this.dragState = {
                    kind: 'create-bounds',
                    pointerId: E.pointerId,
                    startWx: Wx, startWz: Wz,
                    curWx: Wx, curWz: Wz
                };
                this.requestRedraw();
                return;
            }

            // Select / Move / Resize Terrain Bounds Box (also allow grabbing handles directly when in 'select' mode)
            if (this.tool === 'select')
            {
                const HandleHit = this._hitTestBoundsHandles(Sx, Sy);
                if (HandleHit)
                {
                    El.setPointerCapture(E.pointerId);
                    this._selectRegion(HandleHit.region.id, false);
                    this.dragState = {
                        kind: 'resize-bounds',
                        pointerId: E.pointerId,
                        region: HandleHit.region,
                        handle: HandleHit.handle,
                        origBounds: { ...HandleHit.region.bounds },
                        startWx: Wx, startWz: Wz
                    };
                    return;
                }

                const RegionHit = this._hitTestRegionBody(Wx, Wz);
                if (RegionHit)
                {
                    El.setPointerCapture(E.pointerId);
                    this._selectRegion(RegionHit.id, E.shiftKey || E.ctrlKey || E.metaKey);
                    this.dragState = {
                        kind: 'move-bounds',
                        pointerId: E.pointerId,
                        region: RegionHit,
                        origBounds: { ...RegionHit.bounds },
                        startWx: Wx, startWz: Wz
                    };
                    return;
                }

                // Clicked empty ocean in select mode -> pan
                El.setPointerCapture(E.pointerId);
                this.dragState = {
                    kind: 'pan',
                    pointerId: E.pointerId,
                    startSx: Sx, startSy: Sy,
                    startCamX: this.camX, startCamZ: this.camZ
                };
                return;
            }

            // Brush / Erase / Smooth 2D Mask Stroke
            if (this.tool === 'brush' || this.tool === 'erase' || this.tool === 'smooth')
            {
                const MaskLayer = this._getEditableMaskLayer();
                if (!MaskLayer || MaskLayer.locked) return;

                El.setPointerCapture(E.pointerId);
                const PaintMode = (this.tool === 'erase' || E.button === 2 || E.shiftKey)
                    ? 'erase'
                    : (this.tool === 'smooth' ? 'smooth' : 'paint');

                this.brushSeed = ((this.brushSeed * 1664525 + 1013904223) >>> 0) & 0x7fffffff;
                this.cb.onBeforeMaskEdit?.();
                this._applyMaskCapsule([Wx, Wz], [Wx, Wz], PaintMode);

                this.dragState = {
                    kind: 'paint-mask',
                    pointerId: E.pointerId,
                    mode: PaintMode,
                    lastW: [Wx, Wz]
                };
            }
        });

        El.addEventListener('pointermove', E =>
        {
            const Rect = El.getBoundingClientRect();
            const Sx = E.clientX - Rect.left, Sy = E.clientY - Rect.top;
            const [Wx, Wz] = this.screenToWorld(Sx, Sy);
            this.pointerScreen = [Sx, Sy];
            this.pointerWorld = [Wx, Wz];
            this.pointerInside = true;
            this.cb.onCursorMove?.(Wx, Wz);

            if (!this.dragState)
            {
                if (this.tool === 'select')
                {
                    const Hit = this._hitTestBoundsHandles(Sx, Sy);
                    this.hoverHandle = Hit ? `${Hit.region.id}:${Hit.handle}` : null;
                }
                else
                {
                    this.hoverHandle = null;
                }
                this.requestRedraw();
                return;
            }

            const D = this.dragState;
            if (D.kind === 'pan')
            {
                this.camX = D.startCamX - (Sx - D.startSx) / this.pxPerMeter;
                this.camZ = D.startCamZ - (Sy - D.startSy) / this.pxPerMeter;
                this.requestRedraw();
            }
            else if (D.kind === 'paint-mask')
            {
                const Dist = Math.hypot(Wx - D.lastW[0], Wz - D.lastW[1]);
                if (Dist >= Math.max(4.0, this.brushRadiusM * 0.14))
                {
                    this._applyMaskCapsule(D.lastW, [Wx, Wz], D.mode);
                    D.lastW = [Wx, Wz];
                }
                this.requestRedraw();
            }
            else if (D.kind === 'create-bounds')
            {
                D.curWx = Wx;
                D.curWz = Wz;
                this.requestRedraw();
            }
            else if (D.kind === 'move-bounds' && !D.region.locked)
            {
                const Dx = Wx - D.startWx, Dz = Wz - D.startWz;
                D.region.bounds.x = Math.round(D.origBounds.x + Dx);
                D.region.bounds.z = Math.round(D.origBounds.z + Dz);
                D.region.dirty = true;
                this.invalidateRegionPreview(D.region.id);
                this.cb.onRegionBoundsEdited?.(D.region, false);
            }
            else if (D.kind === 'resize-bounds' && !D.region.locked)
            {
                this._applyBoundsResize(D.region, D.origBounds, D.handle, Wx - D.startWx, Wz - D.startWz);
                D.region.dirty = true;
                this.invalidateRegionPreview(D.region.id);
                this.cb.onRegionBoundsEdited?.(D.region, false);
            }
            else if (D.kind === 'drag-spline')
            {
                D.layer.points[D.pointIndex][0] = Math.round(Wx);
                D.layer.points[D.pointIndex][1] = Math.round(Wz);
                D.region.dirty = true;
                this.invalidateRegionPreview(D.region.id);
                this.cb.onSplineEdited?.(D.region, D.layer, false);
            }
        });

        const EndDrag = E =>
        {
            if (!this.dragState) return;
            const D = this.dragState;
            this.dragState = null;
            if (El.hasPointerCapture?.(E.pointerId)) El.releasePointerCapture(E.pointerId);

            if (D.kind === 'paint-mask')
            {
                this.cb.onMaskStrokeFinished?.();
            }
            else if (D.kind === 'create-bounds')
            {
                const MinX = Math.min(D.startWx, D.curWx), MaxX = Math.max(D.startWx, D.curWx);
                const MinZ = Math.min(D.startWz, D.curWz), MaxZ = Math.max(D.startWz, D.curWz);
                const Width = Math.max(180.0, MaxX - MinX);
                const Depth = Math.max(180.0, MaxZ - MinZ);
                if (Math.hypot(D.curWx - D.startWx, D.curWz - D.startWz) > 25.0)
                {
                    this.cb.onCreateRegionFromBox?.({
                        x: Math.round(MinX),
                        z: Math.round(MinZ),
                        width: Math.round(Width),
                        depth: Math.round(Depth)
                    });
                }
            }
            else if (D.kind === 'move-bounds' || D.kind === 'resize-bounds')
            {
                this.cb.onRegionBoundsEdited?.(D.region, true);
            }
            else if (D.kind === 'drag-spline')
            {
                this.cb.onSplineEdited?.(D.region, D.layer, true);
            }
            this.requestRedraw();
        };

        El.addEventListener('pointerup', EndDrag);
        El.addEventListener('pointercancel', EndDrag);
        El.addEventListener('pointerleave', () =>
        {
            this.pointerInside = false;
            this.requestRedraw();
        });

        El.addEventListener('dblclick', E =>
        {
            if (this.tool === 'polygon' && this.polygonDraft.length >= 3)
            {
                this.commitPolygonDraft(E.shiftKey);
                return;
            }
            const Rect = El.getBoundingClientRect();
            const [Wx, Wz] = this.screenToWorld(E.clientX - Rect.left, E.clientY - Rect.top);
            const Hit = this._hitTestRegionBody(Wx, Wz);
            if (Hit && this.tool === 'select')
            {
                this._selectRegion(Hit.id, false);
                this.cb.onDoubleClickRegion?.(Hit);
            }
        });

        El.addEventListener('wheel', E =>
        {
            E.preventDefault();
            const Rect = El.getBoundingClientRect();
            const Sx = E.clientX - Rect.left, Sy = E.clientY - Rect.top;
            const [BeforeWx, BeforeWz] = this.screenToWorld(Sx, Sy);

            const Factor = Math.exp(-E.deltaY * 0.0014);
            this.pxPerMeter = Math.max(0.025, Math.min(6.5, this.pxPerMeter * Factor));

            const [AfterWx, AfterWz] = this.screenToWorld(Sx, Sy);
            this.camX += BeforeWx - AfterWx;
            this.camZ += BeforeWz - AfterWz;
            this.requestRedraw();
        }, { passive: false });
    }

    commitPolygonDraft(Erase = false)
    {
        const Layer = this._getEditableMaskLayer();
        if (Layer && !Layer.locked && this.polygonDraft.length >= 3)
        {
            this.cb.onBeforeMaskEdit?.();
            const Changed = this.world.paintPolygonMeters(Layer, this.polygonDraft, Erase, this.coastRoughness, this.brushSeed);
            this.polygonDraft = [];
            if (Changed.length > 0)
            {
                this.regionCanvases.clear();
                this.cb.onMaskStrokeFinished?.();
            }
        }
        else
        {
            this.polygonDraft = [];
        }
        this.requestRedraw();
    }

    _getEditableMaskLayer()
    {
        return this.world.getMaskLayer(this.activeMaskLayerId) || this.world.maskLayers[0] || null;
    }

    _applyMaskCapsule(P0, P1, Mode)
    {
        const Layer = this._getEditableMaskLayer();
        if (!Layer) return;
        this.world.paintCapsuleMeters(
            Layer,
            P0,
            P1,
            this.brushRadiusM,
            Mode,
            this.brushStrength,
            this.coastRoughness,
            this.brushSeed
        );
        // Invalidate relief previews for overlapping regions
        const Pad = this.brushRadiusM + 40.0;
        const MinX = Math.min(P0[0], P1[0]) - Pad, MaxX = Math.max(P0[0], P1[0]) + Pad;
        const MinZ = Math.min(P0[1], P1[1]) - Pad, MaxZ = Math.max(P0[1], P1[1]) + Pad;
        for (const R of this.world.regions)
        {
            const B = R.bounds;
            if (MaxX >= B.x && MinX <= B.x + B.width && MaxZ >= B.z && MinZ <= B.z + B.depth)
            {
                this.regionCanvases.delete(R.id);
            }
        }
        this.cb.onMaskLivePaint?.();
        this.requestRedraw();
    }

    _selectRegion(RegionId, ToggleMulti = false)
    {
        if (ToggleMulti)
        {
            if (this.selectedRegionIds.has(RegionId) && this.selectedRegionIds.size > 1)
            {
                this.selectedRegionIds.delete(RegionId);
                if (this.activeRegionId === RegionId)
                {
                    this.activeRegionId = Array.from(this.selectedRegionIds)[0];
                }
            }
            else
            {
                this.selectedRegionIds.add(RegionId);
                this.activeRegionId = RegionId;
            }
        }
        else
        {
            this.selectedRegionIds.clear();
            this.selectedRegionIds.add(RegionId);
            this.activeRegionId = RegionId;
        }
        this.cb.onRegionSelectionChanged?.(this.activeRegionId, Array.from(this.selectedRegionIds));
        this.requestRedraw();
    }

    _hitTestRegionBody(Wx, Wz)
    {
        for (let I = this.world.regions.length - 1; I >= 0; --I)
        {
            const R = this.world.regions[I];
            const B = R.bounds;
            if (Wx >= B.x && Wx <= B.x + B.width && Wz >= B.z && Wz <= B.z + B.depth)
            {
                return R;
            }
        }
        return null;
    }

    _getRegionHandleScreenPoints(Region)
    {
        const B = Region.bounds;
        const [X0, Y0] = this.worldToScreen(B.x, B.z);
        const [X1, Y1] = this.worldToScreen(B.x + B.width, B.z + B.depth);
        const Xm = 0.5 * (X0 + X1), Ym = 0.5 * (Y0 + Y1);
        return {
            nw: [X0, Y0], n: [Xm, Y0], ne: [X1, Y0],
            e:  [X1, Ym], se: [X1, Y1], s: [Xm, Y1],
            sw: [X0, Y1], w: [X0, Ym]
        };
    }

    _hitTestBoundsHandles(Sx, Sy)
    {
        for (let I = this.world.regions.length - 1; I >= 0; --I)
        {
            const R = this.world.regions[I];
            if (R.locked) continue;
            const Pts = this._getRegionHandleScreenPoints(R);
            for (const H of HANDLE_NAMES)
            {
                const [Hx, Hy] = Pts[H];
                if (Math.hypot(Sx - Hx, Sy - Hy) <= 9.5)
                {
                    return { region: R, handle: H };
                }
            }
        }
        return null;
    }

    _applyBoundsResize(Region, Orig, Handle, Dx, Dz)
    {
        let X0 = Orig.x, Z0 = Orig.z, X1 = Orig.x + Orig.width, Z1 = Orig.z + Orig.depth;
        const MinSpan = 160.0;
        if (Handle.includes('w')) X0 = Math.min(X1 - MinSpan, X0 + Dx);
        if (Handle.includes('e')) X1 = Math.max(X0 + MinSpan, X1 + Dx);
        if (Handle.includes('n')) Z0 = Math.min(Z1 - MinSpan, Z0 + Dz);
        if (Handle.includes('s')) Z1 = Math.max(Z0 + MinSpan, Z1 + Dz);
        Region.bounds.x = Math.round(X0);
        Region.bounds.z = Math.round(Z0);
        Region.bounds.width = Math.round(X1 - X0);
        Region.bounds.depth = Math.round(Z1 - Z0);
    }

    _handleSplinePointerDown(Wx, Wz, Sx, Sy, Event)
    {
        let Region = this.world.getRegion(this.activeRegionId) || this._hitTestRegionBody(Wx, Wz) || this.world.regions[0];
        if (!Region || Region.locked) return;
        if (Region.id !== this.activeRegionId) this._selectRegion(Region.id, false);

        // Find active spline layer or create one
        let SplineLayer = Region.stack.find(L => L.id === this.activeSplineLayerId && L.type === 'spline')
                       || Region.stack.find(L => L.type === 'spline');
        if (!SplineLayer)
        {
            SplineLayer = CreateTerrainLayer('spline', {
                name: '3D Graded Spline Landform',
                splineMode: 'grade',
                width: 105.0,
                amount: 32.0,
                falloff: { mode: 'none', distance: 100, edgeFloor: 1.0 },
                points: []
            });
            Region.stack.push(SplineLayer);
            this.activeSplineLayerId = SplineLayer.id;
            this.cb.onLayerStackChanged?.(Region, SplineLayer.id);
        }

        // Check if clicking an existing control point
        for (let I = 0; I < SplineLayer.points.length; ++I)
        {
            const [Px, Py] = this.worldToScreen(SplineLayer.points[I][0], SplineLayer.points[I][1]);
            if (Math.hypot(Sx - Px, Sy - Py) <= 11.0)
            {
                if (Event.button === 2 || Event.shiftKey)
                {
                    SplineLayer.points.splice(I, 1);
                    Region.dirty = true;
                    this.invalidateRegionPreview(Region.id);
                    this.cb.onSplineEdited?.(Region, SplineLayer, true, Math.max(0, I - 1));
                    return;
                }
                this.cb.onSplineEdited?.(Region, SplineLayer, false, I);
                this.dragState = {
                    kind: 'drag-spline',
                    pointerId: Event.pointerId,
                    region: Region,
                    layer: SplineLayer,
                    pointIndex: I
                };
                return;
            }
        }

        // Append new control point with sampled 3D elevation wy
        if (Event.button === 0)
        {
            let SampledY = (this.world.seaLevel ?? 0) + 18.0;
            const Rt = Region.runtime;
            if (Rt && Rt.hasLand)
            {
                const Ix = Math.max(0, Math.min(Rt.nx - 1, Math.round((Wx - Region.bounds.x) / Rt.stepX)));
                const Iz = Math.max(0, Math.min(Rt.nz - 1, Math.round((Wz - Region.bounds.z) / Rt.stepZ)));
                SampledY = Math.round(Rt.heights[Iz * Rt.nx + Ix] * 10) / 10;
            }
            SplineLayer.points.push([Math.round(Wx), Math.round(Wz), SampledY]);
            const NewIdx = SplineLayer.points.length - 1;
            Region.dirty = true;
            this.invalidateRegionPreview(Region.id);
            this.cb.onSplineEdited?.(Region, SplineLayer, true, NewIdx);
            this.dragState = {
                kind: 'drag-spline',
                pointerId: Event.pointerId,
                region: Region,
                layer: SplineLayer,
                pointIndex: NewIdx
            };
        }
    }

    /*======================================================================================================================================
                                      SPARSE TILE & TOPOGRAPHIC RELIEF BITMAP CACHING
    ======================================================================================================================================*/
    _getTileBitmap(Layer, TileKey, TileRecord)
    {
        const CacheKey = `${Layer.id}:${TileKey}`;
        let Entry = this.tileCanvases.get(CacheKey);
        if (!Entry)
        {
            const Off = document.createElement('canvas');
            Off.width = MASK_TILE_RES;
            Off.height = MASK_TILE_RES;
            Entry = { canvas: Off, ctx: Off.getContext('2d'), img: null, color: '' };
            Entry.img = Entry.ctx.createImageData(MASK_TILE_RES, MASK_TILE_RES);
            this.tileCanvases.set(CacheKey, Entry);
            TileRecord.dirty = true;
        }

        if (TileRecord.dirty || Entry.color !== Layer.color)
        {
            const [Cr, Cg, Cb] = HexToRgb(Layer.color);
            const Src = TileRecord.data;
            const Dst = Entry.img.data;

            for (let Z = 0; Z < MASK_TILE_RES; ++Z)
            {
                const Zm = Math.max(0, Z - 1), Zp = Math.min(MASK_TILE_RES - 1, Z + 1);
                for (let X = 0; X < MASK_TILE_RES; ++X)
                {
                    const I = Z * MASK_TILE_RES + X;
                    const Val = Src[I];
                    const P = I * 4;
                    if (Val === 0)
                    {
                        Dst[P + 3] = 0;
                        continue;
                    }
                    const Xm = Math.max(0, X - 1), Xp = Math.min(MASK_TILE_RES - 1, X + 1);
                    const MinN = Math.min(Src[Z * MASK_TILE_RES + Xm], Src[Z * MASK_TILE_RES + Xp], Src[Zm * MASK_TILE_RES + X], Src[Zp * MASK_TILE_RES + X]);
                    const IsCoast = Val >= 127 && MinN < 127;
                    if (IsCoast)
                    {
                        Dst[P] = Math.min(255, Cr + 75);
                        Dst[P + 1] = Math.min(255, Cg + 75);
                        Dst[P + 2] = Math.min(255, Cb + 60);
                        Dst[P + 3] = 240;
                    }
                    else
                    {
                        Dst[P] = Cr;
                        Dst[P + 1] = Cg;
                        Dst[P + 2] = Cb;
                        Dst[P + 3] = Math.round((Val / 255.0) * 145);
                    }
                }
            }
            Entry.ctx.putImageData(Entry.img, 0, 0);
            Entry.color = Layer.color;
            TileRecord.dirty = false;
        }
        return Entry.canvas;
    }

    _getRegionReliefBitmap(Region)
    {
        const Rt = Region.runtime;
        if (!Rt || !Rt.hasLand) return null;

        let Entry = this.regionCanvases.get(Region.id);
        const ModeTag = this.showFalloffIn2D ? 'falloff' : 'relief';
        if (Entry && Entry.nx === Rt.nx && Entry.nz === Rt.nz && Entry.mode === ModeTag)
        {
            return Entry.canvas;
        }

        const Off = Entry?.canvas || document.createElement('canvas');
        Off.width = Rt.nx;
        Off.height = Rt.nz;
        const Ctx = Off.getContext('2d');
        const Img = Ctx.createImageData(Rt.nx, Rt.nz);
        const Dst = Img.data;
        const Sea = this.world.seaLevel ?? 0.0;

        for (let Z = 0; Z < Rt.nz; ++Z)
        {
            const Zm = Math.max(0, Z - 1), Zp = Math.min(Rt.nz - 1, Z + 1);
            for (let X = 0; X < Rt.nx; ++X)
            {
                const I = Z * Rt.nx + X;
                const P = I * 4;
                const D2 = Rt.sdf2D[I];
                if (D2 > 0.0)
                {
                    // Subtle shallow coastal bathymetry halo right around shoreline
                    if (D2 < 55.0)
                    {
                        const Halo = (1.0 - D2 / 55.0) * 0.36;
                        Dst[P] = 44;
                        Dst[P + 1] = 148;
                        Dst[P + 2] = 178;
                        Dst[P + 3] = Math.round(Halo * 255);
                    }
                    else
                    {
                        Dst[P + 3] = 0;
                    }
                    continue;
                }

                const Xm = Math.max(0, X - 1), Xp = Math.min(Rt.nx - 1, X + 1);
                const DhDx = (Rt.heights[Z * Rt.nx + Xp] - Rt.heights[Z * Rt.nx + Xm]) / ((Xp - Xm || 1) * Rt.stepX);
                const DhDz = (Rt.heights[Zp * Rt.nx + X] - Rt.heights[Zm * Rt.nx + X]) / ((Zp - Zm || 1) * Rt.stepZ);
                const Shade = 0.5 + 0.5 * ((-DhDx * 0.62 - DhDz * 0.48 + 0.62) / Math.hypot(DhDx, DhDz, 1.0));

                if (this.showFalloffIn2D)
                {
                    // Heatmap of active layer's Boundary Falloff: cool cyan at soft edge -> warm gold/coral at strong center!
                    const W = Rt.falloffMap[I];
                    Dst[P] = Math.round((38 + 212 * W) * (0.72 + 0.35 * Shade));
                    Dst[P + 1] = Math.round((95 + 105 * Math.sin(W * Math.PI)) * (0.72 + 0.35 * Shade));
                    Dst[P + 2] = Math.round((195 * (1.0 - W) + 55 * W) * (0.72 + 0.35 * Shade));
                    Dst[P + 3] = 235;
                    continue;
                }

                const H = Rt.heights[I] - Sea;
                const Contour = (Math.abs(((H % 30.0) + 30.0) % 30.0) < 1.6) ? 0.84 : 1.0;
                const Lit = (0.58 + 0.52 * Shade) * Contour;

                let R, G, B;
                if (Rt.waterChannel[I] > 0.16)
                {
                    R = 46; G = 128; B = 152;
                }
                else if (H < 8.0)
                {
                    R = 148; G = 138; B = 104;
                }
                else if (H < 75.0)
                {
                    R = 76; G = 104; B = 68;
                }
                else if (H < 165.0)
                {
                    R = 116; G = 106; B = 78;
                }
                else if (H < 225.0)
                {
                    R = 132; G = 128; B = 124;
                }
                else
                {
                    R = 205; G = 208; B = 214;
                }

                // Crisp bright coastline pixel
                if (D2 > -Math.max(Rt.stepX, Rt.stepZ) * 1.15)
                {
                    R = 232; G = 212; B = 158;
                }

                Dst[P] = Math.min(255, Math.round(R * Lit));
                Dst[P + 1] = Math.min(255, Math.round(G * Lit));
                Dst[P + 2] = Math.min(255, Math.round(B * Lit));
                Dst[P + 3] = 238;
            }
        }

        Ctx.putImageData(Img, 0, 0);
        this.regionCanvases.set(Region.id, { canvas: Off, nx: Rt.nx, nz: Rt.nz, mode: ModeTag });
        return Off;
    }

    /*======================================================================================================================================
                                                      MAIN 2D CANVAS RENDER PASS
    ======================================================================================================================================*/
    render()
    {
        const El = this.canvas;
        const Rect = El.getBoundingClientRect();
        if (Rect.width < 4 || Rect.height < 4) return;

        const Dpr = Math.min(window.devicePixelRatio || 1, 2);
        const TargetW = Math.round(Rect.width * Dpr);
        const TargetH = Math.round(Rect.height * Dpr);
        if (El.width !== TargetW || El.height !== TargetH)
        {
            El.width = TargetW;
            El.height = TargetH;
            this.needsRedraw = true;
        }
        if (!this.needsRedraw) return;
        this.needsRedraw = false;

        const Ctx = this.ctx;
        Ctx.save();
        Ctx.scale(Dpr, Dpr);

        const W = Rect.width, H = Rect.height;

        // 1. Bathymetric Obsidian Ocean Backdrop (matches 3D Clay Studio dark shell)
        const Grad = Ctx.createRadialGradient(W * 0.5, H * 0.45, 20, W * 0.5, H * 0.5, Math.max(W, H) * 0.78);
        Grad.addColorStop(0.0, '#0e161d');
        Grad.addColorStop(0.65, '#0a0f14');
        Grad.addColorStop(1.0, '#070a0e');
        Ctx.fillStyle = Grad;
        Ctx.fillRect(0, 0, W, H);

        const [ViewMinX, ViewMinZ] = this.screenToWorld(0, 0);
        const [ViewMaxX, ViewMaxZ] = this.screenToWorld(W, H);

        // 2. Infinite Multi-Scale Metric World Grid (100m / 500m / 1km / 5km)
        if (this.showGrid)
        {
            this._drawMetricGrid(Ctx, W, H, ViewMinX, ViewMinZ, ViewMaxX, ViewMaxZ);
        }

        // 3. Sparse 2D Mask Tiles across the visible infinite viewport
        const MinTx = Math.floor(ViewMinX / TILE_WORLD_METERS);
        const MaxTx = Math.floor(ViewMaxX / TILE_WORLD_METERS);
        const MinTz = Math.floor(ViewMinZ / TILE_WORLD_METERS);
        const MaxTz = Math.floor(ViewMaxZ / TILE_WORLD_METERS);

        Ctx.imageSmoothingEnabled = this.pxPerMeter < 1.2;
        for (const Layer of this.world.maskLayers)
        {
            if (!Layer.visible) continue;
            Ctx.globalAlpha = Layer.opacity ?? 1.0;
            for (let Tz = MinTz; Tz <= MaxTz; ++Tz)
            {
                for (let Tx = MinTx; Tx <= MaxTx; ++Tx)
                {
                    const Key = `${Tx},${Tz}`;
                    const Tile = Layer.tiles.get(Key);
                    if (!Tile) continue;

                    const Bitmap = this._getTileBitmap(Layer, Key, Tile);
                    const [Sx0, Sy0] = this.worldToScreen(Tx * TILE_WORLD_METERS, Tz * TILE_WORLD_METERS);
                    const SpanPx = TILE_WORLD_METERS * this.pxPerMeter;
                    Ctx.drawImage(Bitmap, Sx0, Sy0, SpanPx + 0.4, SpanPx + 0.4);
                }
            }
        }
        Ctx.globalAlpha = 1.0;

        // 4. Live Shaded Topographic Relief / Falloff Heatmap inside Enabled Terrain Bounds Boxes
        if (this.showReliefIn2D || this.showFalloffIn2D)
        {
            for (const Region of this.world.regions)
            {
                if (!Region.enabled) continue;
                const B = Region.bounds;
                if (B.x + B.width < ViewMinX || B.x > ViewMaxX || B.z + B.depth < ViewMinZ || B.z > ViewMaxZ) continue;

                const ReliefBmp = this._getRegionReliefBitmap(Region);
                if (!ReliefBmp) continue;

                const [Rx0, Ry0] = this.worldToScreen(B.x, B.z);
                const Rw = B.width * this.pxPerMeter;
                const Rh = B.depth * this.pxPerMeter;
                Ctx.save();
                Ctx.globalAlpha = 0.88;
                Ctx.drawImage(ReliefBmp, Rx0, Ry0, Rw, Rh);
                Ctx.restore();
            }
        }

        // 5. Terrain Bounds Boxes, Selection Badges, Spline Curves & Resize Handles
        for (const Region of this.world.regions)
        {
            this._drawRegionBoundsOverlay(Ctx, Region);
        }

        // 6. Active Bounds Creation Marquee
        if (this.dragState?.kind === 'create-bounds')
        {
            const D = this.dragState;
            const [Ax, Ay] = this.worldToScreen(Math.min(D.startWx, D.curWx), Math.min(D.startWz, D.curWz));
            const [Bx, By] = this.worldToScreen(Math.max(D.startWx, D.curWx), Math.max(D.startWz, D.curWz));
            Ctx.save();
            Ctx.fillStyle = 'rgba(232, 194, 122, 0.12)';
            Ctx.strokeStyle = '#e8c27a';
            Ctx.lineWidth = 1.75;
            Ctx.setLineDash([6, 4]);
            Ctx.fillRect(Ax, Ay, Bx - Ax, By - Ay);
            Ctx.strokeRect(Ax, Ay, Bx - Ax, By - Ay);
            Ctx.setLineDash([]);

            const Wm = Math.abs(D.curWx - D.startWx), Dm = Math.abs(D.curWz - D.startWz);
            Ctx.font = '600 11px "DM Sans", sans-serif';
            Ctx.fillStyle = '#e8c27a';
            Ctx.fillText(`New Terrain Bounds · ${(Wm / 1000).toFixed(2)} × ${(Dm / 1000).toFixed(2)} km`, Ax + 8, Ay - 7);
            Ctx.restore();
        }

        // 7. Polygon Continent Draft
        if (this.polygonDraft.length > 0)
        {
            this._drawPolygonDraft(Ctx);
        }

        // 8. Live Metric Brush Cursor Ring
        if (this.pointerInside && (this.tool === 'brush' || this.tool === 'erase' || this.tool === 'smooth'))
        {
            const [Cx, Cy] = this.pointerScreen;
            const RadPx = Math.max(3.0, this.brushRadiusM * this.pxPerMeter);
            Ctx.save();
            Ctx.beginPath();
            Ctx.arc(Cx, Cy, RadPx, 0, Math.PI * 2);
            Ctx.strokeStyle = this.tool === 'erase' ? '#ff7b7b' : (this.tool === 'smooth' ? '#5ac8fa' : '#e8c27a');
            Ctx.lineWidth = 1.6;
            Ctx.stroke();

            Ctx.beginPath();
            Ctx.arc(Cx, Cy, RadPx * 0.25, 0, Math.PI * 2);
            Ctx.strokeStyle = 'rgba(255,255,255,0.28)';
            Ctx.lineWidth = 1.0;
            Ctx.stroke();
            Ctx.restore();
        }

        // 9. Bottom-Right Cartographic Scale Bar
        this._drawScaleBar(Ctx, W, H);

        Ctx.restore();
    }

    _drawMetricGrid(Ctx, W, H, ViewMinX, ViewMinZ, ViewMaxX, ViewMaxZ)
    {
        // Choose crisp major step in meters so major lines sit ~90..240 px apart
        const Candidates = [100, 250, 500, 1000, 2000, 5000, 10000, 25000];
        let MajorM = 1000;
        for (const Step of Candidates)
        {
            if (Step * this.pxPerMeter >= 95) { MajorM = Step; break; }
        }
        const MinorM = MajorM / 5;

        Ctx.save();
        // Minor grid
        if (MinorM * this.pxPerMeter >= 16)
        {
            Ctx.strokeStyle = 'rgba(135, 185, 210, 0.055)';
            Ctx.lineWidth = 1;
            Ctx.beginPath();
            for (let X = Math.floor(ViewMinX / MinorM) * MinorM; X <= ViewMaxX; X += MinorM)
            {
                const [Sx] = this.worldToScreen(X, 0);
                Ctx.moveTo(Math.round(Sx) + 0.5, 0);
                Ctx.lineTo(Math.round(Sx) + 0.5, H);
            }
            for (let Z = Math.floor(ViewMinZ / MinorM) * MinorM; Z <= ViewMaxZ; Z += MinorM)
            {
                const [, Sy] = this.worldToScreen(0, Z);
                Ctx.moveTo(0, Math.round(Sy) + 0.5);
                Ctx.lineTo(W, Math.round(Sy) + 0.5);
            }
            Ctx.stroke();
        }

        // Major kilometer grid + coordinate labels
        Ctx.strokeStyle = 'rgba(145, 198, 224, 0.13)';
        Ctx.lineWidth = 1;
        Ctx.font = '500 10px "DM Sans", sans-serif';
        Ctx.fillStyle = 'rgba(165, 202, 220, 0.42)';
        Ctx.beginPath();
        for (let X = Math.floor(ViewMinX / MajorM) * MajorM; X <= ViewMaxX; X += MajorM)
        {
            const [Sx] = this.worldToScreen(X, 0);
            Ctx.moveTo(Math.round(Sx) + 0.5, 0);
            Ctx.lineTo(Math.round(Sx) + 0.5, H);
            const Label = Math.abs(X) >= 1000 ? `${(X / 1000).toFixed(X % 1000 === 0 ? 0 : 1)} km` : `${X} m`;
            Ctx.fillText(Label, Sx + 5, 15);
        }
        for (let Z = Math.floor(ViewMinZ / MajorM) * MajorM; Z <= ViewMaxZ; Z += MajorM)
        {
            const [, Sy] = this.worldToScreen(0, Z);
            Ctx.moveTo(0, Math.round(Sy) + 0.5);
            Ctx.lineTo(W, Math.round(Sy) + 0.5);
            const Label = Math.abs(Z) >= 1000 ? `${(Z / 1000).toFixed(Z % 1000 === 0 ? 0 : 1)} km` : `${Z} m`;
            Ctx.fillText(Label, 6, Sy - 4);
        }
        Ctx.stroke();

        // World Origin Axes (0, 0)
        const [Ox, Oy] = this.worldToScreen(0, 0);
        Ctx.strokeStyle = 'rgba(232, 194, 122, 0.24)';
        Ctx.beginPath();
        if (Ox >= 0 && Ox <= W) { Ctx.moveTo(Ox, 0); Ctx.lineTo(Ox, H); }
        if (Oy >= 0 && Oy <= H) { Ctx.moveTo(0, Oy); Ctx.lineTo(W, Oy); }
        Ctx.stroke();
        Ctx.restore();
    }

    _drawRegionBoundsOverlay(Ctx, Region)
    {
        const B = Region.bounds;
        const [X0, Y0] = this.worldToScreen(B.x, B.z);
        const [X1, Y1] = this.worldToScreen(B.x + B.width, B.z + B.depth);
        const Rw = X1 - X0, Rh = Y1 - Y0;

        const IsPrimary = Region.id === this.activeRegionId;
        const IsSelected = this.selectedRegionIds.has(Region.id);
        const Accent = Region.color || '#e8c27a';

        Ctx.save();

        // Subtle tint for disabled vs selected region
        if (!Region.enabled)
        {
            Ctx.fillStyle = 'rgba(12, 18, 24, 0.45)';
            Ctx.fillRect(X0, Y0, Rw, Rh);
        }
        else if (IsSelected)
        {
            Ctx.fillStyle = 'rgba(232, 194, 122, 0.045)';
            Ctx.fillRect(X0, Y0, Rw, Rh);
        }

        // Frame border
        Ctx.strokeStyle = !Region.enabled ? 'rgba(160, 170, 182, 0.38)' : (IsPrimary ? '#e8c27a' : (IsSelected ? '#5ac8fa' : Accent));
        Ctx.lineWidth = IsPrimary ? 2.1 : (IsSelected ? 1.7 : 1.25);
        if (!Region.enabled) Ctx.setLineDash([6, 5]);
        else if (!IsSelected) Ctx.setLineDash([8, 4]);
        Ctx.strokeRect(X0, Y0, Rw, Rh);
        Ctx.setLineDash([]);

        // Corner L-brackets on primary/selected bounds
        const CornerLen = Math.min(14, Math.min(Rw, Rh) * 0.2);
        if (IsSelected && CornerLen > 4)
        {
            Ctx.strokeStyle = IsPrimary ? '#fff1d0' : '#9be1ff';
            Ctx.lineWidth = 2.4;
            Ctx.beginPath();
            // NW
            Ctx.moveTo(X0, Y0 + CornerLen); Ctx.lineTo(X0, Y0); Ctx.lineTo(X0 + CornerLen, Y0);
            // NE
            Ctx.moveTo(X1 - CornerLen, Y0); Ctx.lineTo(X1, Y0); Ctx.lineTo(X1, Y0 + CornerLen);
            // SE
            Ctx.moveTo(X1, Y1 - CornerLen); Ctx.lineTo(X1, Y1); Ctx.lineTo(X1 - CornerLen, Y1);
            // SW
            Ctx.moveTo(X0 + CornerLen, Y1); Ctx.lineTo(X0, Y1); Ctx.lineTo(X0, Y1 - CornerLen);
            Ctx.stroke();
        }

        // Draw Spline Landform overlays inside this region
        for (const Layer of Region.stack)
        {
            if (Layer.type !== 'spline' || !Layer.enabled || !Layer.points || Layer.points.length === 0) continue;
            if (Layer.points.length >= 2)
            {
                const Poly = SampleCatmullRomPolyline(Layer.points, 7);
                Ctx.beginPath();
                Poly.forEach((Pt, Idx) =>
                {
                    const [Px, Py] = this.worldToScreen(Pt[0], Pt[1]);
                    if (Idx === 0) Ctx.moveTo(Px, Py);
                    else Ctx.lineTo(Px, Py);
                });
                Ctx.strokeStyle = Layer.splineMode === 'ridge' ? '#f0935b' : (Layer.splineMode === 'canyon' ? '#ff7b7b' : '#5ac8fa');
                Ctx.lineWidth = this.tool === 'spline' && IsPrimary ? 2.5 : 1.7;
                Ctx.stroke();
            }

            if (this.tool === 'spline' && IsPrimary)
            {
                for (let I = 0; I < Layer.points.length; ++I)
                {
                    const [Px, Py] = this.worldToScreen(Layer.points[I][0], Layer.points[I][1]);
                    Ctx.beginPath();
                    Ctx.arc(Px, Py, 5.2, 0, Math.PI * 2);
                    Ctx.fillStyle = '#141a20';
                    Ctx.fill();
                    Ctx.strokeStyle = '#3ec46d';
                    Ctx.lineWidth = 2.0;
                    Ctx.stroke();
                }
            }
        }

        // Header pill label on top-left of Terrain Bounds Box
        const DimText = `${(B.width / 1000).toFixed(2)}×${(B.depth / 1000).toFixed(2)} km`;
        const StatusPrefix = Region.enabled ? '● ' : '○ OFF · ';
        const Label = `${StatusPrefix}${Region.name}  (${DimText})`;
        Ctx.font = '600 11px "DM Sans", sans-serif';
        const TextW = Ctx.measureText(Label).width;
        const PillH = 20;
        const PillY = Math.max(22, Y0) - PillH - 3;

        Ctx.fillStyle = IsPrimary ? 'rgba(32, 27, 18, 0.92)' : 'rgba(14, 20, 26, 0.86)';
        Ctx.strokeStyle = IsPrimary ? 'rgba(232, 194, 122, 0.65)' : 'rgba(255, 255, 255, 0.14)';
        Ctx.lineWidth = 1;
        Ctx.beginPath();
        Ctx.roundRect(X0, PillY, TextW + 16, PillH, 5);
        Ctx.fill();
        Ctx.stroke();

        Ctx.fillStyle = !Region.enabled ? '#86919c' : (IsPrimary ? '#f3d497' : '#d8dee6');
        Ctx.fillText(Label, X0 + 8, PillY + 14);

        // 8 Resize Handles when in Select/Bounds mode and selected
        if (this.tool === 'select' && IsSelected && !Region.locked)
        {
            const Handles = this._getRegionHandleScreenPoints(Region);
            for (const H of HANDLE_NAMES)
            {
                const [Hx, Hy] = Handles[H];
                const Hovered = this.hoverHandle === `${Region.id}:${H}`;
                Ctx.fillStyle = Hovered ? '#fff4d6' : '#161c22';
                Ctx.strokeStyle = '#e8c27a';
                Ctx.lineWidth = 1.6;
                Ctx.beginPath();
                Ctx.rect(Hx - 4.2, Hy - 4.2, 8.4, 8.4);
                Ctx.fill();
                Ctx.stroke();
            }
        }

        Ctx.restore();
    }

    _drawPolygonDraft(Ctx)
    {
        Ctx.save();
        Ctx.beginPath();
        this.polygonDraft.forEach((Pt, I) =>
        {
            const [Sx, Sy] = this.worldToScreen(Pt[0], Pt[1]);
            if (I === 0) Ctx.moveTo(Sx, Sy);
            else Ctx.lineTo(Sx, Sy);
        });
        if (this.pointerInside)
        {
            Ctx.lineTo(this.pointerScreen[0], this.pointerScreen[1]);
        }
        Ctx.strokeStyle = '#e8c27a';
        Ctx.lineWidth = 1.8;
        Ctx.setLineDash([5, 4]);
        Ctx.stroke();
        Ctx.setLineDash([]);

        for (let I = 0; I < this.polygonDraft.length; ++I)
        {
            const [Sx, Sy] = this.worldToScreen(this.polygonDraft[I][0], this.polygonDraft[I][1]);
            Ctx.beginPath();
            Ctx.arc(Sx, Sy, I === 0 ? 5.5 : 4.0, 0, Math.PI * 2);
            Ctx.fillStyle = I === 0 ? '#3ec46d' : '#e8c27a';
            Ctx.fill();
        }
        Ctx.restore();
    }

    _drawScaleBar(Ctx, W, H)
    {
        const Steps = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
        let BarM = 500;
        for (const S of Steps)
        {
            if (S * this.pxPerMeter >= 70 && S * this.pxPerMeter <= 190) { BarM = S; break; }
        }
        const BarPx = BarM * this.pxPerMeter;
        const X1 = W - 18, X0 = X1 - BarPx, Y = H - 16;

        Ctx.save();
        Ctx.fillStyle = 'rgba(10, 15, 20, 0.78)';
        Ctx.strokeStyle = 'rgba(255, 255, 255, 0.12)';
        Ctx.beginPath();
        Ctx.roundRect(X0 - 10, Y - 20, BarPx + 20, 28, 6);
        Ctx.fill();
        Ctx.stroke();

        Ctx.strokeStyle = '#e8c27a';
        Ctx.lineWidth = 2;
        Ctx.beginPath();
        Ctx.moveTo(X0, Y - 4);
        Ctx.lineTo(X0, Y + 1);
        Ctx.lineTo(X1, Y + 1);
        Ctx.lineTo(X1, Y - 4);
        Ctx.stroke();

        const Label = BarM >= 1000 ? `${(BarM / 1000).toFixed(BarM % 1000 === 0 ? 0 : 1)} km` : `${BarM} m`;
        Ctx.font = '600 10.5px "DM Sans", sans-serif';
        Ctx.fillStyle = '#e5ebf2';
        Ctx.textAlign = 'center';
        Ctx.fillText(Label, 0.5 * (X0 + X1), Y - 6);
        Ctx.restore();
    }
}
