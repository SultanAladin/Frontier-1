/*============================================================================================================================================
                                                      VIEWPORT PANEL
============================================================================================================================================*/
// 🧩 The 3-D field viewport dock: transport, view overlays, camera rig (orbit / pan / dolly with inertia), probe placement by picking, the HUD
// readout and the live legend. Owns the animation clock that drives the global transport while playing.

import { El, Icon, IconEl, Toast, FitCanvas } from './Kit.js';
import { FieldViewport } from '../Viewport/FieldViewport.js';
import { SoftwareViewport } from '../Viewport/SoftwareViewport.js';
import { EvaluateAt, QuantityAt, SuggestMaximum, PrepareFrame } from '../Engine/FieldEvaluator.js';
import * as M from '../Engine/FieldMath.js';
import { ColormapCss, FormatMeasure, BearingOf, Length } from '../Engine/FieldMath.js';
import { EvaluatedQuantities } from '../Engine/ForceFieldCatalog.js';

export const DEFAULT_CAMERA = { Yaw: 0.78, Pitch: 0.46, Distance: 34, Target: [0, 0, 0], Fov: 46 };

export function CreateViewportPanel(state) {
  const root = El('section', { class: 'dock', 'aria-label': 'Field viewport' });
  const tabStrip = El('div', { class: 'tab-strip' },
    El('button', { class: 'document-tab active', type: 'button' }, IconEl('arrow', 13), 'Viewport'),
    El('button', { class: 'tab-add', type: 'button', title: 'Next workspace (not part of this pass)', disabled: true }, '+'),
  );

  const heading = El('div', { class: 'viewport-heading' },
    El('span', { class: 'scene-icon', html: Icon('cube', 18) }),
    El('span', { class: 'scene-title' }, El('b', {}, 'Open field'), El('small', {}, 'accumulated velocity · acceleration · damping')),
    El('span', { class: 'spacer' }),
    El('span', { class: 'state-tag live', html: 'LIVE FIELD' }),
  );

  const quantitySelect = El('select', { 'aria-label': 'Evaluated quantity' });
  for (const quantity of EvaluatedQuantities) quantitySelect.append(El('option', { value: quantity.Id }, `${quantity.Label} [${quantity.Unit}]`));

  const toolButton = (name, title, onClick, extraClass = 'tool round') => {
    const button = El('button', { type: 'button', class: extraClass, title, 'aria-label': title, html: Icon(name, 15) });
    button.addEventListener('click', onClick);
    return button;
  };

  const playButton = toolButton('pause', 'Play / pause the transport (Space)', () => state.TogglePlay(), 'tool round active');
  const rewindButton = toolButton('rewind', 'Rewind to t = 0 (R)', () => state.ResetTime());
  const frameButton = toolButton('frame', 'Frame the whole field domain (F)', () => frameDomain());
  const gridButton = toolButton('grid', 'Ground grid', () => toggleOverlay('ShowGrid'), 'tool round active');
  const envelopeButton = toolButton('sphere', 'Influence envelopes', () => toggleOverlay('ShowEnvelopes'), 'tool round active');
  const vectorButton = toolButton('arrows', 'Field vectors', () => toggleOverlay('ShowVectors'), 'tool round active');
  const streamlineButton = toolButton('waves', 'Streamlines', () => toggleOverlay('ShowStreamlines'), 'tool round active');
  const moteButton = toolButton('sparkle', 'Advected motes', () => toggleOverlay('ShowMotes'), 'tool round active');
  const probeButton = toolButton('probe', 'Probe: click the field to sample it', () => {
    probeMode = !probeMode;
    probeButton.classList.toggle('active', probeMode);
    Toast(probeMode ? 'Probe: click anywhere in the viewport to sample the field' : 'Probe locked');
  }, 'tool round active');
  const axisSegmented = El('div', { class: 'segmented small' });
  for (const axis of ['XY', 'XZ', 'YZ']) {
    const button = El('button', { type: 'button' }, axis);
    button.addEventListener('click', () => {
      for (const b of axisSegmented.children) b.classList.toggle('active', b === button);
      sectionAxis = axis;
      state.SetOverlay({ SliceAxis: axis });
      setSectionAxis(axis);
    });
    if (axis === 'XZ') button.classList.add('active');
    axisSegmented.append(button);
  }

  const tools = El('div', { class: 'viewport-tools' },
    rewindButton, frameButton,
    El('span', { class: 'divider' }),
    gridButton, envelopeButton, vectorButton, streamlineButton, moteButton, probeButton,
    El('span', { class: 'divider' }),
    El('span', { class: 'field-inline' }, quantitySelect),
    El('span', { class: 'spacer' }),
    axisSegmented,
  );

  const well = El('div', { class: 'canvas-well' });
  const canvas = El('canvas', { tabindex: '0', 'aria-label': 'Three-dimensional force field viewport' });
  const readout = El('div', { class: 'readout' });
  const legend = El('div', { class: 'legend' });
  const playback = El('div', { class: 'playback' });
  const clock = El('span', { class: 'clock' }, 't 0.00 s');
  const note = El('div', { class: 'hud-note' }, 'orbit drag · pan right-drag · dolly wheel · click a source to select');
  well.append(canvas, readout, legend, playback, note);

  const sectionCartouche = El('div', { class: 'section-shell' });
  root.append(tabStrip, heading, tools, well, sectionCartouche);

  let camera = { ...DEFAULT_CAMERA, Target: DEFAULT_CAMERA.Target.slice() };
  fitInitialCamera();
  let probeMode = false;
  let sectionAxis = 'XZ';
  let renderer = null;
  let software = null;
  let sectionRenderer = null;
  let lastFrame = 0;
  let running = false;
  let hudRevision = -1;
  let sectionDirty = true;
  let lastClock = -1;

  // ---------------------------------------------------------------- overlay helpers
  const overlayOf = () => state.Overlay();

  function syncToolStates() {
    const overlay = overlayOf();
    gridButton.classList.toggle('active', overlay.ShowGrid);
    envelopeButton.classList.toggle('active', overlay.ShowEnvelopes);
    vectorButton.classList.toggle('active', overlay.ShowVectors);
    streamlineButton.classList.toggle('active', overlay.ShowStreamlines);
    moteButton.classList.toggle('active', overlay.ShowMotes);
    probeButton.classList.toggle('active', overlay.ShowProbe);
    playButton.innerHTML = Icon(state.Playing() ? 'pause' : 'play', 15);
  }

  function toggleOverlay(key) {
    state.SetOverlay({ [key]: !overlayOf()[key] });
    syncToolStates();
  }

  // Frames the authored domain exactly, honouring the current orbit and the live canvas aspect.
  function frameDomain() {
    const domain = state.Document().Domain;
    const aspect = canvas.clientWidth && canvas.clientHeight ? canvas.clientWidth / canvas.clientHeight : 16 / 9;
    const fitted = M.FitCameraToBox(domain.Min, domain.Max, { Yaw: camera.Yaw, Pitch: camera.Pitch, Fov: camera.Fov, Aspect: aspect });
    camera.Target = fitted.Target;
    camera.Distance = fitted.Distance;
  }

  // The document's saved view is a starting opinion only: fit it to the actual domain so nothing opens cropped.
  function fitInitialCamera() {
    const domain = state.Document().Domain;
    const saved = state.Document().View || {};
    const aspect = canvas.clientWidth && canvas.clientHeight ? canvas.clientWidth / canvas.clientHeight : 16 / 9;
    const fitted = M.FitCameraToBox(domain.Min, domain.Max, {
      Yaw: Number.isFinite(saved.Yaw) ? saved.Yaw : DEFAULT_CAMERA.Yaw,
      Pitch: Number.isFinite(saved.Pitch) ? saved.Pitch : DEFAULT_CAMERA.Pitch,
      Fov: Number.isFinite(saved.Fov) ? saved.Fov : DEFAULT_CAMERA.Fov,
      Aspect: aspect,
    });
    camera = { ...camera, Yaw: fitted.Yaw, Pitch: fitted.Pitch, Target: fitted.Target, Distance: fitted.Distance, Fov: fitted.Fov };
  }

  // ---------------------------------------------------------------- camera input
  const pointers = new Map();
  let hoverPoint = null;
  canvas.addEventListener('contextmenu', (event) => event.preventDefault());
  canvas.addEventListener('pointerdown', (event) => {
    canvas.focus({ preventScroll: true });
    canvas.setPointerCapture?.(event.pointerId);
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY, button: event.button, shift: event.shiftKey, moved: 0 });
  });
  canvas.addEventListener('pointermove', (event) => {
    const entry = pointers.get(event.pointerId);
    if (!entry) {
      if (probeMode) hoverPoint = pickPoint(event);
      return;
    }
    const dx = event.clientX - entry.x;
    const dy = event.clientY - entry.y;
    entry.x = event.clientX;
    entry.y = event.clientY;
    entry.moved += Math.abs(dx) + Math.abs(dy);
    if (entry.button === 0 && !entry.shift) {
      camera.Yaw -= dx * 0.0072;
      camera.Pitch = Math.max(-1.45, Math.min(1.45, camera.Pitch - dy * 0.0062));
    } else {
      const scale = camera.Distance * 0.0016;
      const right = [Math.cos(camera.Yaw + Math.PI / 2), Math.sin(camera.Yaw + Math.PI / 2), 0];
      const up = [Math.cos(camera.Yaw) * Math.sin(camera.Pitch), Math.sin(camera.Yaw) * Math.sin(camera.Pitch), Math.cos(camera.Pitch)];
      camera.Target = [
        camera.Target[0] + (-right[0] * dx + up[0] * dy) * scale,
        camera.Target[1] + (-right[1] * dx + up[1] * dy) * scale,
        camera.Target[2] + (-right[2] * dx + up[2] * dy) * scale,
      ];
    }
    sectionDirty = true;
  });
  const endPointer = (event) => {
    const entry = pointers.get(event.pointerId);
    pointers.delete(event.pointerId);
    if (!entry) return;
    if (entry.moved < 4) {
      if (probeMode) {
        const point = pickPoint(event);
        if (point) {
          state.SetProbe(point);
          state.SetOverlay({ ShowProbe: true });
          Toast(`Probe at ${point.map((v) => v.toFixed(2)).join(', ')} m`);
        }
      } else {
        const hit = pickSource(event);
        if (hit) state.Select(hit);
      }
    }
  };
  canvas.addEventListener('pointerup', endPointer);
  canvas.addEventListener('pointercancel', endPointer);
  canvas.addEventListener('wheel', (event) => {
    event.preventDefault();
    camera.Distance = Math.max(4, Math.min(220, camera.Distance * Math.exp(event.deltaY * 0.0011)));
    sectionDirty = true;
  }, { passive: false });

  function rayFrom(event) {
    const rect = canvas.getBoundingClientRect();
    const nx = ((event.clientX - rect.left) / Math.max(1, rect.width)) * 2 - 1;
    const ny = 1 - ((event.clientY - rect.top) / Math.max(1, rect.height)) * 2;
    const cp = Math.cos(camera.Pitch);
    const eye = [camera.Target[0] + Math.cos(camera.Yaw) * cp * camera.Distance, camera.Target[1] + Math.sin(camera.Yaw) * cp * camera.Distance, camera.Target[2] + Math.sin(camera.Pitch) * camera.Distance];
    const forward = [camera.Target[0] - eye[0], camera.Target[1] - eye[1], camera.Target[2] - eye[2]];
    const f = forward.map((c) => c / Math.hypot(...forward));
    const right = [f[1] * 0 - f[2] * 0, f[2] * 0 - f[0] * 0, 0];
    const r = [(-f[1]), f[0], 0];
    const rl = Math.hypot(r[0], r[1], r[2]) || 1;
    r[0] /= rl; r[1] /= rl; r[2] /= rl;
    const u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]];
    const tanFov = Math.tan(((camera.Fov || 46) * Math.PI) / 360);
    const aspect = Math.max(0.001, rect.width / Math.max(1, rect.height));
    const dir = [
      f[0] + r[0] * nx * tanFov * aspect + u[0] * ny * tanFov,
      f[1] + r[1] * nx * tanFov * aspect + u[1] * ny * tanFov,
      f[2] + r[2] * nx * tanFov * aspect + u[2] * ny * tanFov,
    ];
    const dl = Math.hypot(...dir);
    return { Eye: eye, Direction: dir.map((c) => c / dl) };
  }

  // Marches the ray through the field, returning the first point where the sampled speed climbs above a threshold.
  function pickPoint(event) {
    const { Eye, Direction } = rayFrom(event);
    const domain = state.Document().Domain;
    const runtime = state.Frame();
    const time = state.Time();
    const inside = (p) => p[0] >= domain.Min[0] && p[0] <= domain.Max[0] && p[1] >= domain.Min[1] && p[1] <= domain.Max[1] && p[2] >= domain.Min[2] && p[2] <= domain.Max[2];
    let lastInside = null;
    for (let t = 0; t < 160; t += 0.35) {
      const p = [Eye[0] + Direction[0] * t, Eye[1] + Direction[1] * t, Eye[2] + Direction[2] * t];
      if (!inside(p)) continue;
      lastInside = p;
      const sample = EvaluateAt(runtime, p, time);
      if (Length(sample.Velocity) > 0.05 || Length(sample.Acceleration) > 0.05) return p;
    }
    if (lastInside) return lastInside;
    // Fall back to the horizontal plane through the probe / origin.
    const planeZ = state.Probe()[2];
    if (Math.abs(Direction[2]) < 1e-5) return [0, 0, 0];
    const t = (planeZ - Eye[2]) / Direction[2];
    if (t > 0) return [Eye[0] + Direction[0] * t, Eye[1] + Direction[1] * t, planeZ];
    return null;
  }

  function pickSource(event) {
    const rect = canvas.getBoundingClientRect();
    const mx = event.clientX - rect.left;
    const my = event.clientY - rect.top;
    let best = null;
    let bestDistance = 26;
    for (const source of state.Sources()) {
      const anchor = source.Position;
      const projected = projectToScreen(anchor);
      if (!projected) continue;
      const d = Math.hypot(projected[0] - mx, projected[1] - my);
      if (d < bestDistance) {
        bestDistance = d;
        best = source.Id;
      }
    }
    return best;
  }

  function projectToScreen(point) {
    const rect = canvas.getBoundingClientRect();
    if (renderer && renderer.UsesWebGL) {
      const p = renderer.Project(camera, point);
      return p ? [p[0] + 0, p[1] + 0] : null;
    }
    // Software projection mirrors the 2-D fallback's camera.
    const cp = Math.cos(camera.Pitch);
    const eye = [camera.Target[0] + Math.cos(camera.Yaw) * cp * camera.Distance, camera.Target[1] + Math.sin(camera.Yaw) * cp * camera.Distance, camera.Target[2] + Math.sin(camera.Pitch) * camera.Distance];
    const forward = [camera.Target[0] - eye[0], camera.Target[1] - eye[1], camera.Target[2] - eye[2]];
    const fl = Math.hypot(...forward);
    const f = forward.map((c) => c / fl);
    const rl = Math.hypot(f[1], f[0]) || 1;
    const r = [-f[1] / rl, f[0] / rl, 0];
    const u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]];
    const d = [point[0] - eye[0], point[1] - eye[1], point[2] - eye[2]];
    const z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (z <= 0.1) return null;
    const tanFov = Math.tan(((camera.Fov || 46) * Math.PI) / 360);
    return [rect.width / 2 + ((d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / z) * (rect.width / 2) / tanFov / (rect.width / rect.height) * tanFov * tanFov, rect.height / 2 - ((d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / z) * (rect.height / 2) / tanFov];
  }

  // ---------------------------------------------------------------- section (2-D) panel
  const sectionHeading = El('div', { class: 'viewport-heading', style: { height: '34px' } },
    El('span', { class: 'scene-icon', html: Icon('target', 15) }),
    El('span', { class: 'scene-title' }, El('b', { style: { fontSize: '12px' } }, 'Cross-section'), El('small', {}, 'heatmap · isolines · streamlines · vectors')),
    El('span', { class: 'spacer' }),
    El('span', { class: 'state-tag', html: '2-D SECTION' }),
  );
  const sectionWell = El('div', { class: 'section-well' });
  const sectionCanvas = El('canvas', { tabindex: '0', 'aria-label': 'Two-dimensional field cross-section' });
  const sectionStats = El('div', { class: 'section-stats' });
  const sectionHint = El('div', { class: 'section-hint' }, 'click the section to move the probe');
  sectionWell.append(sectionCanvas, sectionStats, sectionHint);
  sectionCartouche.append(sectionHeading, sectionWell);
  sectionCartouche.style.display = 'flex';
  sectionCartouche.style.flexDirection = 'column';
  sectionCartouche.style.minHeight = '0';
  sectionCartouche.style.flex = '0 0 42%';

  function setSectionAxis(axis) {
    sectionAxis = axis;
    sectionDirty = true;
  }

  sectionCanvas.addEventListener('pointerdown', (event) => {
    const rect = sectionCanvas.getBoundingClientRect();
    const domain = state.Document().Domain;
    const frameMap = { XY: [0, 1], XZ: [0, 2], YZ: [1, 2] }[sectionAxis];
    const pad = 30;
    const spanU = domain.Max[frameMap[0]] - domain.Min[frameMap[0]];
    const spanV = domain.Max[frameMap[1]] - domain.Min[frameMap[1]];
    const scale = Math.min((rect.width - pad * 2) / spanU, (rect.height - pad * 2) / spanV);
    const originX = pad + ((rect.width - pad * 2) - spanU * scale) / 2;
    const originY = pad + ((rect.height - pad * 2) - spanV * scale) / 2;
    const u = domain.Min[frameMap[0]] + (event.clientX - rect.left - originX) / scale;
    const v = domain.Max[frameMap[1]] - (event.clientY - rect.top - originY) / scale;
    const probe = state.Probe().slice();
    probe[frameMap[0]] = Math.max(domain.Min[frameMap[0]], Math.min(domain.Max[frameMap[0]], u));
    probe[frameMap[1]] = Math.max(domain.Min[frameMap[1]], Math.min(domain.Max[frameMap[1]], v));
    probe[frameMap[0] === 0 ? 2 : 0] = sectionAxis === 'XY' ? probe[2] : probe[frameMap[0] === 0 ? 2 : 0];
    state.SetProbe(probe);
    sectionDirty = true;
  });

  async function drawSection() {
    if (!sectionRenderer) {
      try {
        sectionRenderer = await import('../Viewport/SectionRasterizer.js');
      } catch {
        return;
      }
    }
    const R = sectionRenderer;
    const canvasRect = sectionCanvas.getBoundingClientRect();
    if (canvasRect.width < 4 || canvasRect.height < 4) return;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    const width = Math.round(canvasRect.width * dpr);
    const height = Math.round(canvasRect.height * dpr);
    if (sectionCanvas.width !== width || sectionCanvas.height !== height) {
      sectionCanvas.width = width;
      sectionCanvas.height = height;
    }
    const context = sectionCanvas.getContext('2d');
    const document = state.Document();
    const overlay = overlayOf();
    const runtime = state.Frame();
    const time = state.Time();
    const surface = R.CreateSurface(width, height);
    surface.Fill(11, 12, 15, 255);
    const projection = R.SectionProjection(surface, sectionAxis, document.Domain, Math.round(30 * dpr));
    const maximum = overlay.AutoRange ? SuggestMaximum(runtime, document.Domain, time, overlay.Quantity) : overlay.MaxValue;
    const field = R.SectionHeatmap(surface, projection, runtime, time, {
      Quantity: overlay.Quantity,
      Colormap: overlay.Colormap,
      Maximum: maximum,
      Offset: overlay.SliceOffset,
      Opacity: overlay.HeatmapOpacity,
      Domain: document.Domain,
      Samples: Math.max(48, Math.round(height / 10)),
    });
    R.SectionIsolines(surface, projection, field, { Maximum: maximum, Domain: document.Domain, Levels: 4 });
    // Overlay passes share the heat map's calibration, so an arrow's colour means the same as the plate beneath it.
    const plateMaximum = field.SpeedScale || maximum;
    R.SectionStreamlines(surface, projection, runtime, time, {
      Domain: document.Domain,
      Colormap: overlay.Colormap,
      Count: Math.round(overlay.StreamSeedCount * 0.6),
      Steps: Math.round(overlay.StreamSteps * 0.6),
      StepSize: overlay.StreamStepSize,
      Offset: overlay.SliceOffset,
      Maximum: plateMaximum,
      Thickness: Math.max(1, dpr * 0.9),
    });
    R.SectionArrows(surface, projection, runtime, time, {
      Domain: document.Domain,
      Colormap: overlay.Colormap,
      Density: Math.max(7, Math.round(overlay.ArrowDensity * 1.9)),
      Scale: overlay.ArrowScale,
      Offset: overlay.SliceOffset,
      Maximum: plateMaximum,
    });
    R.SectionSources(surface, projection, document, time, { Offset: overlay.SliceOffset, ShowEnvelopes: overlay.ShowEnvelopes, Selected: state.Selection() });
    if (overlay.ShowProbe) {
      const p = state.Probe().slice();
      // The section shows the plane at SliceOffset: move the probe onto it so the marker matches the sampled field.
      const w = { XY: 2, XZ: 1, YZ: 0 }[sectionAxis];
      p[w] = overlay.SliceOffset;
      R.SectionProbe(surface, projection, p);
    }
    const image = new ImageData(new Uint8ClampedArray(surface.Data), width, height);
    context.setTransform(1, 0, 0, 1, 0, 0);
    context.putImageData(image, 0, 0);

    const probeSample = state.Probe().slice();
    probeSample[{ XY: 2, XZ: 1, YZ: 0 }[sectionAxis]] = overlay.SliceOffset;
    const quantity = EvaluatedQuantities.find((q) => q.Id === overlay.Quantity);
    const value = QuantityAt(runtime, probeSample, time, overlay.Quantity);
    // Colour and numbers agree: the readout names the value that sits at the hot end of the plate's ramp.
    const hot = field.SpeedScale || maximum;
    sectionStats.textContent = [
      `${projection.Frame.LabelU} × ${projection.Frame.LabelV}`,
      `${quantity.Label} ${FormatMeasure(value, quantity.Unit)} @ probe`,
      `ramp 0 → ${FormatMeasure(hot, quantity.Unit)}`,
      `slice ${FormatMeasure(overlay.SliceOffset, 'm')}`,
    ].join('\n');
    sectionDirty = false;
  }

  // ---------------------------------------------------------------- HUD
  function updateHud() {
    const overlay = overlayOf();
    const runtime = state.Frame();
    const probe = state.Probe();
    const sample = EvaluateAt(runtime, probe, state.Time());
    const sourceCount = runtime.length;
    const quantity = EvaluatedQuantities.find((q) => q.Id === overlay.Quantity);
    const value = QuantityAt(runtime, probe, state.Time(), overlay.Quantity);
    const bearing = BearingOf(sample.Velocity);
    readout.textContent = [
      `probe  ${probe.map((v) => v.toFixed(2)).join('  ')} m`,
      `speed  ${FormatMeasure(Length(sample.Velocity), 'm/s')}   bearing ${bearing.toFixed(0).padStart(3, '0')}°`,
      `accel  ${FormatMeasure(Length(sample.Acceleration), 'm/s²')}   damping ${FormatMeasure(sample.Damping, '1/s')}`,
      `influence ${sample.Influence.toFixed(2)}  ·  ${sourceCount} live source${sourceCount === 1 ? '' : 's'}`,
    ].join('\n');
    const maximum = overlay.AutoRange ? SuggestMaximum(runtime, state.Document().Domain, state.Time(), overlay.Quantity) : overlay.MaxValue;
    legend.innerHTML = '';
    legend.append(
      El('span', { class: 'legend-title' }, `${quantity.Label.toUpperCase()}  [${quantity.Unit}]`),
      El('span', { class: 'ramp', style: { background: ColormapCss[overlay.Colormap] || ColormapCss.Engineering } }),
      El('span', { class: 'ramp-labels' }, El('span', {}, '0'), El('span', {}, FormatMeasure(maximum, quantity.Unit))),
    );
    if (overlay.Quantity === 'Divergence') {
      legend.append(El('span', { class: 'legend-title', style: { color: '#8a8a8a' } }, 'blue = convergence · red = expansion'));
    }
    hudRevision = state.Get().Revision;
  }

  // ---------------------------------------------------------------- loop
  function ensureRenderers() {
    if (!renderer) {
      renderer = new FieldViewport(canvas);
      if (!renderer.UsesWebGL) {
        software = new SoftwareViewport(canvas);
        note.textContent = 'software raster viewport (WebGL2 unavailable) · orbit drag · right-drag pan · wheel dolly';
      }
    }
  }

  function frame(now) {
    if (!running) return;
    const dt = Math.min(0.08, lastFrame ? (now - lastFrame) / 1000 : 1 / 60);
    lastFrame = now;
    ensureRenderers();
    if (state.Playing()) {
      const scale = state.Sim().TimeScale ?? 1;
      state.Advance(dt * scale);
    }
    const overlay = overlayOf();
    const document = state.Document();
    const runtime = state.Frame();
    const time = state.Time();
    // Streamline advection is the expensive part, so it is pinned to a coarse signature: document revision plus whole
    // seconds of field time. Motes, arrows and the camera still redraw every animation frame.
    const geometryKey = `${state.Get().Revision}:${Math.floor(time)}`;
    if (renderer.UsesWebGL) {
      renderer.ProbePoint = state.Probe();
      renderer.BuildGeometry(document, runtime, time, overlay, geometryKey);
      renderer.UpdateMotes(document, runtime, state.Playing() ? dt : 0, time, overlay);
      renderer.Render(document, runtime, time, camera, overlay);
    } else if (software) {
      software.ProbePoint = state.Probe();
      software.Render(document, runtime, time, camera, overlay, state.Playing() ? dt : 0);
    }
    if (!state.Playing() && lastClock !== time) lastClock = time;
    clock.textContent = `t ${time.toFixed(2)} s`;
    if (state.Playing() || sectionDirty) {
      const nowMs = performance.now();
      if (!drawSection.lastRun || nowMs - drawSection.lastRun > 130 || sectionDirty) {
        drawSection.lastRun = nowMs;
        drawSection();
      }
    }
    updateHud();
    requestAnimationFrame(frame);
  }

  // ---------------------------------------------------------------- wiring
  quantitySelect.addEventListener('change', () => {
    state.SetOverlay({ Quantity: quantitySelect.value });
    sectionDirty = true;
    hudRevision = -1;
  });

  // Transport lives in the floating pill, exactly as the Project-Zero studios seat it.
  playback.append(playButton, clock);

  state.Subscribe((reason) => {
    if (reason === 'document' || reason === 'selection' || reason === 'sim') syncToolStates();
    if (reason === 'document' || reason === 'view') sectionDirty = true;
    if (reason === 'time') sectionDirty = true;
  });

  const syncOverlay = () => {
    const overlay = overlayOf();
    quantitySelect.value = overlay.Quantity;
    syncToolStates();
    sectionDirty = true;
  };

  function mount() {
    syncOverlay();
    frameDomain();
    running = true;
    requestAnimationFrame(frame);
    if (!renderer) ensureRenderers();
    return root;
  }

  return {
    Node: root,
    Mount: mount,
    Camera: () => camera,
    FrameDomain: frameDomain,
    SetOverlayVisibility: syncToolStates,
    RedrawSection: () => {
      sectionDirty = true;
    },
    IsWebGL: () => !!(renderer && renderer.UsesWebGL),
  };
}
