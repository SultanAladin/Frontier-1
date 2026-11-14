// Geometry Compiler: the UV layout of the current geometry, shown statically in the Inspector and editable in a
// full-screen overlay. Edits move, rotate and mirror whole charts; the layout is packed at one texel density.
import { unwrapCharts, packCharts, cornerUvs } from './Engine/UvCharts.js';
import { formatObj, formatUvSvg } from './Engine/ObjExport.js';

const $ = (id) => document.getElementById(id);
const hue = (i) => (i * 137.508) % 360;

function download(name, blob) {
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 2000);
}

export function createGeometryCompiler({ getGeometry, getName, toast }) {
  let geometry = null, layout = null, uvs = null, paths = [], seamPath = null, borderPath = null;
  let selected = -1, dragging = null, open = false;
  const flags = { checker: true, seams: true, fill: true };

  // --- Layout lifecycle ------------------------------------------------------------------------------------
  function ensure(force = false) {
    const next = getGeometry();
    if (!next) return null;
    if (force || next !== geometry) {
      geometry = next;
      layout = packCharts(unwrapCharts(geometry.positions, geometry.indices));
      selected = -1;
      refreshAll();
    }
    return layout;
  }

  // Recomputes corner UVs, path caches and bounds. Charts keep their own transform; only UV output changes.
  function refreshAll() {
    uvs = cornerUvs(layout);
    paths = layout.charts.map((chart) => buildChartPath(chart));
    rebuildOverlayPaths();
  }
  function refreshAfterMove(index) {
    uvs = cornerUvs(layout);
    paths[index] = buildChartPath(layout.charts[index]);
    rebuildOverlayPaths();
  }
  function buildChartPath(chart) {
    const path = new Path2D();
    let minU = Infinity, minV = Infinity, maxU = -Infinity, maxV = -Infinity;
    for (const t of chart.triangles) {
      const a = t * 6;
      path.moveTo(uvs[a], uvs[a + 1]); path.lineTo(uvs[a + 2], uvs[a + 3]); path.lineTo(uvs[a + 4], uvs[a + 5]); path.closePath();
      for (let k = 0; k < 6; k += 2) { minU = Math.min(minU, uvs[a + k]); maxU = Math.max(maxU, uvs[a + k]); minV = Math.min(minV, uvs[a + k + 1]); maxV = Math.max(maxV, uvs[a + k + 1]); }
    }
    chart.bounds = [minU, minV, maxU, maxV];
    return path;
  }
  function rebuildOverlayPaths() {
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

  // --- Drawing ---------------------------------------------------------------------------------------------
  // Draws the unit square into a size x size canvas with v pointing up.
  function paint(canvas, { interactive }) {
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    const size = Math.max(64, Math.round(canvas.clientWidth * ratio));
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
        const on = i === selected;
        if (flags.fill || on) {
          g.fillStyle = `hsla(${hue(i)} 75% ${on ? 70 : 62}% / ${on ? 0.78 : interactive ? 0.3 : 0.4})`;
          g.fill(paths[i]);
        }
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

  // --- Inspector preview -----------------------------------------------------------------------------------
  function refreshPreview() {
    const canvas = $('UvPreview');
    if (!canvas || !layout) {
      if (canvas) paintEmpty(canvas);
      $('UvCharts').textContent = geometry ? 'unwrapping…' : '—';
      return;
    }
    paint(canvas, { interactive: false });
    $('UvCharts').textContent = `${layout.charts.length} · ${layout.seams.length.toLocaleString('en-US')} seam edges`;
    $('UvScale').textContent = `${layout.scale.toExponential(2)} UV / unit`;
  }
  function paintEmpty(canvas) {
    const g = canvas.getContext('2d'), size = canvas.width || 280;
    if (canvas.width !== size) canvas.width = canvas.height = size;
    g.fillStyle = '#101010'; g.fillRect(0, 0, size, size);
  }

  // --- Overlay ---------------------------------------------------------------------------------------------
  function chartRowsRender() {
    const host = $('ChartRows');
    host.innerHTML = '';
    layout.charts.forEach((chart, i) => {
      const row = document.createElement('button');
      row.className = 'chart-row' + (i === selected ? ' active' : '');
      row.innerHTML = `<i style="background:hsl(${hue(i)} 70% 62%)"></i><div><b>Chart ${i + 1}</b><small></small></div><span></span>`;
      row.querySelector('small').textContent = `${chart.triangles.length.toLocaleString('en-US')} tris${chart.rotation || chart.mirror ? ' · ' + [chart.rotation ? (chart.rotation * 90) + '°' : '', chart.mirror ? 'mirrored' : ''].filter(Boolean).join(' · ') : ''}`;
      row.querySelector('span').textContent = Math.round(chart.projection * 100) + ' %';
      row.title = 'Projection fit: 100 % = flat chart; lower = the chart bends away from its plane';
      row.addEventListener('click', () => select(i));
      host.append(row);
    });
  }
  function select(index) {
    selected = index;
    renderSide();
    for (const row of $('ChartRows').children) row.classList.toggle('active', false);
    if (index >= 0) $('ChartRows').children[index]?.classList.add('active');
    if (index >= 0) $('ChartRows').children[index]?.scrollIntoView({ block: 'nearest' });
    paint($('UvCanvas'), { interactive: true });
  }
  function renderSide() {
    const chart = layout.charts[selected];
    const totalArea = layout.charts.reduce((sum, c) => sum + c.area, 0);
    $('CompilerCount').textContent = `${layout.charts.length} charts · ${layout.seams.length.toLocaleString('en-US')} seams · ${layout.borders.length.toLocaleString('en-US')} border edges`;
    $('CompilerScale').textContent = layout.scale.toExponential(3);
    $('CompilerFill').textContent = (layout.charts.reduce((sum, c) => sum + c.triangles.length, 0) > 0 ? (uvAreaTotal() * 100).toFixed(1) : '0.0') + ' %';
    const detail = $('ChartDetail');
    if (!chart) { detail.innerHTML = '<p>Select a chart to move, rotate or mirror it. Drag a chart on the canvas to move it. Seams are amber; mesh borders are white.</p>'; return; }
    const uvArea = chartUvArea(chart);
    detail.innerHTML = `<div class="rows">
      <span>Chart</span><b>${selected + 1} of ${layout.charts.length}</b>
      <span>Triangles</span><b>${chart.triangles.length.toLocaleString('en-US')}</b>
      <span>Surface share</span><b>${((chart.area / totalArea) * 100).toFixed(2)} %</b>
      <span>UV share</span><b>${((uvArea / Math.max(1e-12, uvAreaTotal())) * 100).toFixed(2)} %</b>
      <span>Projection fit</span><b>${(chart.projection * 100).toFixed(1)} %</b>
      <span>Rotation</span><b>${chart.rotation * 90}°${chart.mirror ? ' · mirrored' : ''}</b>
      <span>Normal</span><b>${chart.normal.map((n) => n.toFixed(2)).join(' ')}</b>
    </div>`;
  }
  function uvAreaTotal() {
    let total = 0;
    for (let t = 0; t < layout.triangleCount; t++) total += triangleUvArea(t);
    return total;
  }
  function triangleUvArea(t) {
    const a = t * 6;
    return 0.5 * Math.abs((uvs[a + 2] - uvs[a]) * (uvs[a + 5] - uvs[a + 1]) - (uvs[a + 4] - uvs[a]) * (uvs[a + 3] - uvs[a + 1]));
  }
  function chartUvArea(chart) {
    let total = 0;
    for (const t of chart.triangles) total += triangleUvArea(t);
    return total;
  }

  function changeSelected(mutate) {
    if (selected < 0) return toast('Select a chart first');
    mutate(layout.charts[selected]);
    refreshAfterMove(selected);
    afterEdit();
  }
  function afterEdit() {
    chartRowsRender();
    renderSide();
    paint($('UvCanvas'), { interactive: true });
    refreshPreview();
  }
  function packAll() {
    packCharts(layout);
    refreshAll();
    afterEdit();
    toast('Packed all charts at one texel density');
  }
  function resetLayout() {
    ensure(true);
    chartRowsRender();
    renderSide();
    paint($('UvCanvas'), { interactive: true });
    refreshPreview();
    toast('Reset to the automatic unwrap');
  }

  // Maps a pointer position to unit UV space (v up).
  function uvAt(event) {
    const rect = $('UvCanvas').getBoundingClientRect();
    return [(event.clientX - rect.left) / rect.width, 1 - (event.clientY - rect.top) / rect.height];
  }
  function hitChart(u, v) {
    for (let i = layout.charts.length - 1; i >= 0; i--) {
      const chart = layout.charts[i], b = chart.bounds;
      if (u < b[0] || u > b[2] || v < b[1] || v > b[3]) continue;
      for (const t of chart.triangles) {
        const a = t * 6;
        const x0 = uvs[a], y0 = uvs[a + 1], x1 = uvs[a + 2], y1 = uvs[a + 3], x2 = uvs[a + 4], y2 = uvs[a + 5];
        const d1 = (u - x1) * (y0 - y1) - (x0 - x1) * (v - y1);
        const d2 = (u - x2) * (y1 - y2) - (x1 - x2) * (v - y2);
        const d3 = (u - x0) * (y2 - y0) - (x2 - x0) * (v - y0);
        const negative = d1 < 0 || d2 < 0 || d3 < 0, positive = d1 > 0 || d2 > 0 || d3 > 0;
        if (!(negative && positive)) return i;
      }
    }
    return -1;
  }

  function bindCanvas() {
    const canvas = $('UvCanvas');
    canvas.addEventListener('pointerdown', (event) => {
      const [u, v] = uvAt(event);
      const hit = hitChart(u, v);
      select(hit);
      if (hit >= 0) {
        dragging = { index: hit, u, v, centre: layout.charts[hit].center.slice(), moved: false };
        canvas.setPointerCapture(event.pointerId);
      }
    });
    canvas.addEventListener('pointermove', (event) => {
      if (!dragging) return;
      const [u, v] = uvAt(event);
      const chart = layout.charts[dragging.index];
      chart.center = [dragging.centre[0] + (u - dragging.u), dragging.centre[1] + (v - dragging.v)];
      dragging.moved = true;
      refreshAfterMove(dragging.index);
      paint(canvas, { interactive: true });
    });
    const end = () => { if (dragging && dragging.moved) afterEdit(); dragging = null; };
    canvas.addEventListener('pointerup', end);
    canvas.addEventListener('pointercancel', end);
    $('CompilerRotate').addEventListener('click', () => changeSelected((c) => { c.rotation = (c.rotation + 1) % 4; }));
    $('CompilerMirror').addEventListener('click', () => changeSelected((c) => { c.mirror = !c.mirror; }));
    $('CompilerPack').addEventListener('click', packAll);
    $('CompilerReset').addEventListener('click', resetLayout);
    for (const [id, key] of [['CompilerChecker', 'checker'], ['CompilerSeams', 'seams'], ['CompilerFillToggle', 'fill']]) {
      $(id).addEventListener('click', () => { flags[key] = !flags[key]; $(id).classList.toggle('active', flags[key]); paint(canvas, { interactive: true }); });
    }
    $('CompilerExportObj').addEventListener('click', () => {
      const name = getName();
      download(`${name.replace(/\s+/g, '-').toLowerCase()}-uv.obj`, new Blob([formatObj(geometry.positions, geometry.indices, uvs, name)], { type: 'text/plain' }));
      toast('Exported OBJ with UV coordinates');
    });
    $('CompilerExportSvg').addEventListener('click', () => {
      const name = getName();
      download(`${name.replace(/\s+/g, '-').toLowerCase()}-uv-layout.svg`, new Blob([formatUvSvg(layout, uvs, name)], { type: 'image/svg+xml' }));
      toast('Exported UV layout SVG');
    });
  }

  function keydown(event) {
    if (!open) return false;
    const key = event.key.toLowerCase();
    if (key === 'escape') { if (selected >= 0) select(-1); else close(); }
    else if (key === 'r' && selected >= 0) changeSelected((c) => { c.rotation = (c.rotation + 1) % 4; });
    else if (key === 'm' && selected >= 0) changeSelected((c) => { c.mirror = !c.mirror; });
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
    $('Compiler').hidden = false;
    $('CompilerSource').textContent = `${getName()} · ${geometry.indices.length / 3} triangles`;
    chartRowsRender();
    renderSide();
    paint($('UvCanvas'), { interactive: true });
    $('CompilerClose').focus({ preventScroll: true });
  }
  function close() {
    open = false;
    $('Compiler').hidden = true;
    refreshPreview();
  }

  bindCanvas();
  $('OpenCompiler').addEventListener('click', show);
  $('CompilerClose').addEventListener('click', close);

  return {
    get open() { return open; },
    show, close, keydown,
    // Called once the geometry is adopted: unwraps lazily so the build itself stays fast.
    prepare() {
      if (!getGeometry()) return;
      setTimeout(() => { ensure(); refreshPreview(); }, 30);
    },
    refreshPreview,
  };
}
