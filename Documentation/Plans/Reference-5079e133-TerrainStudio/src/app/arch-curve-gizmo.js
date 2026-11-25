/**
 * Interactive Multi-Spline 3D Rig & RGB XYZ Translation Gizmo Overlay (`ArchCurveGizmoOverlay`).
 *
 * Supports:
 * - Multi-spline geological rigs across all 25 Rock & Cliff Studio presets:
 *   - 3-Spline Canyon Rigs (`Left Wall` · `Riverbed / Thalweg` · `Right Wall`)
 *   - Cliff / Escarpment / Yardang / Dome Spine Rigs
 *   - Multi-Span Arch Rigs (+ Branch Arches)
 *   - Spire / Hoodoo / Monolith Stem Rigs
 * - Click-to-Place 3D Control Points (`placeMode`) on Studio Ground
 * - Clicking any Control Point (`P1..Pn`) on ANY spline to select that spline and attach the
 *   3D World-Space RGB XYZ Axis Arrow Gizmo
 * - Exact 3D Ray-to-Axis closest-point dragging along X (Red), Y (Green), Z (Blue), or View Plane
 * - Per-point Scale / Width (`r`) control + Mid-segment `+` subdivision + Role Labels!
 */

import { DEFAULT_CUSTOM_ARCH_CURVES, evalCatmullRom3D } from '../geology/custom-arch-spline.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

const FALLBACK_COLORS = ['#6bc2ff', '#ff7b54', '#4ce08a', '#c084fc', '#fbbf24'];

function cloneCurves(curves) {
  if (!Array.isArray(curves)) return [];
  return curves.map((pts) =>
    Array.isArray(pts)
      ? pts.map((p) => ({
          x: Number(p.x) || 0,
          y: Number(p.y) || 0,
          z: Number(p.z) || 0,
          r: Number.isFinite(Number(p.r)) ? Number(p.r) : 1.0,
        }))
      : []
  );
}

export class ArchCurveGizmoOverlay {
  /**
   * @param {HTMLElement} stageEl - Parent `.viewport-stage` container
   * @param {Object} callbacks
   * @param {() => Object|null} callbacks.getCameraAdapter - Returns current camera matrices & raycaster
   * @param {(curves: Array) => void} callbacks.onCurveChanged - Fired live during drag
   * @param {(curves: Array) => void} callbacks.onCurveCommitted - Fired on pointerup / point add / delete
   * @param {(sel: Object) => void} [callbacks.onSelectionChanged] - Fired when active spline or point changes
   */
  constructor(stageEl, { getCameraAdapter, onCurveChanged, onCurveCommitted, onSelectionChanged }) {
    this.stageEl = stageEl;
    this.getCameraAdapter = getCameraAdapter;
    this.onCurveChanged = onCurveChanged;
    this.onCurveCommitted = onCurveCommitted;
    this.onSelectionChanged = onSelectionChanged || null;

    this.enabled = false;
    this.placeMode = false;
    this.rigSpec = {
      family: 'arch',
      modeLabel: 'Arch Span Splines',
      canAddBranch: true,
      roles: [{ id: 'arch-main', label: 'Main Arch Span', color: '#6bc2ff' }],
    };
    this.curves = cloneCurves(DEFAULT_CUSTOM_ARCH_CURVES);
    this.selectedCurveIdx = 0;
    this.selectedPointIdx = 2;

    this._dragState = null;
    this._raf = 0;

    this._buildDom();
    this._bindPointerEvents();
  }

  _buildDom() {
    const svg = document.createElementNS(SVG_NS, 'svg');
    svg.setAttribute('class', 'arch-gizmo-svg hidden-panel');
    svg.style.position = 'absolute';
    svg.style.inset = '0';
    svg.style.width = '100%';
    svg.style.height = '100%';
    svg.style.zIndex = '14';
    svg.style.pointerEvents = 'none';

    this.gSplines = document.createElementNS(SVG_NS, 'g');
    this.gMidHandles = document.createElementNS(SVG_NS, 'g');
    this.gGizmoAxes = document.createElementNS(SVG_NS, 'g');
    this.gPoints = document.createElementNS(SVG_NS, 'g');

    svg.appendChild(this.gSplines);
    svg.appendChild(this.gMidHandles);
    svg.appendChild(this.gGizmoAxes);
    svg.appendChild(this.gPoints);

    this.stageEl.appendChild(svg);
    this.svg = svg;
  }

  setEnabled(flag) {
    this.enabled = Boolean(flag);
    this.svg.classList.toggle('hidden-panel', !this.enabled);
    this.svg.style.pointerEvents = this.enabled && this.placeMode ? 'auto' : 'none';
    if (this.enabled) {
      this._startRenderLoop();
    } else {
      this.placeMode = false;
      this.svg.style.cursor = 'default';
      if (this._raf) {
        cancelAnimationFrame(this._raf);
        this._raf = 0;
      }
    }
  }

  setPlaceMode(flag) {
    this.placeMode = Boolean(flag);
    if (this.enabled) {
      this.svg.style.pointerEvents = this.placeMode ? 'auto' : 'none';
      this.svg.style.cursor = this.placeMode ? 'crosshair' : 'default';
    }
    this._notifySelection();
  }

  setSplineRig(spec, curvesInput = null, triggerCommit = false) {
    this.rigSpec = spec || {
      family: 'cliff',
      modeLabel: 'Spine Spline',
      canAddBranch: false,
      roles: [{ id: 'spine', label: 'Formation Spine', color: '#6bc2ff' }],
    };
    const nextCurves = cloneCurves(curvesInput || spec?.defaultCurves || DEFAULT_CUSTOM_ARCH_CURVES);
    this.curves = nextCurves;

    // Default active spline: middle riverbed/stem for multi-curve rigs, or curve 0
    if (this.selectedCurveIdx >= this.curves.length) {
      this.selectedCurveIdx = this.rigSpec.family === 'canyon' && this.curves.length >= 3 ? 1 : 0;
    }
    const activePts = this.curves[this.selectedCurveIdx] || [];
    this.selectedPointIdx = activePts.length > 0 ? Math.floor(activePts.length / 2) : -1;
    this._notifySelection();

    if (triggerCommit && this.onCurveCommitted) {
      this.onCurveCommitted(this.getCurves());
    }
  }

  setCurves(curves, triggerCommit = true) {
    this.curves = cloneCurves(curves);
    this.selectedCurveIdx = Math.min(this.selectedCurveIdx, Math.max(0, this.curves.length - 1));
    const c = this.curves[this.selectedCurveIdx] || [];
    this.selectedPointIdx = c.length > 0 ? Math.min(Math.max(0, this.selectedPointIdx), c.length - 1) : -1;
    this._notifySelection();
    if (triggerCommit && this.onCurveCommitted) {
      this.onCurveCommitted(this.getCurves());
    }
  }

  selectCurve(curveIdx) {
    if (curveIdx < 0 || curveIdx >= this.curves.length) return;
    this.selectedCurveIdx = curveIdx;
    const pts = this.curves[curveIdx] || [];
    if (this.selectedPointIdx < 0 || this.selectedPointIdx >= pts.length) {
      this.selectedPointIdx = pts.length > 0 ? Math.floor(pts.length / 2) : -1;
    }
    this._notifySelection();
  }

  getCurveRole(cIdx = this.selectedCurveIdx) {
    const roles = this.rigSpec?.roles || [];
    if (roles[cIdx]) return roles[cIdx];
    return {
      id: `branch-${cIdx + 1}`,
      label: `Branch ${cIdx + 1}`,
      color: FALLBACK_COLORS[cIdx % FALLBACK_COLORS.length],
    };
  }

  getCurves() {
    return cloneCurves(this.curves);
  }

  getSelectedPoint() {
    const c = this.curves[this.selectedCurveIdx];
    if (!c) return null;
    return c[this.selectedPointIdx] || null;
  }

  _notifySelection() {
    if (this.onSelectionChanged) {
      this.onSelectionChanged({
        curveIdx: this.selectedCurveIdx,
        pointIdx: this.selectedPointIdx,
        role: this.getCurveRole(this.selectedCurveIdx),
        point: this.getSelectedPoint(),
        placeMode: this.placeMode,
      });
    }
  }

  setSelectedPointRadius(newR, commit = false) {
    const pt = this.getSelectedPoint();
    if (!pt) return;
    pt.r = Math.max(0.4, Math.min(2.4, Number(newR) || 1.0));
    if (this.onCurveChanged) this.onCurveChanged(this.getCurves());
    if (commit && this.onCurveCommitted) this.onCurveCommitted(this.getCurves());
  }

  /**
   * Extends or subdivides the currently active spline with a new control point.
   */
  addPointToActiveCurve() {
    const pts = this.curves[this.selectedCurveIdx];
    if (!pts || pts.length === 0) {
      if (pts) {
        pts.push({ x: -4, y: 4, z: 0, r: 1.0 }, { x: 4, y: 4, z: 0, r: 1.0 });
        this.selectedPointIdx = 1;
      }
    } else if (pts.length === 1) {
      const p0 = pts[0];
      pts.push({ x: Number((p0.x + 4).toFixed(2)), y: p0.y, z: p0.z, r: p0.r ?? 1.0 });
      this.selectedPointIdx = 1;
    } else if (this.selectedPointIdx >= 0 && this.selectedPointIdx < pts.length - 1) {
      const seg = this.selectedPointIdx;
      const tMid = (seg + 0.5) / (pts.length - 1);
      const mid3 = evalCatmullRom3D(pts, tMid);
      pts.splice(seg + 1, 0, {
        x: Number(mid3.x.toFixed(2)),
        y: Number(mid3.y.toFixed(2)),
        z: Number(mid3.z.toFixed(2)),
        r: Number((mid3.r ?? 1.0).toFixed(2)),
      });
      this.selectedPointIdx = seg + 1;
    } else {
      // Extrapolate smoothly past the tail of the spline!
      const pLast = pts[pts.length - 1];
      const pPrev = pts[pts.length - 2];
      const dx = pLast.x - pPrev.x;
      const dy = pLast.y - pPrev.y;
      const dz = pLast.z - pPrev.z;
      pts.push({
        x: Number((pLast.x + dx * 0.65).toFixed(2)),
        y: Number(Math.max(0, pLast.y + dy * 0.35).toFixed(2)),
        z: Number((pLast.z + dz * 0.65).toFixed(2)),
        r: pLast.r ?? 1.0,
      });
      this.selectedPointIdx = pts.length - 1;
    }
    this._notifySelection();
    if (this.onCurveCommitted) this.onCurveCommitted(this.getCurves());
  }

  addBranchArch(worldOffset = [0, 0, 3.2], scale = 1.0) {
    if (this.rigSpec && !this.rigSpec.canAddBranch) {
      // On cliff / canyon / spire rigs, `+ Point` extends/subdivides the active spline cleanly!
      this.addPointToActiveCurve();
      return;
    }
    const [ox, oy, oz] = worldOffset;
    this.curves.push([
      { x: Number((ox - 6.2 * scale).toFixed(2)), y: oy + 0.0, z: Number((oz - 1.8 * scale).toFixed(2)), r: 1.18 },
      { x: Number((ox - 3.6 * scale).toFixed(2)), y: Number((oy + 4.4 * scale).toFixed(2)), z: Number((oz - 0.5 * scale).toFixed(2)), r: 1.00 },
      { x: ox + 0.0, y: Number((oy + 7.2 * scale).toFixed(2)), z: Number((oz + 0.4 * scale).toFixed(2)), r: 0.85 },
      { x: Number((ox + 3.8 * scale).toFixed(2)), y: Number((oy + 4.2 * scale).toFixed(2)), z: Number((oz + 1.2 * scale).toFixed(2)), r: 1.00 },
      { x: Number((ox + 6.4 * scale).toFixed(2)), y: oy + 0.0, z: Number((oz + 2.0 * scale).toFixed(2)), r: 1.15 },
    ]);
    this.selectedCurveIdx = this.curves.length - 1;
    this.selectedPointIdx = 2;
    this._notifySelection();
    if (this.onCurveCommitted) this.onCurveCommitted(this.getCurves());
  }

  clearForClickDraw(anchorY = 0) {
    const minCurves = this.rigSpec?.roles?.length || 1;
    if (minCurves > 1) {
      // Clear only the currently selected spline so Left/River/Right canyon partners stay visible!
      this.curves[this.selectedCurveIdx] = [];
    } else {
      this.curves = [[]];
      this.selectedCurveIdx = 0;
    }
    this.selectedPointIdx = -1;
    this.anchorY = anchorY;
    this.setPlaceMode(true);
    if (this.onCurveChanged) this.onCurveChanged(this.getCurves());
  }

  deleteSelectedPoint() {
    const c = this.curves[this.selectedCurveIdx];
    if (!c) return;
    const minRequiredCurves = this.rigSpec?.roles?.length || 1;
    if (c.length <= 2) {
      if (this.curves.length > minRequiredCurves) {
        this.curves.splice(this.selectedCurveIdx, 1);
        this.selectedCurveIdx = Math.max(0, this.selectedCurveIdx - 1);
        this.selectedPointIdx = 0;
        this._notifySelection();
        if (this.onCurveCommitted) this.onCurveCommitted(this.getCurves());
      }
      return;
    }
    c.splice(this.selectedPointIdx, 1);
    this.selectedPointIdx = Math.max(0, Math.min(c.length - 1, this.selectedPointIdx));
    this._notifySelection();
    if (this.onCurveCommitted) this.onCurveCommitted(this.getCurves());
  }

  _project3D(wx, wy, wz, cam) {
    const m = cam.viewProj;
    const cx = m[0] * wx + m[4] * wy + m[8]  * wz + m[12];
    const cy = m[1] * wx + m[5] * wy + m[9]  * wz + m[13];
    const cz = m[2] * wx + m[6] * wy + m[10] * wz + m[14];
    const cw = m[3] * wx + m[7] * wy + m[11] * wz + m[15];
    if (cw <= 0.05) return null;
    const ndcX = cx / cw;
    const ndcY = cy / cw;
    return {
      sx: (ndcX * 0.5 + 0.5) * cam.width,
      sy: (0.5 - ndcY * 0.5) * cam.height,
      depth: cz / cw,
      w: cw,
    };
  }

  _mouseRay3D(clientX, clientY, cam) {
    const rect = this.stageEl.getBoundingClientRect();
    const ndcX = ((clientX - rect.left) / Math.max(1, rect.width)) * 2 - 1;
    const ndcY = 1 - ((clientY - rect.top) / Math.max(1, rect.height)) * 2;
    const aspect = rect.width / Math.max(1, rect.height);
    const tanHalf = Math.tan((cam.fovDeg * Math.PI) / 360);

    const dx = cam.forward[0] + cam.right[0] * ndcX * aspect * tanHalf + cam.up[0] * ndcY * tanHalf;
    const dy = cam.forward[1] + cam.right[1] * ndcX * aspect * tanHalf + cam.up[1] * ndcY * tanHalf;
    const dz = cam.forward[2] + cam.right[2] * ndcX * aspect * tanHalf + cam.up[2] * ndcY * tanHalf;
    const dl = Math.hypot(dx, dy, dz) || 1;
    return {
      origin: cam.eye,
      dir: [dx / dl, dy / dl, dz / dl],
    };
  }

  _projectRayOntoAxis(clientX, clientY, p0, axis, cam) {
    const { origin, dir } = this._mouseRay3D(clientX, clientY, cam);
    const w0x = p0.x - origin[0];
    const w0y = p0.y - origin[1];
    const w0z = p0.z - origin[2];

    const b = axis[0] * dir[0] + axis[1] * dir[1] + axis[2] * dir[2];
    const aDotW0 = axis[0] * w0x + axis[1] * w0y + axis[2] * w0z;
    const dDotW0 = dir[0] * w0x + dir[1] * w0y + dir[2] * w0z;
    const denom = Math.max(1e-4, 1 - b * b);
    return (b * dDotW0 - aDotW0) / denom;
  }

  _projectRayOntoViewPlane(clientX, clientY, p0, cam) {
    const { origin, dir } = this._mouseRay3D(clientX, clientY, cam);
    const n = cam.forward;
    const denom = n[0] * dir[0] + n[1] * dir[1] + n[2] * dir[2];
    if (Math.abs(denom) < 1e-4) return { x: p0.x, y: p0.y, z: p0.z };
    const t = ((p0.x - origin[0]) * n[0] + (p0.y - origin[1]) * n[1] + (p0.z - origin[2]) * n[2]) / denom;
    return {
      x: origin[0] + dir[0] * t,
      y: origin[1] + dir[1] * t,
      z: origin[2] + dir[2] * t,
    };
  }

  _bindPointerEvents() {
    // Click on empty SVG background when `placeMode === true` -> places a new 3D control point on active spline!
    this.svg.addEventListener('pointerdown', (e) => {
      if (!this.enabled || !this.placeMode || e.target !== this.svg) return;
      const cam = this.getCameraAdapter();
      if (!cam || !cam.raycastGround) return;

      const hit = cam.raycastGround(e.clientX, e.clientY);
      if (!hit) return;

      e.stopPropagation();
      e.preventDefault();

      if (this.curves.length === 0) this.curves.push([]);
      if (!this.curves[this.selectedCurveIdx]) this.curves[this.selectedCurveIdx] = [];
      const c = this.curves[this.selectedCurveIdx];
      const role = this.getCurveRole(this.selectedCurveIdx);
      const family = this.rigSpec?.family || 'arch';

      let placeY = hit[1];
      if (family === 'arch') {
        if (c.length === 1) {
          const spanSoFar = Math.hypot(hit[0] - c[0].x, hit[2] - c[0].z);
          placeY = hit[1] + Math.max(3.5, spanSoFar * 0.48);
        }
      } else if (family === 'canyon') {
        placeY = role.id === 'center' ? hit[1] + 0.5 : hit[1] + 10.5;
      } else if (family === 'cliff') {
        placeY = hit[1] + 9.5;
      } else if (family === 'spire') {
        placeY = c.length === 0 ? hit[1] : hit[1] + c.length * 4.5;
      }

      c.push({
        x: Number(hit[0].toFixed(2)),
        y: Number(placeY.toFixed(2)),
        z: Number(hit[2].toFixed(2)),
        r: 1.0,
      });
      this.selectedPointIdx = c.length - 1;
      this._notifySelection();

      if (this.onCurveChanged) this.onCurveChanged(this.getCurves());
      if (c.length >= 2 && this.onCurveCommitted) {
        this.onCurveCommitted(this.getCurves());
      }
    });

    window.addEventListener('pointermove', (e) => {
      if (!this._dragState || !this.enabled) return;
      const cam = this.getCameraAdapter();
      if (!cam) return;

      const ds = this._dragState;
      const pt = this.curves[ds.cIdx]?.[ds.pIdx];
      if (!pt) return;

      if (ds.mode === 'axis') {
        const sNow = this._projectRayOntoAxis(e.clientX, e.clientY, ds.startPt, ds.axis, cam);
        const delta = sNow - ds.startS;
        pt.x = Number((ds.startPt.x + ds.axis[0] * delta).toFixed(3));
        pt.y = Number(Math.max(ds.minY, ds.startPt.y + ds.axis[1] * delta).toFixed(3));
        pt.z = Number((ds.startPt.z + ds.axis[2] * delta).toFixed(3));
      } else if (ds.mode === 'free') {
        const pNow = this._projectRayOntoViewPlane(e.clientX, e.clientY, ds.startPt, cam);
        pt.x = Number((ds.startPt.x + (pNow.x - ds.startHit.x)).toFixed(3));
        pt.y = Number(Math.max(ds.minY, ds.startPt.y + (pNow.y - ds.startHit.y)).toFixed(3));
        pt.z = Number((ds.startPt.z + (pNow.z - ds.startHit.z)).toFixed(3));
      }

      if (this.onCurveChanged) this.onCurveChanged(this.getCurves());
    });

    window.addEventListener('pointerup', () => {
      if (!this._dragState) return;
      const moved = this._dragState;
      this._dragState = null;
      void moved;
      if (this.onCurveCommitted) this.onCurveCommitted(this.getCurves());
    });
  }

  _beginDrag(e, mode, cIdx, pIdx, axis = null) {
    e.stopPropagation();
    e.preventDefault();
    const cam = this.getCameraAdapter();
    if (!cam) return;

    const selectionChanged = this.selectedCurveIdx !== cIdx || this.selectedPointIdx !== pIdx;
    this.selectedCurveIdx = cIdx;
    this.selectedPointIdx = pIdx;
    if (selectionChanged) this._notifySelection();

    const pt = this.curves[cIdx]?.[pIdx];
    if (!pt) return;

    const startPt = { x: pt.x, y: pt.y, z: pt.z, r: pt.r ?? 1.0 };
    const minY = cam.minWorldY ?? -5.0;

    if (mode === 'axis') {
      const startS = this._projectRayOntoAxis(e.clientX, e.clientY, startPt, axis, cam);
      this._dragState = { mode, cIdx, pIdx, axis, startPt, startS, minY };
    } else {
      const startHit = this._projectRayOntoViewPlane(e.clientX, e.clientY, startPt, cam);
      this._dragState = { mode, cIdx, pIdx, startPt, startHit, minY };
    }
  }

  _startRenderLoop() {
    if (this._raf) return;
    const loop = () => {
      if (!this.enabled) {
        this._raf = 0;
        return;
      }
      this._raf = requestAnimationFrame(loop);
      this._renderOverlay();
    };
    this._raf = requestAnimationFrame(loop);
  }

  _renderOverlay() {
    const cam = this.getCameraAdapter();
    if (!cam || cam.width < 4 || cam.height < 4) return;

    this.svg.setAttribute('viewBox', `0 0 ${cam.width} ${cam.height}`);
    this.gSplines.innerHTML = '';
    this.gMidHandles.innerHTML = '';
    this.gGizmoAxes.innerHTML = '';
    this.gPoints.innerHTML = '';

    // 1. Render smooth 3D Catmull-Rom Splines + Role Badges + Mid-Segment `+` Handles + Plumb Lines
    this.curves.forEach((pts, cIdx) => {
      if (!pts || pts.length === 0) return;
      const role = this.getCurveRole(cIdx);
      const roleColor = role.color || '#6bc2ff';
      const isCurveActive = cIdx === this.selectedCurveIdx;

      if (pts.length >= 2) {
        let dPath = '';
        const N = 56;
        for (let i = 0; i <= N; i++) {
          const st = evalCatmullRom3D(pts, i / N);
          const pr = this._project3D(st.x, st.y, st.z, cam);
          if (!pr) continue;
          dPath += (dPath === '' ? `M ${pr.sx.toFixed(1)} ${pr.sy.toFixed(1)}` : ` L ${pr.sx.toFixed(1)} ${pr.sy.toFixed(1)}`);
        }

        if (dPath) {
          const glow = document.createElementNS(SVG_NS, 'path');
          glow.setAttribute('d', dPath);
          glow.setAttribute('fill', 'none');
          glow.setAttribute('stroke', roleColor);
          glow.setAttribute('stroke-opacity', isCurveActive ? '0.36' : '0.14');
          glow.setAttribute('stroke-width', isCurveActive ? '7.5' : '5');
          glow.setAttribute('stroke-linecap', 'round');
          this.gSplines.appendChild(glow);

          const line = document.createElementNS(SVG_NS, 'path');
          line.setAttribute('d', dPath);
          line.setAttribute('fill', 'none');
          line.setAttribute('stroke', roleColor);
          line.setAttribute('stroke-opacity', isCurveActive ? '1.0' : '0.68');
          line.setAttribute('stroke-width', isCurveActive ? '2.6' : '1.9');
          line.setAttribute('stroke-dasharray', isCurveActive ? '7 4' : '4 4');
          this.gSplines.appendChild(line);
        }

        // Mid-segment `+` handles on active spline (and subtle clickable `+` on companion splines)
        for (let seg = 0; seg < pts.length - 1; seg++) {
          const tMid = (seg + 0.5) / (pts.length - 1);
          const mid3 = evalCatmullRom3D(pts, tMid);
          const pm = this._project3D(mid3.x, mid3.y, mid3.z, cam);
          if (!pm) continue;

          const btnG = document.createElementNS(SVG_NS, 'g');
          btnG.style.pointerEvents = 'auto';
          btnG.style.cursor = 'copy';
          btnG.style.opacity = isCurveActive ? '1.0' : '0.62';

          const circ = document.createElementNS(SVG_NS, 'circle');
          circ.setAttribute('cx', pm.sx.toFixed(1));
          circ.setAttribute('cy', pm.sy.toFixed(1));
          circ.setAttribute('r', isCurveActive ? '7' : '5.8');
          circ.setAttribute('fill', 'rgba(18, 25, 38, 0.92)');
          circ.setAttribute('stroke', roleColor);
          circ.setAttribute('stroke-width', '1.4');

          const plus = document.createElementNS(SVG_NS, 'text');
          plus.setAttribute('x', pm.sx.toFixed(1));
          plus.setAttribute('y', (pm.sy + 3.5).toFixed(1));
          plus.setAttribute('text-anchor', 'middle');
          plus.setAttribute('fill', '#e6f4ff');
          plus.setAttribute('font-size', '10.5');
          plus.setAttribute('font-weight', '700');
          plus.textContent = '+';

          btnG.appendChild(circ);
          btnG.appendChild(plus);
          btnG.addEventListener('pointerdown', (e) => {
            e.stopPropagation();
            e.preventDefault();
            pts.splice(seg + 1, 0, {
              x: Number(mid3.x.toFixed(2)),
              y: Number(mid3.y.toFixed(2)),
              z: Number(mid3.z.toFixed(2)),
              r: Number((mid3.r ?? 1.0).toFixed(2)),
            });
            this.selectedCurveIdx = cIdx;
            this.selectedPointIdx = seg + 1;
            this._notifySelection();
            if (this.onCurveCommitted) this.onCurveCommitted(this.getCurves());
          });
          this.gMidHandles.appendChild(btnG);
        }
      }

      // 2. Render Control Point Spheres + Vertical Plumb Reference Lines + Spline Role Tag
      pts.forEach((pt, pIdx) => {
        const pr = this._project3D(pt.x, pt.y, pt.z, cam);
        if (!pr) return;

        const baseFloorY = cam.baseFloorY ? cam.baseFloorY(pt.x, pt.z) : 0;
        const prGround = this._project3D(pt.x, baseFloorY, pt.z, cam);
        if (prGround && Math.abs(pt.y - baseFloorY) > 0.25) {
          const plumb = document.createElementNS(SVG_NS, 'line');
          plumb.setAttribute('x1', pr.sx.toFixed(1));
          plumb.setAttribute('y1', pr.sy.toFixed(1));
          plumb.setAttribute('x2', prGround.sx.toFixed(1));
          plumb.setAttribute('y2', prGround.sy.toFixed(1));
          plumb.setAttribute('stroke', roleColor);
          plumb.setAttribute('stroke-opacity', isCurveActive ? '0.35' : '0.18');
          plumb.setAttribute('stroke-width', '1.2');
          plumb.setAttribute('stroke-dasharray', '3 3');
          this.gSplines.appendChild(plumb);
        }

        const isSel = cIdx === this.selectedCurveIdx && pIdx === this.selectedPointIdx;
        const nodeG = document.createElementNS(SVG_NS, 'g');
        nodeG.style.pointerEvents = 'auto';
        nodeG.style.cursor = 'grab';

        const dot = document.createElementNS(SVG_NS, 'circle');
        dot.setAttribute('cx', pr.sx.toFixed(1));
        dot.setAttribute('cy', pr.sy.toFixed(1));
        dot.setAttribute('r', isSel ? '9.5' : isCurveActive ? '7.6' : '6.4');
        dot.setAttribute('fill', isSel ? '#ffb838' : '#1b2638');
        dot.setAttribute('stroke', isSel ? '#ffffff' : roleColor);
        dot.setAttribute('stroke-width', isSel ? '2.3' : '1.9');

        const lbl = document.createElementNS(SVG_NS, 'text');
        lbl.setAttribute('x', pr.sx.toFixed(1));
        lbl.setAttribute('y', (pr.sy - 13).toFixed(1));
        lbl.setAttribute('text-anchor', 'middle');
        lbl.setAttribute('fill', isSel ? '#ffe6a8' : roleColor);
        lbl.setAttribute('font-size', '10');
        lbl.setAttribute('font-weight', '700');
        lbl.textContent = pIdx === 0 && this.curves.length > 1
          ? `${role.label} · P1`
          : `P${pIdx + 1}`;

        nodeG.appendChild(dot);
        nodeG.appendChild(lbl);

        nodeG.addEventListener('pointerdown', (e) => {
          this._beginDrag(e, 'free', cIdx, pIdx);
        });

        this.gPoints.appendChild(nodeG);
      });
    });

    // 3. Render 3D World-Space RGB XYZ Translation Axis Gizmo on Selected Control Point!
    const selPt = this.getSelectedPoint();
    if (selPt) {
      const pOrigin = this._project3D(selPt.x, selPt.y, selPt.z, cam);
      if (pOrigin) {
        const distToCam = Math.hypot(
          selPt.x - cam.eye[0],
          selPt.y - cam.eye[1],
          selPt.z - cam.eye[2]
        );
        const worldAxisLen = Math.max(0.8, distToCam * 0.115);

        const axes = [
          { name: 'X', vec: [1, 0, 0], color: '#ff4d5e' },
          { name: 'Y', vec: [0, 1, 0], color: '#45e680' },
          { name: 'Z', vec: [0, 0, 1], color: '#4da3ff' },
        ];

        for (const ax of axes) {
          const pTip = this._project3D(
            selPt.x + ax.vec[0] * worldAxisLen,
            selPt.y + ax.vec[1] * worldAxisLen,
            selPt.z + ax.vec[2] * worldAxisLen,
            cam
          );
          if (!pTip) continue;

          const dx = pTip.sx - pOrigin.sx;
          const dy = pTip.sy - pOrigin.sy;
          const lenPx = Math.hypot(dx, dy);
          if (lenPx < 6) continue;

          const ux = dx / lenPx;
          const uy = dy / lenPx;
          const px = -uy;
          const py = ux;

          const axisGroup = document.createElementNS(SVG_NS, 'g');
          axisGroup.style.pointerEvents = 'auto';
          axisGroup.style.cursor = 'pointer';

          const hitShaft = document.createElementNS(SVG_NS, 'line');
          hitShaft.setAttribute('x1', pOrigin.sx.toFixed(1));
          hitShaft.setAttribute('y1', pOrigin.sy.toFixed(1));
          hitShaft.setAttribute('x2', pTip.sx.toFixed(1));
          hitShaft.setAttribute('y2', pTip.sy.toFixed(1));
          hitShaft.setAttribute('stroke', 'transparent');
          hitShaft.setAttribute('stroke-width', '15');

          const shaft = document.createElementNS(SVG_NS, 'line');
          shaft.setAttribute('x1', pOrigin.sx.toFixed(1));
          shaft.setAttribute('y1', pOrigin.sy.toFixed(1));
          shaft.setAttribute('x2', pTip.sx.toFixed(1));
          shaft.setAttribute('y2', pTip.sy.toFixed(1));
          shaft.setAttribute('stroke', ax.color);
          shaft.setAttribute('stroke-width', '3.2');
          shaft.setAttribute('stroke-linecap', 'round');

          const headLen = 12;
          const headHalfW = 5.5;
          const bx = pTip.sx - ux * headLen;
          const by = pTip.sy - uy * headLen;
          const head = document.createElementNS(SVG_NS, 'polygon');
          head.setAttribute(
            'points',
            `${(pTip.sx + ux * 2).toFixed(1)},${(pTip.sy + uy * 2).toFixed(1)} ` +
            `${(bx + px * headHalfW).toFixed(1)},${(by + py * headHalfW).toFixed(1)} ` +
            `${(bx - px * headHalfW).toFixed(1)},${(by - py * headHalfW).toFixed(1)}`
          );
          head.setAttribute('fill', ax.color);

          const tag = document.createElementNS(SVG_NS, 'text');
          tag.setAttribute('x', (pTip.sx + ux * 11).toFixed(1));
          tag.setAttribute('y', (pTip.sy + uy * 11 + 3.5).toFixed(1));
          tag.setAttribute('text-anchor', 'middle');
          tag.setAttribute('fill', ax.color);
          tag.setAttribute('font-size', '10.5');
          tag.setAttribute('font-weight', '800');
          tag.textContent = ax.name;

          axisGroup.appendChild(hitShaft);
          axisGroup.appendChild(shaft);
          axisGroup.appendChild(head);
          axisGroup.appendChild(tag);

          axisGroup.addEventListener('pointerdown', (e) => {
            this._beginDrag(e, 'axis', this.selectedCurveIdx, this.selectedPointIdx, ax.vec);
          });

          this.gGizmoAxes.appendChild(axisGroup);
        }
      }
    }
  }
}
