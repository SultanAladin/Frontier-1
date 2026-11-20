// SDF Workspace + Inspector SDF Slice preview controller.
// Runs Sparse 8x8x8 SDF baking asynchronously in Engine/SdfWorker.js, renders a live 2D slice preview
// with a depth slider right inside the Inspector, and opens the full-screen 3D WebGPU Raymarch + Baker workspace.

import { selectBakeIndices, serializeVolumeFvol } from './Engine/SparseSdfBaker.js';
import { SdfRaymarchViewport, renderSdfSliceToCanvas } from './Engine/SdfRaymarchViewport.js';

const $ = (id) => document.getElementById(id);
const AXIS_LABELS = ['Z', 'Y', 'X'];
const VIZ_LABELS = ['Iso + AO', '3D Cutaway', 'Step Heat', 'Sparse Bricks'];

function formatKb(bytes) {
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(2) + ' MB';
  return (bytes / 1024).toFixed(1) + ' KB';
}

function downloadBlob(name, blob) {
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 2000);
}

export function createSdfWorkspace({ getGeometry, getName, toast }) {
  const state = {
    gridDim: 64,
    paddingFraction: 0.16,
    narrowBandMult: 4.0,
    vramBudgetKb: 8192,
    previewMode: 'raymarch', // 'raymarch' | 'slice2d'
    vizMode: 0,              // 0=Iso+AO, 1=3D Cutaway, 2=Step Heat, 3=Sparse Bricks
    sliceAxis: 0,            // 0=Z, 1=Y, 2=X
    sliceDepth: 0.5,         // 0.02..0.98
    sliceLevel: 0,           // 0=Mip0+1, 1=Mip1
    isoOffsetMm: 0,          // -40..+40 mm
    cutFlip: false,
    autoSweep: false,
    sweepDir: 1,
    camera: { target: [0, 0, 0], yaw: 0.65, pitch: 0.34, distance: 3.1 },
  };

  let volume = null;
  let baking = false;
  let open = false;
  let worker = null;
  let ticket = 0;
  let rayViewport = null;
  let rafId = 0;
  let lastTs = 0;
  let drag = null;
  let sliceDirty = true;
  let stageDirty = true;
  let lastStageSize = '';

  function markStageDirty() {
    stageDirty = true;
  }

  function setSliderVisual(el, min, max, val) {
    if (!el) return;
    el.value = String(val);
    const pct = ((val - min) / Math.max(1e-6, max - min)) * 100;
    el.style.setProperty('--fill', `${Math.max(0, Math.min(100, pct)).toFixed(1)}%`);
  }

  function sliceWorldCoord() {
    if (!volume) return '—';
    const axIdx = state.sliceAxis === 0 ? 2 : state.sliceAxis === 1 ? 1 : 0;
    const lo = volume.boundsMin[axIdx];
    const hi = volume.boundsMax[axIdx];
    return (lo + state.sliceDepth * (hi - lo)).toFixed(2) + ' u';
  }

  function fitSdfCamera() {
    if (!volume) return;
    const cx = (volume.boundsMin[0] + volume.boundsMax[0]) * 0.5;
    const cy = (volume.boundsMin[1] + volume.boundsMax[1]) * 0.5;
    const cz = (volume.boundsMin[2] + volume.boundsMax[2]) * 0.5;
    const span = Math.max(
      volume.boundsMax[0] - volume.boundsMin[0],
      volume.boundsMax[1] - volume.boundsMin[1],
      volume.boundsMax[2] - volume.boundsMin[2],
    );
    state.camera.target = [cx, cy, cz];
    state.camera.yaw = 0.65;
    state.camera.pitch = 0.34;
    state.camera.distance = Math.max(1.2, span * 1.48);
    stageDirty = true;
  }

  function refreshInspectorSlice() {
    const canvas = $('SdfInspectorPreview');
    const pct = `${Math.round(state.sliceDepth * 100)}%`;
    const axName = AXIS_LABELS[state.sliceAxis];
    const out = $('SdfInspectorDepthOut');
    if (out) out.textContent = `${axName} ${pct} (${sliceWorldCoord()})`;
    setSliderVisual($('SdfInspectorSlider'), 0.02, 0.98, state.sliceDepth);

    document.querySelectorAll('[data-sdf-insp-axis]').forEach((btn) => {
      const on = Number(btn.dataset.sdfInspAxis) === state.sliceAxis;
      btn.classList.toggle('active', on);
      btn.setAttribute('aria-pressed', String(on));
    });
    document.querySelectorAll('[data-sdf-insp-mip]').forEach((btn) => {
      const on = Number(btn.dataset.sdfInspMip) === state.sliceLevel;
      btn.classList.toggle('active', on);
      btn.setAttribute('aria-pressed', String(on));
    });

    if (canvas) {
      renderSdfSliceToCanvas(canvas, volume, {
        axis: state.sliceAxis,
        depth: state.sliceDepth,
        useMip1: state.sliceLevel === 1,
        isoOffsetMeters: state.isoOffsetMm * 0.001,
        showBrickGrid: true,
        resolution: 160,
      });
    }

    if (!volume) {
      $('SdfInspectorGrid').textContent = baking ? 'baking…' : '—';
      $('SdfInspectorBricks').textContent = '—';
      $('SdfInspectorMem').textContent = '—';
      return;
    }
    const mipStatus = volume.mip0Evicted ? 'Mip1 fallback' : volume.monotonicityVerified ? 'Conservative ✓' : 'Check';
    $('SdfInspectorGrid').textContent = `${volume.dim}³ · ${mipStatus}`;
    $('SdfInspectorBricks').textContent = `${volume.allocatedBrickCount}/${volume.totalBricks} (${volume.prunedPercent}% pruned)`;
    $('SdfInspectorMem').textContent = `${formatKb(volume.mip0Bytes)} Mip0 · ${formatKb(volume.mip1Bytes)} Mip1`;
  }

  function refreshWorkspaceChrome() {
    if (!open) return;
    const axName = AXIS_LABELS[state.sliceAxis];
    const pct = `${Math.round(state.sliceDepth * 100)}%`;
    const coord = sliceWorldCoord();

    $('SdfWsSource').textContent = `${getName()}${volume ? ` · ${volume.dim}³ sparse 8×8×8` : ''}`;
    $('SdfWsEngineTag').textContent = rayViewport ? rayViewport.engineLabel : 'Initializing…';
    $('SdfWsEngineTag').classList.toggle('live', Boolean(rayViewport && rayViewport.webGpuAvailable));

    // Mode tabs (4 3D modes + 2D slice)
    document.querySelectorAll('[data-sdf-mode]').forEach((btn) => {
      const m = btn.dataset.sdfMode;
      const active = (m === 'slice2d' && state.previewMode === 'slice2d') ||
        (m !== 'slice2d' && state.previewMode === 'raymarch' && Number(m) === state.vizMode);
      btn.classList.toggle('active', active);
      btn.setAttribute('aria-pressed', String(active));
    });

    // Resolution pills
    document.querySelectorAll('[data-sdf-dim]').forEach((btn) => {
      const on = Number(btn.dataset.sdfDim) === state.gridDim;
      btn.classList.toggle('active', on);
      btn.setAttribute('aria-pressed', String(on));
    });

    // Axis & Mip pills (both right dock & HUD)
    document.querySelectorAll('[data-sdf-axis]').forEach((btn) => {
      const on = Number(btn.dataset.sdfAxis) === state.sliceAxis;
      btn.classList.toggle('active', on);
      btn.setAttribute('aria-pressed', String(on));
    });
    document.querySelectorAll('[data-sdf-mip]').forEach((btn) => {
      const on = Number(btn.dataset.sdfMip) === state.sliceLevel;
      btn.classList.toggle('active', on);
      btn.setAttribute('aria-pressed', String(on));
    });

    // Sliders
    setSliderVisual($('SdfWsSliceSlider'), 0.02, 0.98, state.sliceDepth);
    setSliderVisual($('SdfHudSliceSlider'), 0.02, 0.98, state.sliceDepth);
    $('SdfWsSliceOut').textContent = `${axName} ${pct} (${coord})`;
    $('SdfHudSliceOut').textContent = `${axName} ${pct} (${coord})`;

    setSliderVisual($('SdfWsIsoSlider'), -40, 40, state.isoOffsetMm);
    setSliderVisual($('SdfHudIsoSlider'), -40, 40, state.isoOffsetMm);
    const isoStr = `${state.isoOffsetMm >= 0 ? '+' : ''}${state.isoOffsetMm} mm`;
    $('SdfWsIsoOut').textContent = isoStr;
    $('SdfHudIsoOut').textContent = isoStr;

    setSliderVisual($('SdfWsPadSlider'), 0.06, 0.35, state.paddingFraction);
    $('SdfWsPadOut').textContent = `${Math.round(state.paddingFraction * 100)} %`;

    setSliderVisual($('SdfWsBandSlider'), 2.5, 6.0, state.narrowBandMult);
    $('SdfWsBandOut').textContent = `${state.narrowBandMult.toFixed(1)}× voxel`;

    setSliderVisual($('SdfWsVramSlider'), 32, 8192, state.vramBudgetKb);
    $('SdfWsVramOut').textContent = state.vramBudgetKb >= 1024
      ? `${(state.vramBudgetKb / 1024).toFixed(1)} MB`
      : `${state.vramBudgetKb} KB`;

    // Toggles
    ['SdfWsFlip', 'SdfHudFlip'].forEach((id) => {
      const b = $(id);
      if (b) { b.classList.toggle('active', state.cutFlip); b.setAttribute('aria-pressed', String(state.cutFlip)); }
    });
    ['SdfWsSweep', 'SdfHudSweep'].forEach((id) => {
      const b = $(id);
      if (b) {
        b.classList.toggle('active', state.autoSweep);
        b.setAttribute('aria-pressed', String(state.autoSweep));
        b.textContent = state.autoSweep ? 'Pause sweep' : 'Auto sweep';
      }
    });

    // Show slice HUD controls when in 3D Cutaway or 2D Slice, iso controls otherwise
    const showSliceHud = state.previewMode === 'slice2d' || state.vizMode === 1;
    $('SdfHudSliceGroup').hidden = !showSliceHud;
    $('SdfHudIsoGroup').hidden = showSliceHud;

    // Top status pills & diagnostics
    if (volume) {
      const modeName = state.previewMode === 'slice2d' ? '2D Slice Cross-Section' : VIZ_LABELS[state.vizMode];
      const mipTag = volume.mip0Evicted ? 'Mip1 (Mip0 Evicted)' : state.sliceLevel === 1 ? 'Mip1 Only' : 'Mip0 + Mip1';
      $('SdfWsPillMode').textContent = `${volume.dim}³ · ${modeName} · ${mipTag}`;
      $('SdfWsPillBricks').textContent = `${volume.allocatedBrickCount}/${volume.totalBricks} bricks (${volume.prunedPercent}% pruned)`;

      const savings = volume.denseBytes / Math.max(1, volume.mip0Bytes + volume.mip1Bytes);
      $('SdfWsStats').innerHTML = `
        <span>Resolution</span><b>${volume.dim}³ (Mip1 ${volume.mip1Dim}³)</b>
        <span>Sparse bricks</span><b>${volume.allocatedBrickCount} / ${volume.totalBricks} (${volume.prunedPercent}% skipped)</b>
        <span>Mip0 · Mip1</span><b>${formatKb(volume.mip0Bytes)} · ${formatKb(volume.mip1Bytes)}</b>
        <span>vs Dense float32</span><b>${formatKb(volume.denseBytes)} (${savings.toFixed(1)}× smaller)</b>
        <span>Voxel step</span><b>${volume.voxelStep.toFixed(4)} u</b>
        <span>Narrow band</span><b>±${volume.narrowBandMeters.toFixed(3)} u</b>
        <span>Conservative Mip1</span><b style="color:${volume.monotonicityVerified ? '#46d267' : '#ff6b5e'}">${volume.monotonicityVerified ? 'VERIFIED ≤ Mip0' : 'CHECK'}</b>
        <span>Bake source</span><b>${volume.sourceTriangles.toLocaleString('en-US')} tris (LOD ${volume.lodLevel ?? 0})</b>
        <span>Bake time</span><b>${volume.bakeMs.toFixed(0)} ms</b>`;
    } else {
      $('SdfWsPillMode').textContent = baking ? 'Baking sparse 8×8×8 SDF…' : 'No volume baked';
      $('SdfWsPillBricks').textContent = '—';
      $('SdfWsStats').innerHTML = '<span>Status</span><b>Baking…</b>';
    }

    if (sliceDirty) {
      renderSdfSliceToCanvas($('SdfWsMiniSlice'), volume, {
        axis: state.sliceAxis,
        depth: state.sliceDepth,
        useMip1: state.sliceLevel === 1,
        isoOffsetMeters: state.isoOffsetMm * 0.001,
        showBrickGrid: true,
        resolution: 160,
      });
      sliceDirty = false;
    }
  }

  function requestBake({ silent = false } = {}) {
    const geom = getGeometry();
    if (!geom) return;
    if (worker) worker.terminate();
    worker = new Worker(new URL('./Engine/SdfWorker.js', import.meta.url), { type: 'module' });
    const mine = ++ticket;
    baking = true;

    const maxTris = state.gridDim >= 96 ? 32000 : state.gridDim >= 64 ? 24000 : 18000;
    const picked = selectBakeIndices(geom.indices, geom.hierarchy, maxTris);
    // Copy typed arrays for transfer so the main geometry remains intact
    const posCopy = new Float32Array(geom.positions);
    const idxCopy = new Uint32Array(picked.indices);

    const overlay = $('SdfBakeOverlay');
    if (overlay && open) {
      overlay.classList.add('open');
      $('SdfBakeLabel').textContent = `Baking ${state.gridDim}³ Sparse 8×8×8 SDF`;
      $('SdfBakeBar').style.width = '4%';
      $('SdfBakeNote').textContent = `${picked.triangleCount.toLocaleString('en-US')} triangles (LOD ${picked.lodLevel})`;
    }
    refreshInspectorSlice();
    if (open) refreshWorkspaceChrome();

    worker.onmessage = (event) => {
      const msg = event.data;
      if (msg.ticket !== mine) return;
      if (msg.type === 'progress') {
        if ($('SdfBakeBar')) $('SdfBakeBar').style.width = `${Math.round(msg.fraction * 100)}%`;
        if ($('SdfBakeNote')) $('SdfBakeNote').textContent = msg.label;
      } else if (msg.type === 'error') {
        baking = false;
        if (overlay) overlay.classList.remove('open');
        toast('SDF bake failed: ' + msg.message);
      } else if (msg.type === 'done') {
        baking = false;
        const firstForHero = !volume;
        volume = msg.volume;
        sliceDirty = true;
        if (overlay) overlay.classList.remove('open');
        if (firstForHero) fitSdfCamera();
        if (rayViewport && rayViewport.webGpuAvailable) {
          rayViewport.uploadVolume(volume);
        }
        stageDirty = true;
        refreshInspectorSlice();
        if (open) refreshWorkspaceChrome();
        if (!silent) {
          const evNote = msg.eviction.evicted ? ' · Mip0 evicted (Mip1 fallback)' : '';
          toast(`Baked ${volume.dim}³ SDF (${volume.allocatedBrickCount}/${volume.totalBricks} bricks, ${volume.bakeMs.toFixed(0)} ms${evNote})`);
        }
      }
    };
    worker.onerror = (err) => {
      baking = false;
      if (overlay) overlay.classList.remove('open');
      toast('SDF worker error: ' + (err.message || 'failed to start'));
    };
    worker.postMessage(
      {
        ticket: mine,
        request: {
          positions: posCopy,
          indices: idxCopy,
          closed: geom.closed,
          lodLevel: picked.lodLevel,
          gridDim: state.gridDim,
          paddingFraction: state.paddingFraction,
          narrowBandMult: state.narrowBandMult,
          vramBudgetBytes: state.vramBudgetKb * 1024,
        },
      },
      [posCopy.buffer, idxCopy.buffer],
    );
  }

  function onGeometryChanged() {
    volume = null;
    sliceDirty = true;
    requestBake({ silent: true });
  }

  async function ensureViewport() {
    if (!rayViewport) {
      rayViewport = new SdfRaymarchViewport($('SdfWebGpuCanvas'), $('SdfOverlayCanvas'), () => {
        stageDirty = true;
        sliceDirty = true;
        refreshInspectorSlice();
        refreshWorkspaceChrome();
      });
    }
    await rayViewport.initialize();
    if (volume && rayViewport.webGpuAvailable) {
      rayViewport.uploadVolume(volume);
    }
  }

  function startLoop() {
    if (rafId) return;
    lastTs = performance.now();
    const step = (ts) => {
      if (!open) { rafId = 0; return; }
      const dt = Math.min(0.05, (ts - lastTs) * 0.001);
      lastTs = ts;
      if (state.autoSweep) {
        state.sliceDepth += state.sweepDir * dt * 0.24;
        if (state.sliceDepth >= 0.96) { state.sliceDepth = 0.96; state.sweepDir = -1; }
        else if (state.sliceDepth <= 0.04) { state.sliceDepth = 0.04; state.sweepDir = 1; }
        sliceDirty = true;
        stageDirty = true;
        refreshInspectorSlice();
        refreshWorkspaceChrome();
      }
      const host = $('SdfStageWell');
      if (host) {
        const sizeKey = `${host.clientWidth}x${host.clientHeight}`;
        if (sizeKey !== lastStageSize) { lastStageSize = sizeKey; stageDirty = true; }
      }
      if (rayViewport && stageDirty && !rayViewport.isBusy) {
        const rendered = rayViewport.render(state, volume);
        if (rendered) stageDirty = false;
      }
      rafId = requestAnimationFrame(step);
    };
    rafId = requestAnimationFrame(step);
  }

  async function show() {
    if (!getGeometry()) { toast('Wait for the geometry to finish building'); return; }
    open = true;
    $('SdfWorkspace').hidden = false;
    if (volume) fitSdfCamera();
    if (!rayViewport) {
      rayViewport = new SdfRaymarchViewport($('SdfWebGpuCanvas'), $('SdfOverlayCanvas'), () => {
        stageDirty = true;
        sliceDirty = true;
        refreshInspectorSlice();
        refreshWorkspaceChrome();
      });
    }
    stageDirty = true;
    sliceDirty = true;
    refreshWorkspaceChrome();
    startLoop(); // Renders frame 1 immediately via software raymarch while WebGPU compiles
    await ensureViewport();
    if (!volume && !baking) requestBake({ silent: true });
    stageDirty = true;
    sliceDirty = true;
    refreshWorkspaceChrome();
  }

  function close() {
    open = false;
    state.autoSweep = false;
    $('SdfWorkspace').hidden = true;
    if (rafId) { cancelAnimationFrame(rafId); rafId = 0; }
    refreshInspectorSlice();
  }

  function keydown(event) {
    if (event.key === 'Escape') { close(); event.preventDefault(); }
    else if (event.key === ' ') { state.autoSweep = !state.autoSweep; refreshWorkspaceChrome(); event.preventDefault(); }
    else if (event.key.toLowerCase() === 'f') { fitSdfCamera(); event.preventDefault(); }
    else if (event.key >= '1' && event.key <= '4') {
      state.previewMode = 'raymarch';
      state.vizMode = Number(event.key) - 1;
      markStageDirty();
      refreshWorkspaceChrome();
    } else if (event.key === '5') {
      state.previewMode = 'slice2d';
      markStageDirty();
      refreshWorkspaceChrome();
    }
  }

  function setSliceDepth(val) {
    state.autoSweep = false;
    state.sliceDepth = Math.max(0.02, Math.min(0.98, val));
    sliceDirty = true;
    stageDirty = true;
    refreshInspectorSlice();
    if (open) refreshWorkspaceChrome();
  }

  function setAxis(ax) {
    state.sliceAxis = ax;
    sliceDirty = true;
    stageDirty = true;
    if (open && state.previewMode === 'raymarch' && state.vizMode !== 1) {
      state.vizMode = 1; // Switch smoothly to 3D Cutaway along selected axis
    }
    refreshInspectorSlice();
    if (open) refreshWorkspaceChrome();
  }

  function setMipLevel(lvl) {
    state.sliceLevel = lvl;
    sliceDirty = true;
    stageDirty = true;
    refreshInspectorSlice();
    if (open) refreshWorkspaceChrome();
  }

  // --- Event wiring -----------------------------------------------------------------------------------------
  function bindEvents() {
    // Inspector controls (always visible!)
    $('OpenSdfWorkspace')?.addEventListener('click', show);
    $('ToolSdf')?.addEventListener('click', show);
    $('SdfInspectorSlider')?.addEventListener('input', (e) => setSliceDepth(Number(e.target.value)));
    document.querySelectorAll('[data-sdf-insp-axis]').forEach((btn) => {
      btn.addEventListener('click', () => setAxis(Number(btn.dataset.sdfInspAxis)));
    });
    document.querySelectorAll('[data-sdf-insp-mip]').forEach((btn) => {
      btn.addEventListener('click', () => setMipLevel(Number(btn.dataset.sdfInspMip)));
    });

    // Workspace header & modes
    $('SdfWsClose')?.addEventListener('click', close);
    document.querySelectorAll('[data-sdf-mode]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const m = btn.dataset.sdfMode;
        if (m === 'slice2d') state.previewMode = 'slice2d';
        else { state.previewMode = 'raymarch'; state.vizMode = Number(m); }
        markStageDirty();
        refreshWorkspaceChrome();
      });
    });

    // Baker settings
    document.querySelectorAll('[data-sdf-dim]').forEach((btn) => {
      btn.addEventListener('click', () => {
        state.gridDim = Number(btn.dataset.sdfDim);
        refreshWorkspaceChrome();
        requestBake();
      });
    });
    $('SdfWsPadSlider')?.addEventListener('input', (e) => {
      state.paddingFraction = Number(e.target.value);
      refreshWorkspaceChrome();
    });
    $('SdfWsBandSlider')?.addEventListener('input', (e) => {
      state.narrowBandMult = Number(e.target.value);
      refreshWorkspaceChrome();
    });
    $('SdfWsVramSlider')?.addEventListener('input', (e) => {
      state.vramBudgetKb = Number(e.target.value);
      refreshWorkspaceChrome();
    });
    $('SdfWsBakeBtn')?.addEventListener('click', () => requestBake());

    // Slice & Iso sliders (Workspace right dock + bottom HUD)
    document.querySelectorAll('[data-sdf-axis]').forEach((btn) => {
      btn.addEventListener('click', () => setAxis(Number(btn.dataset.sdfAxis)));
    });
    document.querySelectorAll('[data-sdf-mip]').forEach((btn) => {
      btn.addEventListener('click', () => setMipLevel(Number(btn.dataset.sdfMip)));
    });
    $('SdfWsSliceSlider')?.addEventListener('input', (e) => setSliceDepth(Number(e.target.value)));
    $('SdfHudSliceSlider')?.addEventListener('input', (e) => setSliceDepth(Number(e.target.value)));

    const setIso = (v) => {
      state.isoOffsetMm = Number(v);
      sliceDirty = true;
      stageDirty = true;
      refreshInspectorSlice();
      refreshWorkspaceChrome();
    };
    $('SdfWsIsoSlider')?.addEventListener('input', (e) => setIso(e.target.value));
    $('SdfHudIsoSlider')?.addEventListener('input', (e) => setIso(e.target.value));

    ['SdfWsFlip', 'SdfHudFlip'].forEach((id) => {
      $(id)?.addEventListener('click', () => { state.cutFlip = !state.cutFlip; markStageDirty(); refreshWorkspaceChrome(); });
    });
    ['SdfWsSweep', 'SdfHudSweep'].forEach((id) => {
      $(id)?.addEventListener('click', () => {
        state.autoSweep = !state.autoSweep;
        if (state.autoSweep && state.previewMode === 'raymarch' && state.vizMode !== 1) state.vizMode = 1;
        markStageDirty();
        refreshWorkspaceChrome();
      });
    });
    $('SdfHudFit')?.addEventListener('click', fitSdfCamera);

    // Exports
    $('SdfWsExportFvol')?.addEventListener('click', () => {
      if (!volume) { toast('Bake an SDF volume first'); return; }
      const json = serializeVolumeFvol(volume, getName());
      const slug = getName().toLowerCase().replace(/[^a-z0-9]+/g, '-');
      downloadBlob(`${slug}-${volume.dim}c.fvol.json`, new Blob([json], { type: 'application/json' }));
      toast(`Exported ${volume.dim}³ sparse .fvol volume`);
    });
    $('SdfWsExportPng')?.addEventListener('click', () => {
      if (!volume) { toast('Bake an SDF volume first'); return; }
      const out = document.createElement('canvas');
      out.width = 512; out.height = 512;
      renderSdfSliceToCanvas(out, volume, {
        axis: state.sliceAxis,
        depth: state.sliceDepth,
        useMip1: state.sliceLevel === 1,
        isoOffsetMeters: state.isoOffsetMm * 0.001,
        showBrickGrid: true,
        resolution: 512,
      });
      out.toBlob((blob) => {
        if (!blob) return;
        const slug = getName().toLowerCase().replace(/[^a-z0-9]+/g, '-');
        downloadBlob(`${slug}-sdf-${AXIS_LABELS[state.sliceAxis]}-${Math.round(state.sliceDepth * 100)}.png`, blob);
        toast('Downloaded 512×512 SDF slice PNG');
      });
    });

    // Interactive orbit / wheel zoom / slice scrub on SDF stage
    const stage = $('SdfStageWell');
    if (stage) {
      stage.addEventListener('pointerdown', (e) => {
        if (e.target.closest('button, input')) return;
        if (state.previewMode === 'slice2d') return;
        drag = { x: e.clientX, y: e.clientY, yaw: state.camera.yaw, pitch: state.camera.pitch };
        stage.setPointerCapture(e.pointerId);
      });
      stage.addEventListener('pointermove', (e) => {
        if (!drag) return;
        state.camera.yaw = drag.yaw - (e.clientX - drag.x) * 0.008;
        state.camera.pitch = Math.max(-1.45, Math.min(1.45, drag.pitch + (e.clientY - drag.y) * 0.008));
        markStageDirty();
      });
      const end = () => { drag = null; };
      stage.addEventListener('pointerup', end);
      stage.addEventListener('pointercancel', end);
      stage.addEventListener('dblclick', fitSdfCamera);
      stage.addEventListener('wheel', (e) => {
        e.preventDefault();
        if (state.previewMode === 'slice2d') {
          setSliceDepth(state.sliceDepth + Math.sign(e.deltaY) * 0.02);
        } else {
          state.camera.distance = Math.max(0.8, Math.min(12.0, state.camera.distance * Math.exp(e.deltaY * 0.0012)));
          markStageDirty();
        }
      }, { passive: false });
    }
  }

  bindEvents();
  refreshInspectorSlice();

  return {
    get open() { return open; },
    get volume() { return volume; },
    show,
    close,
    keydown,
    onGeometryChanged,
    requestBake,
  };
}
