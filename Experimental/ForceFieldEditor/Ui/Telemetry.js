/*============================================================================================================================================
                                                        TELEMETRY
============================================================================================================================================*/
// 🧩 Instrument drawer: a rolling sixty-second anemometer at the probe with the Beaufort band, a per-source contribution spectrum, the four
// attenuation curves drawn from the same functions the engine uses, and the live probe channel strip. Everything is measured, nothing mocked.

import { El, Icon, IconEl, FitCanvas } from './Kit.js';
import * as M from '../Engine/FieldMath.js';
import { EvaluateAt, DEFAULT_OVERLAY } from '../Engine/FieldEvaluator.js';
import { AttenuationKinds, LawById } from '../Engine/ForceFieldCatalog.js';

const HISTORY_SECONDS = 60;

export function CreateTelemetry(state) {
  const shell = El('div', { class: 'drawer-shell' });
  const drawer = El('div', { class: 'drawer' });
  const body = El('div', { class: 'drawer-body' });
  drawer.append(El('header', {},
    El('span', { html: Icon('pulse', 15), style: { color: '#ffb238' } }),
    El('b', {}, 'Telemetry'),
    El('small', { style: { color: '#6e6e6e' } }, 'probe anemometer · source spectrum · attenuation curves · channel strip'),
    El('span', { style: { flex: '1' } }),
    El('small', { id: 'TelemetryClock', style: { color: '#8a8a8a' } }, 't 0.00 s'),
  ), body);
  shell.append(drawer);

  const history = [];
  let lastSample = 0;

  // ---------------------------------------------------------------- anemometer
  const anemoCanvas = El('canvas', { 'aria-label': 'Probe speed, rolling sixty seconds' });
  const anemoCard = El('div', { class: 'tele-card' },
    El('header', {}, El('b', {}, 'Anemometer'), El('small', { id: 'BeaufortTag' }, 'Beaufort —')),
    anemoCanvas,
    El('p', { id: 'AnemoState' }, 'Rolling sixty seconds of speed at the probe cursor.'),
  );

  // ---------------------------------------------------------------- source spectrum
  const spectrumCanvas = El('canvas', { 'aria-label': 'Per-source contribution at the probe' });
  const spectrumCard = El('div', { class: 'tele-card' },
    El('header', {}, El('b', {}, 'Source spectrum'), El('small', {}, 'contribution at probe')),
    spectrumCanvas,
    El('p', {}, 'Bar length is each source\'s attenuated contribution at the probe; the outline marks its influence weight.'),
  );

  // ---------------------------------------------------------------- attenuation curves
  const curveCanvas = El('canvas', { 'aria-label': 'Attenuation curves' });
  const curveCard = El('div', { class: 'tele-card' },
    El('header', {}, El('b', {}, 'Attenuation curves'), El('small', {}, '1.0 → 0 at bound')),
    curveCanvas,
    El('p', {}, 'The four bounded falloff curves. The selected source\'s curve is drawn solid; every one reaches exact zero at u = 1.'),
  );

  // ---------------------------------------------------------------- channel strip
  const strip = El('div', { class: 'tele-card', id: 'ChannelStrip' },
    El('header', {}, El('b', {}, 'Probe channels'), El('small', {}, 'x · y · z')),
  );
  const channelBars = {};
  for (const channel of ['vx', 'vy', 'vz', 'ax', 'ay', 'az']) {
    const bar = El('i');
    channelBars[channel] = bar;
    strip.append(El('div', { style: { display: 'grid', gridTemplateColumns: '28px 1fr 58px', gap: '8px', alignItems: 'center', fontSize: '10px', color: '#9a9a9a' } },
      El('span', {}, channel),
      El('span', { class: 'meter', style: { height: '5px' } }, bar),
      El('span', { style: { textAlign: 'right', fontVariantNumeric: 'tabular-nums', color: '#c9ced8' }, dataset: { channel } }, '0.00'),
    ));
  }
  strip.append(El('p', {}, 'Velocity and acceleration components at the probe, scaled to the field maximum.'));

  body.append(anemoCard, spectrumCard, curveCard, strip);

  // ---------------------------------------------------------------- drawing
  function drawAnemometer() {
    const { Context: g, Width: width, Height: height } = FitCanvas(anemoCanvas, 92);
    g.clearRect(0, 0, width, height);
    g.fillStyle = '#0b0b0e';
    g.fillRect(0, 0, width, height);
    const now = state.Time();
    const runtime = state.Frame();
    const sample = EvaluateAt(runtime, state.Probe(), now);
    const speed = M.Length(sample.Velocity);
    if (history.length === 0 || now - lastSample > 0.12 || now < lastSample) {
      if (now < lastSample) history.length = 0;
      lastSample = now;
      history.push({ T: now, V: speed });
      while (history.length && now - history[0].T > HISTORY_SECONDS) history.shift();
    }
    const maximum = Math.max(2, ...history.map((h) => h.V)) * 1.15;
    const pad = 6;
    const x0 = pad;
    const y0 = height - 12;
    const plotW = width - pad * 2;
    const plotH = height - 22;
    // grid
    g.strokeStyle = 'rgba(255,255,255,0.06)';
    g.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const y = y0 - (i / 4) * plotH;
      g.beginPath();
      g.moveTo(x0, y);
      g.lineTo(x0 + plotW, y);
      g.stroke();
    }
    // mean line
    const mean = history.reduce((a, h) => a + h.V, 0) / Math.max(1, history.length);
    const meanY = y0 - (mean / maximum) * plotH;
    g.strokeStyle = 'rgba(255,178,56,0.35)';
    g.setLineDash([3, 3]);
    g.beginPath();
    g.moveTo(x0, meanY);
    g.lineTo(x0 + plotW, meanY);
    g.stroke();
    g.setLineDash([]);
    // trace
    if (history.length > 1) {
      const t0 = history[0].T;
      const t1 = Math.max(now, t0 + 1);
      g.beginPath();
      history.forEach((h, i) => {
        const x = x0 + ((h.T - t0) / Math.max(1e-6, t1 - t0)) * plotW;
        const y = y0 - (Math.min(h.V, maximum) / maximum) * plotH;
        i ? g.lineTo(x, y) : g.moveTo(x, y);
      });
      g.strokeStyle = 'rgba(140,220,255,0.95)';
      g.lineWidth = 1.4;
      g.stroke();
      const last = history[history.length - 1];
      const lx = x0 + ((last.T - t0) / Math.max(1e-6, t1 - t0)) * plotW;
      const ly = y0 - (Math.min(last.V, maximum) / maximum) * plotH;
      g.fillStyle = '#eaf6ff';
      g.beginPath();
      g.arc(lx, ly, 2.4, 0, Math.PI * 2);
      g.fill();
    }
    g.fillStyle = 'rgba(255,255,255,0.35)';
    g.font = '8px ui-monospace, monospace';
    g.fillText(`${maximum.toFixed(1)} m/s`, x0, 10);
    g.fillText('0', x0, y0 + 9);
    g.textAlign = 'right';
    g.fillText('60 s', x0 + plotW, y0 + 9);
    g.textAlign = 'left';

    const beaufort = M.Beaufort(speed);
    const tag = document.getElementById('BeaufortTag');
    if (tag) tag.textContent = `F${beaufort.Force} · ${beaufort.Name}`;
    const stateLine = document.getElementById('AnemoState');
    if (stateLine) stateLine.textContent = `${speed.toFixed(2)} m/s at the probe — ${beaufort.Land}. Read alongside the site wind the whole scene is resolving.`;
  }

  function drawSpectrum() {
    const { Context: g, Width: width, Height: height } = FitCanvas(spectrumCanvas, 92);
    g.clearRect(0, 0, width, height);
    g.fillStyle = '#0b0b0e';
    g.fillRect(0, 0, width, height);
    const runtime = state.Frame();
    const sample = EvaluateAt(runtime, state.Probe(), state.Time(), { Trace: true, Ambient: null });
    const rows = sample.PerSource || [];
    if (!rows.length) {
      g.fillStyle = 'rgba(255,255,255,0.35)';
      g.font = '9px ui-monospace, monospace';
      g.fillText('no source envelopes reach the probe', 8, height / 2);
      return;
    }
    const contributions = rows.map((row) => ({
      Name: row.Name,
      Value: row.Weight ? M.Length(row.Velocity || [0, 0, 0]) + M.Length(row.Acceleration || [0, 0, 0]) : 0,
      Weight: row.Weight || 0,
      Colour: LawById[row.Law] ? (LawById[row.Law].Channel === 'velocity' ? [104, 200, 255] : LawById[row.Law].Channel === 'acceleration' ? [255, 170, 96] : [150, 150, 170]) : [200, 200, 200],
    }));
    const maximum = Math.max(1e-6, ...contributions.map((c) => c.Value));
    const rowHeight = Math.max(9, Math.min(16, (height - 6) / contributions.length));
    contributions.slice(0, 8).forEach((c, i) => {
      const y = 4 + i * rowHeight;
      if (y + rowHeight > height) return;
      const barWidth = (width - 92) * Math.min(1, c.Value / maximum);
      g.fillStyle = `rgba(${c.Colour[0]},${c.Colour[1]},${c.Colour[2]},0.24)`;
      g.fillRect(88, y, width - 92, rowHeight - 3);
      g.fillStyle = `rgba(${c.Colour[0]},${c.Colour[1]},${c.Colour[2]},0.95)`;
      g.fillRect(88, y, Math.max(1, barWidth), rowHeight - 3);
      g.fillStyle = 'rgba(210,216,226,0.8)';
      g.font = '8px ui-monospace, monospace';
      g.fillText(`${c.Name}`.slice(0, 18), 2, y + rowHeight - 6);
    });
  }

  function drawCurves() {
    const { Context: g, Width: width, Height: height } = FitCanvas(curveCanvas, 92);
    g.clearRect(0, 0, width, height);
    g.fillStyle = '#0b0b0e';
    g.fillRect(0, 0, width, height);
    const pad = 8;
    const x0 = pad;
    const y0 = height - 14;
    const plotW = width - pad * 2;
    const plotH = height - 24;
    g.strokeStyle = 'rgba(255,255,255,0.07)';
    for (let i = 0; i <= 4; i++) {
      const x = x0 + (i / 4) * plotW;
      g.beginPath();
      g.moveTo(x, y0 - plotH);
      g.lineTo(x, y0);
      g.stroke();
    }
    const selected = state.SelectedSource();
    const palette = { SmoothStep: [120, 205, 255], Linear: [255, 178, 56], InverseSquare: [140, 230, 160], Constant: [220, 140, 255] };
    for (const kind of AttenuationKinds) {
      const active = selected && selected.Attenuation === kind;
      g.beginPath();
      for (let i = 0; i <= 90; i++) {
        const u = i / 90;
        const value = M.Attenuation(kind, u);
        const x = x0 + u * plotW;
        const y = y0 - value * plotH;
        i ? g.lineTo(x, y) : g.moveTo(x, y);
      }
      const colour = palette[kind];
      g.strokeStyle = `rgba(${colour[0]},${colour[1]},${colour[2]},${active ? 1 : 0.36})`;
      g.lineWidth = active ? 1.9 : 1.1;
      g.stroke();
    }
    g.fillStyle = 'rgba(255,255,255,0.35)';
    g.font = '8px ui-monospace, monospace';
    g.fillText('u = 0', x0, height - 2);
    g.textAlign = 'right';
    g.fillText('u = 1 → 0', x0 + plotW, height - 2);
    g.textAlign = 'left';
  }

  function drawStrip() {
    const runtime = state.Frame();
    const sample = EvaluateAt(runtime, state.Probe(), state.Time());
    const scale = Math.max(1e-6, M.Length(sample.Velocity), M.Length(sample.Acceleration));
    const values = {
      vx: sample.Velocity[0], vy: sample.Velocity[1], vz: sample.Velocity[2],
      ax: sample.Acceleration[0], ay: sample.Acceleration[1], az: sample.Acceleration[2],
    };
    for (const [key, value] of Object.entries(values)) {
      const bar = channelBars[key];
      if (bar) {
        const magnitude = Math.min(1, Math.abs(value) / scale);
        bar.style.width = `${Math.max(1, magnitude * 100)}%`;
        bar.style.background = value < 0 ? '#ff8f7f' : '#8ad8ff';
      }
      const label = strip.querySelector(`[data-channel="${key}"]`);
      if (label) label.textContent = value.toFixed(2);
    }
    const clock = document.getElementById('TelemetryClock');
    if (clock) clock.textContent = `t ${state.Time().toFixed(2)} s`;
  }

  let visible = false;
  let lastDraw = 0;

  function draw() {
    if (!visible) return;
    const now = performance.now();
    if (now - lastDraw < 90) return;
    lastDraw = now;
    drawAnemometer();
    drawSpectrum();
    drawCurves();
    drawStrip();
  }

  state.Subscribe((reason) => {
    if (reason === 'document' || reason === 'time' || reason === 'probe' || reason === 'selection') draw();
  });

  return {
    Node: shell,
    Toggle: () => {
      visible = !visible;
      shell.classList.toggle('open', visible);
      if (visible) {
        lastDraw = 0;
        draw();
      }
      return visible;
    },
    Open: () => {
      if (!visible) {
        visible = true;
        shell.classList.add('open');
        lastDraw = 0;
        draw();
      }
    },
    Close: () => {
      visible = false;
      shell.classList.remove('open');
    },
    IsOpen: () => visible,
    Draw: draw,
  };
}
