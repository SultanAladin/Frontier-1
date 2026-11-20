/* ════════════════════════════════════════════════════════════════════════════════════════════
   THE MOON PANEL
   Not a list of sliders with "Moon" written above it. A moon is a thing you look at, so the panel
   shows you one: the real phase, the real tint, the real earthshine, sitting at its real altitude
   over a horizon, redrawn the instant anything changes.

   Everything here is an instrument rather than a field:
     · the sky drags left and right to scrub the phase
     · the compass and the altitude arc drag the moon across the sky — and because the moon rides
       the world clock, dragging it scrubs the time of day with it
     · the phase strip is eight little moons, not a dropdown
     · angular size is drawn against the real moon's half a degree
   The numbers are still there, and still typed in, because a designer needs both.
   ════════════════════════════════════════════════════════════════════════════════════════════ */
import { el, colorChip } from '../kit.js';
import { tape, stepper, pillToggle } from './controls.js';
import { ic } from '../icons.js';
import { bus } from '../bus.js';

/* ── lunar arithmetic ──────────────────────────────────────────────────────────────────────── */
export const illumination = p => (1 - Math.cos(2 * Math.PI * p)) / 2;

export function phaseName(p) {
  const q = ((p % 1) + 1) % 1;
  if (q < 0.035 || q >= 0.965) return 'New moon';
  if (q < 0.215) return 'Waxing crescent';
  if (q < 0.285) return 'First quarter';
  if (q < 0.465) return 'Waxing gibbous';
  if (q < 0.535) return 'Full moon';
  if (q < 0.715) return 'Waning gibbous';
  if (q < 0.785) return 'Last quarter';
  return 'Waning crescent';
}
const COMPASS = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
const compassOf = az => COMPASS[Math.round((((az % 360) + 360) % 360) / 22.5) % 16];

/* the moon's place in the sky is a function of the world clock, so moving it moves the clock */
const HOUR_FROM_AZ = az => 18 + (az - 90) / 15;
function hourFromElevation(elev, current) {
  const e = Math.max(-58, Math.min(58, elev));
  const a = Math.asin(e / 58) * 12 / Math.PI;          /* rising branch */
  const opts = [18 + a, 18 + (12 - a)].map(h => ((h % 24) + 24) % 24);
  const dist = h => { const d = Math.abs(h - current); return Math.min(d, 24 - d); };
  return dist(opts[0]) <= dist(opts[1]) ? opts[0] : opts[1];
}

/* ── the moon itself, drawn ────────────────────────────────────────────────────────────────── */
const hex = (c, a) => {
  const n = parseInt((c || '#d8e2f2').slice(1), 16);
  return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${a})`;
};

/* one deterministic little face, so the moon does not shimmer as it redraws */
const CRATERS = [[-0.28, -0.22, 0.20], [0.16, -0.34, 0.13], [0.34, 0.12, 0.17], [-0.14, 0.34, 0.15],
  [-0.46, 0.10, 0.10], [0.05, 0.05, 0.11], [0.44, -0.30, 0.08], [-0.02, -0.55, 0.07]];

export function drawMoon(ctx, cx, cy, r, { phase = 0.5, tint = '#d8e2f2', earthshine = 0.16, brightness = 1 }) {
  const p = ((phase % 1) + 1) % 1;
  const waxing = p < 0.5;
  const a = Math.cos(2 * Math.PI * p) * r;             /* terminator ellipse, signed */
  const k = illumination(p);

  /* halo */
  if (k > 0.02) {
    const halo = ctx.createRadialGradient(cx, cy, r * 0.8, cx, cy, r * (2.6 + brightness));
    halo.addColorStop(0, hex(tint, 0.20 * k * Math.min(brightness, 2)));
    halo.addColorStop(1, hex(tint, 0));
    ctx.fillStyle = halo;
    ctx.beginPath(); ctx.arc(cx, cy, r * (2.6 + brightness), 0, Math.PI * 2); ctx.fill();
  }

  /* the dark side, lit by earthshine only */
  ctx.save();
  ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.clip();
  ctx.fillStyle = hex(tint, 0.06 + earthshine * 0.5);
  ctx.fillRect(cx - r, cy - r, r * 2, r * 2);

  /* the lit crescent / gibbous */
  if (k > 0.005) {
    ctx.save();
    if (!waxing) { ctx.translate(cx, 0); ctx.scale(-1, 1); ctx.translate(-cx, 0); }
    ctx.beginPath();
    ctx.arc(cx, cy, r, -Math.PI / 2, Math.PI / 2, false);
    ctx.ellipse(cx, cy, Math.abs(a), r, 0, Math.PI / 2, -Math.PI / 2, a > 0);
    ctx.closePath();
    const g = ctx.createLinearGradient(cx - r, cy - r, cx + r, cy + r);
    g.addColorStop(0, hex(tint, Math.min(1, 0.72 * brightness)));
    g.addColorStop(1, hex(tint, Math.min(1, 0.98 * brightness)));
    ctx.fillStyle = g;
    ctx.fill();
    ctx.restore();
  }

  /* maria, faint on the dark side and readable on the lit one */
  CRATERS.forEach(([x, y, s], i) => {
    const px = cx + x * r, py = cy + y * r;
    const lit = waxing ? (px - cx) > a * -1 : (cx - px) > a * -1;
    ctx.beginPath();
    ctx.arc(px, py, s * r, 0, Math.PI * 2);
    ctx.fillStyle = `rgba(20,26,40,${(lit ? 0.13 : 0.05) + (i % 3) * 0.015})`;
    ctx.fill();
  });
  ctx.restore();

  /* limb */
  ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.strokeStyle = hex(tint, 0.16 + 0.2 * k);
  ctx.lineWidth = 1;
  ctx.stroke();
}

/* ── the panel ─────────────────────────────────────────────────────────────────────────────── */
export function moonPanel(node, ctx) {
  const { compact = false, setProp, register } = ctx;
  const P = node.props;
  const host = el('div', 'mpanel');
  const syncers = [];
  const on = (elm, ev, fn) => elm.addEventListener(ev, fn);

  /* the sky is where the moon lives; the clock is what moves it */
  const setSky = (elev, az) => {
    const now = ((HOUR_FROM_AZ(P.azimuth ?? 292) % 24) + 24) % 24;   /* the clock the moon implies */
    const cur = az != null ? HOUR_FROM_AZ(az) : hourFromElevation(elev, now);
    bus.emit('settod', ((cur % 24) + 24) % 24);
  };

  /* ── card 1 · tonight ─────────────────────────────────────────────────────────────────── */
  const hero = el('div', 'pcard mp-hero');
  const cv = el('canvas', 'mp-sky');
  cv.title = 'Drag left and right to walk the phase';
  const cap = el('div', 'mp-cap',
    `<div class="l"><b class="mp-name">—</b><span class="mp-illum">—</span></div>
     <div class="r"><span class="mp-alt">—</span></div>`);
  hero.append(cv, cap);
  host.appendChild(hero);

  /* ── the rail · four numbers you read without stopping ────────────────────────────────── */
  const rail = el('div', 'mp-rail');
  const pill = k => {
    const b = el('div', 'mp-pill', `<b class="v">—</b><span class="k">${k}</span>`);
    rail.appendChild(b);
    return b.querySelector('.v');
  };
  const pLit = pill('Lit'), pAge = pill('Age'), pAlt = pill('Alt'), pAz = pill('Bearing');
  pAz.parentElement.classList.add('wide');
  host.appendChild(rail);

  /* ── the duo · is it up, and is it worth anything ─────────────────────────────────────── */
  const duo = el('div', 'mp-duo');
  const statCard = (icon, label) => {
    const c = el('div', 'pcard mp-stat',
      `<span class="i">${icon}</span><span class="l">${label}</span><b class="n">—</b>`);
    duo.appendChild(c);
    return c;
  };
  const sUp = statCard(ic('check', { size: 12 }), 'Above horizon');
  const sLx = statCard(ic('moon', { size: 12 }), 'Moonlight');
  host.appendChild(duo);

  /* ── the metric · illumination across the whole cycle ─────────────────────────────────── */
  const mc = el('div', 'pcard mp-metric');
  mc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Illumination</span></div>
      <button class="mp-x" title="Taller chart">${ic('arrowout', { size: 12 })}</button>
    </div>
    <div class="mp-num"><span class="i">—</span><span class="d"></span><span class="u">%</span></div>
    <div class="mp-k mp-target">Peak <span class="v">100% at full</span></div>`;
  const chartWrap = el('div', 'mp-chartwrap');
  const chart = el('canvas');
  chartWrap.appendChild(chart);
  mc.appendChild(chartWrap);
  mc.querySelector('.mp-x').onclick = () => { mc.classList.toggle('tall'); paintChart(); };
  host.appendChild(mc);

  const numI = mc.querySelector('.mp-num .i');
  const numD = mc.querySelector('.mp-num .d');

  function paintChart() {
    const w = chartWrap.clientWidth || 280, h = mc.classList.contains('tall') ? 168 : 104;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (chart.width !== w * dpr || chart.height !== h * dpr) { chart.width = w * dpr; chart.height = h * dpr; }
    chart.style.height = h + 'px';
    const g = chart.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);

    const R = 34, L = 2, T = 8, B = 16;                /* the axis lives on the right, as in a console */
    const px = t => L + t * (w - L - R);
    const py = v => T + (1 - v) * (h - T - B);
    g.font = '9px ui-sans-serif, system-ui';

    /* the grid, and the numbers hung off the right edge */
    [0.25, 0.5, 0.75, 1].forEach(v => {
      g.strokeStyle = v === 1 ? 'rgba(255,255,255,.16)' : 'rgba(255,255,255,.07)';
      g.setLineDash(v === 1 ? [4, 4] : [2, 5]);
      g.beginPath(); g.moveTo(px(0), py(v)); g.lineTo(px(1), py(v)); g.stroke();
      g.setLineDash([]);
      g.fillStyle = 'rgba(255,255,255,.30)';
      g.textAlign = 'left';
      g.fillText(`${v * 100}%`, w - R + 7, py(v) + 3);
    });

    const cur = ((P.phase ?? 0.68) % 1 + 1) % 1;

    /* the band under the moment you are looking at */
    const bw = Math.max(10, (w - L - R) * 0.055);
    g.fillStyle = 'rgba(255,255,255,.05)';
    g.fillRect(px(cur) - bw / 2, T, bw, h - T - B);

    /* apparent brightness sits under the lit curve, faint, like a second trace */
    const bAmp = Math.min(1, (P.brightness ?? 1.1) / 2);
    g.strokeStyle = 'rgba(255,255,255,.14)';
    g.lineWidth = 1;
    g.beginPath();
    for (let i = 0; i <= 120; i++) {
      const t = i / 120, v = illumination(t) * bAmp;
      i ? g.lineTo(px(t), py(v)) : g.moveTo(px(t), py(v));
    }
    g.stroke();

    /* the lit curve itself — amber where there is barely a moon to speak of */
    g.lineWidth = 1.6;
    for (let i = 0; i < 120; i++) {
      const t0 = i / 120, t1 = (i + 1) / 120;
      const v0 = illumination(t0), v1 = illumination(t1);
      g.strokeStyle = Math.min(v0, v1) < 0.08 ? 'rgba(245,158,11,.85)' : 'rgba(232,238,255,.75)';
      g.beginPath(); g.moveTo(px(t0), py(v0)); g.lineTo(px(t1), py(v1)); g.stroke();
    }

    /* the quarters, ticked and named along the foot */
    const marks = [[0, 'NEW'], [0.25, '1Q'], [0.5, 'FULL'], [0.75, '3Q'], [1, 'NEW']];
    g.fillStyle = 'rgba(255,255,255,.26)';
    marks.forEach(([t, lbl], i) => {
      g.strokeStyle = 'rgba(255,255,255,.10)';
      g.beginPath(); g.moveTo(px(t), py(0)); g.lineTo(px(t), py(0) + 4); g.stroke();
      g.textAlign = i === 0 ? 'left' : i === marks.length - 1 ? 'right' : 'center';
      g.fillText(lbl, px(t), h - 4);
    });

    /* and where the moon is tonight */
    const v = illumination(cur);
    g.fillStyle = '#fff';
    g.beginPath(); g.arc(px(cur), py(v), 3, 0, Math.PI * 2); g.fill();
    g.strokeStyle = 'rgba(255,255,255,.25)';
    g.beginPath(); g.moveTo(px(cur), py(v) + 4); g.lineTo(px(cur), py(0)); g.stroke();
  }

  /* ── the atlas · the near side on a grid, with the terminator laid over it ────────────── */
  const MARIA = [
    [-57, 16, 23, 27, 'PROCELLARUM'], [-16, 33, 19, 13, 'IMBRIUM'],
    [18, 28, 12, 10, 'SERENITATIS'], [31, 7, 13, 11, 'TRANQUILLITATIS'],
    [59, 17, 8, 7, 'CRISIUM'], [52, -8, 10, 10, 'FECUNDITATIS'],
    [34, -15, 6, 6, 'NECTARIS'], [-17, -21, 11, 8, 'NUBIUM'],
    [-39, -24, 7, 7, 'HUMORUM'], [4, 13, 5, 4, 'VAPORUM'], [-5, 55, 32, 5, 'FRIGORIS'],
  ];
  const CRATERS_A = [[-11, -43, 'TYCHO'], [-20, 10, 'COPERNICUS'], [-2, -2, ''], [23, -44, ''], [-48, -12, '']];

  const ac = el('div', 'pcard mp-atlas');
  ac.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Atlas</span><span class="s">Near side · selenographic grid</span></div>
      <button class="mp-x" title="Taller atlas">${ic('arrowout', { size: 12 })}</button>
    </div>`;
  const mapWrap = el('div', 'mp-map');
  const map = el('canvas');
  map.title = 'Drag to walk the terminator across the face';
  mapWrap.appendChild(map);
  const readout = el('div', 'mp-read', '');
  mapWrap.appendChild(readout);
  ac.appendChild(mapWrap);

  const layers = { grid: true, labels: true, term: true };
  const tags = el('div', 'mp-tags');
  Object.keys(layers).forEach(k => {
    const t = el('button', 'mp-tag on', k === 'term' ? 'TERMINATOR' : k.toUpperCase());
    t.onclick = () => { layers[k] = !layers[k]; t.classList.toggle('on', layers[k]); paintMap(); };
    tags.appendChild(t);
  });
  ac.appendChild(tags);

  ac.querySelector('.mp-x').onclick = () => { ac.classList.toggle('tall'); paintMap(); };
  host.appendChild(ac);

  const subsolar = p => 180 - 360 * (((p % 1) + 1) % 1);   /* longitude the sun stands over */

  function paintMap() {
    const w = mapWrap.clientWidth || 280, h = ac.classList.contains('tall') ? 214 : 148;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (map.width !== w * dpr || map.height !== h * dpr) { map.width = w * dpr; map.height = h * dpr; }
    map.style.height = h + 'px';
    const g = map.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const X = lon => (lon + 90) / 180 * w;
    const Y = lat => (62 - lat) / 124 * h;

    /* regolith */
    const base = g.createLinearGradient(0, 0, 0, h);
    base.addColorStop(0, '#171a20');
    base.addColorStop(0.5, '#1d2027');
    base.addColorStop(1, '#141619');
    g.fillStyle = base; g.fillRect(0, 0, w, h);

    /* the seas, and the highlands they sit in */
    MARIA.forEach(([lon, lat, rx, ry], i) => {
      const x = X(lon), y = Y(lat);
      const gx = g.createRadialGradient(x, y, 0, x, y, Math.max(rx / 180 * w, ry / 124 * h));
      gx.addColorStop(0, `rgba(8,10,16,${0.62 - (i % 3) * 0.06})`);
      gx.addColorStop(1, 'rgba(8,10,16,0)');
      g.save();
      g.translate(x, y);
      g.scale(1, (ry / 124 * h) / (rx / 180 * w));
      g.fillStyle = gx;
      g.beginPath(); g.arc(0, 0, rx / 180 * w, 0, Math.PI * 2); g.fill();
      g.restore();
    });
    CRATERS_A.forEach(([lon, lat, name]) => {
      const x = X(lon), y = Y(lat);
      g.strokeStyle = 'rgba(255,255,255,.18)';
      g.lineWidth = 1;
      g.beginPath(); g.arc(x, y, name === 'TYCHO' ? 4.5 : 3.2, 0, Math.PI * 2); g.stroke();
      if (name === 'TYCHO') {                        /* the rays, because everyone knows them */
        g.strokeStyle = 'rgba(255,255,255,.07)';
        for (let a = 0; a < 9; a++) {
          const t = a / 9 * Math.PI * 2 + 0.3;
          g.beginPath(); g.moveTo(x + Math.cos(t) * 6, y + Math.sin(t) * 6);
          g.lineTo(x + Math.cos(t) * (26 + (a % 3) * 12), y + Math.sin(t) * (18 + (a % 3) * 9));
          g.stroke();
        }
      }
    });

    /* the graticule */
    if (layers.grid) {
      g.font = '8px ui-sans-serif, system-ui';
      for (let lon = -90; lon <= 90; lon += 15) {
        const major = lon % 45 === 0;
        g.strokeStyle = lon === 0 ? 'rgba(255,255,255,.20)' : `rgba(255,255,255,${major ? 0.11 : 0.05})`;
        g.setLineDash(lon === 0 ? [] : [2, 4]);
        g.beginPath(); g.moveTo(X(lon), 0); g.lineTo(X(lon), h); g.stroke();
        if (major) {
          g.setLineDash([]);
          g.fillStyle = 'rgba(255,255,255,.34)';
          g.textAlign = lon <= -90 ? 'left' : lon >= 90 ? 'right' : 'center';
          g.fillText(lon === 0 ? '0°' : `${Math.abs(lon)}°${lon < 0 ? 'W' : 'E'}`, X(lon), h - 5);
        }
      }
      for (let lat = -60; lat <= 60; lat += 15) {
        const major = lat % 30 === 0;
        g.strokeStyle = lat === 0 ? 'rgba(255,255,255,.20)' : `rgba(255,255,255,${major ? 0.11 : 0.05})`;
        g.setLineDash(lat === 0 ? [] : [2, 4]);
        g.beginPath(); g.moveTo(0, Y(lat)); g.lineTo(w, Y(lat)); g.stroke();
        if (major && lat !== 0) {
          g.setLineDash([]);
          g.fillStyle = 'rgba(255,255,255,.34)';
          g.textAlign = 'left';
          g.fillText(`${lat > 0 ? '+' : '−'}${Math.abs(lat)}`, 4, Y(lat) - 3);
        }
      }
      g.setLineDash([]);
    }

    /* names, only where there is room for them */
    if (layers.labels && w > 250) {
      g.font = '7.5px ui-sans-serif, system-ui';
      g.textAlign = 'center';
      MARIA.forEach(([lon, lat, rx, , name]) => {
        if (rx < 11) return;                          /* only the seas with room for their names */
        g.fillStyle = 'rgba(226,234,255,.40)';
        g.fillText(name, X(lon), Y(lat) + 2);
      });
      g.fillStyle = 'rgba(226,234,255,.32)';
      g.fillText('TYCHO', X(-11), Y(-43) + 13);
    }

    /* night, poured over the face from wherever the sun is not */
    const ls = subsolar(P.phase ?? 0.68);
    for (let x = 0; x < w; x++) {
      const lon = x / w * 180 - 90;
      const f = Math.cos((lon - ls) * Math.PI / 180);
      const dark = Math.max(0, Math.min(1, 1 - Math.max(0, f) ** 0.55));
      g.fillStyle = `rgba(3,5,11,${(0.9 * dark).toFixed(3)})`;
      g.fillRect(x, 0, 1.02, h);
    }
    if (layers.term) {
      for (const t of [ls - 90, ls + 90]) {          /* both edges of the lit hemisphere */
        if (t < -92 || t > 92) continue;
        g.strokeStyle = 'rgba(160,190,255,.55)';
        g.lineWidth = 1;
        g.setLineDash([5, 4]);
        g.beginPath(); g.moveTo(X(t), 0); g.lineTo(X(t), h); g.stroke();
        g.setLineDash([]);
      }
    }

    /* the sub-solar point, and the point the earth stares at */
    if (Math.abs(ls) <= 90) {
      const x = X(ls), y = Y(0);
      g.strokeStyle = 'rgba(255,214,140,.9)';
      g.beginPath(); g.arc(x, y, 4.5, 0, Math.PI * 2); g.stroke();
      g.beginPath(); g.moveTo(x - 9, y); g.lineTo(x - 6, y); g.moveTo(x + 6, y); g.lineTo(x + 9, y);
      g.moveTo(x, y - 9); g.lineTo(x, y - 6); g.moveTo(x, y + 6); g.lineTo(x, y + 9); g.stroke();
    }
    const ex = X(0), ey = Y(0);
    g.strokeStyle = 'rgba(255,255,255,.8)';
    g.lineWidth = 1.4;
    g.beginPath(); g.moveTo(ex - 5, ey); g.lineTo(ex + 5, ey); g.moveTo(ex, ey - 5); g.lineTo(ex, ey + 5); g.stroke();
    g.fillStyle = 'rgba(255,255,255,.9)';
    g.beginPath(); g.arc(ex, ey, 1.7, 0, Math.PI * 2); g.fill();

    readout.innerHTML =
      `<span class="k">sub-earth</span><b>0.0°N 0.0°E</b><span class="k">sub-solar</span><b>${ls >= 0 ? '' : '−'}${Math.abs(ls).toFixed(1)}°${ls < 0 ? 'W' : 'E'}</b>`;
  }

  const scrub = (elm, fn) => {
    const move = e => {
      const r = elm.getBoundingClientRect();
      fn(Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)), e);
    };
    on(elm, 'pointerdown', e => { elm.setPointerCapture(e.pointerId); elm.classList.add('grabbing'); move(e); });
    on(elm, 'pointermove', e => { if (elm.hasPointerCapture?.(e.pointerId)) move(e); });
    on(elm, 'pointerup', e => { elm.releasePointerCapture?.(e.pointerId); elm.classList.remove('grabbing'); });
  };
  /* dragging the atlas walks the terminator, which is the same thing as walking the phase */
  let mdrag = null;
  on(map, 'pointerdown', e => { map.setPointerCapture(e.pointerId); mdrag = { x: e.clientX, p: P.phase ?? 0.68 }; map.classList.add('grabbing'); });
  on(map, 'pointermove', e => {
    if (!mdrag) return;
    const p = (((mdrag.p - (e.clientX - mdrag.x) / (map.clientWidth || 280) * 0.5) % 1) + 1) % 1;
    setProp(node, 'phase', +p.toFixed(4));
    paintAll();
  });
  const endMap = () => { mdrag = null; map.classList.remove('grabbing'); };
  on(map, 'pointerup', endMap);
  on(map, 'pointercancel', endMap);

  const sky = cv.getContext('2d');
  function paintSky() {
    const w = cv.clientWidth || 300, h = compact ? 116 : 138;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (cv.width !== w * dpr || cv.height !== h * dpr) { cv.width = w * dpr; cv.height = h * dpr; }
    cv.style.height = h + 'px';
    sky.setTransform(dpr, 0, 0, dpr, 0, 0);
    sky.clearRect(0, 0, w, h);

    const elev = P.elevation ?? 46;
    const up = Math.max(-1, Math.min(1, elev / 60));
    const horizon = h - 22;

    /* night sky, warmer near the horizon */
    const g = sky.createLinearGradient(0, 0, 0, h);
    g.addColorStop(0, '#05070f');
    g.addColorStop(0.62, '#0a1020');
    g.addColorStop(1, '#141c2c');
    sky.fillStyle = g; sky.fillRect(0, 0, w, h);

    /* stars — fixed, so redraws do not sparkle */
    for (let i = 0; i < 46; i++) {
      const x = ((i * 977) % 1000) / 1000 * w;
      const y = ((i * 613) % 1000) / 1000 * horizon;
      const s = ((i * 37) % 10) / 10;
      sky.fillStyle = `rgba(210,224,255,${0.10 + s * 0.35})`;
      sky.fillRect(x, y, 1.1, 1.1);
    }

    /* horizon and its haze */
    const hz = sky.createLinearGradient(0, horizon - 26, 0, horizon);
    hz.addColorStop(0, 'rgba(90,120,180,0)');
    hz.addColorStop(1, 'rgba(120,150,205,.16)');
    sky.fillStyle = hz; sky.fillRect(0, horizon - 26, w, 26);
    sky.fillStyle = '#070a11'; sky.fillRect(0, horizon, w, h - horizon);
    sky.strokeStyle = 'rgba(255,255,255,.10)';
    sky.beginPath(); sky.moveTo(0, horizon + .5); sky.lineTo(w, horizon + .5); sky.stroke();

    /* the moon, placed by altitude and by which way it is facing */
    const r = Math.max(13, Math.min(30, 9 + (P.angular ?? 1.6) * 8));
    const cx = w * (0.18 + (((P.azimuth ?? 292) % 360) / 360) * 0.64);
    const cy = horizon - 14 - up * (horizon - 34);
    drawMoon(sky, cx, cy, r, {
      phase: P.phase ?? 0.68, tint: P.tint, earthshine: P.earthshine ?? 0.16,
      brightness: P.brightness ?? 1.1,
    });

    /* a moon under the horizon is a fact worth showing, not hiding */
    if (elev < 0) {
      sky.fillStyle = 'rgba(4,6,11,.72)';
      sky.fillRect(0, horizon, w, h - horizon);
      sky.fillStyle = 'rgba(4,6,11,.55)';
      sky.fillRect(0, 0, w, h);
    }
  }

  const nameEl = cap.querySelector('.mp-name');
  const illumEl = cap.querySelector('.mp-illum');
  const altEl = cap.querySelector('.mp-alt');
  function paintCap() {
    const p = P.phase ?? 0.68;
    nameEl.textContent = phaseName(p);
    illumEl.textContent = `${Math.round(illumination(p) * 100)}% lit`;
    const e = P.elevation ?? 46, az = P.azimuth ?? 292;
    altEl.innerHTML = e < 0
      ? `<span class="down">below the horizon</span> · ${compassOf(az)}`
      : `${e.toFixed(0)}° above · ${compassOf(az)} ${Math.round(az)}°`;
  }

  /* drag across the sky to walk the phase */
  let drag = null;
  on(cv, 'pointerdown', e => {
    cv.setPointerCapture(e.pointerId);
    drag = { x: e.clientX, p: P.phase ?? 0.68 };
    cv.classList.add('grabbing');
  });
  on(cv, 'pointermove', e => {
    if (!drag) return;
    const w = cv.clientWidth || 300;
    const p = (((drag.p + (e.clientX - drag.x) / w) % 1) + 1) % 1;
    setProp(node, 'phase', +p.toFixed(4));
    paintAll();
  });
  const endDrag = () => { drag = null; cv.classList.remove('grabbing'); };
  on(cv, 'pointerup', endDrag);
  on(cv, 'pointercancel', endDrag);

  /* ── card 2 · phase ───────────────────────────────────────────────────────────────────── */
  const pc = el('div', 'pcard mp-phase');
  pc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Phase</span><span class="s">—</span></div>
      <button class="mp-x" title="Jump to full">${ic('arrowout', { size: 12 })}</button>
    </div>`;
  const pb = el('div', 'pbody');
  pc.appendChild(pb);
  pc.querySelector('.mp-chead .l').onclick = () => pc.classList.toggle('shut');
  pc.querySelector('.mp-x').onclick = () => { setProp(node, 'phase', 0.5); paintAll(); };
  const phaseSub = pc.querySelector('.mp-chead .s');

  const strip = el('div', 'mp-strip');
  const stops = [0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875];
  const chips = stops.map(v => {
    const b = el('button', 'mp-chip');
    b.title = phaseName(v);
    const c = el('canvas');
    c.width = 68; c.height = 68;
    b.appendChild(c);
    const g = c.getContext('2d');
    g.setTransform(2, 0, 0, 2, 0, 0);
    drawMoon(g, 17, 17, 12, { phase: v, tint: P.tint, earthshine: 0.14, brightness: 1 });
    b.onclick = () => { setProp(node, 'phase', v); paintAll(); };
    strip.appendChild(b);
    return { b, v };
  });
  pb.appendChild(strip);

  /* the cycle, ticked out day by day, with the moon itself as the handle */
  const tlWrap = el('div', 'mp-tl');
  const tl = el('canvas');
  tlWrap.appendChild(tl);
  const tlLbl = el('div', 'mp-tllbl', '<span>NEW</span><span>FULL</span><span>NEW</span>');
  pb.append(tlWrap, tlLbl);

  function paintTl() {
    const w = tlWrap.clientWidth || 280, h = 26;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (tl.width !== w * dpr || tl.height !== h * dpr) { tl.width = w * dpr; tl.height = h * dpr; }
    tl.style.height = h + 'px';
    const g = tl.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const pad = 9, span = w - pad * 2;
    const days = Math.max(1, Math.round(P.period ?? 27.3));

    g.fillStyle = 'rgba(255,255,255,.22)';
    g.beginPath(); g.arc(3.5, h / 2, 2.2, 0, Math.PI * 2); g.fill();
    g.beginPath(); g.arc(w - 3.5, h / 2, 2.2, 0, Math.PI * 2); g.fill();

    for (let i = 0; i <= days; i++) {
      const t = i / days, x = pad + t * span;
      const q = Math.abs(((t * 4) % 1)) < 0.02 || Math.abs(((t * 4) % 1)) > 0.98;
      g.strokeStyle = q ? 'rgba(255,255,255,.34)' : 'rgba(255,255,255,.14)';
      g.lineWidth = 1;
      const len = q ? 9 : 5;
      g.beginPath(); g.moveTo(x, h / 2 - len / 2); g.lineTo(x, h / 2 + len / 2); g.stroke();
    }

    const cur = ((P.phase ?? 0.68) % 1 + 1) % 1;
    const hx = pad + cur * span;
    g.fillStyle = 'rgba(18,18,18,.96)';
    g.strokeStyle = 'rgba(255,255,255,.16)';
    const bw = 30, bh = 20;
    g.beginPath();
    g.roundRect(Math.max(0, Math.min(w - bw, hx - bw / 2)), h / 2 - bh / 2, bw, bh, 10);
    g.fill(); g.stroke();
    drawMoon(g, Math.max(bw / 2, Math.min(w - bw / 2, hx)), h / 2, 6.4,
      { phase: cur, tint: P.tint, earthshine: 0.18, brightness: 1 });
  }
  scrub(tlWrap, t => { setProp(node, 'phase', +t.toFixed(4)); paintAll(); });

  /* the exact number, for when a designer knows what they want */
  const fine = stepper({
    value: P.phase ?? 0.68, min: 0, max: 1, dec: 3, step: 0.005,
    onInput: v => { setProp(node, 'phase', v); paintAll(); },
  });
  const fineRow = el('div', 'mp-subhead', '<span class="k">phase</span>');
  fineRow.appendChild(fine);
  pb.appendChild(fineRow);

  const period = tape({
    label: 'Cycle length', min: 1, max: 60, value: P.period ?? 27.3, dec: 1, unit: 'd', step: 0.1,
    marks: [{ t: 0, l: '1 d' }, { t: 27.3 / 59, l: 'LUNAR 27.3' }, { t: 1, l: '60 d' }],
    onInput: v => { setProp(node, 'period', v); paintAll(); },
  });
  pb.appendChild(period);
  const nextFull = el('div', 'mp-note', '');
  pb.appendChild(nextFull);
  host.appendChild(pc);

  /* ── card · the sky track ─────────────────────────────────────────────────────────────────
     Where it is now, where it has been, and where it is going — read as an arc over a horizon
     line, with the hours ticked out underneath and the moon itself as the handle. */
  const sc = el('div', 'pcard mp-track2');
  sc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Sky track</span><span class="s">Tonight's arc · noon to noon</span></div>
      <button class="mp-x" title="Taller arc">${ic('arrowout', { size: 12 })}</button>
    </div>
    <div class="mp-num"><span class="i">—</span><span class="d"></span><span class="u">°</span></div>
    <div class="mp-k mp-target">Bearing <span class="v">—</span></div>`;
  const sb = el('div', 'pbody');
  sc.appendChild(sb);
  sc.querySelector('.mp-chead .l').onclick = () => sc.classList.toggle('shut');
  sc.querySelector('.mp-x').onclick = () => { sc.classList.toggle('tall'); paintArc(); };
  const altI = sc.querySelector('.mp-num .i'), altD = sc.querySelector('.mp-num .d');
  const bearV = sc.querySelector('.mp-target .v');

  const arcWrap = el('div', 'mp-chartwrap');
  const arc = el('canvas');
  arcWrap.appendChild(arc);
  sb.appendChild(arcWrap);

  /* noon → midnight → noon, so the night the moon belongs to sits in one piece in the middle */
  const ALT_OF = h => 58 * Math.sin((h - 18) / 12 * Math.PI);

  function paintArc() {
    const w = arcWrap.clientWidth || 280, h = sc.classList.contains('tall') ? 168 : 108;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (arc.width !== w * dpr || arc.height !== h * dpr) { arc.width = w * dpr; arc.height = h * dpr; }
    arc.style.height = h + 'px';
    const g = arc.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const R = 30, L = 2, T = 8, B = 15;
    const px = t => L + t * (w - L - R);                  /* t: 0 = noon, 1 = noon */
    const py = v => T + (1 - (v + 66) / 132) * (h - T - B);
    g.font = '9px ui-sans-serif, system-ui';

    [60, 30, 0, -30, -60].forEach(v => {
      const horizon = v === 0;
      g.strokeStyle = horizon ? 'rgba(255,255,255,.24)' : 'rgba(255,255,255,.06)';
      g.setLineDash(horizon ? [] : [2, 5]);
      g.beginPath(); g.moveTo(px(0), py(v)); g.lineTo(px(1), py(v)); g.stroke();
      g.setLineDash([]);
      g.fillStyle = horizon ? 'rgba(255,255,255,.40)' : 'rgba(255,255,255,.26)';
      g.textAlign = 'left';
      g.fillText(horizon ? '0°' : `${v > 0 ? '+' : '−'}${Math.abs(v)}`, w - R + 6, py(v) + 3);
    });

    /* the ground, filled in, so up and down are never in question */
    g.fillStyle = 'rgba(255,255,255,.022)';
    g.fillRect(px(0), py(0), px(1) - px(0), py(-66) - py(0));

    const now = ((HOUR_FROM_AZ(P.azimuth ?? 292) % 24) + 24) % 24;
    const tOf = hr => (((hr - 12) % 24) + 24) % 24 / 24;

    /* the arc, bright where the moon is up and dim where it is under your feet */
    for (let i = 0; i < 144; i++) {
      const t0 = i / 144, t1 = (i + 1) / 144;
      const v0 = ALT_OF(12 + t0 * 24), v1 = ALT_OF(12 + t1 * 24);
      const up = Math.min(v0, v1) >= 0;
      g.strokeStyle = up ? 'rgba(232,238,255,.8)' : 'rgba(255,255,255,.16)';
      g.lineWidth = up ? 1.7 : 1;
      g.beginPath(); g.moveTo(px(t0), py(v0)); g.lineTo(px(t1), py(v1)); g.stroke();
    }

    /* rise and set, marked where the arc cuts the horizon */
    [[18, 'RISE'], [6, 'SET']].forEach(([hr, lbl]) => {
      const x = px(tOf(hr));
      g.fillStyle = 'rgba(255,255,255,.55)';
      g.beginPath(); g.arc(x, py(0), 2.4, 0, Math.PI * 2); g.fill();
      g.font = '8px ui-sans-serif, system-ui';
      g.textAlign = 'center';
      g.fillStyle = 'rgba(255,255,255,.34)';
      g.fillText(lbl, x, py(0) + 12);
      g.font = '9px ui-sans-serif, system-ui';
    });

    /* the hours */
    g.fillStyle = 'rgba(255,255,255,.26)';
    [[0, '12:00'], [0.25, '18:00'], [0.5, '00:00'], [0.75, '06:00'], [1, '12:00']].forEach(([t, lbl], i) => {
      g.strokeStyle = 'rgba(255,255,255,.09)';
      g.beginPath(); g.moveTo(px(t), T); g.lineTo(px(t), py(-66)); g.stroke();
      g.textAlign = i === 0 ? 'left' : i === 4 ? 'right' : 'center';
      g.fillText(lbl, px(t), h - 3);
    });

    /* and this exact minute */
    const t = tOf(now), v = P.elevation ?? ALT_OF(now);
    const bw = Math.max(8, (w - L - R) * 0.04);
    g.fillStyle = 'rgba(255,255,255,.05)';
    g.fillRect(px(t) - bw / 2, T, bw, py(-66) - T);
    g.strokeStyle = 'rgba(255,255,255,.22)';
    g.beginPath(); g.moveTo(px(t), py(v)); g.lineTo(px(t), py(0)); g.stroke();
    drawMoon(g, px(t), py(v), 5.6, { phase: P.phase ?? 0.68, tint: P.tint, earthshine: 0.2, brightness: 1 });
  }

  /* the arc is the instrument: drag along it to fly the moon through the night */
  const arcTime = e => {
    const r = arc.getBoundingClientRect();
    const L = 2, R = 30;
    const t = Math.max(0, Math.min(1, (e.clientX - r.left - L) / Math.max(1, r.width - L - R)));
    bus.emit('settod', (12 + t * 24) % 24);
  };
  on(arc, 'pointerdown', e => { arc.setPointerCapture(e.pointerId); arc.classList.add('drag'); arcTime(e); });
  on(arc, 'pointermove', e => { if (arc.hasPointerCapture?.(e.pointerId)) arcTime(e); });
  on(arc, 'pointerup', e => { arc.releasePointerCapture?.(e.pointerId); arc.classList.remove('drag'); });

  /* bearing, read off a compass tape rather than a dial */
  const bearing = tape({
    label: 'Bearing', min: 0, max: 360, value: ((P.azimuth ?? 292) % 360 + 360) % 360, dec: 0, unit: '°', step: 1,
    marks: [{ t: 0, l: 'N' }, { t: 0.25, l: 'E' }, { t: 0.5, l: 'S' }, { t: 0.75, l: 'W' }, { t: 1, l: 'N' }],
    onInput: v => setSky(null, v),
  });
  sb.appendChild(bearing);

  /* rise · transit · set, three numbers on one line */
  const track = el('div', 'mp-3up');
  sb.appendChild(track);

  /* the day, ticked hour by hour, with the moon as the handle — it moves the world clock */
  const dayWrap = el('div', 'mp-tl');
  const dayCv = el('canvas');
  dayWrap.appendChild(dayCv);
  const dayLbl = el('div', 'mp-tllbl', '<span>12:00</span><span>MIDNIGHT</span><span>12:00</span>');
  sb.append(dayWrap, dayLbl);

  function paintDay() {
    const w = dayWrap.clientWidth || 280, h = 26;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (dayCv.width !== w * dpr || dayCv.height !== h * dpr) { dayCv.width = w * dpr; dayCv.height = h * dpr; }
    dayCv.style.height = h + 'px';
    const g = dayCv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const pad = 9, span = w - pad * 2;
    g.fillStyle = 'rgba(255,255,255,.22)';
    g.beginPath(); g.arc(3.5, h / 2, 2.2, 0, Math.PI * 2); g.fill();
    g.beginPath(); g.arc(w - 3.5, h / 2, 2.2, 0, Math.PI * 2); g.fill();

    for (let i = 0; i <= 24; i++) {
      const t = i / 24, x = pad + t * span;
      const major = i % 6 === 0;
      const up = ALT_OF(12 + t * 24) >= 0;                /* the hours the moon is actually up */
      g.strokeStyle = major ? 'rgba(255,255,255,.34)' : `rgba(255,255,255,${up ? 0.2 : 0.08})`;
      const len = major ? 9 : 5;
      g.beginPath(); g.moveTo(x, h / 2 - len / 2); g.lineTo(x, h / 2 + len / 2); g.stroke();
    }

    const now = ((HOUR_FROM_AZ(P.azimuth ?? 292) % 24) + 24) % 24;
    const t = (((now - 12) % 24) + 24) % 24 / 24;
    const hx = pad + t * span;
    g.fillStyle = 'rgba(18,18,18,.96)';
    g.strokeStyle = 'rgba(255,255,255,.16)';
    const bw = 30, bh = 20;
    g.beginPath();
    g.roundRect(Math.max(0, Math.min(w - bw, hx - bw / 2)), h / 2 - bh / 2, bw, bh, 10);
    g.fill(); g.stroke();
    drawMoon(g, Math.max(bw / 2, Math.min(w - bw / 2, hx)), h / 2, 6.4,
      { phase: P.phase ?? 0.68, tint: P.tint, earthshine: 0.18, brightness: 1 });
  }
  scrub(dayWrap, t => bus.emit('settod', (12 + t * 24) % 24));
  host.appendChild(sc);

  /* ── card · light ─────────────────────────────────────────────────────────────────────────
     One number that means something on the ground, a meter that says what you could do by it,
     and the disc drawn against the half a degree the real moon takes up. */
  const lc = el('div', 'pcard mp-light');
  lc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Light</span><span class="s">Output · colour · scale</span></div>
      <button class="mp-x" title="Taller scale">${ic('arrowout', { size: 12 })}</button>
    </div>
    <div class="mp-num"><span class="i">—</span><span class="d"></span><span class="u">lx</span></div>
    <div class="mp-k mp-target">On the ground <span class="v">—</span></div>`;
  const lb = el('div', 'pbody');
  lc.appendChild(lb);
  lc.querySelector('.mp-chead .l').onclick = () => lc.classList.toggle('shut');
  const luxI = lc.querySelector('.mp-num .i'), luxD = lc.querySelector('.mp-num .d');
  const luxV = lc.querySelector('.mp-target .v');

  /* the meter — a log scale, because moonlight lives in the bottom decade */
  const meterWrap = el('div', 'mp-meter');
  meterWrap.innerHTML = '<div class="hd"><span class="k">moonlight</span></div>';
  const meter = el('canvas');
  meterWrap.appendChild(meter);
  const luxStep = stepper({
    value: P.moonlight ?? 0.35, min: 0, max: 2, dec: 2, step: 0.01, unit: 'lx',
    onInput: v => { setProp(node, 'moonlight', v); paintAll(); },
  });
  meterWrap.querySelector('.hd').appendChild(luxStep);
  lb.appendChild(meterWrap);
  const STOPS = [[0.0005, 'STARLIGHT'], [0.05, 'WALK'], [0.25, 'READ'], [1, 'PRINT'], [2, '']];
  const lpos = v => {
    const lo = Math.log10(0.0005), hi = Math.log10(2);
    return Math.max(0, Math.min(1, (Math.log10(Math.max(0.0005, v)) - lo) / (hi - lo)));
  };
  /* the meter is the control: grab it anywhere and the ground gets brighter */
  const luxFrom = e => {
    const r = meter.getBoundingClientRect();
    const pad = 8, span = Math.max(1, r.width - pad * 2);
    const t = Math.max(0, Math.min(1, (e.clientX - r.left - pad) / span));
    const lo = Math.log10(0.0005), hi = Math.log10(2);
    const ground = 10 ** (lo + t * (hi - lo));
    const k = Math.max(0.02, illumination(P.phase ?? 0.68));
    setProp(node, 'moonlight', +Math.min(2, ground / k).toFixed(3));
    paintAll();
  };
  on(meter, 'pointerdown', e => { meter.setPointerCapture(e.pointerId); meter.classList.add('drag'); luxFrom(e); });
  on(meter, 'pointermove', e => { if (meter.hasPointerCapture?.(e.pointerId)) luxFrom(e); });
  on(meter, 'pointerup', e => { meter.releasePointerCapture?.(e.pointerId); meter.classList.remove('drag'); });

  function paintLux() {
    const w = meterWrap.clientWidth || 280, h = 40;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (meter.width !== w * dpr || meter.height !== h * dpr) { meter.width = w * dpr; meter.height = h * dpr; }
    meter.style.height = h + 'px';
    const g = meter.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const pad = 8, span = w - pad * 2, y = 15;
    const val = (P.moonlight ?? 0.35) * illumination(P.phase ?? 0.68);

    const grad = g.createLinearGradient(pad, 0, pad + span, 0);
    grad.addColorStop(0, 'rgba(255,255,255,.05)');
    grad.addColorStop(1, hex(P.tint, 0.55));
    g.fillStyle = grad;
    g.beginPath(); g.roundRect(pad, y - 3, span, 6, 3); g.fill();
    g.fillStyle = 'rgba(0,0,0,.55)';
    g.beginPath(); g.roundRect(pad + lpos(val) * span, y - 3, span - lpos(val) * span, 6, 3); g.fill();

    g.font = '8px ui-sans-serif, system-ui';
    STOPS.forEach(([v, lbl]) => {
      const x = pad + lpos(v) * span;
      g.strokeStyle = 'rgba(255,255,255,.16)';
      g.beginPath(); g.moveTo(x, y + 5); g.lineTo(x, y + 9); g.stroke();
      if (!lbl) return;
      g.fillStyle = 'rgba(255,255,255,.30)';
      g.textAlign = v <= 0.0005 ? 'left' : 'center';
      g.fillText(lbl, x, h - 2);
    });
    const mx = pad + lpos(val) * span;
    g.fillStyle = '#fff';
    g.beginPath(); g.arc(mx, y, 4, 0, Math.PI * 2); g.fill();
    g.strokeStyle = 'rgba(0,0,0,.6)'; g.lineWidth = 1;
    g.beginPath(); g.arc(mx, y, 4, 0, Math.PI * 2); g.stroke();
    g.font = '8.5px ui-sans-serif, system-ui';
    g.fillStyle = 'rgba(255,255,255,.5)';
    g.textAlign = 'left';
    g.fillText('0.0005 lx', pad, 8);
    g.textAlign = 'right';
    g.fillText('2 lx', w - pad, 8);
  }

  /* angular size, on a ruler */
  const sizeWrap = el('div', 'mp-size');
  sizeWrap.innerHTML = '<div class="hd"><span class="k">angular size</span></div>';
  const sizeCv = el('canvas');
  sizeWrap.appendChild(sizeCv);
  const sizeCap = el('div', 'mp-k mp-note', '');
  const sizeStep = stepper({
    value: P.angular ?? 1.6, min: 0.2, max: 6, dec: 2, step: 0.05, unit: '°',
    onInput: v => { setProp(node, 'angular', v); paintAll(); },
  });
  sizeWrap.querySelector('.hd').appendChild(sizeStep);
  lb.append(sizeWrap, sizeCap);
  /* the ruler under the disc is the control — drag it and the moon grows */
  const sizeFrom = e => {
    const r = sizeCv.getBoundingClientRect();
    const pad = 8, span = Math.max(1, r.width - pad * 2);
    const v = Math.max(0.2, Math.min(6, (e.clientX - r.left - pad) / span * 6));
    setProp(node, 'angular', +v.toFixed(2));
    paintAll();
  };
  on(sizeCv, 'pointerdown', e => { sizeCv.setPointerCapture(e.pointerId); sizeCv.classList.add('drag'); sizeFrom(e); });
  on(sizeCv, 'pointermove', e => { if (sizeCv.hasPointerCapture?.(e.pointerId)) sizeFrom(e); });
  on(sizeCv, 'pointerup', e => { sizeCv.releasePointerCapture?.(e.pointerId); sizeCv.classList.remove('drag'); });
  lc.querySelector('.mp-x').onclick = () => { lc.classList.toggle('tall'); paintSize(); };

  function paintSize() {
    const w = sizeWrap.clientWidth || 280, h = lc.classList.contains('tall') ? 116 : 76;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (sizeCv.width !== w * dpr || sizeCv.height !== h * dpr) { sizeCv.width = w * dpr; sizeCv.height = h * dpr; }
    sizeCv.style.height = h + 'px';
    const g = sizeCv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const real = 0.52, mine = Math.max(0.2, P.angular ?? 1.6);
    const cx = w / 2, cy = (h - 14) / 2;
    const scale = (h - 30) / 2 / Math.max(real, mine);

    /* a degree ruler along the foot, so the numbers have somewhere to stand */
    const pad = 8, span = w - pad * 2, maxDeg = 6;
    g.font = '8px ui-sans-serif, system-ui';
    for (let d = 0; d <= maxDeg; d += 0.5) {
      const x = pad + d / maxDeg * span;
      const major = d % 2 === 0;
      g.strokeStyle = `rgba(255,255,255,${major ? 0.22 : 0.09})`;
      g.beginPath(); g.moveTo(x, h - 12); g.lineTo(x, h - 12 + (major ? 6 : 3)); g.stroke();
      if (major) {
        g.fillStyle = 'rgba(255,255,255,.28)';
        g.textAlign = d === 0 ? 'left' : d === maxDeg ? 'right' : 'center';
        g.fillText(`${d}°`, x, h - 1);
      }
    }
    g.strokeStyle = 'rgba(255,255,255,.10)';
    g.beginPath(); g.moveTo(pad, h - 12.5); g.lineTo(w - pad, h - 12.5); g.stroke();
    const mx = pad + Math.min(mine, maxDeg) / maxDeg * span;
    g.strokeStyle = hex(P.tint, 0.75);
    g.lineWidth = 1.4;
    g.beginPath(); g.moveTo(mx, h - 16); g.lineTo(mx, h - 8); g.stroke();
    g.lineWidth = 1;

    drawMoon(g, cx, cy, mine * scale, { phase: 0.5, tint: P.tint, earthshine: 0.2, brightness: 0.7 });
    g.lineWidth = 2.5; g.strokeStyle = 'rgba(0,0,0,.45)';
    g.beginPath(); g.arc(cx, cy, real * scale, 0, Math.PI * 2); g.stroke();
    g.lineWidth = 1; g.strokeStyle = 'rgba(255,255,255,.85)';
    g.setLineDash([3, 3]);
    g.beginPath(); g.arc(cx, cy, real * scale, 0, Math.PI * 2); g.stroke();
    g.setLineDash([]);
    g.fillStyle = 'rgba(255,255,255,.34)';
    g.font = '8px ui-sans-serif, system-ui';
    g.textAlign = 'center';
    g.fillText('REAL 0.52°', cx, cy + Math.max(real, mine) * scale + 10);
    sizeCap.textContent = `${(mine / real).toFixed(1)}× the real moon`;
  }

  /* tint, as named presets rather than four anonymous squares */
  const TINTS = [['#d8e2f2', 'COLD'], ['#f2ece0', 'NEUTRAL'], ['#f6d9b0', 'HARVEST'], ['#c3d0ff', 'BLUE HOUR']];
  const tintRow = el('div', 'mp-tags mp-tints');
  const tintBtns = TINTS.map(([c, label]) => {
    const b = el('button', 'mp-tag', `<i style="background:${c}"></i>${label}`);
    b.onclick = () => { setProp(node, 'tint', c); paintAll(); };
    tintRow.appendChild(b);
    return { b, c };
  });
  const markTints = () => tintBtns.forEach(({ b, c }) =>
    b.classList.toggle('on', (P.tint || '').toLowerCase() === c));
  const chip = colorChip(P.tint, v => { setProp(node, 'tint', v); paintAll(); });
  const tintHead = el('div', 'mp-subhead', '<span class="k">tint</span>');
  tintHead.appendChild(chip);
  lb.append(tintHead, tintRow);

  const bright = tape({
    label: 'Brightness', min: 0, max: 4, value: P.brightness ?? 1.1, dec: 2, unit: '×', step: 0.05,
    marks: [{ t: 0, l: 'OFF' }, { t: 0.25, l: 'REAL 1.0' }, { t: 1, l: '4×' }],
    onInput: v => { setProp(node, 'brightness', v); paintAll(); },
  });
  const earth = tape({
    label: 'Earthshine', min: 0, max: 1, value: P.earthshine ?? 0.16, dec: 2, step: 0.01,
    marks: [{ t: 0, l: 'NONE' }, { t: 0.16, l: 'REAL' }, { t: 1, l: 'FULL' }],
    onInput: v => { setProp(node, 'earthshine', v); paintAll(); },
  });
  lb.append(bright, earth);
  host.appendChild(lc);
  /* ── keeping every surface honest ─────────────────────────────────────────────────────── */
  function paintAll() {
    paintSky(); paintCap(); paintSize(); paintChart(); paintMap();
    paintArc(); paintDay(); paintLux(); paintTl(); markTints();
    bearing._set(((P.azimuth ?? 292) % 360 + 360) % 360);
    const p = ((P.phase ?? 0.68) % 1 + 1) % 1;
    const litPc = illumination(p) * 100;
    const per = P.period ?? 27.3;
    const elev = P.elevation ?? 46, azm = P.azimuth ?? 292;
    numI.textContent = Math.floor(litPc);
    numD.textContent = `.${Math.round(litPc * 10) % 10}`;
    pLit.innerHTML = `${Math.round(litPc)}<em>%</em>`;
    pAge.innerHTML = `${(p * per).toFixed(1)}<em>d</em>`;
    pAlt.innerHTML = `${elev >= 0 ? '+' : '−'}${Math.abs(elev).toFixed(0)}<em>°</em>`;
    pAz.innerHTML = `${compassOf(azm)}<em>${Math.round(azm)}°</em>`;
    pAz.parentElement.title = `Azimuth ${azm.toFixed(1)}°`;
    sUp.classList.toggle('down', elev < 0);
    sUp.querySelector('.i').innerHTML = elev < 0 ? ic('alert', { size: 12 }) : ic('check', { size: 12 });
    sUp.querySelector('.l').textContent = elev < 0 ? 'Below horizon' : 'Above horizon';
    sUp.querySelector('.n').innerHTML = `${elev >= 0 ? '' : '−'}${Math.abs(elev).toFixed(0)}<em>°</em>`;
    sLx.querySelector('.n').innerHTML =
      `${((P.moonlight ?? 0.35) * illumination(p)).toFixed(2)}<em>lx</em>`;
    chips.forEach(({ b, v }) => b.classList.toggle('on', Math.abs(((p - v + 1.5) % 1) - 0.5) < 0.0626));
    phaseSub.textContent = `${phaseName(p)} · day ${(p * per).toFixed(1)} of ${per.toFixed(1)}`;
    const days = ((0.5 - p + 1) % 1) * (P.period ?? 27.3);
    nextFull.textContent = phaseName(p) === 'Full moon'
      ? 'Full tonight'
      : `Full in ${days.toFixed(1)} days · ${((1 - p) % 1 * (P.period ?? 27.3)).toFixed(1)} to new`;
    /* the sky track, in the same voice as everything else */
    altI.textContent = `${elev >= 0 ? '+' : '−'}${Math.floor(Math.abs(elev))}`;
    altD.textContent = `.${Math.round(Math.abs(elev) * 10) % 10}`;
    bearV.textContent = `${compassOf(azm)} ${Math.round(azm)}° · ${elev < 0 ? 'under the horizon' : 'up'}`;
    const rise = hourFromElevation(0, 21), set = hourFromElevation(0, 9), transit = 0;
    const clock = h => `${String(Math.floor(h)).padStart(2, '0')}:${String(Math.round(h % 1 * 60)).padStart(2, '0')}`;
    track.innerHTML = [['rise', clock(rise)], ['transit', clock(transit)], ['set', clock(set)]]
      .map(([k, v]) => `<div><span class="k">${k}</span><b>${v}</b></div>`).join('');
    /* and the light it actually throws */
    const l = (P.moonlight ?? 0.35) * illumination(p);
    luxI.textContent = l.toFixed(2).split('.')[0];
    luxD.textContent = `.${l.toFixed(2).split('.')[1]}`;
    luxV.textContent = elev < 0 ? 'nothing while it is down'
      : l < 0.02 ? 'starlight only' : l < 0.08 ? 'shapes, no colour'
      : l < 0.25 ? 'you could walk' : l < 1 ? 'you could read' : 'bright enough to work by';
    /* keep the fine controls in step when the value came from somewhere else */
    fine._set(P.phase ?? 0.68);
    period._set(P.period ?? 27.3);
    sizeStep._set(P.angular ?? 1.6);
    luxStep._set(P.moonlight ?? 0.35);
    bright._set(P.brightness ?? 1.1);
    earth._set(P.earthshine ?? 0.16);
    chip._set && chip._set(P.tint);
  }

  function row(label, ctl, wide = false) {
    const r = el('div', `prow${wide ? ' wide' : ''}`);
    r.append(el('span', 'pl', label), ctl);
    return r;
  }

  syncers.push(paintAll);
  register && register(() => syncers.forEach(f => f()));

  /* the hero canvas is width-driven, so it has to know when the dock does */
  const ro = new ResizeObserver(() => {
    paintSky(); paintSize(); paintChart(); paintMap(); paintArc(); paintDay(); paintTl(); paintLux();
    [bearing, period, bright, earth].forEach(t => t._paint && t._paint());
  });
  ro.observe(host);
  host._dispose = () => ro.disconnect();

  requestAnimationFrame(paintAll);
  paintAll();
  return host;
}
