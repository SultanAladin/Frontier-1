// UV Workspace: the UV layout of the current geometry. The Inspector shows it statically; the workspace edits it.
// Islands select (click, Shift for a range, Ctrl/Cmd to toggle), move as a group, rotate and mirror, and pack
// into one shared texel density. The layout is recomputed whenever a different geometry is adopted.
import { unwrapCharts, packCharts, cornerUvs } from './Engine/UvCharts.js';
import { formatObj, formatUvSvg } from './Engine/ObjExport.js';

const $ = (id) => document.getElementById(id);
const hue = (i) => (i * 137.508) % 360;
const count = (n) => n.toLocaleString('en-US');
const percent = (x) => (x * 100).toFixed(2) + ' %';

function download(name, blob) {
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 2000);
}

export function createUvWorkspace({ getGeometry, getName, toast }) {
  let geometry = null, layout = null, uvs = null;
  let paths = [], seamPath = null, borderPath = null;
  let selection = new Set(), anchor = -1, drag = null, open = false;
  const flags = { checker: true, seams: true, fill: true };

  // --- Layout lifecycle -------------------------------------------------------------------------------------
  function ensure() {
    const next = getGeometry();
    if (!next) return null;
    if (next !== geometry || !layout) {
      geometry = next;
      layout = packCharts(unwrapCharts(geometry.positions, geometry.indices));
      selection = new Set(); anchor = -1;
      rebuildAll();
    }
    return layout;
  }
  function rebuildAll() {
    uvs = cornerUvs(layout);
    paths = layout.charts.map(buildIslandPath);
    rebuildOverlay();
  }
  function buildIslandPath(chart) {
    const path = new Path2D();
    let minU = Infinity, minV = Infinity, maxU = -Infinity, maxV = -Infinity;
    for (const t of chart.triangles) {
      const a = t * 6;
      path.moveTo(uvs[a], uvs[a + 1]); path.lineTo(uvs[a + 2], uvs[a + 3]); path.lineTo(uvs[a + 4], uvs[a + 5]); path.closePath();
      for (let k = 0; k < 6; k += 2) {
        minU = Math.min(minU, uvs[a + k]); maxU = Math.max(maxU, uvs[a + k]);
        minV = Math.min(minV, uvs[a + k + 1]); maxV = Math.max(maxV, uvs[a + k + 1]);
      }
    }
    chart.bounds = [minU, minV, maxU, maxV];
    return path;
  }
  function rebuildOverlay() {
    seamPath = new Path2D(); borderPath = new Path2D();
    const add = (path, slots) => {
      for (const slot of slots) {
        const t = (slot / 3) | 0, k = slot % 3;
        const a = (t * 3 + k) * 2, b = (t * 3 + ((k + 1) % 3)) * 2;
        path.moveTo(uvs[a], uvs[a + 1]); path.lineTo(uvs[b], uvs[b + 1]);
      }
    };
    add(seamPath, layout.seams);
    add(borderPath, layout.borders);
  }
  function triangleUvArea(t) {
    const a = t * 6;
    return 0.5 * Math.abs((uvs[a + 2] - uvs[a]) * (uvs[a + 5] - uvs[a + 1]) - (uvs[a + 4] - uvs[a]) * (uvs[a + 3] - uvs[a + 1]));
  }
  function islandUvArea(chart) {
    let total = 0;
    for (const t of chart.triangles) total += triangleUvArea(t);
    return total;
  }

  // --- Drawing ----------------------------------------------------------------------------------------------
  function paint(canvas, interactive) {
    const size = Math.max(64, Math.round(canvas.clientWidth * Math.min(window.devicePixelRatio || 1, 2)));
    if (canvas.width !== size || canvas.height !== size) { canvas.width = size; canvas.height = size; }
    const g = canvas.getContext('2d');
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.fillStyle = '#0d0d0f';
    g.fillRect(0, 0, size, size);
    g.setTransform(size, 0, 0, -size, 0, size);
    if (interactive && flags.checker) {
      for (let i = 0; i < 8; i++) for (let j = 0; j < 8; j++) {
        g.fillStyle = (i + j) % 2 ? '#26262b' : '#1b1b20';
        g.fillRect(i / 8, j / 8, 1 / 8, 1 / 8);
      }
    }
    if (layout) {
      layout.charts.forEach((chart, i) => {
        const on = selection.has(i);
        if (!flags.fill && !on) return;
        g.fillStyle = `hsla(${hue(i)} 75% ${on ? 70 : 62}% / ${on ? 0.8 : interactive ? 0.3 : 0.4})`;
        g.fill(paths[i]);
      });
      g.lineJoin = 'round';
      g.lineWidth = 1 / size;
      g.strokeStyle = '#f0f0f0';
      g.stroke(borderPath);
      if (flags.seams || !interactive) {
        g.lineWidth = 1.4 / size;
        g.strokeStyle = '#ffb238';
        g.stroke(seamPath);
      }
    }
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.strokeStyle = '#3a3a3a';
    g.lineWidth = 1;
    g.strokeRect(0.5, 0.5, size - 1, size - 1);
  }

  // --- Inspector preview ------------------------------------------------------------------------------------
  function refreshPreview() {
    const canvas = $('UvPreview');
    if (!canvas) return;
    if (!layout) {
      $('UvCharts').textContent = geometry ? 'unwrapping…' : '—';
      $('UvScale').textContent = '—';
      return;
    }
    paint(canvas, false);
    $('UvCharts').textContent = `${layout.charts.length} islands · ${count(layout.seams.length)} seams`;
    $('UvScale').textContent = `${layout.scale.toExponential(2)} UV per unit`;
  }

  // --- Workspace panels -------------------------------------------------------------------------------------
  function renderRows() {
    const host = $('ChartRows');
    host.innerHTML = '';
    layout.charts.forEach((chart, i) => {
      const row = document.createElement('button');
      row.className = 'chart-row';
      row.dataset.index = i;
      row.innerHTML = `<i></i><div><b>Island ${i + 1}</b><small></small></div><span></span>`;
      row.querySelector('i').style.background = `hsl(${hue(i)} 70% 62%)`;
      const notes = [chart.rotation ? chart.rotation * 90 + '°' : '', chart.mirror ? 'mirrored' : ''].filter(Boolean);
      row.querySelector('small').textContent = [count(chart.triangles.length) + ' tris', ...notes].join(' · ');
      row.querySelector('span').textContent = Math.round(chart.projection * 100) + ' %';
      row.title = 'Click to select · Shift-click for a range · Ctrl/Cmd-click to add or remove';
      row.addEventListener('click', (event) => choose(i, event));
      host.append(row);
    });
    syncRows();
  }
  function syncRows() {
    for (const row of $('ChartRows').children) row.classList.toggle('active', selection.has(+row.dataset.index));
  }
  function choose(index, event = {}) {
    if (index < 0) selection = new Set();
    else if (event.shiftKey && anchor >= 0) {
      const [a, b] = [Math.min(anchor, index), Math.max(anchor, index)];
      selection = new Set();
      for (let i = a; i <= b; i++) selection.add(i);
    } else if (event.ctrlKey || event.metaKey) {
      if (selection.has(index)) selection.delete(index); else selection.add(index);
      anchor = index;
    } else {
      selection = new Set([index]);
      anchor = index;
    }
    refreshSelection();
    if (index >= 0) $('ChartRows').children[index]?.scrollIntoView({ block: 'nearest' });
  }
  function selectAll() {
    if (!layout) return;
    selection = new Set(layout.charts.map((_, i) => i));
    anchor = 0;
    refreshSelection();
  }
  function refreshSelection() {
    syncRows();
    renderDetail();
    paint($('UvCanvas'), true);
  }
  function renderSummary() {
    $('CompilerCount').textContent = `${layout.charts.length} islands · ${count(layout.seams.length)} seams · ${count(layout.borders.length)} borders`;
    $('CompilerScale').textContent = layout.scale.toExponential(3);
    let uvTotal = 0;
    for (let t = 0; t < layout.triangleCount; t++) uvTotal += triangleUvArea(t);
    $('CompilerFill').textContent = (uvTotal * 100).toFixed(1) + ' %';
    $('CompilerSource').textContent = `${getName()} · ${count(layout.triangleCount)} triangles`;
  }
  function renderDetail() {
    const detail = $('ChartDetail');
    if (!selection.size) {
      detail.innerHTML = '<p>Click an island to select it. Shift-click selects a range, Ctrl/Cmd-click adds or removes one, and A selects all. Drag a selected island to move the group.</p>';
      return;
    }
    const picked = [...selection].map((i) => layout.charts[i]);
    const totalArea = layout.charts.reduce((sum, c) => sum + c.area, 0);
    let uvTotal = 0;
    for (let t = 0; t < layout.triangleCount; t++) uvTotal += triangleUvArea(t);
    const tris = picked.reduce((n, c) => n + c.triangles.length, 0);
    const area = picked.reduce((n, c) => n + c.area, 0);
    const uv = picked.reduce((n, c) => n + islandUvArea(c), 0);
    const fit = area > 0 ? picked.reduce((n, c) => n + c.projection * c.area, 0) / area : 1;
    if (picked.length === 1) {
      const chart = picked[0], index = [...selection][0];
      detail.innerHTML = `<div class="rows">
        <span>Island</span><b>${index + 1} of ${layout.charts.length}</b>
        <span>Triangles</span><b>${count(tris)}</b>
        <span>Surface share</span><b>${percent(area / totalArea)}</b>
        <span>UV share</span><b>${percent(uv / Math.max(uvTotal, 1e-12))}</b>
        <span>Projection fit</span><b>${(chart.projection * 100).toFixed(1)} %</b>
        <span>Rotation</span><b>${chart.rotation * 90}°${chart.mirror ? ' · mirrored' : ''}</b>
        <span>Normal</span><b>${chart.normal.map((n) => n.toFixed(2)).join(' ')}</b>
      </div>`;
    } else {
      detail.innerHTML = `<div class="rows">
        <span>Selected</span><b>${picked.length} islands</b>
        <span>Triangles</span><b>${count(tris)}</b>
        <span>Surface share</span><b>${percent(area / totalArea)}</b>
        <span>UV share</span><b>${percent(uv / Math.max(uvTotal, 1e-12))}</b>
        <span>Projection fit</span><b>${(fit * 100).toFixed(1)} %</b>
      </div>`;
    }
  }

  // Applies `mutate` to each selected island, then refreshes the layout and every panel.
  function changeSelected(mutate) {
    if (!selection.size) return toast('Select an island first');
    for (const i of selection) mutate(layout.charts[i]);
    rebuildAll();
    renderRows();
    renderSummary();
    refreshSelection();
    refreshPreview();
  }
  function packAll() {
    if (!layout) return;
    packCharts(layout);
    rebuildAll();
    renderRows();
    renderSummary();
    refreshSelection();
    refreshPreview();
    toast('Packed all islands at one texel density');
  }
  function resetLayout() {
    selection = new Set(); anchor = -1;
    geometry = null;
    ensure();
    renderRows();
    renderSummary();
    refreshSelection();
    refreshPreview();
    toast('Reset to the automatic unwrap');
  }

  // Unit UV space, v up, from a pointer event over the canvas.
  function uvAt(event) {
    const rect = $('UvCanvas').getBoundingClientRect();
    return [(event.clientX - rect.left) / rect.width, 1 - (event.clientY - rect.top) / rect.height];
  }
  function islandAt(u, v) {
    for (let i = layout.charts.length - 1; i >= 0; i--) {
      const chart = layout.charts[i], b = chart.bounds;
      if (u < b[0] || u > b[2] || v < b[1] || v > b[3]) continue;
      for (const t of chart.triangles) {
        const a = t * 6;
        const x0 = uvs[a], y0 = uvs[a + 1], x1 = uvs[a + 2], y1 = uvs[a + 3], x2 = uvs[a + 4], y2 = uvs[a + 5];
        const d1 = (u - x1) * (y0 - y1) - (x0 - x1) * (v - y1);
        const d2 = (u - x2) * (y1 - y2) - (x1 - x2) * (v - y2);
        const d3 = (u - x0) * (y2 - y0) - (x2 - x0) * (v - y0);
        const hasNeg = d1 < 0 || d2 < 0 || d3 < 0, hasPos = d1 > 0 || d2 > 0 || d3 > 0;
        if (!(hasNeg && hasPos)) return i;
      }
    }
    return -1;
  }

  function bindCanvas() {
    const canvas = $('UvCanvas');
    canvas.addEventListener('pointerdown', (event) => {
      const [u, v] = uvAt(event);
      const hit = islandAt(u, v);
      const modifier = event.shiftKey || event.ctrlKey || event.metaKey;
      if (hit < 0) { if (!modifier) choose(-1); return; }
      if (modifier) { choose(hit, event); return; }
      if (!selection.has(hit)) choose(hit, event);
      drag = {
        u, v, moved: false,
        starts: new Map([...selection].map((i) => [i, layout.charts[i].center.slice()])),
      };
      canvas.setPointerCapture(event.pointerId);
    });
    canvas.addEventListener('pointermove', (event) => {
      if (!drag) return;
      const [u, v] = uvAt(event);
      for (const [i, start] of drag.starts) {
        layout.charts[i].center = [start[0] + (u - drag.u), start[1] + (v - drag.v)];
      }
      drag.moved = true;
      uvs = cornerUvs(layout);
      for (const i of drag.starts.keys()) paths[i] = buildIslandPath(layout.charts[i]);
      rebuildOverlay();
      paint(canvas, true);
    });
    const release = () => {
      if (drag && drag.moved) { renderSummary(); renderDetail(); refreshPreview(); }
      drag = null;
    };
    canvas.addEventListener('pointerup', release);
    canvas.addEventListener('pointercancel', release);

    $('CompilerRotate').addEventListener('click', () => changeSelected((c) => { c.rotation = (c.rotation + 1) % 4; }));
    $('CompilerMirror').addEventListener('click', () => changeSelected((c) => { c.mirror = !c.mirror; }));
    $('CompilerPack').addEventListener('click', packAll);
    $('CompilerReset').addEventListener('click', resetLayout);
    for (const [id, key] of [['CompilerChecker', 'checker'], ['CompilerSeams', 'seams'], ['CompilerFillToggle', 'fill']]) {
      $(id).addEventListener('click', () => {
        flags[key] = !flags[key];
        $(id).classList.toggle('active', flags[key]);
        $(id).setAttribute('aria-pressed', String(flags[key]));
        paint(canvas, true);
      });
    }
    $('CompilerExportObj').addEventListener('click', () => {
      const name = getName();
      const blob = new Blob([formatObj(geometry.positions, geometry.indices, uvs, name)], { type: 'text/plain' });
      download(`${slug(name)}-uv.obj`, blob);
      toast('Exported OBJ with UV coordinates');
    });
    $('CompilerExportSvg').addEventListener('click', () => {
      const name = getName();
      download(`${slug(name)}-uv-layout.svg`, new Blob([formatUvSvg(layout, uvs, name)], { type: 'image/svg+xml' }));
      toast('Exported UV layout SVG');
    });
  }
  const slug = (name) => name.trim().replace(/\s+/g, '-').toLowerCase();

  // Called from Studio.js while the workspace is open. Returns true when the key was handled.
  function keydown(event) {
    if (!open) return false;
    const key = event.key.toLowerCase();
    if (key === 'escape') close();
    else if (key === 'r') changeSelected((c) => { c.rotation = (c.rotation + 1) % 4; });
    else if (key === 'm') changeSelected((c) => { c.mirror = !c.mirror; });
    else if (key === 'a') selectAll();
    else if (key === 'p') packAll();
    else if (key === 'k') $('CompilerChecker').click();
    else if (key === 's') $('CompilerSeams').click();
    else return false;
    event.preventDefault();
    return true;
  }

  function show() {
    if (!ensure()) return toast('Wait for the geometry to finish building');
    open = true;
    $('UvWorkspace').hidden = false;
    renderRows();
    renderSummary();
    refreshSelection();
    $('CompilerClose').focus({ preventScroll: true });
  }
  function close() {
    if (!open) return;
    open = false;
    $('UvWorkspace').hidden = true;
    refreshPreview();
  }

  bindCanvas();
  $('OpenCompiler').addEventListener('click', show);
  $('CompilerClose').addEventListener('click', close);

  return {
    get open() { return open; },
    get layout() { return layout; },
    show, close, keydown,
    // Unwrap after a short delay so the hierarchy build finishes first.
    prepare() {
      setTimeout(() => { if (getGeometry()) { ensure(); refreshPreview(); } }, 30);
    },
    refreshPreview,
  };
}
