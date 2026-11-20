/* ════════════════════════════════════════════════════════════════════════════════════════════
   THE WIND PANEL
   Wind is the only entity in the scene you cannot see. It has no mesh, no billboard and no
   position — it is a direction and a number that other things obey. So this panel is the one
   place it becomes visible:

     · a live flow field, streaming in the direction the wind blows, curling with turbulence and
       breathing with the gusts. Drag out from the middle and you are setting the vector itself —
       the angle is the bearing, the distance is the speed
     · an anemometer trace: a rolling sixty seconds of gusts and lulls against the mean
     · the Beaufort scale, named band by band, with the marker where you are
     · and a list of what is actually listening to it
   ════════════════════════════════════════════════════════════════════════════════════════════ */
import { el } from '../kit.js';
import { tape, stepper, pillToggle } from './controls.js';
import { ic } from '../icons.js';
import { bus } from '../bus.js';
import { flat } from '../world.js';

/* ── wind arithmetic ───────────────────────────────────────────────────────────────────────── */
const BEAUFORT = [
  [0.5, 0, 'Calm'], [1.5, 1, 'Light air'], [3.3, 2, 'Light breeze'], [5.5, 3, 'Gentle breeze'],
  [7.9, 4, 'Moderate breeze'], [10.7, 5, 'Fresh breeze'], [13.8, 6, 'Strong breeze'],
  [17.1, 7, 'Near gale'], [20.7, 8, 'Gale'], [24.4, 9, 'Strong gale'], [28.4, 10, 'Storm'],
];
export function beaufort(v) {
  for (const [lim, n, name] of BEAUFORT) if (v < lim) return { n, name };
  return { n: 11, name: 'Violent storm' };
}
/* what the wind is visibly doing at this speed — the land description on the scale */
const LAND = [
  [0.5, 'smoke rises straight up'], [1.5, 'smoke drifts'], [3.3, 'leaves rustle'],
  [5.5, 'flags stir, leaves move'], [7.9, 'dust lifts, small branches move'],
  [10.7, 'small trees sway'], [13.8, 'large branches move'], [17.1, 'whole trees in motion'],
  [20.7, 'twigs break off'], [24.4, 'slates lift'], [28.4, 'trees uprooted'],
];
const landOf = v => (LAND.find(([lim]) => v < lim) || [0, 'structural damage'])[1];

const COMPASS = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
const compassOf = d => COMPASS[Math.round((((d % 360) + 360) % 360) / 22.5) % 16];

/* ── the panel ─────────────────────────────────────────────────────────────────────────────── */
export function windPanel(node, ctx) {
  const { compact = false, setProp, register } = ctx;
  const P = node.props;
  const host = el('div', 'mpanel windpanel');
  const syncers = [];
  const on = (elm, ev, fn) => elm.addEventListener(ev, fn);
  const SPD = () => P.speed ?? 4.2;
  const DIR = () => ((P.direction ?? 214) % 360 + 360) % 360;
  const GUST = () => P.gust ?? 0.3;
  const TURB = () => P.turbulence ?? 0.24;

  /* ── hero · the field, streaming ───────────────────────────────────────────────────────── */
  const hero = el('div', 'pcard mp-hero wf-hero');
  const cv = el('canvas', 'mp-sky');
  cv.title = 'Drag out from the middle: the angle is the bearing, the distance is the speed';
  const cap = el('div', 'mp-cap',
    `<div class="l"><b class="wf-name">—</b><span class="mp-illum wf-sub">—</span></div>
     <div class="r"><span class="wf-r">—</span></div>`);
  hero.append(cv, cap);
  host.appendChild(hero);

  /* the particles live in normalised space so a resize never disturbs them */
  const N = 190;
  const parts = Array.from({ length: N }, (_, i) => ({
    x: ((i * 977) % 1000) / 1000,
    y: ((i * 613) % 1000) / 1000,
    life: ((i * 37) % 100) / 100,
    seed: ((i * 131) % 1000) / 1000,
  }));
  let t0 = performance.now() / 1000, phase = 0, gustNow = 1;

  /* a cheap curl: two offset sines, enough to make turbulence look like turbulence */
  const curl = (x, y, t) =>
    Math.sin(x * 7.1 + t * 0.7) * Math.cos(y * 6.3 - t * 0.5) +
    0.5 * Math.sin(x * 13.7 - t * 1.1) * Math.cos(y * 11.3 + t * 0.9);

  const sky = cv.getContext('2d');
  function paintHero(dt = 0) {
    const w = cv.clientWidth || 300, h = compact ? 138 : 168;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (cv.width !== w * dpr || cv.height !== h * dpr) { cv.width = w * dpr; cv.height = h * dpr; }
    cv.style.height = h + 'px';
    const g = sky;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);

    const bg = g.createLinearGradient(0, 0, 0, h);
    bg.addColorStop(0, '#0a1014');
    bg.addColorStop(1, '#0d1a1b');
    g.fillStyle = bg;
    g.fillRect(0, 0, w, h);

    const spd = SPD(), dir = DIR();
    /* meteorological convention: the bearing is where it comes FROM, so it blows the other way */
    const a = (dir + 180 - 90) * Math.PI / 180;
    const ux = Math.cos(a), uy = Math.sin(a);
    const turb = TURB();
    const norm = Math.min(1, spd / 30);

    /* the ground plane, faintly, so the field has somewhere to be */
    g.strokeStyle = 'rgba(255,255,255,.035)';
    for (let x = 0; x < w; x += 26) { g.beginPath(); g.moveTo(x, 0); g.lineTo(x, h); g.stroke(); }
    for (let y = 0; y < h; y += 26) { g.beginPath(); g.moveTo(0, y); g.lineTo(w, y); g.stroke(); }

    const step = (0.03 + norm * 0.55) * gustNow * dt;
    parts.forEach(p => {
      const c = curl(p.x, p.y, phase) * turb * 0.9;
      const ca = Math.cos(c * 0.9), sa = Math.sin(c * 0.9);
      const dx = ux * ca - uy * sa, dy = ux * sa + uy * ca;
      const sp = step * (0.65 + p.seed * 0.7);
      p.x += dx * sp; p.y += dy * sp * (w / h);
      p.life -= dt * (0.25 + norm * 0.5);
      if (p.life <= 0 || p.x < -0.05 || p.x > 1.05 || p.y < -0.05 || p.y > 1.05) {
        p.x = ux > 0 ? -0.02 - p.seed * 0.1 : ux < 0 ? 1.02 + p.seed * 0.1 : p.seed;
        if (Math.abs(ux) < 0.35) { p.x = p.seed; p.y = uy > 0 ? -0.02 : 1.02; }
        else p.y = ((p.seed * 7919) % 1000) / 1000;
        p.life = 0.6 + p.seed * 0.9;
      }
      const len = (7 + norm * 34) * (0.5 + p.seed);
      const x = p.x * w, y = p.y * h;
      const alpha = Math.min(0.8, (0.3 + norm * 0.45) * Math.min(1, p.life * 2.2) * gustNow);
      const gd = g.createLinearGradient(x - dx * len, y - dy * len, x, y);
      gd.addColorStop(0, 'rgba(137,224,196,0)');
      gd.addColorStop(1, `rgba(137,224,196,${alpha})`);
      g.strokeStyle = gd;
      g.lineWidth = 0.9 + norm * 1.3;
      g.beginPath(); g.moveTo(x - dx * len, y - dy * len); g.lineTo(x, y); g.stroke();
    });
    g.lineWidth = 1;

    /* the vector you are actually setting */
    const cx = w / 2, cy = h / 2;
    const R = Math.min(w, h) * 0.34;
    const L = R * (0.25 + norm * 0.75);
    g.strokeStyle = 'rgba(255,255,255,.10)';
    g.setLineDash([2, 4]);
    g.beginPath(); g.arc(cx, cy, R, 0, Math.PI * 2); g.stroke();
    g.setLineDash([]);
    g.strokeStyle = 'rgba(255,255,255,.85)';
    g.lineWidth = 2;
    g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx + ux * L, cy + uy * L); g.stroke();
    g.lineWidth = 1;
    const hx = cx + ux * L, hy = cy + uy * L;
    g.fillStyle = '#fff';
    g.beginPath();
    g.moveTo(hx + ux * 7, hy + uy * 7);
    g.lineTo(hx - uy * 4.5 - ux * 2, hy + ux * 4.5 - uy * 2);
    g.lineTo(hx + uy * 4.5 - ux * 2, hy - ux * 4.5 - uy * 2);
    g.closePath(); g.fill();
    g.fillStyle = 'rgba(255,255,255,.55)';
    g.beginPath(); g.arc(cx, cy, 2.5, 0, Math.PI * 2); g.fill();

    /* where it is coming from, written on the rim */
    g.font = '8px ui-sans-serif, system-ui';
    g.fillStyle = 'rgba(255,255,255,.42)';
    g.textAlign = 'center';
    const fa = (dir - 90) * Math.PI / 180;
    g.fillText(compassOf(dir), cx + Math.cos(fa) * (R + 11), cy + Math.sin(fa) * (R + 11) + 3);
    ['N', 'E', 'S', 'W'].forEach((l, i) => {
      const ang = (i * 90 - 90) * Math.PI / 180;
      g.fillStyle = 'rgba(255,255,255,.18)';
      g.fillText(l, cx + Math.cos(ang) * (R + 11), cy + Math.sin(ang) * (R + 11) + 3);
    });
  }

  const nameEl = cap.querySelector('.wf-name');
  const subEl = cap.querySelector('.wf-sub');
  const rEl = cap.querySelector('.wf-r');
  function paintCap() {
    const b = beaufort(SPD());
    nameEl.textContent = b.name;
    subEl.textContent = `${SPD().toFixed(1)} m/s from ${compassOf(DIR())} ${Math.round(DIR())}° · ${landOf(SPD())}`;
    rEl.innerHTML = `force ${b.n}`;
  }

  /* the hero is the vector: angle is the bearing it comes from, radius is the speed */
  const vecFrom = e => {
    const r = cv.getBoundingClientRect();
    const dx = e.clientX - r.left - r.width / 2, dy = e.clientY - r.top - r.height / 2;
    const R = Math.min(r.width, r.height) * 0.34;
    const to = (Math.atan2(dy, dx) * 180 / Math.PI + 90 + 360) % 360;
    setProp(node, 'direction', Math.round((to + 180) % 360));
    const d = Math.hypot(dx, dy);
    setProp(node, 'speed', +Math.max(0, Math.min(30, (d / R - 0.25) / 0.75 * 30)).toFixed(1));
    paintAll();
  };
  on(cv, 'pointerdown', e => { cv.setPointerCapture(e.pointerId); cv.classList.add('grabbing'); vecFrom(e); });
  on(cv, 'pointermove', e => { if (cv.hasPointerCapture?.(e.pointerId)) vecFrom(e); });
  const endDrag = () => cv.classList.remove('grabbing');
  on(cv, 'pointerup', endDrag);
  on(cv, 'pointercancel', endDrag);

  /* ── the rail ──────────────────────────────────────────────────────────────────────────── */
  const rail = el('div', 'mp-rail');
  const pill = k => {
    const b = el('div', 'mp-pill', `<b class="v">—</b><span class="k">${k}</span>`);
    rail.appendChild(b);
    return b.querySelector('.v');
  };
  const pSpd = pill('Mean'), pGust = pill('Gust'), pFrom = pill('From'), pBf = pill('Force');
  host.appendChild(rail);

  /* ── the duo ───────────────────────────────────────────────────────────────────────────── */
  const duo = el('div', 'mp-duo');
  const statCard = (icon, label) => {
    const c = el('div', 'pcard mp-stat',
      `<span class="i">${icon}</span><span class="l">${label}</span><b class="n">—</b>`);
    duo.appendChild(c);
    return c;
  };
  const sGust = statCard(ic('wind', { size: 12 }), 'Gusting to');
  const sLull = statCard(ic('wind', { size: 12 }), 'Lulling to');
  host.appendChild(duo);

  /* ── the anemometer ────────────────────────────────────────────────────────────────────── */
  const mc = el('div', 'pcard mp-metric wf-trace');
  mc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Anemometer</span><span class="s">Last 60 seconds</span></div>
      <button class="mp-x" title="Taller trace">${ic('arrowout', { size: 12 })}</button>
    </div>
    <div class="mp-num"><span class="i">—</span><span class="d"></span><span class="u">m/s</span></div>
    <div class="mp-k mp-target">Mean <span class="v">—</span></div>`;
  const chartWrap = el('div', 'mp-chartwrap');
  const chart = el('canvas');
  chartWrap.appendChild(chart);
  mc.appendChild(chartWrap);
  const specs = el('div', 'mp-spec wf-specs');
  mc.appendChild(specs);
  const numI = mc.querySelector('.mp-num .i'), numD = mc.querySelector('.mp-num .d');
  const meanV = mc.querySelector('.mp-target .v');
  mc.querySelector('.mp-x').onclick = () => { mc.classList.toggle('tall'); paintTrace(); };
  host.appendChild(mc);

  /* sixty seconds at four samples a second */
  const gustAt = ph => {
    const g1 = Math.sin(ph * 0.9) * 0.6 + Math.sin(ph * 2.3 + 1.7) * 0.3 + Math.sin(ph * 5.1) * 0.1;
    const noise = (Math.sin(ph * 17.3) + Math.sin(ph * 29.7 + 2.1)) * 0.5;
    return 1 + GUST() * (g1 * 0.55) + TURB() * noise * 0.18;
  };
  /* the trace starts with a minute of history already on it, so it never looks switched off */
  const TRACE = new Array(240);
  const prefill = () => {
    const rate = 0.4 + TURB() * 2.2;
    for (let i = 0; i < TRACE.length; i++) {
      TRACE[i] = Math.max(0, SPD() * gustAt(phase - (TRACE.length - i) * 0.25 * rate));
    }
  };
  prefill();
  let acc = 0, inst = SPD();
  function sample(dt) {
    phase += dt * (0.4 + TURB() * 2.2);
    gustNow = gustAt(phase);
    inst = Math.max(0, SPD() * gustNow);
    acc += dt;
    while (acc >= 0.25) { acc -= 0.25; TRACE.push(inst); TRACE.shift(); }
  }

  function paintTrace() {
    const w = chartWrap.clientWidth || 280, h = mc.classList.contains('tall') ? 170 : 112;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (chart.width !== w * dpr || chart.height !== h * dpr) { chart.width = w * dpr; chart.height = h * dpr; }
    chart.style.height = h + 'px';
    const g = chart.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const R = 30, L = 2, T = 8, B = 15;
    const top = Math.max(2, Math.max(...TRACE, SPD()) * 1.18);
    const px = t => L + t * (w - L - R);
    const py = v => T + (1 - v / top) * (h - T - B);
    g.font = '9px ui-sans-serif, system-ui';

    [0.25, 0.5, 0.75, 1].forEach(f => {
      g.strokeStyle = 'rgba(255,255,255,.06)';
      g.setLineDash([2, 5]);
      g.beginPath(); g.moveTo(px(0), py(top * f)); g.lineTo(px(1), py(top * f)); g.stroke();
      g.setLineDash([]);
      g.fillStyle = 'rgba(255,255,255,.26)';
      g.textAlign = 'left';
      g.fillText((top * f).toFixed(top > 12 ? 0 : 1), w - R + 6, py(top * f) + 3);
    });

    /* the mean, and the band the gusts live in */
    const mean = SPD();
    const hi = Math.max(...TRACE), lo = Math.min(...TRACE);
    g.fillStyle = 'rgba(137,224,196,.07)';
    g.fillRect(px(0), py(hi), px(1) - px(0), Math.max(1, py(lo) - py(hi)));
    g.strokeStyle = 'rgba(255,255,255,.28)';
    g.setLineDash([4, 4]);
    g.beginPath(); g.moveTo(px(0), py(mean)); g.lineTo(px(1), py(mean)); g.stroke();
    g.setLineDash([]);

    /* the trace itself */
    g.beginPath();
    TRACE.forEach((v, i) => {
      const x = px(i / (TRACE.length - 1)), y = py(v);
      i ? g.lineTo(x, y) : g.moveTo(x, y);
    });
    g.strokeStyle = 'rgba(137,224,196,.9)';
    g.lineWidth = 1.4;
    g.stroke();
    g.lineWidth = 1;
    g.fillStyle = '#fff';
    g.beginPath(); g.arc(px(1), py(TRACE[TRACE.length - 1]), 2.6, 0, Math.PI * 2); g.fill();

    g.fillStyle = 'rgba(255,255,255,.26)';
    [[0, '−60 s'], [0.5, '−30 s'], [1, 'now']].forEach(([t, l], i) => {
      g.textAlign = i === 0 ? 'left' : i === 2 ? 'right' : 'center';
      g.fillText(l, px(t), h - 3);
    });
  }

  /* ── the scale ─────────────────────────────────────────────────────────────────────────── */
  const bc = el('div', 'pcard mp-light wf-scale');
  bc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Beaufort</span><span class="s">Force · bearing</span></div>
    </div>`;
  const bb = el('div', 'pbody');
  bc.appendChild(bb);
  bc.querySelector('.mp-chead .l').onclick = () => bc.classList.toggle('shut');

  const scaleWrap = el('div', 'mp-meter wf-meter');
  scaleWrap.innerHTML = '<div class="hd"><span class="k">wind speed</span></div>';
  const scaleCv = el('canvas');
  scaleWrap.appendChild(scaleCv);
  const spdStep = stepper({
    value: SPD(), min: 0, max: 30, dec: 1, step: 0.1, unit: 'm/s',
    onInput: v => { setProp(node, 'speed', v); paintAll(); },
  });
  scaleWrap.querySelector('.hd').appendChild(spdStep);
  bb.appendChild(scaleWrap);
  const spdFrom = e => {
    const r = scaleCv.getBoundingClientRect();
    const pad = 8, span = Math.max(1, r.width - pad * 2);
    setProp(node, 'speed', +Math.max(0, Math.min(30, (e.clientX - r.left - pad) / span * 30)).toFixed(1));
    paintAll();
  };
  on(scaleCv, 'pointerdown', e => { scaleCv.setPointerCapture(e.pointerId); scaleCv.classList.add('drag'); spdFrom(e); });
  on(scaleCv, 'pointermove', e => { if (scaleCv.hasPointerCapture?.(e.pointerId)) spdFrom(e); });
  on(scaleCv, 'pointerup', e => { scaleCv.releasePointerCapture?.(e.pointerId); scaleCv.classList.remove('drag'); });

  const dirTape = tape({
    label: 'Coming from', min: 0, max: 360, value: DIR(), dec: 0, unit: '°', step: 1,
    marks: [{ t: 0, l: 'N' }, { t: 0.25, l: 'E' }, { t: 0.5, l: 'S' }, { t: 0.75, l: 'W' }, { t: 1, l: 'N' }],
    onInput: v => { setProp(node, 'direction', Math.round(v) % 360); paintAll(); },
  });
  bb.appendChild(dirTape);
  const landNote = el('div', 'mp-note', '');
  bb.appendChild(landNote);
  host.appendChild(bc);

  function paintScale() {
    const w = scaleWrap.clientWidth || 280, h = 44;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (scaleCv.width !== w * dpr || scaleCv.height !== h * dpr) { scaleCv.width = w * dpr; scaleCv.height = h * dpr; }
    scaleCv.style.height = h + 'px';
    const g = scaleCv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const pad = 8, span = w - pad * 2, y = 10, bh = 14, max = 30;
    const px = v => pad + Math.max(0, Math.min(1, v / max)) * span;

    /* one block per force, calm through storm */
    let prev = 0;
    BEAUFORT.concat([[30, 11, '']]).forEach(([lim, n]) => {
      const x0 = px(prev), x1 = px(lim);
      const t = n / 11;
      g.fillStyle = `rgb(${Math.round(60 + t * 195)},${Math.round(200 - t * 120)},${Math.round(180 - t * 120)})`;
      g.globalAlpha = 0.85;
      g.fillRect(x0, y, Math.max(1, x1 - x0 - 1), bh);
      g.globalAlpha = 1;
      if (n % 2 === 0 && x1 - x0 > 9) {
        g.font = '7.5px ui-sans-serif, system-ui';
        g.fillStyle = 'rgba(0,0,0,.55)';
        g.textAlign = 'center';
        g.fillText(String(n), (x0 + x1) / 2, y + bh - 4);
      }
      prev = lim;
    });

    g.font = '8px ui-sans-serif, system-ui';
    [[0, '0'], [10, '10'], [20, '20'], [30, '30 m/s']].forEach(([v, l]) => {
      const x = px(v);
      g.strokeStyle = 'rgba(255,255,255,.18)';
      g.beginPath(); g.moveTo(x, y + bh + 1); g.lineTo(x, y + bh + 5); g.stroke();
      g.fillStyle = 'rgba(255,255,255,.30)';
      g.textAlign = v === 0 ? 'left' : v >= 30 ? 'right' : 'center';
      g.fillText(l, x, h - 3);
    });

    const mx = px(SPD());
    g.fillStyle = '#fff';
    g.beginPath(); g.moveTo(mx, y - 1); g.lineTo(mx + 4, y - 7); g.lineTo(mx - 4, y - 7); g.closePath(); g.fill();
    g.strokeStyle = 'rgba(255,255,255,.95)';
    g.lineWidth = 1.4;
    g.beginPath(); g.moveTo(mx, y); g.lineTo(mx, y + bh); g.stroke();
    g.lineWidth = 1;
  }

  /* ── steadiness ────────────────────────────────────────────────────────────────────────── */
  const gc = el('div', 'pcard mp-light wf-steady');
  gc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Steadiness</span><span class="s">Gust · turbulence</span></div>
    </div>`;
  const gb = el('div', 'pbody');
  gc.appendChild(gb);
  gc.querySelector('.mp-chead .l').onclick = () => gc.classList.toggle('shut');
  const gustT = tape({
    label: 'Gustiness', min: 0, max: 1, value: GUST(), dec: 2, step: 0.01,
    marks: [{ t: 0, l: 'STEADY' }, { t: 0.3, l: 'BREEZY' }, { t: 1, l: 'SQUALLY' }],
    onInput: v => { setProp(node, 'gust', v); paintAll(); },
  });
  const turbT = tape({
    label: 'Turbulence', min: 0, max: 1, value: TURB(), dec: 2, step: 0.01,
    marks: [{ t: 0, l: 'LAMINAR' }, { t: 0.24, l: 'OPEN AIR' }, { t: 1, l: 'ROTOR' }],
    onInput: v => { setProp(node, 'turbulence', v); paintAll(); },
  });
  const steadySpecs = el('div', 'mp-spec');
  gb.append(gustT, turbT, steadySpecs);
  host.appendChild(gc);

  /* ── who is listening ──────────────────────────────────────────────────────────────────── */
  const lc = el('div', 'pcard mp-light wf-drives');
  lc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Driving</span><span class="s">Everything that follows this field</span></div>
    </div>`;
  const lb = el('div', 'pbody');
  lc.appendChild(lb);
  lc.querySelector('.mp-chead .l').onclick = () => lc.classList.toggle('shut');
  const drivenRow = el('div', 'mp-tags');
  lb.appendChild(drivenRow);
  const drivenNote = el('div', 'mp-note',
    'Clouds and water can each be cut loose from the field — switch one off and it keeps its own drift.');
  lb.appendChild(drivenNote);
  host.appendChild(lc);

  const driven = flat.filter(n => 'windLinked' in (n.props || {}));
  const drivenPills = driven.map(n => {
    const p = pillToggle(n.name.toUpperCase(), n.props.windLinked !== false, v => {
      n.props.windLinked = v;
      bus.emit('propchange', { node: n, key: 'windLinked', src: 'wind-panel' });
      paintAll();
    });
    drivenRow.appendChild(p);
    return { p, n };
  });

  /* ── keeping every surface honest ──────────────────────────────────────────────────────── */
  /* the half of the panel that changes on its own */
  function paintLive() {
    const b = beaufort(SPD());
    const hi = Math.max(...TRACE), lo = Math.min(...TRACE);
    const q = 0.5 * 1.225 * SPD() ** 2;

    pSpd.innerHTML = `${SPD().toFixed(1)}<em>m/s</em>`;
    pGust.innerHTML = `${hi.toFixed(1)}<em>m/s</em>`;
    pFrom.innerHTML = `${compassOf(DIR())}<em>${Math.round(DIR())}°</em>`;
    pBf.innerHTML = `${b.n}<em>bf</em>`;

    sGust.querySelector('.n').innerHTML = `${hi.toFixed(1)}<em>m/s</em>`;
    sLull.querySelector('.n').innerHTML = `${lo.toFixed(1)}<em>m/s</em>`;
    sGust.classList.toggle('down', hi > 17);
    sGust.querySelector('.i').innerHTML = hi > 17 ? ic('alert', { size: 12 }) : ic('wind', { size: 12 });
    meanV.textContent = `${SPD().toFixed(1)} m/s · ${b.name.toLowerCase()}, force ${b.n}`;

    specs.innerHTML = [
      ['gust factor', `${(hi / Math.max(SPD(), 0.1)).toFixed(2)}×`],
      ['spread', `${(hi - lo).toFixed(1)} m/s`],
      ['pressure', `${q.toFixed(q < 10 ? 1 : 0)} Pa`],
      ['also', `${(SPD() * 3.6).toFixed(0)} km/h · ${(SPD() * 1.944).toFixed(1)} kn`],
    ].map(([k, v]) => `<div><span class="k">${k}</span><b>${v}</b></div>`).join('');
  }

  function paintAll() {
    paintCap(); paintTrace(); paintScale(); paintLive();
    const b = beaufort(SPD());
    numI.textContent = Math.floor(inst);
    numD.textContent = `.${Math.round(inst * 10) % 10}`;

    steadySpecs.innerHTML = [
      ['turbulence intensity', `${(TURB() * 100).toFixed(0)}%`],
      ['gust to mean', `${(1 + GUST() * 0.6).toFixed(2)}×`],
    ].map(([k, v]) => `<div><span class="k">${k}</span><b>${v}</b></div>`).join('');

    landNote.textContent = `Force ${b.n} · ${b.name.toLowerCase()} — ${landOf(SPD())}.`;

    spdStep._set(SPD());
    dirTape._set(DIR());
    gustT._set(GUST());
    turbT._set(TURB());
    drivenPills.forEach(({ p, n }) => p._set(n.props.windLinked !== false));
  }

  /* ── the only panel that has to keep moving ────────────────────────────────────────────── */
  let raf = 0, alive = true, visible = true, live = 0;
  const io = new IntersectionObserver(es => { visible = es[0].isIntersecting; }, { threshold: 0 });
  io.observe(mc); // Observe the retained Anemometer, not the removed standalone hero.
  const frame = () => {
    if (!alive) return;
    const now = performance.now() / 1000;
    const dt = Math.min(0.12, now - t0);              /* a slow frame should still move the air */
    t0 = now;
    if (visible) {
      sample(dt);
      paintHero(dt);
      paintTrace();
      numI.textContent = Math.floor(inst);
      numD.textContent = `.${Math.round(inst * 10) % 10}`;
      live += dt;
      if (live > 0.4) { live = 0; paintLive(); }        /* gust, lull and the derived numbers */
    }
    raf = requestAnimationFrame(frame);
  };
  raf = requestAnimationFrame(frame);

  syncers.push(paintAll);
  register && register(() => syncers.forEach(f => f()));

  const ro = new ResizeObserver(() => {
    paintHero(0); paintTrace(); paintScale();
    [dirTape, gustT, turbT].forEach(t => t._paint && t._paint());
  });
  ro.observe(host);
  host._dispose = () => { alive = false; cancelAnimationFrame(raf); ro.disconnect(); io.disconnect(); };

  paintAll();
  return host;
}
