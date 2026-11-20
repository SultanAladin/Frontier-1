// Geometry Compile: the import inspector that opens whenever an object is imported (or recompiled). Left: the source
// card and the pipeline; centre: a preview of the selected step (3D stage, UV sheet, SDF slice / raymarch, icon
// frame, content card); right: the step's settings and outputs. "Compile & open" hands the asset to the
// Component Viewer when this page is framed by it (`host`), otherwise it keeps the asset in this session.
// Workspace: Experimental/GeometryCompiler/Compile.html (split out of the Component Viewer, M0).
import { $, el, iconEl, section, panel, rangeField, segmented, segmentedField, toggle, selectField, vec3Field, textField, rows, metrics, toast, download } from '../Shared/Studio/Ui.js';
import { icon, hydrateIcons } from '../Shared/Studio/Icons.js';
import { STEPS, STEP, invalidate, runStep, compileAll, isCompiled, staleSteps, fmt, formatMs } from '../Shared/GeometryCompile/CompileSteps.js';
import { identityTransform } from '../Shared/GeometryCompile/Engine/GeometryTransform.js';
import { formatObj, formatUvSvg } from '../Shared/GeometryCompile/Engine/ObjExport.js';
import { collisionVolume } from '../Shared/GeometryCompile/Engine/MeshAnalysis.js';
import { THREE, Stage, meshObject, collisionObject, boxOf, assetSlots } from '../Shared/Studio/Render.js';
import { fieldShell, beadTrail, anchorMark, densityColors } from '../Shared/Studio/Visuals.js';
import { createCameraHud } from '../Shared/Studio/Camera.js';

const AUTO = new Set(['source', 'geometry', 'collision', 'meta', 'icon']);
const STATE_LABEL = { done: 'BAKED', stale: 'STALE', running: 'RUNNING', off: 'SKIPPED', error: 'FAILED' };

export function createGeometryCompile({ ctx, onFinish, host = 'Component Viewer' }) {
  const shell = $('Compile');
  let stage = null;
  let asset = null, selected = 'source', busy = false, existing = false, autoTimer = 0;
  const view = { uv: 'layout', sdf: 'slice', sdfAxis: 1, sdfDepth: 0.5, sdfYaw: 0.7, sdfPitch: 0.35, sdfIso: 0, lod: 'clusters', lodLevel: 0, source: 'shaded' };

  // ------------------------------------------------------------------------------------------------- open / close
  function open(target, { recompile = false } = {}) {
    asset = target;
    existing = recompile;
    selected = 'source';
    shell.hidden = false;
    if (!stage) {
      stage = new Stage($('CompileViewport'), { background: '#0b0b0d' });
      stage.frameCallbacks.add(updateReadout);
      createCameraHud(stage.rig, document.querySelector('.compile-well'));
      bindCanvas2d();
    }
    $('CompileLog').innerHTML = '';
    log(`Imported ${asset.fileName || asset.name} · ${asset.format}${asset.fileSize ? ` · ${(asset.fileSize / 1024).toFixed(0)} KB` : ''}`, 'info');
    for (const n of asset.notes) log(n, 'info');
    renderAll();
    // Source and Geometry are cheap: run them so the object is on screen straight away.
    runQueue(['source', 'geometry', 'meta']).then(() => { frameStage(); renderAll(); });
  }

  function close() {
    shell.hidden = true;
    asset = null;
  }

  // ----------------------------------------------------------------------------------------------------- running
  function log(text, kind = 'info') {
    const t = new Date();
    const stamp = `${String(t.getHours()).padStart(2, '0')}:${String(t.getMinutes()).padStart(2, '0')}:${String(t.getSeconds()).padStart(2, '0')}`;
    const box = $('CompileLog');
    box.append(el('div', { class: `log-line ${kind}` }, el('time', {}, stamp), el('span', {}, text)));
    box.scrollTop = box.scrollHeight;
  }

  const runCtx = { ...ctx, log };

  function onProgress(id, fraction, label) {
    const row = $('StepList').querySelector(`[data-step="${id}"] .step-meter i`);
    if (row) row.style.width = `${Math.round(fraction * 100)}%`;
    $('CompileStatus').textContent = `${STEP[id].label} · ${label || ''} ${Math.round(fraction * 100)}%`;
  }

  async function runQueue(ids) {
    if (!asset) return;
    busy = true;
    renderHeader();
    try {
      for (const id of ids) {
        if (['done', 'off'].includes(asset.steps[id].state)) continue;
        await runStep(asset, id, runCtx, (sid, f, l) => { onProgress(sid, f, l); renderSteps(); });
        renderSteps();
      }
    } catch (error) {
      toast(String(error.message || error));
    } finally {
      busy = false;
      if (asset) renderAll();
    }
  }

  async function compileEverything() {
    if (!asset || busy) return false;
    busy = true;
    renderHeader();
    const started = performance.now();
    try {
      await compileAll(asset, runCtx, (sid, f, l) => { onProgress(sid, f, l); renderSteps(); });
      log(`Compiled ${asset.name} in ${formatMs(performance.now() - started)}`, 'ok');
      return true;
    } catch (error) {
      toast(String(error.message || error));
      return false;
    } finally {
      busy = false;
      if (asset) renderAll();
    }
  }

  // Setting changed: mark the step (and downstream) stale, then re-run the cheap ones after a short pause.
  function changed(stepId, { rerender = true } = {}) {
    invalidate(asset, stepId);
    renderSteps();
    renderHeader();
    clearTimeout(autoTimer);
    autoTimer = setTimeout(async () => {
      const queue = STEPS.filter((s) => AUTO.has(s.id) && asset.steps[s.id].state === 'stale').map((s) => s.id)
        .filter((id) => !(id === 'collision' && asset.settings.collision.type === 'sdf' && asset.steps.sdf.state !== 'done'));
      await runQueue(queue);
      if (rerender) renderSettings();
      renderPreview();
    }, 220);
  }

  // ----------------------------------------------------------------------------------------------------- render
  function renderAll() {
    if (!asset) return;
    renderHeader();
    renderSourceCard();
    renderSteps();
    renderSettings();
    renderPreview();
  }

  function renderHeader() {
    const stale = staleSteps(asset);
    const done = STEPS.filter((s) => asset.steps[s.id].state === 'done' || asset.steps[s.id].state === 'off').length;
    $('CompileFile').textContent = `${asset.fileName || asset.name} · ${asset.format}${asset.fileSize ? ` · ${(asset.fileSize / 1024).toFixed(0)} KB` : ''}`;
    $('CompileBar').style.width = `${(done / STEPS.length) * 100}%`;
    if (!busy) $('CompileStatus').textContent = stale.length ? `${stale.length} step${stale.length > 1 ? 's' : ''} to compile` : 'All steps baked · ready';
    $('CompileRunAll').disabled = busy;
    $('CompileOpen').disabled = busy;
    $('CompileOpen').classList.toggle('stale', !isCompiled(asset));
    $('CompileOpenLabel').textContent = existing ? 'Compile & update' : host ? `Compile & open in ${host}` : 'Compile & keep';
  }

  function renderSourceCard() {
    const src = asset.out.source;
    const card = $('SourceCard');
    card.innerHTML = '';
    const thumb = el('div', { class: 'source-thumb-lg' });
    if (asset.out.icon) thumb.append(el('img', { src: asset.out.icon, alt: '' }));
    else thumb.append(el('span', { class: 'format-badge' }, asset.format.split(' ')[0]));
    card.append(el('div', { class: 'source-card-top' }, thumb,
      el('div', { class: 'source-card-text' }, el('b', {}, asset.settings.meta.name || asset.name), el('small', {}, asset.fileName || 'Procedural source'),
        el('div', { class: 'chips' }, el('span', { class: 'chip' }, asset.format), src ? el('span', { class: `chip ${src.topology.closed ? 'ok' : 'warn'}` }, src.topology.closed ? 'Closed' : 'Open') : null))));
    const tris = src ? src.indices.length / 3 : asset.raw.indices.length / 3;
    const verts = src ? src.positions.length / 3 : asset.raw.positions.length / 3;
    card.append(el('div', { class: 'stats compact' },
      el('div', { class: 'stat-tile' }, el('small', {}, 'Triangles'), el('strong', {}, fmt(tris))),
      el('div', { class: 'stat-tile' }, el('small', {}, 'Vertices'), el('strong', {}, fmt(verts)))));
  }

  function renderSteps() {
    const list = $('StepList');
    list.innerHTML = '';
    let total = 0;
    STEPS.forEach((s, i) => {
      const st = asset.steps[s.id];
      total += st.state === 'done' ? st.ms : 0;
      const disc = st.state === 'done' ? el('span', { class: 'status-disc', html: icon('check', 10) })
        : st.state === 'running' ? el('span', { class: 'spinner' })
        : st.state === 'error' ? el('span', { class: 'status-disc error', html: icon('x', 10) })
        : st.state === 'off' ? el('span', { class: 'muted-disc', html: icon('x', 9) })
        : el('span', { class: 'stale-disc' });
      const row = el('button', { type: 'button', class: `entity step-row ${s.id === selected ? 'active' : ''} state-${st.state}`, dataset: { step: s.id } },
        el('span', { class: 'step-index' }, String(i + 1).padStart(2, '0')),
        iconEl(s.icon, 16, 'entity-icon'),
        el('div', {}, el('b', {}, s.label), el('small', {}, st.error || st.summary || s.note), st.state === 'running' ? el('div', { class: 'step-meter' }, el('i')) : null),
        disc);
      row.addEventListener('click', () => { selected = s.id; renderSteps(); renderSettings(); renderPreview(); });
      list.append(row);
    });
    $('CompileTotal').textContent = formatMs(total);
    $('CompileMemory').textContent = estimateMemory();
    $('CompileStepCount').textContent = `${STEPS.filter((s) => asset.steps[s.id].state === 'done').length}/${STEPS.length}`;
  }

  function estimateMemory() {
    let bytes = 0;
    const g = asset.out.geometry;
    if (g) bytes += g.positions.byteLength + g.indices.byteLength + g.normals.byteLength;
    if (asset.out.uv) bytes += asset.out.uv.uvs.byteLength;
    if (asset.out.sdf) bytes += asset.out.sdf.values.byteLength;
    if (asset.out.lod) bytes += asset.out.lod.cornerVertex.byteLength + asset.out.lod.clusterFirst.byteLength * 6;
    return bytes > 1048576 ? `${(bytes / 1048576).toFixed(1)} MB` : `${(bytes / 1024).toFixed(0)} KB`;
  }

  // --------------------------------------------------------------------------------------------------- settings
  function renderSettings() {
    const box = $('StepSettings');
    const scroll = box.scrollTop;
    box.innerHTML = '';
    const s = STEP[selected], st = asset.steps[selected];
    box.append(
      el('div', { class: 'crumbs' }, `Geometry Compile / ${s.label}`),
      el('h1', {}, s.label),
      el('div', { class: 'subtitle' }, st.error ? `Failed · ${st.error}` : st.summary || s.note),
    );
    if (s.optional) {
      box.append(panel(toggle({
        label: 'Include in compile', value: asset.settings[selected].enabled, note: 'Skipped steps produce no data',
        onChange: (v) => { asset.settings[selected].enabled = v; changed(selected); },
      })));
    }
    BUILDERS[selected](box);
    const runnable = !AUTO.has(selected) || st.state === 'error';
    if (runnable && (!s.optional || asset.settings[selected].enabled)) {
      const run = el('button', { type: 'button', class: `primary wide ${st.state === 'stale' ? 'stale' : ''}`, disabled: busy }, iconEl('hammer', 14), st.state === 'done' ? `Re-bake ${s.label}` : `Bake ${s.label}`);
      run.addEventListener('click', () => { invalidate(asset, selected); runQueue([selected]).then(() => { if (selected === 'lod') view.lodLevel = 0; }); });
      box.append(el('div', { class: 'run-row' }, run));
    }
    box.scrollTop = scroll;
  }

  const BUILDERS = {
    source(box) {
      const set = asset.settings.source;
      box.append(section('UNITS & AXES'), panel(
        segmentedField({ label: 'File units', options: ['m', 'cm', 'mm', 'in', 'ft'], value: set.unit, onChange: (v) => { set.unit = v; changed('source'); } }),
        segmentedField({ label: 'Up axis', options: [{ id: 'y', label: 'Y-up' }, { id: 'z', label: 'Z-up' }], value: set.up, onChange: (v) => { set.up = v; changed('source'); } }),
        rangeField({ label: 'Weld tolerance', value: set.weld, min: 0, max: 0.01, step: 0.0005, format: (v) => (v ? `${(v * 1000).toFixed(1)} mm` : 'exact'), onChange: (v) => { set.weld = v; changed('source'); }, note: 'Vertices closer than this merge. Exact keeps only identical positions.' }),
      ));
      box.append(section('FILE'), panel(rows([
        ['Format', asset.format], ['File', asset.fileName || '—'], ['Raw vertices', fmt(asset.raw.positions.length / 3)], ['Raw triangles', fmt(asset.raw.indices.length / 3)],
        ['Material slots', String(assetSlots(asset).length)],
      ])));
      const src = asset.out.source;
      if (src) {
        const t = src.topology;
        box.append(section('TOPOLOGY'), panel(
          metrics([[fmt(src.positions.length / 3), 'VERTICES'], [fmt(src.indices.length / 3), 'TRIANGLES'], [fmt(t.boundary), 'OPEN EDGES'], [String(t.parts), 'PARTS']]),
          rows([['Welded vertices', fmt(src.welded)], ['Dropped triangles', fmt(src.dropped)], ['Non-manifold edges', fmt(t.nonManifold)], ['Surface area', `${src.area.toFixed(3)} m²`], ['Size', src.bounds.size.map((v) => v.toFixed(3)).join(' × ') + ' m']]),
          el('p', { class: `verdict ${t.closed ? 'ok' : 'warn'}` }, t.closed ? 'Closed surface: the SDF sign is exact and the mesh can be voxelised.' : `Open surface: ${fmt(t.boundary)} border edges (amber in the preview). The SDF sign is a guess on open meshes.`),
        ));
      }
    },

    geometry(box) {
      const set = asset.settings.geometry;
      const reset = el('button', { type: 'button', class: 'mini text' }, 'Reset');
      reset.addEventListener('click', () => { Object.assign(set, identityTransform(), { centre: 'floor' }); changed('geometry'); });
      box.append(section('TRANSFORM', reset), panel(
        vec3Field({ label: 'Scale', value: set.scale, step: 0.01, unit: '×', linkable: true, onChange: (v) => { set.scale = v; changed('geometry', { rerender: false }); } }),
        vec3Field({ label: 'Rotate', value: set.rotate, step: 1, unit: 'degrees', onChange: (v) => { set.rotate = v; changed('geometry', { rerender: false }); } }),
        el('div', { class: 'field' }, el('span', {}, 'Mirror'), el('span'), el('div', { class: 'field-wide geo-row3' }, ...['X', 'Y', 'Z'].map((a, i) => {
          const b = el('button', { type: 'button', class: `tool ${set.mirror[i] ? 'active' : ''}` }, a);
          b.addEventListener('click', () => { set.mirror[i] = !set.mirror[i]; b.classList.toggle('active', set.mirror[i]); changed('geometry', { rerender: false }); });
          return b;
        }))),
        toggle({ label: 'Flip normals', value: set.flipNormals, onChange: (v) => { set.flipNormals = v; changed('geometry', { rerender: false }); } }),
      ));
      box.append(section('PIVOT'), panel(
        segmentedField({ label: 'Origin', options: [{ id: 'none', label: 'Keep' }, { id: 'origin', label: 'Centre' }, { id: 'floor', label: 'Floor' }], value: set.centre, onChange: (v) => { set.centre = v; changed('geometry', { rerender: false }); } }),
        el('p', {}, 'Floor puts the pivot at the bottom centre, so the asset stands on the ground when placed.'),
      ));
      const angle = rangeField({ label: 'Hard edge angle', value: set.angle, min: 5, max: 89, step: 1, unit: '°', onChange: (v) => { set.angle = v; changed('geometry', { rerender: false }); } });
      angle.hidden = set.normals !== 'angle';
      box.append(section('NORMALS'), panel(
        segmentedField({ label: 'Shading', options: [{ id: 'smooth', label: 'Smooth' }, { id: 'angle', label: 'By angle' }, { id: 'flat', label: 'Flat' }], value: set.normals, onChange: (v) => { set.normals = v; angle.hidden = v !== 'angle'; changed('geometry', { rerender: false }); } }),
        angle,
      ));
      const g = asset.out.geometry;
      if (g) box.append(section('RESULT'), panel(rows([['Size', g.bounds.size.map((v) => v.toFixed(3)).join(' × ') + ' m'], ['Pivot to floor', `${(-g.bounds.lo[1]).toFixed(3)} m`], ['Radius', `${g.bounds.radius.toFixed(3)} m`]])));
    },

    uv(box) {
      const set = asset.settings.uv;
      box.append(section('UNWRAP'), panel(
        rangeField({ label: 'Chart normal limit', value: set.normalLimitDeg, min: 15, max: 89, step: 1, unit: '°', note: 'Triangles join a chart while their normal stays within this angle of the seed.', onChange: (v) => { set.normalLimitDeg = v; changed('uv'); } }),
        rangeField({ label: 'Merge islands below', value: set.mergeBelow, min: 0, max: 200, step: 1, format: (v) => `${v} tris`, onChange: (v) => { set.mergeBelow = v; changed('uv'); } }),
        rangeField({ label: 'Merge angle limit', value: set.mergeLimitDeg, min: 30, max: 90, step: 1, unit: '°', onChange: (v) => { set.mergeLimitDeg = v; changed('uv'); } }),
      ));
      const uv = asset.out.uv;
      if (uv) {
        const exportObj = el('button', { type: 'button', class: 'tool' }, iconEl('download', 14), 'OBJ + UVs');
        exportObj.addEventListener('click', () => download(`${slug()}.obj`, formatObj(asset.out.geometry.positions, asset.out.geometry.indices, uv.uvs, asset.name), 'text/plain'));
        const exportSvg = el('button', { type: 'button', class: 'tool' }, iconEl('download', 14), 'SVG sheet');
        exportSvg.addEventListener('click', () => download(`${slug()}-uv.svg`, formatUvSvg(uv.layout, uv.uvs, asset.name), 'image/svg+xml'));
        box.append(section('OUTPUT'), panel(
          metrics([[fmt(uv.layout.charts.length), 'CHARTS'], [`${(uv.stats.uvArea * 100).toFixed(1)}%`, 'UV FILL'], [fmt(uv.seams), 'SEAM EDGES'], [uv.stats.texelScale.toFixed(3), 'UV / METRE']]),
          el('div', { class: 'hop' }, exportObj, exportSvg),
          el('p', {}, 'Planar projection charts, shelf packed at one texel density. Charts that bend far from their plane are foreshortened.'),
        ));
      }
    },

    sdf(box) {
      const set = asset.settings.sdf;
      box.append(section('BAKE'), panel(
        segmentedField({ label: 'Resolution', options: [24, 32, 40, 48, 64].map((n) => ({ id: n, label: `${n}³` })), value: set.resolution, onChange: (v) => { set.resolution = +v; changed('sdf'); } }),
        rangeField({ label: 'Narrow band', value: set.band, min: 1, max: 4, step: 0.5, format: (v) => `${v} voxels`, note: 'Distances are exact inside the band and clamped outside it.', onChange: (v) => { set.band = v; changed('sdf'); } }),
      ));
      const sdf = asset.out.sdf;
      const depth = rangeField({ label: 'Slice depth', value: view.sdfDepth, min: 0, max: 1, step: 0.01, format: (v) => `${Math.round(v * 100)}%`, onInput: (v) => { view.sdfDepth = v; renderPreview(); } });
      const iso = rangeField({ label: 'Iso offset', value: view.sdfIso, min: -0.1, max: 0.1, step: 0.005, format: (v) => `${(v * 100).toFixed(1)}% size`, onInput: (v) => { view.sdfIso = v; renderPreview(); } });
      box.append(section('PREVIEW'), panel(
        segmentedField({ label: 'View', options: [{ id: 'slice', label: 'Slice' }, { id: 'raymarch', label: 'Raymarch' }], value: view.sdf, onChange: (v) => { view.sdf = v; renderSettings(); renderPreview(); } }),
        view.sdf === 'slice' ? segmentedField({ label: 'Slice axis', options: [{ id: 0, label: 'X' }, { id: 1, label: 'Y' }, { id: 2, label: 'Z' }], value: view.sdfAxis, onChange: (v) => { view.sdfAxis = +v; renderPreview(); } }) : null,
        view.sdf === 'slice' ? depth : iso,
      ));
      if (sdf) {
        const raw = el('button', { type: 'button', class: 'tool' }, iconEl('download', 14), `Volume .r32 (${sdf.dims}³)`);
        raw.addEventListener('click', () => download(`${slug()}-sdf-${sdf.dims}.r32`, sdf.values.buffer.slice(0), 'application/octet-stream'));
        box.append(section('OUTPUT'), panel(
          metrics([[`${sdf.dims}³`, 'VOXELS'], [`${(sdf.values.byteLength / 1024).toFixed(0)} KB`, 'R32F'], [`${(sdf.insideShare * 100).toFixed(1)}%`, 'INSIDE'], [formatMs(sdf.ms), 'BAKE']]),
          rows([['Voxel size', `${(sdf.voxel * 1000).toFixed(1)} mm`], ['Band', `${(sdf.band * 1000).toFixed(1)} mm`]]),
          raw,
          asset.out.source && !asset.out.source.topology.closed ? el('p', { class: 'verdict warn' }, 'Open surface: crossing parity cannot prove inside from outside, so the sign may flip in places.') : null,
        ));
      }
    },

    lod(box) {
      const set = asset.settings.lod;
      box.append(section('BUILD'), panel(
        segmentedField({ label: 'Cluster size', options: [{ id: 64, label: '64 tris' }, { id: 128, label: '128 tris' }], value: set.clusterTriangles, onChange: (v) => { set.clusterTriangles = +v; changed('lod'); } }),
        rangeField({ label: 'Clusters per group', value: set.groupClusters, min: 2, max: 8, step: 1, onChange: (v) => { set.groupClusters = v; changed('lod'); } }),
        rangeField({ label: 'Keep ratio', value: set.simplifyRatio, min: 0.3, max: 0.7, step: 0.05, format: (v) => `${Math.round(v * 100)}%`, note: 'Each group simplifies its interior to this share; borders stay locked so levels meet without cracks.', onChange: (v) => { set.simplifyRatio = v; changed('lod'); } }),
      ));
      const familyCount = asset.out.geometry && asset.out.geometry.families ? new Set(asset.out.geometry.families).size : 1;
      const seamField = segmentedField({ label: 'Material seams', options: [{ id: 'slide', label: 'Slide' }, { id: 'pin', label: 'Pin' }], value: set.seams || 'slide',
        note: 'Slide: seam vertices may move along the seam, so borders simplify like outlines. Pin: seam vertices never move, so borders stay exact and reduction stops earlier near them.',
        onChange: (v) => { set.seams = v; changed('lod'); } });
      seamField.hidden = set.preserveMaterials === false;
      box.append(section('MATERIALS'), panel(
        toggle({ label: 'Preserve materials', value: set.preserveMaterials !== false,
          note: familyCount > 1 ? `${familyCount} material slots. Each material is clustered as its own mesh and the seams between materials are held, so every face keeps its colour. Clusters on either side still share the same vertices and edges.` : 'Only one material slot on this object, so this has no effect.',
          onChange: (v) => { set.preserveMaterials = v; seamField.hidden = !v; changed('lod', { rerender: false }); } }),
        seamField,
      ));
      const h = asset.out.lod;
      if (h) {
        const top = h.levels.length - 1;
        box.append(section('PREVIEW'), panel(
          segmentedField({ label: 'Colour', options: [{ id: 'clusters', label: 'Clusters' }, { id: 'shaded', label: 'Materials' }], value: view.lod, onChange: (v) => { view.lod = v; renderPreview(); } }),
          rangeField({ label: 'LOD level', value: Math.min(view.lodLevel, top), min: 0, max: top, step: 1, format: (v) => `LOD ${v} of ${top}`, onInput: (v) => { view.lodLevel = v; renderPreview(); highlightLevel(); } }),
        ));
        const table = el('div', { class: 'level-list' });
        const max = h.levels[0].triangles;
        h.levels.forEach((L, i) => {
          const row = el('button', { type: 'button', class: `level-row ${i === view.lodLevel ? 'active' : ''}`, dataset: { level: i } },
            el('i', { class: 'swatch', style: { background: `hsl(${(i * 47) % 360} 55% 58%)` } }),
            el('div', {}, el('b', {}, `LOD ${i}`), el('small', {}, `${fmt(L.clusters)} clusters · err ${L.error.toFixed(4)}${h.materialFamilies > 1 ? ` · wrong mat ${((L.materialError || 0) * 100).toFixed(1)}%` : ''}`)),
            el('div', { class: 'level-meter' }, el('span', {}, fmt(L.triangles)), el('div', { class: 'meter' }, el('i', { style: { width: `${(L.triangles / max) * 100}%` } }))));
          row.addEventListener('click', () => { view.lodLevel = i; renderSettings(); renderPreview(); });
          table.append(row);
        });
        const mixed = h.clusterFamily ? [...h.clusterFamily].filter((f) => f < 0).length : 0;
        const topError = h.levels[top].materialError || 0;
        box.append(section('HIERARCHY'), table, panel(rows([['Clusters', fmt(h.clusterCount)], ['Groups', fmt(h.groupCount)],
          ...(h.materialFamilies > 1 ? [['Materials', `${h.materialFamilies} · ${h.preserveMaterials ? (h.config.pinSeams ? 'seams pinned' : 'seams held') : 'not preserved'}`], ['Mixed-material clusters', fmt(mixed)], ['Wrong material at top LOD', `${(topError * 100).toFixed(1)}% of surface`]] : []),
          ['Build', formatMs(h.buildMs)], ['Stalled', h.stalled ? 'yes' : 'no']])),
          h.materialFamilies > 1 ? el('p', { class: 'muted-note' }, 'Wrong material: the share of a level\'s surface showing a different material from the source surface nearest to it. Cluster borders and material seams index one shared vertex buffer, so neighbouring clusters meet without cracks.') : '');
      }
    },

    collision(box) {
      const set = asset.settings.collision;
      box.append(section('SHAPE'), panel(
        segmentedField({ label: 'Type', options: [{ id: 'box', label: 'Box' }, { id: 'sphere', label: 'Sphere' }, { id: 'capsule', label: 'Capsule' }, { id: 'sdf', label: 'SDF' }, { id: 'none', label: 'None' }], value: set.type, onChange: (v) => { set.type = v; changed('collision'); } }),
        rangeField({ label: 'Margin', value: set.margin, min: 0, max: 0.1, step: 0.005, format: (v) => `${(v * 100).toFixed(1)} cm`, onChange: (v) => { set.margin = v; changed('collision'); } }),
        set.type === 'sdf' ? el('p', {}, 'SDF collision reuses the baked distance volume, so the SDF Bake step runs first.') : null,
      ));
      const c = asset.out.collision, g = asset.out.geometry;
      if (c && g && c.type !== 'none') {
        const boundsVol = g.bounds.size[0] * g.bounds.size[1] * g.bounds.size[2];
        const pairs = [['Type', c.type]];
        if (c.half) pairs.push(['Half extent', c.half.map((v) => v.toFixed(3)).join(' · ')]);
        if (c.radius) pairs.push(['Radius', `${c.radius.toFixed(3)} m`]);
        if (c.halfHeight !== undefined) pairs.push(['Half height', `${c.halfHeight.toFixed(3)} m`], ['Axis', 'XYZ'[c.axis]]);
        if (c.resolution) pairs.push(['Volume', `${c.resolution}³ SDF`]);
        pairs.push(['Shape volume', `${collisionVolume(c).toFixed(3)} m³`], ['vs bounds', `${((collisionVolume(c) / (boundsVol || 1)) * 100).toFixed(0)}%`]);
        box.append(section('OUTPUT'), panel(rows(pairs)));
      }
    },

    icon(box) {
      const set = asset.settings.icon;
      const useView = el('button', { type: 'button', class: 'tool wide' }, iconEl('camera', 14), 'Use current preview angle');
      useView.addEventListener('click', () => {
        const d = stage.camera.position.clone().sub(stage.rig.target).normalize();
        set.yaw = Math.round(THREE.MathUtils.radToDeg(Math.atan2(d.x, d.z)));
        set.pitch = Math.round(THREE.MathUtils.radToDeg(Math.asin(d.y)));
        changed('icon');
      });
      box.append(section('CAMERA'), panel(
        rangeField({ label: 'Yaw', value: set.yaw, min: -180, max: 180, step: 1, unit: '°', onChange: (v) => { set.yaw = v; changed('icon', { rerender: false }); framePreviewAngle(); } }),
        rangeField({ label: 'Pitch', value: set.pitch, min: -10, max: 80, step: 1, unit: '°', onChange: (v) => { set.pitch = v; changed('icon', { rerender: false }); framePreviewAngle(); } }),
        useView,
      ));
      box.append(section('LOOK'), panel(
        segmentedField({ label: 'Size', options: [128, 256, 512].map((n) => ({ id: n, label: `${n}` })), value: set.size, onChange: (v) => { set.size = +v; changed('icon'); } }),
        segmentedField({ label: 'Shading', options: [{ id: 'lit', label: 'Lit' }, { id: 'clay', label: 'Clay' }, { id: 'normals', label: 'Normals' }], value: set.shading, onChange: (v) => { set.shading = v; changed('icon'); } }),
        segmentedField({ label: 'Background', options: [{ id: 'transparent', label: 'Clear' }, { id: 'dark', label: 'Dark' }, { id: 'studio', label: 'Studio' }], value: set.background, onChange: (v) => { set.background = v; changed('icon'); } }),
      ));
      const preview = el('div', { class: 'icon-preview' });
      if (asset.out.icon) preview.append(el('img', { src: asset.out.icon, alt: 'Baked icon' }));
      else preview.append(el('div', { class: 'icon-empty' }, 'The icon bakes from the compiled geometry.'));
      const dl = el('button', { type: 'button', class: 'tool', disabled: !asset.out.icon }, iconEl('download', 14), 'PNG');
      dl.addEventListener('click', () => download(`${slug()}-icon.png`, asset.out.icon));
      box.append(section('RESULT'), preview, el('div', { class: 'icon-meta' }, `${set.size} px · ${set.background} · ${set.shading}`), dl);
    },

    meta(box) {
      const m = asset.settings.meta;
      box.append(section('IDENTITY'), panel(
        textField({ label: 'Name', value: m.name, onChange: (v) => { m.name = v; changed('meta'); renderSourceCard(); } }),
        selectField({ label: 'Category', options: ['Prop', 'Vehicle', 'Vehicle part', 'Hero', 'Architecture', 'Nature', 'Kit piece'], value: m.category, onChange: (v) => { m.category = v; changed('meta'); } }),
        textField({ label: 'Tags', value: m.tags, placeholder: 'comma, separated', onChange: (v) => { m.tags = v; changed('meta'); } }),
        textField({ label: 'Notes', value: m.notes, multiline: true, onChange: (v) => { m.notes = v; changed('meta'); } }),
      ));
    },
  };

  function highlightLevel() {
    for (const r of $('StepSettings').querySelectorAll('.level-row')) r.classList.toggle('active', +r.dataset.level === view.lodLevel);
  }

  const slug = () => (asset.settings.meta.name || asset.name).toLowerCase().replace(/[^a-z0-9]+/g, '-');

  // ---------------------------------------------------------------------------------------------------- preview
  let lastFramed = '';

  function frameStage() {
    const g = asset && asset.out.geometry;
    if (!g) return;
    const box = new THREE.Box3(new THREE.Vector3(...g.bounds.lo), new THREE.Vector3(...g.bounds.hi));
    stage.frame(box);
    lastFramed = asset.id;
  }

  function framePreviewAngle() {
    const g = asset.out.geometry;
    if (!g) return;
    stage.frame(new THREE.Box3(new THREE.Vector3(...g.bounds.lo), new THREE.Vector3(...g.bounds.hi)), { yaw: asset.settings.icon.yaw, pitch: asset.settings.icon.pitch });
  }

  function renderPreview() {
    if (!asset) return;
    const s = STEP[selected], st = asset.steps[selected];
    $('PreviewTab').textContent = s.label;
    $('PreviewTitle').textContent = asset.settings.meta.name || asset.name;
    $('PreviewSubtitle').textContent = `${s.label} · ${st.summary || s.note}`;
    const tag = $('PreviewState');
    tag.textContent = STATE_LABEL[st.state];
    tag.className = `state-tag ${st.state === 'done' ? 'live' : st.state === 'error' ? 'bad' : ''}`;
    const canvas2d = $('Compile2d'), well3d = $('CompileViewport');
    const card = $('CompileCard'), capture = $('CaptureFrame'), empty = $('CompileEmpty');
    card.hidden = true; capture.hidden = true; empty.hidden = true;
    const modes = $('PreviewModes');
    modes.innerHTML = '';
    let mode2d = false;

    if (selected === 'uv') {
      modes.append(segmented({ options: [{ id: 'layout', label: 'UV sheet' }, { id: 'checker', label: 'Checker on mesh' }], value: view.uv, onChange: (v) => { view.uv = v; renderPreview(); } }));
      mode2d = view.uv === 'layout';
    }
    if (selected === 'sdf') {
      modes.append(segmented({ options: [{ id: 'slice', label: 'Slice' }, { id: 'raymarch', label: 'Raymarch' }], value: view.sdf, onChange: (v) => { view.sdf = v; renderSettings(); renderPreview(); } }));
      mode2d = true;
    }
    if (selected === 'source') {
      modes.append(segmented({ options: [{ id: 'shaded', label: 'Shaded' }, { id: 'density', label: 'Density' }], value: view.source, onChange: (v) => { view.source = v; renderPreview(); } }));
    }
    if (selected === 'meta') { card.hidden = false; renderCard(); }

    canvas2d.hidden = !mode2d;
    well3d.style.visibility = mode2d || selected === 'meta' ? 'hidden' : 'visible';
    const g = asset.out.geometry;
    if (!g && selected !== 'meta') { empty.hidden = false; empty.textContent = st.state === 'error' ? st.error : 'Reading source…'; stage.setContent(null); stage.setOverlay(null); return; }
    if (lastFramed !== asset.id) frameStage();

    if (mode2d) {
      if (selected === 'uv') drawUv(canvas2d);
      else drawSdf(canvas2d);
      return;
    }
    // 3D previews
    let object = null, overlay = null;
    if (selected === 'source') {
      object = sourceObject(view.source === 'density');
      overlay = openEdges();
    } else if (selected === 'geometry') {
      object = meshObject(asset, { channel: 'lit' });
      overlay = anchorMark(Math.max(0.06, g.bounds.radius * 0.12)); // the pivot the Geometry step placed
    } else if (selected === 'uv') {
      object = meshObject(asset, { channel: 'uv' });
    } else if (selected === 'lod') {
      object = asset.out.lod ? meshObject(asset, { channel: view.lod === 'clusters' ? 'lod' : 'lit', lod: view.lodLevel }) : meshObject(asset, { channel: 'density' });
    } else if (selected === 'collision') {
      object = meshObject(asset, { channel: 'collision' });
    } else if (selected === 'icon') {
      object = meshObject(asset, { channel: 'lit' });
      capture.hidden = false;
    }
    stage.setContent(object);
    stage.setOverlay(overlay);
  }

  function sourceObject(density) {
    const src = asset.out.source;
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(src.positions, 3));
    geo.setIndex(new THREE.BufferAttribute(src.indices, 1));
    geo.computeVertexNormals();
    // Show the source where the Geometry step will put it (same pivot), so switching steps does not jump.
    const g = asset.out.geometry;
    const group = new THREE.Group();
    const shown = density ? geo.toNonIndexed() : geo;
    if (density) { shown.setAttribute('color', new THREE.BufferAttribute(densityColors(shown.attributes.position.array), 3)); shown.computeVertexNormals(); }
    const mesh = new THREE.Mesh(shown, density
      ? new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.55, side: THREE.DoubleSide, flatShading: true })
      : new THREE.MeshStandardMaterial({ color: '#9aa3b2', roughness: 0.6, side: THREE.DoubleSide, flatShading: false }));
    mesh.castShadow = true;
    group.add(mesh);
    if (g) { const b = src.bounds; group.position.set(g.bounds.centre[0] - b.centre[0], g.bounds.lo[1] - b.lo[1], g.bounds.centre[2] - b.centre[2]); }
    // Source bounds as a faint field: soft borders pooled at the faces, no edge lines.
    const lo = src.bounds.lo, hi = src.bounds.hi;
    group.add(fieldShell({ type: 'box', centre: lo.map((v, i) => (v + hi[i]) / 2), half: lo.map((v, i) => Math.max(1e-3, (hi[i] - v) / 2)) }, '#7d8aa3', { base: 0, rim: 0.06, scan: 0, border: 0.16 }));
    return group;
  }

  function openEdges() {
    const src = asset.out.source;
    if (!src || src.topology.closed) return null;
    const n = src.positions.length / 3, uses = new Map();
    for (let t = 0; t < src.indices.length; t += 3) for (let k = 0; k < 3; k++) {
      const a = src.indices[t + k], b = src.indices[t + ((k + 1) % 3)];
      const key = a < b ? a * n + b : b * n + a;
      uses.set(key, (uses.get(key) || 0) + 1);
    }
    const pts = [];
    for (const [key, count] of uses) if (count === 1) {
      const a = Math.floor(key / n), b = key % n;
      pts.push(src.positions[a * 3], src.positions[a * 3 + 1], src.positions[a * 3 + 2], src.positions[b * 3], src.positions[b * 3 + 1], src.positions[b * 3 + 2]);
    }
    // Open borders as a trail of glowing amber beads, spaced to the object's size.
    const lines = beadTrail(pts, '#ffb238', { spacing: Math.max(0.004, src.bounds.radius * 0.012), size: Math.max(0.01, src.bounds.radius * 0.02) });
    const g = asset.out.geometry;
    if (g) { const b = src.bounds; lines.position.set(g.bounds.centre[0] - b.centre[0], g.bounds.lo[1] - b.lo[1], g.bounds.centre[2] - b.centre[2]); }
    return lines;
  }

  function updateReadout() {
    if (!asset || shell.hidden) return;
    const g = asset.out.geometry;
    const lines = [`${stage.stats.fps.toFixed(0).padStart(3)} fps`];
    if (g) {
      lines.push(`tris    ${fmt(g.indices.length / 3)}`, `size    ${g.bounds.size.map((v) => v.toFixed(2)).join(' × ')} m`);
      if (selected === 'lod' && asset.out.lod) { const L = asset.out.lod.levels[Math.min(view.lodLevel, asset.out.lod.levels.length - 1)]; lines.push(`lod ${view.lodLevel}   ${fmt(L.triangles)} tris · ${L.clusters} clusters`); }
      if (selected === 'collision' && asset.out.collision) lines.push(`shape   ${asset.out.collision.type}`);
    }
    $('CompileReadout').textContent = lines.join('\n');
  }

  function renderCard() {
    const m = asset.settings.meta;
    const card = $('CompileCard');
    card.innerHTML = '';
    const tags = m.tags.split(',').map((t) => t.trim()).filter(Boolean);
    card.append(el('div', { class: 'content-card big' },
      el('div', { class: 'content-thumb' }, asset.out.icon ? el('img', { src: asset.out.icon, alt: '' }) : el('span', { class: 'format-badge' }, asset.format.split(' ')[0])),
      el('div', { class: 'content-meta' }, el('b', {}, m.name || asset.name), el('small', {}, `${m.category} · ${asset.out.geometry ? fmt(asset.out.geometry.indices.length / 3) + ' tris' : '—'}`),
        el('div', { class: 'chips' }, ...tags.map((t) => el('span', { class: 'chip' }, t))),
        m.notes ? el('p', {}, m.notes) : null),
    ), el('p', { class: 'card-caption' }, 'How this asset appears in the Content tray and the Static Mesh picker.'));
  }

  // ------------------------------------------------------------------------------------------------ 2D canvases
  function bindCanvas2d() {
    const c = $('Compile2d');
    let drag = null;
    c.addEventListener('pointerdown', (e) => { if (selected !== 'sdf' || view.sdf !== 'raymarch') return; drag = { x: e.clientX, y: e.clientY }; c.setPointerCapture(e.pointerId); });
    c.addEventListener('pointermove', (e) => {
      if (!drag) return;
      view.sdfYaw += (e.clientX - drag.x) * 0.01;
      view.sdfPitch = Math.max(-1.4, Math.min(1.4, view.sdfPitch + (e.clientY - drag.y) * 0.01));
      drag = { x: e.clientX, y: e.clientY };
      drawSdf(c, 96);
    });
    c.addEventListener('pointerup', () => { if (drag) { drag = null; drawSdf(c); } });
    new ResizeObserver(() => { if (!c.hidden) renderPreview(); }).observe(c.parentElement);
  }

  function fit2d(canvas) {
    const host = canvas.parentElement;
    const side = Math.max(160, Math.min(host.clientWidth, host.clientHeight) - 28);
    canvas.style.width = canvas.style.height = `${side}px`;
    const px = Math.round(side * Math.min(devicePixelRatio || 1, 2));
    if (canvas.width !== px) { canvas.width = px; canvas.height = px; }
    return px;
  }

  function drawUv(canvas) {
    const size = fit2d(canvas);
    const g = canvas.getContext('2d');
    g.fillStyle = '#0d0d0f';
    g.fillRect(0, 0, size, size);
    const uv = asset.out.uv;
    const pad = size * 0.04, s = size - pad * 2;
    g.strokeStyle = '#2c2c2c';
    g.lineWidth = 1;
    for (let i = 0; i <= 8; i++) { const x = pad + (s * i) / 8; g.beginPath(); g.moveTo(x, pad); g.lineTo(x, pad + s); g.moveTo(pad, x); g.lineTo(pad + s, x); g.stroke(); }
    if (!uv) {
      g.fillStyle = '#6e6e6e';
      g.font = `${Math.round(size / 40)}px DM Sans, sans-serif`;
      g.textAlign = 'center';
      g.fillText(asset.steps.uv.state === 'off' ? 'UV Charts is skipped for this asset' : 'Bake UV Charts to see the layout', size / 2, size / 2);
      return;
    }
    const X = (u) => pad + u * s, Y = (v) => pad + (1 - v) * s;
    const { layout, uvs } = uv;
    layout.charts.forEach((chart, ci) => {
      g.fillStyle = `hsla(${(ci * 137.508) % 360} 50% 58% / 0.55)`;
      g.beginPath();
      for (const t of chart.triangles) {
        const a = t * 6;
        g.moveTo(X(uvs[a]), Y(uvs[a + 1])); g.lineTo(X(uvs[a + 2]), Y(uvs[a + 3])); g.lineTo(X(uvs[a + 4]), Y(uvs[a + 5])); g.closePath();
      }
      g.fill();
    });
    const segs = (slots, color, width) => {
      g.strokeStyle = color; g.lineWidth = width; g.beginPath();
      for (const slot of slots) {
        const t = (slot / 3) | 0, k = slot % 3;
        const a = (t * 3 + k) * 2, b = (t * 3 + ((k + 1) % 3)) * 2;
        g.moveTo(X(uvs[a]), Y(uvs[a + 1])); g.lineTo(X(uvs[b]), Y(uvs[b + 1]));
      }
      g.stroke();
    };
    segs(layout.borders, '#f0f0f0', Math.max(1, size / 900));
    segs(layout.seams, '#ffb238', Math.max(1, size / 1100));
    g.strokeStyle = '#4a4a4a'; g.lineWidth = 1.5; g.strokeRect(pad, pad, s, s);
  }

  function drawSdf(canvas, quick = 0) {
    const size = fit2d(canvas);
    const g = canvas.getContext('2d');
    g.fillStyle = '#0d0d0f';
    g.fillRect(0, 0, size, size);
    const sdf = asset.out.sdf;
    if (!sdf) {
      g.fillStyle = '#6e6e6e';
      g.font = `${Math.round(size / 40)}px DM Sans, sans-serif`;
      g.textAlign = 'center';
      g.fillText(asset.steps.sdf.state === 'running' ? 'Baking…' : asset.steps.sdf.state === 'off' ? 'SDF Bake is skipped for this asset' : 'Bake the SDF to preview the distance field', size / 2, size / 2);
      return;
    }
    const n = sdf.dims, vals = sdf.values;
    const at = (i, j, k) => vals[i + n * (j + n * k)];
    if (view.sdf === 'slice') {
      const img = g.createImageData(n, n);
      const s = Math.round(view.sdfDepth * (n - 1));
      const ring = sdf.band / 3;
      for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
        const row = n - 1 - y;
        let v;
        if (view.sdfAxis === 0) v = at(s, row, x);
        else if (view.sdfAxis === 1) v = at(x, s, n - 1 - y);
        else v = at(x, row, s);
        const t = Math.min(1, Math.abs(v) / sdf.band);
        const band = Math.abs(((Math.abs(v) / ring) % 1) - 0.5) < 0.08 ? 0.75 : 1;
        let r, gg, b;
        if (v < 0) { r = 40 + 30 * t; gg = 110 + 80 * t; b = 230; }
        else { r = 240; gg = 140 + 70 * (1 - t); b = 60 + 40 * (1 - t); }
        const fade = 0.25 + 0.75 * (1 - t);
        const o = (y * n + x) * 4;
        img.data[o] = r * fade * band; img.data[o + 1] = gg * fade * band; img.data[o + 2] = b * fade * band; img.data[o + 3] = 255;
        if (Math.abs(v) < sdf.voxel * 0.5) { img.data[o] = img.data[o + 1] = img.data[o + 2] = 245; }
      }
      const tmp = document.createElement('canvas');
      tmp.width = tmp.height = n;
      tmp.getContext('2d').putImageData(img, 0, 0);
      g.imageSmoothingEnabled = true;
      const pad = size * 0.04;
      g.drawImage(tmp, pad, pad, size - pad * 2, size - pad * 2);
      g.fillStyle = '#c9c9c9';
      g.font = `${Math.round(size / 55)}px ui-monospace, monospace`;
      g.fillText(`${'XYZ'[view.sdfAxis]} slice ${s + 1}/${n} · blue inside · amber outside · white = surface`, pad, size - pad * 0.35);
      return;
    }
    // CPU raymarch of the baked grid (trilinear), shaded with the field gradient.
    const res = quick || Math.min(240, Math.round(size / 3));
    const img = g.createImageData(res, res);
    const ext = sdf.extent, o = sdf.origin, h = sdf.voxel, centre = sdf.centre;
    const iso = view.sdfIso * ext;
    const sample = (x, y, z) => {
      const fx = (x - o[0]) / h, fy = (y - o[1]) / h, fz = (z - o[2]) / h;
      if (fx < 0 || fy < 0 || fz < 0 || fx > n - 1 || fy > n - 1 || fz > n - 1) return sdf.band;
      const i = Math.min(n - 2, fx | 0), j = Math.min(n - 2, fy | 0), k = Math.min(n - 2, fz | 0);
      const tx = fx - i, ty = fy - j, tz = fz - k;
      const l = (a, b, t) => a + (b - a) * t;
      return l(l(l(at(i, j, k), at(i + 1, j, k), tx), l(at(i, j + 1, k), at(i + 1, j + 1, k), tx), ty),
        l(l(at(i, j, k + 1), at(i + 1, j, k + 1), tx), l(at(i, j + 1, k + 1), at(i + 1, j + 1, k + 1), tx), ty), tz) - iso;
    };
    const cy = Math.cos(view.sdfYaw), sy = Math.sin(view.sdfYaw), cp = Math.cos(view.sdfPitch), sp = Math.sin(view.sdfPitch);
    const fwd = [-sy * cp, -sp, -cy * cp], right = [cy, 0, -sy], up = [-sy * sp, cp, -cy * sp];
    const dist = ext * 1.6;
    const eye = [centre[0] - fwd[0] * dist, centre[1] - fwd[1] * dist, centre[2] - fwd[2] * dist];
    const light = [0.45, 0.8, 0.4];
    const span = ext * 0.62;
    for (let py = 0; py < res; py++) for (let px = 0; px < res; px++) {
      const u = (px / (res - 1) - 0.5) * 2 * span, v = (0.5 - py / (res - 1)) * 2 * span;
      // Orthographic rays: origin moves on the image plane, direction is the view direction.
      let x = eye[0] + right[0] * u + up[0] * v, y = eye[1] + right[1] * u + up[1] * v, z = eye[2] + right[2] * u + up[2] * v;
      let t = 0, hit = false;
      for (let step = 0; step < 160 && t < dist * 2; step++) {
        const d = sample(x, y, z);
        if (d < h * 0.15) { hit = true; break; }
        const adv = Math.max(d * 0.85, h * 0.3);
        x += fwd[0] * adv; y += fwd[1] * adv; z += fwd[2] * adv; t += adv;
      }
      const oi = (py * res + px) * 4;
      if (!hit) { const shade = 13 + (py / res) * 6; img.data[oi] = shade; img.data[oi + 1] = shade; img.data[oi + 2] = shade + 2; img.data[oi + 3] = 255; continue; }
      const e = h * 0.7;
      let nx = sample(x + e, y, z) - sample(x - e, y, z), ny = sample(x, y + e, z) - sample(x, y - e, z), nz = sample(x, y, z + e) - sample(x, y, z - e);
      const nl = Math.hypot(nx, ny, nz) || 1; nx /= nl; ny /= nl; nz /= nl;
      const lam = Math.max(0, nx * light[0] + ny * light[1] + nz * light[2]);
      const rim = Math.pow(1 - Math.max(0, -(nx * fwd[0] + ny * fwd[1] + nz * fwd[2])), 3);
      img.data[oi] = 40 + 150 * lam + 60 * rim; img.data[oi + 1] = 60 + 150 * lam + 80 * rim; img.data[oi + 2] = 80 + 140 * lam + 110 * rim; img.data[oi + 3] = 255;
    }
    const tmp = document.createElement('canvas');
    tmp.width = tmp.height = res;
    tmp.getContext('2d').putImageData(img, 0, 0);
    g.imageSmoothingEnabled = true;
    g.drawImage(tmp, 0, 0, size, size);
    g.fillStyle = '#c9c9c9';
    g.font = `${Math.round(size / 55)}px ui-monospace, monospace`;
    g.fillText(`raymarch ${res}² · ${n}³ grid · drag to orbit`, size * 0.04, size * 0.96);
  }

  // -------------------------------------------------------------------------------------------------- buttons
  $('CompileCancel').addEventListener('click', () => { const a = asset; close(); onFinish(a, { cancelled: true, existing }); });
  $('CompileRunAll').addEventListener('click', () => compileEverything());
  $('CompileOpen').addEventListener('click', async () => {
    const ok = await compileEverything();
    if (!ok) return;
    const a = asset;
    close();
    onFinish(a, { open: true, existing });
  });
  addEventListener('keydown', (e) => {
    if (shell.hidden || e.target.matches('input, textarea, select')) return;
    if (e.key === 'Escape') $('CompileCancel').click();
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      const i = STEPS.findIndex((s) => s.id === selected);
      const next = STEPS[Math.max(0, Math.min(STEPS.length - 1, i + (e.key === 'ArrowDown' ? 1 : -1)))];
      selected = next.id; renderSteps(); renderSettings(); renderPreview();
      e.preventDefault();
    }
    if (e.key === 'f' || e.key === 'F') frameStage();
  });
  hydrateIcons(shell);

  return { open, close, get asset() { return asset; } };
}
