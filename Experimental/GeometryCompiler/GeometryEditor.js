// Geometry Editor: transforms the current hero source (scale, centre, mirror, rotate, flip normals) and rebuilds the
// hero with Apply. The preview runs on the main thread from the untransformed base copy, so nothing rebuilds until
// Apply. The import area is interface only; no file parser is wired to it yet.
import { transformGeometry, identityTransform } from './Engine/GeometryTransform.js';

const $ = (id) => document.getElementById(id);
const PREVIEW_TRIANGLES = 150000;
const clone = (t) => ({ scale: [...t.scale], mirror: [...t.mirror], rotate: [...t.rotate], centre: t.centre, flipNormals: t.flipNormals });
const fmt = (n) => (Math.round(n * 1000) / 1000).toString();

export function createGeometryEditor({ getBase, getTransform, applyTransform, getName, toast }) {
  let pending = identityTransform();
  let open = false;
  let yaw = 0.6, pitch = 0.35;
  let frame = 0;
  let drag = null;

  function bindInputs() {
    const scaleInputs = ['GeoScaleX', 'GeoScaleY', 'GeoScaleZ'].map($);
    scaleInputs.forEach((input, k) => input.addEventListener('input', () => {
      if ($('GeoUniform').checked) {
        pending.scale = [+input.value, +input.value, +input.value];
        scaleInputs.forEach((other) => { other.value = input.value; });
      } else pending.scale[k] = +input.value;
      schedule();
    }));
    ['GeoRotX', 'GeoRotY', 'GeoRotZ'].map($).forEach((input, k) => input.addEventListener('input', () => {
      pending.rotate[k] = +input.value || 0; schedule();
    }));
    ['GeoMirrorX', 'GeoMirrorY', 'GeoMirrorZ'].map($).forEach((button, k) => button.addEventListener('click', () => {
      pending.mirror[k] = !pending.mirror[k]; syncControls(); schedule();
    }));
    $('GeoFlip').addEventListener('click', () => { pending.flipNormals = !pending.flipNormals; syncControls(); schedule(); });
    for (const button of document.querySelectorAll('[data-centre]')) {
      button.addEventListener('click', () => { pending.centre = button.dataset.centre; syncControls(); schedule(); });
    }
    $('GeoReset').addEventListener('click', () => { pending = identityTransform(); syncControls(); schedule(); });
    $('GeoApply').addEventListener('click', apply);
    $('GeoCancel').addEventListener('click', close);
    $('GeoEditorClose').addEventListener('click', close);
    const canvas = $('GeoPreview');
    canvas.addEventListener('pointerdown', (event) => { drag = { x: event.clientX, y: event.clientY, yaw, pitch }; canvas.setPointerCapture(event.pointerId); });
    canvas.addEventListener('pointermove', (event) => {
      if (!drag) return;
      yaw = drag.yaw + (event.clientX - drag.x) * 0.01;
      pitch = Math.max(-1.4, Math.min(1.4, drag.pitch + (event.clientY - drag.y) * 0.01));
      schedule();
    });
    const end = () => { drag = null; };
    canvas.addEventListener('pointerup', end);
    canvas.addEventListener('pointercancel', end);
  }

  function syncControls() {
    ['GeoScaleX', 'GeoScaleY', 'GeoScaleZ'].map($).forEach((input, k) => { input.value = fmt(pending.scale[k]); });
    ['GeoRotX', 'GeoRotY', 'GeoRotZ'].map($).forEach((input, k) => { input.value = fmt(pending.rotate[k]); });
    ['GeoMirrorX', 'GeoMirrorY', 'GeoMirrorZ'].map($).forEach((button, k) => {
      button.classList.toggle('active', pending.mirror[k]);
      button.setAttribute('aria-pressed', String(pending.mirror[k]));
    });
    $('GeoFlip').classList.toggle('active', pending.flipNormals);
    $('GeoFlip').setAttribute('aria-pressed', String(pending.flipNormals));
    for (const button of document.querySelectorAll('[data-centre]')) {
      const on = button.dataset.centre === pending.centre;
      button.classList.toggle('active', on);
      button.setAttribute('aria-pressed', String(on));
    }
    const changed = JSON.stringify(pending) !== JSON.stringify(identityTransform());
    $('GeoApply').textContent = changed ? 'Apply and rebuild hero' : 'Rebuild hero';
  }

  function schedule() {
    if (frame) return;
    frame = requestAnimationFrame(() => { frame = 0; render(); });
  }

  // Projects the transformed base orthographically. Faces whose projected winding is reversed draw amber, which
  // shows inside-out triangles from the current view. Thin walls and cavities are normal, so no outward test is used.
  function render() {
    const base = getBase();
    if (!base) return;
    const moved = transformGeometry(base.positions, base.indices, pending);
    const p = moved.positions, idx = moved.indices, n = p.length / 3;
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], p[i * 3 + k]); hi[k] = Math.max(hi[k], p[i * 3 + k]); }
    const centre = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2];
    const radius = Math.max(1e-6, Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) / 2);

    const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
    const view = (x, y, z) => {
      const X = x * cy + z * sy, Z0 = -x * sy + z * cy;
      const Y = y * cp - Z0 * sp, Z = y * sp + Z0 * cp;
      return [X, Y, Z];
    };
    const canvas = $('GeoPreview');
    const size = Math.max(200, Math.round(canvas.clientWidth * Math.min(devicePixelRatio || 1, 2)));
    if (canvas.width !== size || canvas.height !== size) { canvas.width = size; canvas.height = size; }
    const g = canvas.getContext('2d');
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.fillStyle = '#0d0d0f';
    g.fillRect(0, 0, size, size);
    const scale = (size * 0.42) / radius;
    const to = (x, y) => [size / 2 + x * scale, size / 2 - y * scale];

    const step = Math.max(1, Math.ceil((idx.length / 3) / PREVIEW_TRIANGLES));
    // Collect sampled triangles in view space, then paint back to front as opaque shaded faces.
    const count = Math.ceil(idx.length / 3 / step);
    const tri = new Float32Array(count * 9), depth = new Float32Array(count);
    let m = 0;
    for (let f = 0; f + 2 < idx.length; f += 3 * step, m++) {
      const corners = [idx[f], idx[f + 1], idx[f + 2]];
      for (let k = 0; k < 3; k++) {
        const q = corners[k] * 3;
        const v = view(p[q] - centre[0], p[q + 1] - centre[1], p[q + 2] - centre[2]);
        tri[m * 9 + k * 3] = v[0]; tri[m * 9 + k * 3 + 1] = v[1]; tri[m * 9 + k * 3 + 2] = v[2];
      }
      depth[m] = (tri[m * 9 + 2] + tri[m * 9 + 5] + tri[m * 9 + 8]) / 3;
    }
    const order = Array.from({ length: m }, (_, i) => i).sort((a, b) => depth[a] - depth[b]);
    const light = [0.4, 0.6, 0.7], lightLength = Math.hypot(...light);
    g.lineJoin = 'round';
    for (const i of order) {
      const o = i * 9;
      const ux = tri[o + 3] - tri[o], uy = tri[o + 4] - tri[o + 1], uz = tri[o + 5] - tri[o + 2];
      const vx = tri[o + 6] - tri[o], vy = tri[o + 7] - tri[o + 1], vz = tri[o + 8] - tri[o + 2];
      const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      const facing = nz; // view-space normal, z toward the viewer
      const lit = (nx * light[0] + ny * light[1] + nz * light[2]) / (Math.hypot(nx, ny, nz) * lightLength + 1e-12);
      const shade = 0.3 + 0.7 * Math.max(0, Math.abs(lit));
      const [pa, pb, pc] = [0, 3, 6].map((k) => to(tri[o + k], tri[o + k + 1]));
      g.beginPath(); g.moveTo(pa[0], pa[1]); g.lineTo(pb[0], pb[1]); g.lineTo(pc[0], pc[1]); g.closePath();
      const base = facing >= 0 ? [184, 166, 255] : [255, 178, 56];
      g.fillStyle = `rgb(${base.map((c) => Math.round(c * shade)).join(',')})`;
      g.fill();
    }
    // Axes and bounds.
    g.strokeStyle = '#3a3a3a'; g.lineWidth = 1;
    const axes = [[1, 0, 0, 'X', '#ff7a7a'], [0, 1, 0, 'Y', '#7aff9c'], [0, 0, 1, 'Z', '#7ab8ff']];
    for (const [x, y, z, label, colour] of axes) {
      const o = to(0, 0), e = view(x * radius * 0.7, y * radius * 0.7, z * radius * 0.7);
      const pe = to(e[0], e[1]);
      g.strokeStyle = colour; g.beginPath(); g.moveTo(o[0], o[1]); g.lineTo(pe[0], pe[1]); g.stroke();
      g.fillStyle = colour; g.font = '11px DM Sans, sans-serif'; g.fillText(label, pe[0] + 4, pe[1] + 4);
    }
    const reversed = (pending.mirror.filter(Boolean).length % 2 === 1) !== pending.flipNormals;
    $('GeoStats').innerHTML = `
      <span>Triangles</span><b>${(idx.length / 3).toLocaleString('en-US')}</b>
      <span>Size</span><b>${(hi[0] - lo[0]).toFixed(2)} × ${(hi[1] - lo[1]).toFixed(2)} × ${(hi[2] - lo[2]).toFixed(2)}</b>
      <span>Centre</span><b>${centre.map((v) => v.toFixed(2)).join(' ')}</b>
      <span>Winding</span><b>${reversed ? 'reversed' : 'as source'}</b>`;
    $('GeoPreviewNote').textContent = step > 1 ? `Preview shows every ${step} triangles; Apply builds the full mesh.` : 'Drag to orbit the preview.';
  }

  function apply() {
    const next = clone(pending);
    close();
    applyTransform(next);
    toast('Rebuilding the hero with the new geometry transform');
  }

  function show() {
    if (!getBase()) return toast('Wait for the hero to finish building');
    pending = clone(getTransform());
    open = true;
    $('GeometryEditor').hidden = false;
    $('GeoSource').textContent = getName();
    syncControls();
    render();
    $('GeoCancel').focus({ preventScroll: true });
  }
  function close() {
    if (!open) return;
    open = false;
    $('GeometryEditor').hidden = true;
  }
  function keydown(event) {
    if (!open) return false;
    const key = event.key.toLowerCase();
    if (key === 'escape') close();
    else if (key === 'enter' && event.target.tagName !== 'BUTTON') apply();
    else return false;
    event.preventDefault();
    return true;
  }

  bindInputs();
  $('OpenGeometryEditor').addEventListener('click', show);

  return { get open() { return open; }, show, close, keydown };
}

