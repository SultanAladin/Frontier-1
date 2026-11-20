// Compact, single-workspace asset inspector built on Geometry Compile's real pipeline.
import { el, iconEl, panel, rangeField, selectField, toast } from '../Shared/Studio/Ui.js';
import { invalidate, runStep, fmt } from '../Shared/GeometryCompile/CompileSteps.js';
import { assetSlots } from '../Shared/Studio/Render.js';
import { ResolveMaterial, ChangeChannel } from '../FrontierWorldMirror/MaterialSpecification.js';
import { renderSdfSliceToCanvas } from '../GeometryCompiler/Engine/SdfRaymarchViewport.js';
import { DEFAULTS as FRACTURE_DEFAULTS, MATERIALS, normalize, shatter } from '../FractureEditor/FractureModel.js';

const definitions = [
  ['geometry', 'Geometry'], ['uv', 'UV charts'], ['sdf', 'SDF'], ['lod', 'Cluster LOD'],
  ['icon', 'Icon & thumbnail'], ['material', 'Material'], ['fracture', 'Fracture'],
];
const running = new WeakSet();
const sdfViews = new WeakMap();

export function createAssetInspection({ asset, component, ctx, miniUv, miniSdf, openEditor, back, update, previewLod, lodPreviewLevel, iconCamera, matchIconAngle, currentView }) {
  const isBusy = () => running.has(asset);
  const view = sdfViews.get(asset) || { axis: 0, depth: 0.5, mip: 0, iconDraft: null, iconCamera: false, look: 'shaded' };
  sdfViews.set(asset, view);
  const sparse = () => asset.sparseSdfSource === asset.out.geometry ? asset.sparseSdf : null;
  const refresh = () => update({ scene: false });
  const applySetting = (step, key, value) => { asset.settings[step][key] = value; invalidate(asset, step); update(); };
  async function bake(step) {
    if (isBusy()) return;
    running.add(asset);
    invalidate(asset, step);
    refresh();
    try {
      if (step === 'sdf') {
        const geometry = asset.out.geometry;
        const volume = await new Promise((resolve, reject) => {
          const worker = new Worker(new URL('../GeometryCompiler/Engine/SdfWorker.js', import.meta.url), { type: 'module' });
          worker.onmessage = (event) => { const m = event.data; if (m.type === 'done') { worker.terminate(); resolve(m.volume); } else if (m.type === 'error') { worker.terminate(); reject(new Error(m.message)); } };
          worker.onerror = (event) => { worker.terminate(); reject(new Error(event.message || 'SDF worker failed')); };
          const positions = new Float32Array(geometry.positions), indices = new Uint32Array(geometry.indices);
          worker.postMessage({ ticket: 1, request: { positions, indices, closed: geometry.closed, gridDim: asset.settings.sdf.resolution, paddingFraction: 0.16, narrowBandMult: 4 } }, [positions.buffer, indices.buffer]);
        });
        if (asset.out.geometry !== geometry) throw new Error('Geometry changed during SDF bake; bake again');
        asset.sparseSdf = volume; asset.sparseSdfSource = geometry;
        asset.steps.sdf.state = 'done'; asset.steps.sdf.summary = `${volume.dim}³ sparse`;
      } else await runStep(asset, step, ctx);
      toast(`${step.toUpperCase()} ready`);
    }
    catch (e) { console.error(e); toast(`${step.toUpperCase()}: ${e.message || e}`); }
    finally { running.delete(asset); update(); }
  }
  function preview(id) {
    const box = el('div', { class: 'asset-inspect-preview' });
    const state = asset.steps[id];
    if (id === 'geometry') {
      box.append(el('b', {}, `${fmt(asset.out.geometry.indices.length / 3)} triangles`), el('small', {}, asset.steps.geometry.summary));
    } else if (id === 'material') {
      const slots = assetSlots(asset);
      box.append(el('div', { class: 'material-swatches' }, ...slots.slice(0, 10).map((s) => el('i', { title: s.name, style: { background: component.props.slots[s.family] || s.color } }))), el('small', {}, `${slots.length} material slot${slots.length === 1 ? '' : 's'}`));
    } else if (id === 'fracture') {
      const f = component.props.fracturePreview;
      if (f) {
        const canvas = el('canvas', { width: 200, height: 150, 'aria-label': 'Fracture proxy preview' });
        const g = canvas.getContext('2d'), size = Math.max(...f.owner.Scale, 0.01);
        g.fillStyle = '#101012'; g.fillRect(0, 0, 200, 150);
        for (const cell of f.cells) for (const face of cell.faces) {
          if (face.pts.length < 3) continue;
          g.beginPath(); face.pts.forEach((p, i) => { const x = 100 + 100 * p[0] / size, y = 75 - 100 * p[1] / size; if (i) g.lineTo(x, y); else g.moveTo(x, y); });
          g.closePath(); g.fillStyle = face.kind === 'inner' ? '#aa9b86' : '#5f6872'; g.fill(); g.strokeStyle = '#242529'; g.stroke();
        }
        box.append(canvas);
      }
      box.append(el('b', {}, f ? `${f.cells.length} preview pieces` : 'No fracture preview'), el('small', {}, 'Imported geometry uses a proxy hull'));
    } else if (id === 'sdf' && sparse() && state.state === 'done') {
      const canvas = el('canvas', { width: 200, height: 200, 'aria-label': 'SDF slice preview' }); box.append(canvas);
      renderSdfSliceToCanvas(canvas, sparse(), { axis: view.axis, depth: view.depth, useMip1: !!view.mip, resolution: 160 });
    } else if (state.state !== 'done') box.append(el('small', {}, state.state === 'running' ? 'Baking…' : state.state === 'error' ? state.error : 'Not baked'));
    else if (id === 'uv' || id === 'sdf') {
      const canvas = el('canvas', { width: 200, height: 200, 'aria-label': id === 'uv' ? 'UV chart preview' : 'SDF slice preview' });
      box.append(canvas); if (id === 'uv') miniUv(canvas, asset); else miniSdf(canvas, asset.out.sdf);
    } else if (id === 'icon') box.append(el('img', { src: asset.out.icon, alt: 'Baked asset icon' }));
    else if (id === 'lod') {
      const levels = asset.out.lod.levels, max = Math.max(1, levels[0].triangles);
      box.append(el('div', { class: 'asset-lod-bars' }, ...levels.map((level, i) => el('span', { title: `Level ${i}: ${level.triangles} triangles` }, el('i', { style: { height: `${Math.max(5, 100 * level.triangles / max)}%` } })))), el('small', {}, asset.steps.lod.summary));
    }
    return box;
  }
  // Cards follow the Geometry Compiler Studio's ICON / UV LAYOUT / SDF VOLUME / GEOMETRY layout.
  function cards() {
    const root = el('div', { class: 'studio-inspection', 'aria-label': 'Asset inspection' });
    const heading = (label) => el('h4', {}, label);
    const text = (t) => el('p', {}, t);
    const button = (label, action, attrs = {}) => { const b = el('button', { type: 'button', ...attrs }, label); b.addEventListener('click', action); return b; };
    const edit = (id, label) => button(label, () => openEditor(id), { class: 'primary', 'data-inspect': id });
    const iconImg = asset.steps.icon.state === 'done' && asset.out.icon;
    const iconPreview = el('div', { class: 'icon-preview' }, view.iconDraft || iconImg ? el('img', { src: view.iconDraft || iconImg, alt: 'Captured icon preview' }) : el('span', { class: 'icon-empty' }, 'No icon yet. Set the view, then capture.'));
    const capture = async () => {
      if (isBusy()) return; running.add(asset); refresh();
      try { if (view.iconCamera || view.look === 'live') matchIconAngle(); asset.settings.icon.shading = view.look === 'live' && currentView() === 'normals' ? 'normals' : 'lit'; view.iconDraft = await ctx.renderIcon(asset); toast('Captured icon; save it to the asset'); }
      catch (e) { toast(`Icon: ${e.message || e}`); }
      finally { running.delete(asset); refresh(); }
    };
    const saveIcon = () => {
      if (!view.iconDraft) return toast('Capture an icon first');
      asset.out.icon = view.iconDraft; asset.steps.icon.state = 'done'; asset.steps.icon.summary = `${asset.settings.icon.size} px · transparent`;
      view.iconDraft = null; update(); toast('Icon saved for this session');
    };
    const savePng = () => { const url = view.iconDraft || iconImg; if (!url) return toast('Capture an icon first'); const a = el('a', { href: url, download: `${asset.name}-icon.png` }); document.body.append(a); a.click(); a.remove(); };
    const copyPng = async () => { try { const url = view.iconDraft || iconImg; if (!url) return toast('Capture an icon first'); const blob = await (await fetch(url)).blob(); await navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]); toast('Copied PNG'); } catch { toast('Clipboard unavailable — use Download PNG'); } };
    const camera = button('Icon camera', () => { view.iconCamera = !view.iconCamera; iconCamera(view.iconCamera); refresh(); }, { class: `tile ${view.iconCamera ? 'on' : ''}`, 'aria-pressed': String(view.iconCamera) }); camera.append(el('small', {}, view.iconCamera ? 'ON' : 'OFF'));
    const reset = button('Default angle', () => { asset.settings.icon.yaw = 35; asset.settings.icon.pitch = 22; refresh(); }, { class: 'tile' }); reset.append(el('small', {}, 'FIT BOX'));
    const looks = el('div', { class: 'segmented', role: 'group', 'aria-label': 'Icon look' }, ...['shaded', 'live'].map((v) => button(v === 'shaded' ? 'Shaded' : 'Live view', () => { view.look = v; refresh(); }, { class: view.look === v ? 'active' : '' })));
    root.append(heading('ICON'), panel(iconPreview, el('div', { class: 'icon-meta' }, view.iconDraft ? 'Captured · not yet saved' : iconImg ? asset.steps.icon.summary : '—'),
      el('div', { class: 'tiles' }, camera, reset),
      selectField({ label: 'Output size', options: ['256', '512', '1024'], value: String(asset.settings.icon.size), onChange: (v) => { asset.settings.icon.size = +v; view.iconDraft = null; refresh(); } }),
      el('div', { class: 'field' }, el('span', {}, 'Look'), looks),
      el('div', { class: 'icon-actions' }, button('Capture', capture, { class: 'primary', disabled: isBusy() }), button('Save as icon', saveIcon)),
      el('div', { class: 'icon-actions' }, button('Download PNG', savePng), button('Copy PNG', copyPng)),
      text('Captures only this asset with a fitted offscreen camera. Live view follows the viewport angle and supports lit or normals; the framing guide is approximate. Icons stay in this session.')));

    const uvCanvas = el('canvas', { class: 'uv-preview', width: 200, height: 200, 'aria-label': 'Static UV layout preview' });
    if (asset.steps.uv.state === 'done') miniUv(uvCanvas, asset);
    const uv = asset.steps.uv.state === 'done' ? asset.out.uv : null;
    const uvRows = el('div', { class: 'rows' }, el('span', {}, 'Islands · seams'), el('b', {}, uv?.layout ? `${uv.layout.charts.length} · ${uv.seams}` : uv ? 'Authored UVs' : '—'), el('span', {}, 'Texel scale'), el('b', {}, uv?.layout ? `${uv.layout.scale.toExponential(2)} UV per unit` : '—'));
    root.append(heading('UV LAYOUT'), panel(uvCanvas, uvRows, edit('uv', 'Open UV Workspace'), text(uv?.authored ? 'Imported UV coordinates. The UV Workspace starts a new unwrap; your authored coordinates remain unless you edit or repack it.' : uv ? 'Planar island unwrap. Amber lines mark seams.' : 'Open the workspace and bake to preview the imported asset’s UV layout.')));

    const volume = asset.steps.sdf.state === 'done' ? sparse() : null;
    const sdfCanvas = el('canvas', { class: 'sdf-preview', width: 200, height: 200, 'aria-label': 'Live SDF cross-section slice preview' });
    const redraw = () => renderSdfSliceToCanvas(sdfCanvas, volume, { axis: view.axis, depth: view.depth, useMip1: !!view.mip, resolution: 160 }); redraw();
    const axis = el('div', { class: 'segmented', role: 'group', 'aria-label': 'SDF slice axis' }, ...['Z', 'Y', 'X'].map((v, i) => button(v, () => { view.axis = i; refresh(); }, { class: view.axis === i ? 'active' : '', 'aria-pressed': String(view.axis === i) })));
    const mip = el('div', { class: 'segmented', role: 'group', 'aria-label': 'SDF mip level' }, ...['Mip0', 'Mip1'].map((v, i) => button(v, () => { view.mip = i; refresh(); }, { class: view.mip === i ? 'active' : '', 'aria-pressed': String(view.mip === i) })));
    const output = el('output', {}, `${['Z', 'Y', 'X'][view.axis]} ${Math.round(view.depth * 100)}%`);
    const slider = el('input', { type: 'range', min: 0.02, max: 0.98, step: 0.005, value: view.depth, 'aria-label': 'Slice depth' });
    slider.addEventListener('input', () => { view.depth = +slider.value; output.textContent = `${['Z', 'Y', 'X'][view.axis]} ${Math.round(view.depth * 100)}%`; redraw(); });
    const mem = (bytes) => bytes >= 1048576 ? `${(bytes / 1048576).toFixed(2)} MB` : `${(bytes / 1024).toFixed(1)} KB`;
    const sdfRows = el('div', { class: 'rows' }, el('span', {}, 'Grid · Mip1'), el('b', {}, volume ? `${volume.dim}³ · ${volume.monotonicityVerified ? 'Conservative ✓' : 'Check'}` : '—'), el('span', {}, 'Sparse bricks'), el('b', {}, volume ? `${volume.allocatedBrickCount}/${volume.totalBricks}` : '—'), el('span', {}, 'Footprint'), el('b', {}, volume ? `${mem(volume.mip0Bytes)} · ${mem(volume.mip1Bytes)}` : '—'));
    root.append(heading('SDF VOLUME'), panel(sdfCanvas, el('div', { class: 'sdf-insp-bar' }, axis, mip), el('div', { class: 'field' }, el('label', {}, 'Slice depth'), output, slider), sdfRows, edit('sdf', 'Open SDF Workspace'), text(volume ? 'Live sparse 8×8×8 signed distance slice; scrub to inspect interior walls.' : 'Bake the selected asset in the SDF workspace to inspect its sparse volume.')));

    root.append(heading('GEOMETRY'), panel(text('Scale, centre, mirror, rotate and flip the source geometry. Nothing rebuilds until Apply.'), edit('geometry', 'Open Geometry Editor')));

    const h = asset.steps.lod.state === 'done' ? asset.out.lod : null;
    let level = h ? Math.min(h.levels.length - 1, Math.max(0, lodPreviewLevel())) : 0;
    const lodCanvas = el('canvas', { class: 'cluster-card-canvas', width: 240, height: 160, 'aria-label': 'Cluster LOD mesh preview' });
    const lodMeta = el('small', {}, h ? '' : 'Build clusters to preview this mesh');
    const lodPreview = el('div', { class: 'asset-inspect-preview cluster-card-preview' }, lodCanvas, lodMeta);
    const lodOutput = el('output', {}, `LOD ${level}`);
    const lodSlider = el('input', { type: 'range', min: 0, max: h ? h.levels.length - 1 : 0, step: 1, value: level, disabled: !h, 'aria-label': 'Preview LOD level' });
    const drawLod = () => {
      const g = lodCanvas.getContext('2d');
      g.clearRect(0, 0, lodCanvas.width, lodCanvas.height);
      if (!h) return;
      const positions = asset.out.geometry.positions, triangles = [];
      // Orthographic, isometric thumbnail of the actual simplified cluster triangles.
      const projected = new Float32Array(positions.length), bounds = [Infinity, Infinity, -Infinity, -Infinity];
      for (let i = 0; i < positions.length; i += 3) {
        const x = positions[i], y = positions[i + 1], z = positions[i + 2];
        const px = .819 * x - .574 * z, py = .22 * x + .924 * y + .315 * z;
        projected[i] = px; projected[i + 1] = py; projected[i + 2] = .53 * x - .383 * y + .757 * z;
        bounds[0] = Math.min(bounds[0], px); bounds[1] = Math.min(bounds[1], py); bounds[2] = Math.max(bounds[2], px); bounds[3] = Math.max(bounds[3], py);
      }
      const scale = Math.min(210 / Math.max(1e-9, bounds[2] - bounds[0]), 135 / Math.max(1e-9, bounds[3] - bounds[1]));
      const cx = (bounds[0] + bounds[2]) / 2, cy = (bounds[1] + bounds[3]) / 2;
      const total = h.levels[level].triangles, stride = Math.max(1, Math.ceil(total / 20000));
      let index = 0;
      for (let c = 0; c < h.clusterCount; c++) if (h.clusterLevel[c] === level) {
        const end = h.clusterFirst[c] + h.clusterSize[c];
        for (let i = h.clusterFirst[c]; i < end; i += 3, index++) {
          if (index % stride) continue;
          const a = h.cornerVertex[i] * 3, b = h.cornerVertex[i + 1] * 3, d = h.cornerVertex[i + 2] * 3;
          triangles.push({ a, b, d, depth: (projected[a + 2] + projected[b + 2] + projected[d + 2]) / 3, color: `hsl(${(c * 137.508) % 360} 55% 58%)` });
        }
      }
      triangles.sort((a, b) => a.depth - b.depth);
      for (const t of triangles) {
        g.beginPath(); g.moveTo(120 + (projected[t.a] - cx) * scale, 80 - (projected[t.a + 1] - cy) * scale);
        g.lineTo(120 + (projected[t.b] - cx) * scale, 80 - (projected[t.b + 1] - cy) * scale);
        g.lineTo(120 + (projected[t.d] - cx) * scale, 80 - (projected[t.d + 1] - cy) * scale);
        g.closePath(); g.fillStyle = t.color; g.fill();
      }
      lodMeta.textContent = `LOD ${level} · ${fmt(h.levels[level].clusters)} clusters · ${fmt(total)} triangles${stride > 1 ? ' · sampled thumbnail' : ''}`;
    };
    drawLod();
    lodSlider.addEventListener('input', () => { level = +lodSlider.value; lodOutput.textContent = `LOD ${level}`; drawLod(); previewLod(level); });
    const lod = panel(lodPreview, el('label', { class: 'field' }, el('span', {}, 'Preview level'), lodOutput, lodSlider));
    lod.append(rangeField({ label: 'Triangles per cluster', value: asset.settings.lod.clusterTriangles, min: 32, max: 256, step: 16, onChange: (v) => applySetting('lod', 'clusterTriangles', v) }), rangeField({ label: 'Keep per level', value: asset.settings.lod.simplifyRatio, min: 0.3, max: 0.7, step: 0.05, onChange: (v) => applySetting('lod', 'simplifyRatio', v) }));
    const preserve = el('input', { type: 'checkbox', class: 'switch' }); preserve.checked = asset.settings.lod.preserveMaterials; preserve.addEventListener('change', () => applySetting('lod', 'preserveMaterials', preserve.checked));
    lod.append(el('label', { class: 'toggle-row' }, el('span', {}, 'Preserve materials'), preserve), el('div', { class: 'icon-actions' }, button(h ? 'Rebuild clusters' : 'Build clusters', () => bake('lod'), { disabled: isBusy() }), button('Preview in viewport', () => previewLod(level), { disabled: !h })), edit('lod', 'Open Cluster Editor'), text('The slider previews a fixed hierarchy level; cluster colours are diagnostic. This is not a Nanite runtime.'));
    root.append(heading('CLUSTER LOD'), lod);
    for (const id of ['fracture']) {
      const title = definitions.find((d) => d[0] === id)[1];
      root.append(heading(title.toUpperCase()), panel(preview(id), edit(id, `Open ${title} Editor`)));
    }
    return root;
  }
  function editor(id) {
    const title = definitions.find((d) => d[0] === id)?.[1] || id;
    const root = el('div', { class: 'asset-editor', 'data-editor': id });
    const backButton = el('button', { type: 'button', class: 'tool', 'data-editor-back': '' }, '← Inspector');
    backButton.addEventListener('click', back);
    root.append(backButton, el('h2', {}, `${title} editor`), el('p', { class: 'muted-note' }, asset.name), preview(id));
    const controls = panel(); root.append(controls);
    const range = (step, key, label, min, max, increment, unit = '') => controls.append(rangeField({ label, min, max, step: increment, unit, value: asset.settings[step][key], onChange: (v) => applySetting(step, key, v) }));
    if (id === 'geometry') {
      for (let i = 0; i < 3; i++) controls.append(rangeField({ label: `Scale ${'XYZ'[i]}`, min: 0.1, max: 4, step: 0.01, value: asset.settings.geometry.scale[i], onChange: (v) => { const scale = [...asset.settings.geometry.scale]; scale[i] = v; applySetting('geometry', 'scale', scale); } }));
      controls.append(selectField({ label: 'Pivot', options: [{ id: 'none', label: 'Authored' }, { id: 'origin', label: 'Centre' }, { id: 'floor', label: 'Floor' }], value: asset.settings.geometry.centre, onChange: (v) => applySetting('geometry', 'centre', v) }));
      controls.append(selectField({ label: 'Normals', options: ['smooth', 'flat', 'angle'], value: asset.settings.geometry.normals, onChange: (v) => applySetting('geometry', 'normals', v) }));
      root.append(el('p', { class: 'muted-note' }, 'Apply rebuilds this shared asset and marks dependent bakes stale.'));
    } else if (id === 'uv') {
      range('uv', 'normalLimitDeg', 'Chart angle', 10, 120, 1, '°'); range('uv', 'mergeBelow', 'Merge small charts', 0, 100, 1);
    } else if (id === 'sdf') {
      controls.append(selectField({ label: 'Grid resolution', options: ['32', '40', '64', '128'], value: String(asset.settings.sdf.resolution), onChange: (v) => applySetting('sdf', 'resolution', +v) }));
      root.append(el('p', { class: 'muted-note' }, 'Studio sparse 8×8×8 volume. Open meshes have an estimated sign; use the inspector axis and slice controls to explore it.'));
    } else if (id === 'lod') {
      range('lod', 'clusterTriangles', 'Triangles per cluster', 32, 256, 16); range('lod', 'simplifyRatio', 'Reduction ratio', 0.2, 0.9, 0.05);
      root.append(el('p', { class: 'muted-note' }, 'Experimental cluster hierarchy, not a Nanite runtime. Bake then preview clusters in the viewport.'));
      if (asset.steps.lod.state === 'done') {
        controls.append(rangeField({ label: 'Preview level', min: 0, max: Math.max(0, asset.out.lod.levels.length - 1), step: 1, value: Math.max(0, component.props.lod), onChange: (v) => { component.props.lod = v; previewLod(); refresh(); } }));
        const show = el('button', { type: 'button', class: 'tool wide' }, 'Preview clusters in viewport'); show.addEventListener('click', previewLod); root.append(show);
      }
    } else if (id === 'icon') {
      range('icon', 'yaw', 'Yaw', -180, 180, 1, '°'); range('icon', 'pitch', 'Pitch', -80, 80, 1, '°');
      controls.append(selectField({ label: 'Size', options: ['128', '256', '512'], value: String(asset.settings.icon.size), onChange: (v) => applySetting('icon', 'size', +v) }));
    } else if (id === 'material') {
      materialEditor(controls, root);
    } else if (id === 'fracture') {
      fractureEditor(controls, root);
    }
    if (asset.steps[id]) {
      const button = el('button', { type: 'button', class: 'primary wide', disabled: isBusy(), 'data-bake': id }, isBusy() ? 'Working…' : id === 'geometry' ? 'Apply geometry' : asset.steps[id].state === 'done' ? 'Rebake' : 'Bake preview');
      button.addEventListener('click', () => bake(id)); root.append(button);
    }
    if (isBusy()) for (const control of root.querySelectorAll('input, select, button')) control.disabled = true;
    return root;
  }
  function materialEditor(controls, root) {
    const slots = assetSlots(asset);
    if (!slots.length) return;
    const family = component.props.materialEditorSlot ?? slots[0].family;
    const slot = slots.find((s) => s.family === family) || slots[0];
    component.props.materialEdits ||= {};
    const material = component.props.materialEdits[slot.family] || ResolveMaterial({ Albedo: component.props.slots[slot.family] || slot.color, Roughness: slot.roughness, Material: { Channels: { metallic: { Fill: slot.metalness } } } });
    controls.append(selectField({ label: 'Material slot', options: slots.map((s) => ({ id: String(s.family), label: s.name })), value: String(slot.family), onChange: (v) => { component.props.materialEditorSlot = +v; refresh(); } }));
    const change = (key, value) => {
      component.props.materialEdits[slot.family] = ChangeChannel(material, key, { Source: 'Fill', Fill: value });
      if (key === 'colour') component.props.slots[slot.family] = value;
      update();
    };
    const color = el('input', { type: 'color', value: material.Channels.colour.Fill, 'aria-label': 'Base colour' }); color.addEventListener('change', () => change('colour', color.value));
    controls.append(el('label', { class: 'field inline' }, el('span', {}, 'Base colour'), color));
    for (const [id, label] of [['roughness', 'Roughness'], ['metallic', 'Metallic']]) controls.append(rangeField({ label, value: material.Channels[id].Fill, min: 0, max: 1, step: 0.01, onChange: (v) => change(id, v) }));
    root.append(el('p', { class: 'muted-note' }, 'Project Zero material channels · basic surface controls. Advanced layered material authoring is not connected yet.'));
  }
  function fractureEditor(controls, root) {
    const settings = normalize(component.props.fracture || FRACTURE_DEFAULTS);
    const set = (key, value) => { component.props.fracture = normalize({ ...settings, [key]: value }); delete component.props.fracturePreview; refresh(); };
    controls.append(selectField({ label: 'Material', options: MATERIALS.map((m) => ({ id: m.id, label: m.name })), value: settings.Material, onChange: (v) => set('Material', v) }));
    controls.append(rangeField({ label: 'Impact energy', value: settings.Energy, min: 0, max: 20000, step: 100, unit: 'J', onChange: (v) => set('Energy', v) }));
    controls.append(rangeField({ label: 'Piece limit', value: settings.Ceiling, min: 2, max: 80, step: 1, onChange: (v) => set('Ceiling', v) }));
    const button = el('button', { type: 'button', class: 'tool wide' }, 'Generate fracture preview');
    button.addEventListener('click', () => {
      try {
        const size = asset.out.geometry.bounds.size.map((v) => Math.max(0.01, v));
        component.props.fracturePreview = shatter({ Id: component.id, Name: asset.name, Primitive: 'imported', Scale: size }, settings);
        refresh();
      } catch (e) { toast(`Fracture: ${e.message || e}`); }
    });
    root.append(button, el('p', { class: 'muted-note' }, 'Fracture Editor recipe and Voronoi model; imported assets currently use a proxy hull. This is an authoring preview, not the runtime solver.'));
  }
  return { cards, editor };
}
