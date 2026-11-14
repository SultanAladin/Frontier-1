// SDF workspace: bakes the hero's distance field on a worker, then previews it as a slice and as a CPU raymarch.
// Preview only: the field does not feed the renderer. The iso offset moves the surface the preview shows.
const $ = (id) => document.getElementById(id);

export function createSdfBaker({ getGeometry, toast }) {
  let baked = null, ticket = 0, worker = null, mode = 'slice';
  let yaw = 0.6, pitch = 0.3, drag = null;
  let raymarchFrame = 0;
  const state = { resolution: 40, iso: 0, slice: 0 };

  function fieldAt(x, y, z) {
    // Trilinear sample of the baked grid; outside the grid the field is the clamped band.
    const { values, dims: n, origin, voxel: h, band } = baked;
    const gx = (x - origin[0]) / h, gy = (y - origin[1]) / h, gz = (z - origin[2]) / h;
    if (gx < 0 || gy < 0 || gz < 0 || gx > n - 1 || gy > n - 1 || gz > n - 1) return band;
    const i = Math.min(n - 2, Math.floor(gx)), j = Math.min(n - 2, Math.floor(gy)), k = Math.min(n - 2, Math.floor(gz));
    const fx = gx - i, fy = gy - j, fz = gz - k;
    const at = (a, b, c) => values[a + n * (b + n * c)];
    const c00 = at(i, j, k) * (1 - fx) + at(i + 1, j, k) * fx;
    const c10 = at(i, j + 1, k) * (1 - fx) + at(i + 1, j + 1, k) * fx;
    const c01 = at(i, j, k + 1) * (1 - fx) + at(i + 1, j, k + 1) * fx;
    const c11 = at(i, j + 1, k + 1) * (1 - fx) + at(i + 1, j + 1, k + 1) * fx;
    return (c00 * (1 - fy) + c10 * fy) * (1 - fz) + (c01 * (1 - fy) + c11 * fy) * fz - state.iso;
  }

  function bake() {
    const geometry = getGeometry();
    if (!geometry) return toast('Wait for the hero to finish building');
    if (!geometry.closed) toast('This hero is an open surface; the sign of the field is approximate');
    if (worker) worker.terminate();
    worker = new Worker(new URL('./Engine/SdfWorker.js', import.meta.url), { type: 'module' });
    const mine = ++ticket;
    const bar = $('SdfBar'), label = $('SdfProgressLabel');
    $('SdfProgress').hidden = false;
    $('SdfBake').disabled = true;
    bar.style.width = '0%';
    label.textContent = 'Starting';
    worker.onmessage = (event) => {
      const m = event.data;
      if (m.ticket !== mine) return;
      if (m.type === 'progress') { bar.style.width = (m.fraction * 100).toFixed(1) + '%'; label.textContent = m.label; }
      else if (m.type === 'error') { label.textContent = 'Bake failed: ' + m.message; $('SdfBake').disabled = false; }
      else if (m.type === 'done') {
        baked = m.result;
        $('SdfBake').disabled = false;
        $('SdfProgress').hidden = true;
        state.slice = Math.floor(baked.dims / 2);
        $('SdfSlice').max = String(baked.dims - 1);
        $('SdfSlice').value = String(state.slice);
        renderStats();
        refreshPreview();
        toast(`Baked ${baked.dims}³ distance field in ${(baked.ms / 1000).toFixed(2)} s`);
      }
    };
    worker.onerror = (event) => { label.textContent = 'Bake failed: ' + (event.message || 'worker error'); $('SdfBake').disabled = false; };
    worker.postMessage({
      ticket: mine,
      request: {
        positions: new Float32Array(geometry.positions), indices: new Uint32Array(geometry.indices),
        resolution: state.resolution, band: 2,
      },
    });
  }

  function renderStats() {
    if (!baked) return;
    $('SdfStats').innerHTML = `
      <span>Grid</span><b>${baked.dims} × ${baked.dims} × ${baked.dims}</b>
      <span>Voxel</span><b>${baked.voxel.toFixed(4)} units</b>
      <span>Inside</span><b>${(baked.insideShare * 100).toFixed(1)} %</b>
      <span>Bake time</span><b>${(baked.ms / 1000).toFixed(2)} s</b>`;
  }

  // Slice: the XY plane at the chosen Z. Warm = inside, cool = outside, bright band = the iso surface.
  function drawSlice() {
    const canvas = $('SdfCanvas');
    const size = Math.max(200, Math.round(canvas.clientWidth * Math.min(devicePixelRatio || 1, 2)));
    if (canvas.width !== size || canvas.height !== size) { canvas.width = size; canvas.height = size; }
    const g = canvas.getContext('2d');
    const image = g.createImageData(size, size);
    if (!baked) { g.fillStyle = '#0d0d0f'; g.fillRect(0, 0, size, size); return; }
    const { dims: n, origin, voxel: h, band } = baked;
    const z = origin[2] + state.slice * h;
    for (let py = 0; py < size; py++) for (let px = 0; px < size; px++) {
      const x = origin[0] + ((px + 0.5) / size) * (n - 1) * h;
      const y = origin[1] + (1 - (py + 0.5) / size) * (n - 1) * h;
      const d = fieldAt(x, y, z);
      const t = Math.max(-1, Math.min(1, d / band));
      const at = (py * size + px) * 4;
      let r, gg, b;
      if (t < 0) { r = 255 * (0.25 - 0.15 * t); gg = 150 * (1 + t * 0.5); b = 80; }
      else { r = 30; gg = 60 + 80 * t; b = 110 + 110 * t; }
      if (Math.abs(d) < h * 0.6) { r = 255; gg = 255; b = 255; }
      image.data[at] = r; image.data[at + 1] = gg; image.data[at + 2] = b; image.data[at + 3] = 255;
    }
    g.putImageData(image, 0, 0);
  }

  // Raymarch: orthographic rays, sphere tracing on the field, Lambert shading from a central-difference gradient.
  function scheduleRaymarch() {
    if (raymarchFrame) return;
    raymarchFrame = requestAnimationFrame(() => { raymarchFrame = 0; drawRaymarch(); });
  }
  function drawRaymarch() {
    // Only the raymarch mode owns the canvas; a slice redraw must not be overwritten by a pending raymarch frame.
    if (!baked || mode !== 'raymarch') return;
    const canvas = $('SdfCanvas');
    const size = canvas.width;
    const g = canvas.getContext('2d');
    const { dims: n, origin, voxel: h, extent } = baked;
    const centre = baked.centre;
    const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
    // Camera basis (world): right, up, forward (into the scene).
    const right = [cy, 0, -sy];
    const up = [-sy * sp, cp, -cy * sp];
    const forward = [sy * cp, sp, cy * cp];
    const image = g.createImageData(size, size);
    const half = extent * 0.6, steps = 160, eps = h * 0.5;
    const normalAt = (x, y, z) => {
      const e = h * 0.5;
      const gx = fieldAt(x + e, y, z) - fieldAt(x - e, y, z);
      const gy = fieldAt(x, y + e, z) - fieldAt(x, y - e, z);
      const gz = fieldAt(x, y, z + e) - fieldAt(x, y, z - e);
      const l = Math.hypot(gx, gy, gz) || 1;
      return [gx / l, gy / l, gz / l];
    };
    const light = [0.4, 0.7, 0.6], ll = Math.hypot(...light);
    for (let py = 0; py < size; py++) for (let px = 0; px < size; px++) {
      const u = ((px + 0.5) / size * 2 - 1) * half, v = (1 - (py + 0.5) / size * 2) * half;
      let x = centre[0] + right[0] * u + up[0] * v - forward[0] * extent;
      let y = centre[1] + right[1] * u + up[1] * v - forward[1] * extent;
      let z = centre[2] + right[2] * u + up[2] * v - forward[2] * extent;
      const at = (py * size + px) * 4;
      let hit = false;
      let travelled = 0;
      for (let s = 0; s < steps && travelled < extent * 2.2; s++) {
        const d = fieldAt(x, y, z);
        if (d < eps) { hit = true; break; }
        const stepLength = Math.max(d, eps);
        x += forward[0] * stepLength; y += forward[1] * stepLength; z += forward[2] * stepLength;
        travelled += stepLength;
      }
      if (hit) {
        const nrm = normalAt(x, y, z);
        const lit = Math.max(0, (nrm[0] * light[0] + nrm[1] * light[1] + nrm[2] * light[2]) / ll);
        const shade = 0.25 + 0.75 * lit;
        image.data[at] = 230 * shade; image.data[at + 1] = 214 * shade; image.data[at + 2] = 190 * shade;
      } else {
        image.data[at] = 13; image.data[at + 1] = 13; image.data[at + 2] = 15;
      }
      image.data[at + 3] = 255;
    }
    g.putImageData(image, 0, 0);
  }

  function setMode(next) {
    mode = next;
    for (const b of document.querySelectorAll('[data-sdf-mode]')) {
      b.classList.toggle('active', b.dataset.sdfMode === mode);
      b.setAttribute('aria-pressed', String(b.dataset.sdfMode === mode));
    }
    $('SdfSliceRow').hidden = mode !== 'slice';
    refreshPreview();
  }
  function refreshPreview() {
    const canvas = $('SdfCanvas');
    if (canvas.width < 200) canvas.width = canvas.height = 320;
    if (!baked) return;
    if (mode === 'slice') drawSlice(); else scheduleRaymarch();
  }
  function refresh() { refreshPreview(); }

  function bind() {
    $('SdfBake').addEventListener('click', bake);
    for (const b of document.querySelectorAll('[data-sdf-res]')) {
      b.addEventListener('click', () => {
        state.resolution = +b.dataset.sdfRes;
        for (const o of document.querySelectorAll('[data-sdf-res]')) o.classList.toggle('active', o === b);
      });
    }
    for (const b of document.querySelectorAll('[data-sdf-mode]')) b.addEventListener('click', () => setMode(b.dataset.sdfMode));
    $('SdfSlice').addEventListener('input', (event) => { state.slice = +event.target.value; drawSlice(); });
    $('SdfIso').addEventListener('input', (event) => {
      // The slider is a percentage of the hero's size, so the offset reads the same for any hero.
      const size = baked ? baked.extent / 1.1 : 1;
      state.iso = (+event.target.value / 100) * size;
      $('SdfIsoOut').textContent = (+event.target.value >= 0 ? '+' : '') + (+event.target.value).toFixed(0) + ' % of hero size';
      refreshPreview();
    });
    const canvas = $('SdfCanvas');
    canvas.addEventListener('pointerdown', (event) => { drag = { x: event.clientX, y: event.clientY, yaw, pitch }; canvas.setPointerCapture(event.pointerId); });
    canvas.addEventListener('pointermove', (event) => {
      if (!drag || mode !== 'raymarch') return;
      yaw = drag.yaw + (event.clientX - drag.x) * 0.01;
      pitch = Math.max(-1.4, Math.min(1.4, drag.pitch + (event.clientY - drag.y) * 0.01));
      scheduleRaymarch();
    });
    const end = () => { drag = null; };
    canvas.addEventListener('pointerup', end);
    canvas.addEventListener('pointercancel', end);
  }
  bind();
  return { refresh, bake, get baked() { return baked; }, get mode() { return mode; }, reset() { baked = null; renderStats(); $('SdfStats').innerHTML = '<span>Status</span><b>Not baked for this hero</b>'; refreshPreview(); } };
}
