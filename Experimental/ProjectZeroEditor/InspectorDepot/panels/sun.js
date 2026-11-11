/* ════════════════════════════════════════════════════════════════════════════════════════════
   THE SUN PANEL
   The moon panel asks "what does it look like tonight". The sun asks a different set of
   questions — where is it in the sky, how much light is on the ground, what colour is that
   light, and where does the shadow fall — so it gets a different set of instruments:

     · a sky strip that is painted from the sun's own altitude, and drags to scrub the day
     · a stereographic SUN PATH: the classic architect's chart, altitude rings, compass rim,
       the day's arc with its hour marks, twilight shaded, and the sun draggable around it
     · an illuminance curve with the golden hours shaded on it
     · a blackbody tape — the Kelvin scale drawn in the colour it actually is
     · a shadow diagram: a pole, its shadow, the length in heights and the bearing it falls on
   Every control is a tape or a stepper. There is not a slider in the file.
   ════════════════════════════════════════════════════════════════════════════════════════════ */
import { el, colorChip } from '../kit.js';
import { tape, stepper, pillToggle } from './controls.js';
import { ic } from '../icons.js';
import { bus } from '../bus.js';

/* ── solar arithmetic, matching the world clock in main.js ─────────────────────────────────── */
export const SUN_ALT = h => 62 * Math.sin((h - 6) / 12 * Math.PI);
export const SUN_AZ = h => ((90 + (h - 6) * 15) % 360 + 360) % 360;
const HOUR_FROM_AZ = az => 6 + (az - 90) / 15;
const COMPASS = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
const compassOf = az => COMPASS[Math.round((((az % 360) + 360) % 360) / 22.5) % 16];
const clock = h => `${String(Math.floor(((h % 24) + 24) % 24)).padStart(2, '0')}:${String(Math.floor((((h % 1) + 1) % 1) * 60)).padStart(2, '0')}`;

/* what the light is called at this altitude */
export function daylightName(alt) {
  if (alt >= 50) return 'High sun';
  if (alt >= 25) return 'Full day';
  if (alt >= 6) return 'Low sun';
  if (alt >= -0.5) return 'Golden hour';
  if (alt >= -6) return 'Civil twilight';
  if (alt >= -12) return 'Blue hour';
  if (alt >= -18) return 'Astronomical twilight';
  return 'Night';
}

/* colour temperature → something you can put in a fillStyle (Tanner Helland's fit) */
export function kelvinRGB(K) {
  const t = Math.max(1000, Math.min(40000, K)) / 100;
  let r, g, b;
  if (t <= 66) { r = 255; g = 99.47 * Math.log(t) - 161.12; }
  else { r = 329.7 * (t - 60) ** -0.1332; g = 288.12 * (t - 60) ** -0.0755; }
  if (t >= 66) b = 255;
  else if (t <= 19) b = 0;
  else b = 138.52 * Math.log(t - 10) - 305.04;
  const c = x => Math.max(0, Math.min(255, Math.round(x)));
  return [c(r), c(g), c(b)];
}
const kelvinCSS = (K, a = 1) => { const [r, g, b] = kelvinRGB(K); return `rgba(${r},${g},${b},${a})`; };

const hex = (c, a) => {
  const n = parseInt((c || '#fff0d4').slice(1), 16);
  return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${a})`;
};

/* the sky, as a pair of colours picked off an altitude ramp */
const SKY = [
  [-20, '#04060d', '#080b16'],
  [-10, '#06080f', '#131b33'],
  [-5, '#0a0f21', '#2b3157'],
  [-1, '#122045', '#7d5a6a'],
  [3, '#1d3566', '#e08a4a'],
  [9, '#245089', '#e5b273'],
  [20, '#2a63a8', '#bcd0e2'],
  [45, '#2f6dc0', '#a9c9ea'],
  [75, '#2b6ecb', '#9dc4ee'],
];
const mix = (a, b, t) => {
  const pa = parseInt(a.slice(1), 16), pb = parseInt(b.slice(1), 16);
  const f = (sa, sb) => Math.round(sa + (sb - sa) * t);
  return `rgb(${f(pa >> 16 & 255, pb >> 16 & 255)},${f(pa >> 8 & 255, pb >> 8 & 255)},${f(pa & 255, pb & 255)})`;
};
function skyStops(alt) {
  let i = 0;
  while (i < SKY.length - 2 && alt > SKY[i + 1][0]) i++;
  const [a0, z0, h0] = SKY[i], [a1, z1, h1] = SKY[i + 1];
  const t = Math.max(0, Math.min(1, (alt - a0) / (a1 - a0 || 1)));
  return [mix(z0, z1, t), mix(h0, h1, t)];
}

/* ── the panel ─────────────────────────────────────────────────────────────────────────────── */
export function sunPanel(node, ctx) {
  const { compact = false, setProp, register } = ctx;
  const P = node.props;
  const host = el('div', 'mpanel spanel');
  const syncers = [];
  const on = (elm, ev, fn) => elm.addEventListener(ev, fn);
  const setHour = h => bus.emit('settod', ((h % 24) + 24) % 24);
  /* the atmosphere reddens the disc as it drops — the panel should not pretend otherwise */
  const apparentK = () => {
    const alt = P.elevation ?? 14;
    const t = Math.max(0, Math.min(1, (12 - alt) / 18));
    return (P.temperature ?? 5400) + (1700 - (P.temperature ?? 5400)) * (t ** 0.7);
  };
  const nowHour = () => ((HOUR_FROM_AZ(P.azimuth ?? 118) % 24) + 24) % 24;

  /* ── hero · the sky right now ──────────────────────────────────────────────────────────── */
  const hero = el('div', 'pcard mp-hero');
  const cv = el('canvas', 'mp-sky');
  cv.title = 'Drag to run the day';
  const cap = el('div', 'mp-cap',
    `<div class="l"><b class="s-name">—</b><span class="mp-illum s-sub">—</span></div>
     <div class="r"><span class="s-alt">—</span></div>`);
  hero.append(cv, cap);
  host.appendChild(hero);

  const sky = cv.getContext('2d');
  function paintSky() {
    const w = cv.clientWidth || 300, h = compact ? 118 : 142;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (cv.width !== w * dpr || cv.height !== h * dpr) { cv.width = w * dpr; cv.height = h * dpr; }
    cv.style.height = h + 'px';
    sky.setTransform(dpr, 0, 0, dpr, 0, 0);
    sky.clearRect(0, 0, w, h);

    const alt = P.elevation ?? 14, az = P.azimuth ?? 118;
    const horizon = h - 20;
    const [zen, hz] = skyStops(alt);
    const g = sky.createLinearGradient(0, 0, 0, horizon);
    g.addColorStop(0, zen);
    g.addColorStop(1, hz);
    sky.fillStyle = g;
    sky.fillRect(0, 0, w, horizon);

    /* stars only once the sky is dark enough to have any */
    if (alt < -4) {
      const k = Math.min(1, (-alt - 4) / 10);
      for (let i = 0; i < 40; i++) {
        const x = ((i * 977) % 1000) / 1000 * w;
        const y = ((i * 613) % 1000) / 1000 * horizon;
        sky.fillStyle = `rgba(220,232,255,${(0.1 + ((i * 37) % 10) / 10 * 0.3) * k})`;
        sky.fillRect(x, y, 1.1, 1.1);
      }
    }

    /* the ground, and the light lying on it */
    sky.fillStyle = '#080a0e';
    sky.fillRect(0, horizon, w, h - horizon);
    const lit = Math.max(0, Math.sin(Math.max(0, alt) * Math.PI / 180));
    const gr = sky.createLinearGradient(0, horizon, 0, h);
    gr.addColorStop(0, kelvinCSS(apparentK(), 0.18 * lit + 0.04));
    gr.addColorStop(1, 'rgba(0,0,0,0)');
    sky.fillStyle = gr;
    sky.fillRect(0, horizon, w, h - horizon);
    sky.strokeStyle = 'rgba(255,255,255,.10)';
    sky.beginPath(); sky.moveTo(0, horizon + .5); sky.lineTo(w, horizon + .5); sky.stroke();

    /* the disc, sitting where the clock puts it */
    const up = Math.max(-0.35, Math.min(1, alt / 62));
    const cx = w * (0.12 + ((az % 360) / 360) * 0.76);
    const cy = horizon - up * (horizon - 26);
    const r = Math.max(7, Math.min(26, 5 + (P.angular ?? 0.6) * 14));
    const col = kelvinCSS(apparentK());
    const bloom = sky.createRadialGradient(cx, cy, r * 0.5, cx, cy, r * 7);
    bloom.addColorStop(0, kelvinCSS(apparentK(), 0.55 * Math.max(0.18, lit)));
    bloom.addColorStop(0.35, kelvinCSS(apparentK(), 0.14 * Math.max(0.18, lit)));
    bloom.addColorStop(1, 'rgba(0,0,0,0)');
    sky.fillStyle = bloom;
    sky.beginPath(); sky.arc(cx, cy, r * 7, 0, Math.PI * 2); sky.fill();
    if (cy < horizon + r) {
      sky.fillStyle = col;
      sky.beginPath(); sky.arc(cx, cy, r, 0, Math.PI * 2); sky.fill();
      sky.fillStyle = `rgba(255,255,255,${0.15 + 0.45 * lit})`;
      sky.beginPath(); sky.arc(cx, cy, r * 0.62, 0, Math.PI * 2); sky.fill();
    }
  }

  const nameEl = cap.querySelector('.s-name');
  const subEl = cap.querySelector('.s-sub');
  const altEl = cap.querySelector('.s-alt');
  function paintCap() {
    const alt = P.elevation ?? 14, az = P.azimuth ?? 118;
    nameEl.textContent = daylightName(alt);
    subEl.textContent = `${clock(nowHour())} · ${(P.temperature ?? 5400).toFixed(0)}K`;
    altEl.innerHTML = alt < 0
      ? `<span class="down">${Math.abs(alt).toFixed(0)}° below</span> · ${compassOf(az)}`
      : `${alt.toFixed(0)}° above · ${compassOf(az)} ${Math.round(az)}°`;
  }

  let drag = null;
  on(cv, 'pointerdown', e => { cv.setPointerCapture(e.pointerId); drag = e.clientX; cv.classList.add('grabbing'); });
  on(cv, 'pointermove', e => {
    if (drag == null) return;
    setHour(nowHour() + (e.clientX - drag) / (cv.clientWidth || 300) * 12);
    drag = e.clientX;
  });
  const endDrag = () => { drag = null; cv.classList.remove('grabbing'); };
  on(cv, 'pointerup', endDrag);
  on(cv, 'pointercancel', endDrag);

  /* ── the rail ──────────────────────────────────────────────────────────────────────────── */
  const rail = el('div', 'mp-rail');
  const pill = k => {
    const b = el('div', 'mp-pill', `<b class="v">—</b><span class="k">${k}</span>`);
    rail.appendChild(b);
    return b.querySelector('.v');
  };
  const pTime = pill('Local'), pAlt = pill('Alt'), pAz = pill('Bearing'), pK = pill('Colour');
  host.appendChild(rail);

  /* ── the duo ───────────────────────────────────────────────────────────────────────────── */
  const duo = el('div', 'mp-duo');
  const statCard = (icon, label) => {
    const c = el('div', 'pcard mp-stat',
      `<span class="i">${icon}</span><span class="l">${label}</span><b class="n">—</b>`);
    duo.appendChild(c);
    return c;
  };
  const sUp = statCard(ic('check', { size: 12 }), 'Daylight');
  const sLx = statCard(ic('sun', { size: 12 }), 'On the ground');
  host.appendChild(duo);

  /* ── the sun path ──────────────────────────────────────────────────────────────────────── */
  const pc = el('div', 'pcard mp-atlas s-path');
  pc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Sun path</span><span class="s">Stereographic · north up</span></div>
      <button class="mp-x" title="Taller chart">${ic('arrowout', { size: 12 })}</button>
    </div>`;
  const pathWrap = el('div', 'mp-map s-map');
  const pathCv = el('canvas');
  pathCv.title = 'Drag the sun around its path';
  pathWrap.appendChild(pathCv);
  const readout = el('div', 'mp-read', '');
  pathWrap.appendChild(readout);
  pc.appendChild(pathWrap);

  const layers = { hours: true, twilight: true, rings: true };
  const tags = el('div', 'mp-tags');
  Object.keys(layers).forEach(k => {
    const t = el('button', 'mp-tag on', k.toUpperCase());
    t.onclick = () => { layers[k] = !layers[k]; t.classList.toggle('on', layers[k]); paintPath(); };
    tags.appendChild(t);
  });
  pc.appendChild(tags);

  const times = el('div', 'mp-3up');
  pc.appendChild(times);

  const dayWrap = el('div', 'mp-tl');
  const dayCv = el('canvas');
  dayWrap.appendChild(dayCv);
  const dayLbl = el('div', 'mp-tllbl', '<span>00:00</span><span>NOON</span><span>24:00</span>');
  pc.append(dayWrap, dayLbl);

  const animate = pillToggle('RUN THE DAY', !!P.animate, v => { setProp(node, 'animate', v); paintAll(); });
  const animRow = el('div', 'mp-tags');
  animRow.appendChild(animate);
  pc.appendChild(animRow);
  const rate = tape({
    label: 'Clock rate', min: 1, max: 600, value: P.rate ?? 120, dec: 0, unit: '×', step: 5,
    marks: [{ t: 0, l: 'REAL' }, { t: 120 / 599, l: '120×' }, { t: 1, l: '600×' }],
    onInput: v => { setProp(node, 'rate', v); paintAll(); },
  });
  pc.appendChild(rate);
  const rateNote = el('div', 'mp-note', '');
  pc.appendChild(rateNote);
  pc.querySelector('.mp-x').onclick = () => { pc.classList.toggle('tall'); paintPath(); };
  host.appendChild(pc);

  function paintPath() {
    const w = pathWrap.clientWidth || 280, h = pc.classList.contains('tall') ? 300 : 210;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (pathCv.width !== w * dpr || pathCv.height !== h * dpr) { pathCv.width = w * dpr; pathCv.height = h * dpr; }
    pathCv.style.height = h + 'px';
    const g = pathCv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const cx = w / 2, cy = h / 2, R = Math.min(w, h) / 2 - 20;
    const rOf = alt => R * (90 - alt) / 90;                 /* equidistant: zenith centre, horizon rim */
    const XY = (alt, az) => {
      const r = rOf(alt), a = (az - 90) * Math.PI / 180;
      return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
    };

    g.fillStyle = '#0b0d12';
    g.beginPath(); g.arc(cx, cy, R + 8, 0, Math.PI * 2); g.fill();

    /* the twilight collar, just under the horizon */
    if (layers.twilight) {
      const tw = g.createRadialGradient(cx, cy, R - 1, cx, cy, R + 9);
      tw.addColorStop(0, 'rgba(86,116,196,.34)');
      tw.addColorStop(1, 'rgba(86,116,196,0)');
      g.fillStyle = tw;
      g.beginPath();
      g.arc(cx, cy, R + 9, 0, Math.PI * 2);
      g.arc(cx, cy, R, 0, Math.PI * 2, true);      /* a collar, not a wash over the whole sky */
      g.fill();
    }

    /* altitude rings and the compass rim */
    g.font = '8px ui-sans-serif, system-ui';
    if (layers.rings) {
      [0, 30, 60].forEach(a => {
        g.strokeStyle = a === 0 ? 'rgba(255,255,255,.26)' : 'rgba(255,255,255,.09)';
        g.setLineDash(a === 0 ? [] : [2, 4]);
        g.beginPath(); g.arc(cx, cy, rOf(a), 0, Math.PI * 2); g.stroke();
        g.setLineDash([]);
        if (a) {
          g.fillStyle = 'rgba(255,255,255,.30)';
          g.textAlign = 'center';
          g.fillText(`${a}°`, cx, cy - rOf(a) + 9);
        }
      });
      g.fillStyle = 'rgba(255,255,255,.10)';
      g.beginPath(); g.arc(cx, cy, 2, 0, Math.PI * 2); g.fill();
    }
    for (let a = 0; a < 360; a += 15) {
      const major = a % 90 === 0;
      const [x1, y1] = XY(0, a);
      const r2 = R - (major ? 7 : 4);
      const ang = (a - 90) * Math.PI / 180;
      g.strokeStyle = `rgba(255,255,255,${major ? 0.3 : 0.12})`;
      g.beginPath(); g.moveTo(x1, y1); g.lineTo(cx + r2 * Math.cos(ang), cy + r2 * Math.sin(ang)); g.stroke();
    }
    ['N', 'E', 'S', 'W'].forEach((l, i) => {
      const ang = (i * 90 - 90) * Math.PI / 180;
      g.fillStyle = i === 0 ? 'rgba(255,255,255,.55)' : 'rgba(255,255,255,.35)';
      g.textAlign = 'center';
      g.fillText(l, cx + (R + 12) * Math.cos(ang), cy + (R + 12) * Math.sin(ang) + 3);
    });

    /* the day's arc */
    g.lineWidth = 1.6;
    let started = false;
    g.beginPath();
    for (let hh = 0; hh <= 24; hh += 0.1) {
      const a = SUN_ALT(hh);
      if (a < 0) { started = false; continue; }
      const [x, y] = XY(a, SUN_AZ(hh));
      started ? g.lineTo(x, y) : g.moveTo(x, y);
      started = true;
    }
    g.strokeStyle = kelvinCSS(P.temperature ?? 5400, 0.85);
    g.stroke();
    g.lineWidth = 1;

    /* the hours, ticked along it */
    if (layers.hours) {
      for (let hh = 6; hh <= 18; hh += 2) {
        const a = SUN_ALT(hh);
        if (a < -1) continue;
        const [x, y] = XY(Math.max(a, 0), SUN_AZ(hh));
        g.fillStyle = 'rgba(255,255,255,.5)';
        g.beginPath(); g.arc(x, y, 1.8, 0, Math.PI * 2); g.fill();
        g.fillStyle = 'rgba(255,255,255,.34)';
        g.textAlign = 'center';
        g.fillText(String(hh).padStart(2, '0'), x, y - 6);
      }
    }

    /* and the sun itself */
    const alt = P.elevation ?? 14, az = P.azimuth ?? 118;
    const [sx, sy] = XY(Math.max(alt, -6), az);
    const col = kelvinCSS(apparentK());
    const glow = g.createRadialGradient(sx, sy, 0, sx, sy, 16);
    glow.addColorStop(0, kelvinCSS(apparentK(), alt < 0 ? 0.25 : 0.55));
    glow.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = glow;
    g.beginPath(); g.arc(sx, sy, 16, 0, Math.PI * 2); g.fill();
    g.fillStyle = alt < 0 ? 'rgba(140,150,170,.7)' : col;
    g.beginPath(); g.arc(sx, sy, 5, 0, Math.PI * 2); g.fill();
    g.strokeStyle = 'rgba(0,0,0,.5)';
    g.beginPath(); g.arc(sx, sy, 5, 0, Math.PI * 2); g.stroke();

    readout.innerHTML =
      `<span class="k">alt</span><b>${alt >= 0 ? '+' : '−'}${Math.abs(alt).toFixed(1)}°</b>` +
      `<span class="k">az</span><b>${Math.round(az)}°</b>` +
      `<span class="k">t</span><b>${clock(nowHour())}</b>`;
  }

  /* drag anywhere on the dial: the bearing you point at is the hour you get */
  const pathFrom = e => {
    const r = pathCv.getBoundingClientRect();
    const x = e.clientX - r.left - r.width / 2, y = e.clientY - r.top - r.height / 2;
    const az = ((Math.atan2(y, x) * 180 / Math.PI) + 90 + 360) % 360;
    setHour(HOUR_FROM_AZ(az));
  };
  on(pathCv, 'pointerdown', e => { pathCv.setPointerCapture(e.pointerId); pathCv.classList.add('drag'); pathFrom(e); });
  on(pathCv, 'pointermove', e => { if (pathCv.hasPointerCapture?.(e.pointerId)) pathFrom(e); });
  on(pathCv, 'pointerup', e => { pathCv.releasePointerCapture?.(e.pointerId); pathCv.classList.remove('drag'); });

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
      const day = SUN_ALT(i) >= 0;
      g.strokeStyle = major ? 'rgba(255,255,255,.34)' : `rgba(255,255,255,${day ? 0.22 : 0.08})`;
      const len = major ? 9 : 5;
      g.beginPath(); g.moveTo(x, h / 2 - len / 2); g.lineTo(x, h / 2 + len / 2); g.stroke();
    }
    const hx = pad + nowHour() / 24 * span;
    g.fillStyle = 'rgba(18,18,18,.96)';
    g.strokeStyle = 'rgba(255,255,255,.16)';
    const bw = 30, bh = 20;
    g.beginPath();
    g.roundRect(Math.max(0, Math.min(w - bw, hx - bw / 2)), h / 2 - bh / 2, bw, bh, 10);
    g.fill(); g.stroke();
    const cxx = Math.max(bw / 2, Math.min(w - bw / 2, hx));
    const alt = P.elevation ?? 14;
    const gl = g.createRadialGradient(cxx, h / 2, 0, cxx, h / 2, 9);
    gl.addColorStop(0, kelvinCSS(apparentK(), alt < 0 ? 0.2 : 0.6));
    gl.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = gl;
    g.beginPath(); g.arc(cxx, h / 2, 9, 0, Math.PI * 2); g.fill();
    g.fillStyle = alt < 0 ? 'rgba(150,160,180,.75)' : kelvinCSS(apparentK());
    g.beginPath(); g.arc(cxx, h / 2, 4.6, 0, Math.PI * 2); g.fill();
  }
  const scrub = (elm, fn) => {
    const move = e => {
      const r = elm.getBoundingClientRect();
      fn(Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)));
    };
    on(elm, 'pointerdown', e => { elm.setPointerCapture(e.pointerId); elm.classList.add('grabbing'); move(e); });
    on(elm, 'pointermove', e => { if (elm.hasPointerCapture?.(e.pointerId)) move(e); });
    on(elm, 'pointerup', e => { elm.releasePointerCapture?.(e.pointerId); elm.classList.remove('grabbing'); });
  };
  scrub(dayWrap, t => setHour(t * 24));

  /* ── illuminance ───────────────────────────────────────────────────────────────────────── */
  const lightOf = h => Math.max(0, Math.sin(SUN_ALT(h) * Math.PI / 180)) * (P.intensity ?? 88);
  const mc = el('div', 'pcard mp-metric');
  mc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Illuminance</span><span class="s">Through the day</span></div>
      <button class="mp-x" title="Taller chart">${ic('arrowout', { size: 12 })}</button>
    </div>
    <div class="mp-num"><span class="i">—</span><span class="d"></span><span class="u">klx</span></div>
    <div class="mp-k mp-target">Peak <span class="v">—</span></div>`;
  const chartWrap = el('div', 'mp-chartwrap');
  const chart = el('canvas');
  chartWrap.appendChild(chart);
  mc.appendChild(chartWrap);
  const peakV = mc.querySelector('.mp-target .v');
  const numI = mc.querySelector('.mp-num .i'), numD = mc.querySelector('.mp-num .d');
  mc.querySelector('.mp-x').onclick = () => { mc.classList.toggle('tall'); paintChart(); };
  const power = tape({
    label: 'Illuminance at noon', min: 0, max: 160, value: P.intensity ?? 88, dec: 0, unit: 'klx', step: 1,
    marks: [{ t: 0, l: '0' }, { t: 88 / 160, l: 'CLEAR SKY 88' }, { t: 1, l: '160' }],
    onInput: v => { setProp(node, 'intensity', v); paintAll(); },
  });
  mc.appendChild(power);
  host.appendChild(mc);

  function paintChart() {
    const w = chartWrap.clientWidth || 280, h = mc.classList.contains('tall') ? 168 : 110;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (chart.width !== w * dpr || chart.height !== h * dpr) { chart.width = w * dpr; chart.height = h * dpr; }
    chart.style.height = h + 'px';
    const g = chart.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const R = 34, L = 2, T = 8, B = 16;
    const top = Math.max(20, P.intensity ?? 88);
    const px = t => L + t * (w - L - R);
    const py = v => T + (1 - v / top) * (h - T - B);
    g.font = '9px ui-sans-serif, system-ui';

    [0.25, 0.5, 0.75, 1].forEach(f => {
      g.strokeStyle = f === 1 ? 'rgba(255,255,255,.16)' : 'rgba(255,255,255,.07)';
      g.setLineDash(f === 1 ? [4, 4] : [2, 5]);
      g.beginPath(); g.moveTo(px(0), py(top * f)); g.lineTo(px(1), py(top * f)); g.stroke();
      g.setLineDash([]);
      g.fillStyle = 'rgba(255,255,255,.30)';
      g.textAlign = 'left';
      g.fillText(`${Math.round(top * f)}`, w - R + 7, py(top * f) + 3);
    });

    /* the golden hours, shaded where the sun is low but up */
    for (let i = 0; i < 96; i++) {
      const h0 = i / 4, a = SUN_ALT(h0);
      if (a > -4 && a < 6) {
        g.fillStyle = 'rgba(245,158,11,.10)';
        g.fillRect(px(h0 / 24), T, (w - L - R) / 96, h - T - B);
      }
    }

    /* the curve, filled under */
    g.beginPath();
    g.moveTo(px(0), py(0));
    for (let i = 0; i <= 96; i++) { const h0 = i / 4; g.lineTo(px(h0 / 24), py(lightOf(h0))); }
    g.lineTo(px(1), py(0));
    g.closePath();
    const fill = g.createLinearGradient(0, T, 0, py(0));
    fill.addColorStop(0, kelvinCSS(P.temperature ?? 5400, 0.22));
    fill.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = fill;
    g.fill();
    g.strokeStyle = kelvinCSS(P.temperature ?? 5400, 0.85);
    g.lineWidth = 1.6;
    g.beginPath();
    for (let i = 0; i <= 96; i++) { const h0 = i / 4; i ? g.lineTo(px(h0 / 24), py(lightOf(h0))) : g.moveTo(px(h0 / 24), py(lightOf(h0))); }
    g.stroke();
    g.lineWidth = 1;

    g.fillStyle = 'rgba(255,255,255,.26)';
    [[0, '00'], [0.25, '06'], [0.5, '12'], [0.75, '18'], [1, '24']].forEach(([t, l], i) => {
      g.strokeStyle = 'rgba(255,255,255,.09)';
      g.beginPath(); g.moveTo(px(t), py(0)); g.lineTo(px(t), py(0) + 4); g.stroke();
      g.textAlign = i === 0 ? 'left' : i === 4 ? 'right' : 'center';
      g.fillText(l, px(t), h - 3);
    });

    const t = nowHour() / 24, v = lightOf(nowHour());
    const bw = Math.max(8, (w - L - R) * 0.04);
    g.fillStyle = 'rgba(255,255,255,.05)';
    g.fillRect(px(t) - bw / 2, T, bw, py(0) - T);
    g.fillStyle = '#fff';
    g.beginPath(); g.arc(px(t), py(v), 3, 0, Math.PI * 2); g.fill();
  }

  /* ── colour ────────────────────────────────────────────────────────────────────────────── */
  const cc = el('div', 'pcard mp-light');
  cc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Colour</span><span class="s">Blackbody · disc</span></div>
    </div>`;
  const cb = el('div', 'pbody');
  cc.appendChild(cb);
  cc.querySelector('.mp-chead .l').onclick = () => cc.classList.toggle('shut');

  const rampWrap = el('div', 'mp-meter s-ramp');
  rampWrap.innerHTML = '<div class="hd"><span class="k">temperature</span></div>';
  const ramp = el('canvas');
  rampWrap.appendChild(ramp);
  const kStep = stepper({
    value: P.temperature ?? 5400, min: 1600, max: 12000, dec: 0, step: 50, unit: 'K',
    onInput: v => { setProp(node, 'temperature', v); paintAll(); },
  });
  rampWrap.querySelector('.hd').appendChild(kStep);
  cb.appendChild(rampWrap);

  const KPRESETS = [[1900, 'CANDLE'], [2800, 'TUNGSTEN'], [3400, 'GOLDEN'], [5400, 'DAYLIGHT'], [6500, 'OVERCAST'], [8000, 'SHADE']];
  const kRow = el('div', 'mp-tags');
  const kBtns = KPRESETS.map(([K, label]) => {
    const b = el('button', 'mp-tag', `<i style="background:${kelvinCSS(K)}"></i>${label}`);
    b.onclick = () => { setProp(node, 'temperature', K); paintAll(); };
    kRow.appendChild(b);
    return { b, K };
  });
  cb.appendChild(kRow);

  const kFrom = e => {
    const r = ramp.getBoundingClientRect();
    const pad = 8, span = Math.max(1, r.width - pad * 2);
    const t = Math.max(0, Math.min(1, (e.clientX - r.left - pad) / span));
    setProp(node, 'temperature', Math.round((1600 + t * 10400) / 50) * 50);
    paintAll();
  };
  on(ramp, 'pointerdown', e => { ramp.setPointerCapture(e.pointerId); ramp.classList.add('drag'); kFrom(e); });
  on(ramp, 'pointermove', e => { if (ramp.hasPointerCapture?.(e.pointerId)) kFrom(e); });
  on(ramp, 'pointerup', e => { ramp.releasePointerCapture?.(e.pointerId); ramp.classList.remove('drag'); });

  function paintRamp() {
    const w = rampWrap.clientWidth || 280, h = 44;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (ramp.width !== w * dpr || ramp.height !== h * dpr) { ramp.width = w * dpr; ramp.height = h * dpr; }
    ramp.style.height = h + 'px';
    const g = ramp.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const pad = 8, span = w - pad * 2, y = 8, bh = 14;
    for (let x = 0; x < span; x++) {
      g.fillStyle = kelvinCSS(1600 + (x / span) * 10400);
      g.fillRect(pad + x, y, 1.02, bh);
    }
    g.strokeStyle = 'rgba(0,0,0,.35)';
    g.strokeRect(pad + .5, y + .5, span - 1, bh - 1);

    g.font = '8px ui-sans-serif, system-ui';
    [[1600, '1.6k'], [3400, '3.4k'], [5400, '5.4k'], [8000, '8k'], [12000, '12k']].forEach(([K, l]) => {
      const x = pad + (K - 1600) / 10400 * span;
      g.strokeStyle = 'rgba(255,255,255,.18)';
      g.beginPath(); g.moveTo(x, y + bh + 1); g.lineTo(x, y + bh + 5); g.stroke();
      g.fillStyle = 'rgba(255,255,255,.30)';
      g.textAlign = K <= 1600 ? 'left' : K >= 12000 ? 'right' : 'center';
      g.fillText(l, x, h - 3);
    });
    const mx = pad + ((P.temperature ?? 5400) - 1600) / 10400 * span;
    g.fillStyle = '#fff';
    g.beginPath(); g.moveTo(mx, y - 1); g.lineTo(mx + 4, y - 7); g.lineTo(mx - 4, y - 7); g.closePath(); g.fill();
    g.strokeStyle = 'rgba(255,255,255,.9)';
    g.lineWidth = 1.4;
    g.beginPath(); g.moveTo(mx, y); g.lineTo(mx, y + bh); g.stroke();
    g.lineWidth = 1;
    kBtns.forEach(({ b, K }) => b.classList.toggle('on', Math.abs((P.temperature ?? 5400) - K) < 60));
  }

  const tintHead = el('div', 'mp-subhead', '<span class="k">tint</span>');
  const chip = colorChip(P.tint || '#fff0d4', v => { setProp(node, 'tint', v); paintAll(); });
  tintHead.appendChild(chip);
  cb.appendChild(tintHead);

  /* the disc, against the half a degree the real sun takes up */
  const sizeWrap = el('div', 'mp-size');
  sizeWrap.innerHTML = '<div class="hd"><span class="k">angular size</span></div>';
  const sizeCv = el('canvas');
  sizeWrap.appendChild(sizeCv);
  const sizeStep = stepper({
    value: P.angular ?? 0.6, min: 0.1, max: 4, dec: 2, step: 0.05, unit: '°',
    onInput: v => { setProp(node, 'angular', v); paintAll(); },
  });
  sizeWrap.querySelector('.hd').appendChild(sizeStep);
  const sizeCap = el('div', 'mp-k mp-note', '');
  cb.append(sizeWrap, sizeCap);
  const sizeFrom = e => {
    const r = sizeCv.getBoundingClientRect();
    const pad = 8, span = Math.max(1, r.width - pad * 2);
    setProp(node, 'angular', +Math.max(0.1, Math.min(4, (e.clientX - r.left - pad) / span * 4)).toFixed(2));
    paintAll();
  };
  on(sizeCv, 'pointerdown', e => { sizeCv.setPointerCapture(e.pointerId); sizeCv.classList.add('drag'); sizeFrom(e); });
  on(sizeCv, 'pointermove', e => { if (sizeCv.hasPointerCapture?.(e.pointerId)) sizeFrom(e); });
  on(sizeCv, 'pointerup', e => { sizeCv.releasePointerCapture?.(e.pointerId); sizeCv.classList.remove('drag'); });
  host.appendChild(cc);

  function paintSize() {
    const w = sizeWrap.clientWidth || 280, h = 78;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (sizeCv.width !== w * dpr || sizeCv.height !== h * dpr) { sizeCv.width = w * dpr; sizeCv.height = h * dpr; }
    sizeCv.style.height = h + 'px';
    const g = sizeCv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const real = 0.53, mine = Math.max(0.1, P.angular ?? 0.6);
    const cx = w / 2, cy = (h - 14) / 2, scale = (h - 32) / 2 / Math.max(real, mine);
    const pad = 8, span = w - pad * 2, maxDeg = 4;

    g.font = '8px ui-sans-serif, system-ui';
    for (let d = 0; d <= maxDeg; d += 0.25) {
      const x = pad + d / maxDeg * span;
      const major = d % 1 === 0;
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
    g.strokeStyle = kelvinCSS(P.temperature ?? 5400, 0.9);
    g.lineWidth = 1.4;
    g.beginPath(); g.moveTo(mx, h - 16); g.lineTo(mx, h - 8); g.stroke();
    g.lineWidth = 1;

    const glow = g.createRadialGradient(cx, cy, 0, cx, cy, mine * scale * 2.4);
    glow.addColorStop(0, kelvinCSS(P.temperature ?? 5400, 0.42));
    glow.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = glow;
    g.beginPath(); g.arc(cx, cy, mine * scale * 2.4, 0, Math.PI * 2); g.fill();
    g.fillStyle = kelvinCSS(P.temperature ?? 5400);
    g.beginPath(); g.arc(cx, cy, mine * scale, 0, Math.PI * 2); g.fill();
    g.lineWidth = 2.5; g.strokeStyle = 'rgba(0,0,0,.45)';
    g.beginPath(); g.arc(cx, cy, real * scale, 0, Math.PI * 2); g.stroke();
    g.lineWidth = 1; g.strokeStyle = 'rgba(255,255,255,.85)';
    g.setLineDash([3, 3]);
    g.beginPath(); g.arc(cx, cy, real * scale, 0, Math.PI * 2); g.stroke();
    g.setLineDash([]);
    g.fillStyle = 'rgba(255,255,255,.34)';
    g.textAlign = 'center';
    g.fillText('REAL 0.53°', cx, cy + Math.max(real, mine) * scale + 10);
    sizeCap.textContent = `${(mine / real).toFixed(1)}× the real sun`;
  }

  /* ── shadows ───────────────────────────────────────────────────────────────────────────── */
  const sc = el('div', 'pcard mp-light s-shadow');
  sc.innerHTML = `
    <div class="mp-chead">
      <div class="l"><span class="t">Shadows</span><span class="s">Length · bearing · edge</span></div>
    </div>
    <div class="mp-num"><span class="i">—</span><span class="d"></span><span class="u">× height</span></div>
    <div class="mp-k mp-target">Falling <span class="v">—</span></div>`;
  const sbody = el('div', 'pbody');
  sc.appendChild(sbody);
  sc.querySelector('.mp-chead .l').onclick = () => sc.classList.toggle('shut');
  const shI = sc.querySelector('.mp-num .i'), shD = sc.querySelector('.mp-num .d');
  const shV = sc.querySelector('.mp-target .v');

  const shWrap = el('div', 'mp-map s-shadowmap');
  const shCv = el('canvas');
  shWrap.appendChild(shCv);
  sbody.appendChild(shWrap);

  const shPills = el('div', 'mp-tags');
  const castPill = pillToggle('CAST SHADOWS', P.shadows !== false, v => { setProp(node, 'shadows', v); paintAll(); });
  shPills.appendChild(castPill);
  sbody.appendChild(shPills);
  const soft = tape({
    label: 'Edge softness', min: 0, max: 10, value: P.softness ?? 2.4, dec: 1, step: 0.1,
    marks: [{ t: 0, l: 'HARD' }, { t: 0.24, l: 'SUN 2.4' }, { t: 1, l: 'OVERCAST' }],
    onInput: v => { setProp(node, 'softness', v); paintAll(); },
  });
  sbody.appendChild(soft);
  host.appendChild(sc);

  function paintShadow() {
    const w = shWrap.clientWidth || 280, h = 150;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (shCv.width !== w * dpr || shCv.height !== h * dpr) { shCv.width = w * dpr; shCv.height = h * dpr; }
    shCv.style.height = h + 'px';
    const g = shCv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const alt = P.elevation ?? 14, az = P.azimuth ?? 118;
    const cx = w / 2, cy = h / 2 + 4;
    const up = alt > 0.4;
    const len = up ? 1 / Math.tan(alt * Math.PI / 180) : Infinity;
    const rings = len <= 1.5 ? [0.5, 1, 1.5] : len <= 3.5 ? [1, 2, 3] : [2, 4, 6];
    const unit = Math.min(w * 0.42, h * 0.42) / rings[rings.length - 1];
    g.font = '8px ui-sans-serif, system-ui';

    /* the plan grid */
    g.strokeStyle = 'rgba(255,255,255,.045)';
    for (let x = cx % 20; x < w; x += 20) { g.beginPath(); g.moveTo(x, 0); g.lineTo(x, h); g.stroke(); }
    for (let y = cy % 20; y < h; y += 20) { g.beginPath(); g.moveTo(0, y); g.lineTo(w, y); g.stroke(); }

    /* how far a shadow reaches, in heights */
    rings.forEach(r => {
      g.strokeStyle = 'rgba(255,255,255,.10)';
      g.setLineDash([2, 4]);
      g.beginPath(); g.arc(cx, cy, r * unit, 0, Math.PI * 2); g.stroke();
      g.setLineDash([]);
      g.fillStyle = 'rgba(255,255,255,.24)';
      g.textAlign = 'left';
      g.fillText(`${r}×`, cx + r * unit + 3, cy - 2);
    });
    g.strokeStyle = 'rgba(255,255,255,.13)';
    g.beginPath(); g.moveTo(cx, cy - h); g.lineTo(cx, cy + h); g.moveTo(cx - w, cy); g.lineTo(cx + w, cy); g.stroke();
    g.fillStyle = 'rgba(255,255,255,.32)';
    g.textAlign = 'center';
    ['N', 'E', 'S', 'W'].forEach((l, i) => {
      const a = (i * 90 - 90) * Math.PI / 180;
      const rr = Math.min(w, h) / 2 - 6;
      g.fillText(l, cx + Math.cos(a) * rr, cy + Math.sin(a) * rr + 3);
    });

    /* where the light is coming from */
    const sa = (az - 90) * Math.PI / 180;
    const srr = Math.min(w, h) / 2 - 16;
    const sx = cx + Math.cos(sa) * srr, sy = cy + Math.sin(sa) * srr;
    const glow = g.createRadialGradient(sx, sy, 0, sx, sy, 13);
    glow.addColorStop(0, kelvinCSS(P.temperature ?? 5400, up ? 0.55 : 0.18));
    glow.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = glow;
    g.beginPath(); g.arc(sx, sy, 13, 0, Math.PI * 2); g.fill();
    g.fillStyle = up ? kelvinCSS(P.temperature ?? 5400) : 'rgba(150,160,180,.5)';
    g.beginPath(); g.arc(sx, sy, 3.4, 0, Math.PI * 2); g.fill();
    g.strokeStyle = 'rgba(255,255,255,.12)';
    g.setLineDash([2, 4]);
    g.beginPath(); g.moveTo(sx, sy); g.lineTo(cx, cy); g.stroke();
    g.setLineDash([]);

    /* and the shadow it throws */
    if (up && P.shadows !== false) {
      const dir = (az + 180) % 360;
      const a = (dir - 90) * Math.PI / 180;
      const L = Math.min(len, rings[rings.length - 1] * 1.35) * unit;
      const ex = cx + Math.cos(a) * L, ey = cy + Math.sin(a) * L;
      const soft = P.softness ?? 2.4;
      const nx = -Math.sin(a), ny = Math.cos(a);
      const w0 = 4, w1 = 4 + soft * 2.2;
      const grad = g.createLinearGradient(cx, cy, ex, ey);
      grad.addColorStop(0, 'rgba(0,0,0,.85)');
      grad.addColorStop(1, `rgba(0,0,0,${Math.max(0.05, 0.5 - soft * 0.035)})`);
      g.fillStyle = grad;
      g.beginPath();
      g.moveTo(cx + nx * w0 / 2, cy + ny * w0 / 2);
      g.lineTo(ex + nx * w1 / 2, ey + ny * w1 / 2);
      g.lineTo(ex - nx * w1 / 2, ey - ny * w1 / 2);
      g.lineTo(cx - nx * w0 / 2, cy - ny * w0 / 2);
      g.closePath();
      g.fill();
      /* the penumbra, wider as the edge softens */
      const pen = g.createRadialGradient(ex, ey, 0, ex, ey, 4 + soft * 2.4);
      pen.addColorStop(0, `rgba(0,0,0,${Math.max(0.05, 0.45 - soft * 0.03)})`);
      pen.addColorStop(1, 'rgba(0,0,0,0)');
      g.fillStyle = pen;
      g.beginPath(); g.arc(ex, ey, 4 + soft * 2.4, 0, Math.PI * 2); g.fill();
      g.fillStyle = 'rgba(255,255,255,.45)';
      g.textAlign = Math.cos(a) < 0 ? 'right' : 'left';
      g.fillText(`${len.toFixed(1)}× height`, ex + (Math.cos(a) < 0 ? -7 : 7), ey + 3);
    }

    /* the thing casting it */
    g.fillStyle = up ? kelvinCSS(P.temperature ?? 5400, 0.95) : 'rgba(170,180,200,.5)';
    g.beginPath(); g.arc(cx, cy, 4.5, 0, Math.PI * 2); g.fill();
    g.strokeStyle = 'rgba(0,0,0,.6)';
    g.beginPath(); g.arc(cx, cy, 4.5, 0, Math.PI * 2); g.stroke();

    if (!up) {
      g.fillStyle = 'rgba(255,255,255,.32)';
      g.textAlign = 'center';
      g.font = '9px ui-sans-serif, system-ui';
      g.fillText('the sun is down — nothing to cast', cx, cy + 24);
    }
  }

  /* ── keeping every surface honest ──────────────────────────────────────────────────────── */
  function paintAll() {
    paintSky(); paintCap(); paintPath(); paintDay(); paintChart(); paintRamp(); paintSize(); paintShadow();
    const alt = P.elevation ?? 14, az = P.azimuth ?? 118, K = P.temperature ?? 5400;
    const lx = lightOf(nowHour());
    const dayPc = Math.max(0, Math.sin(Math.max(0, alt) * Math.PI / 180)) * 100;

    pTime.innerHTML = clock(nowHour());
    pAlt.innerHTML = `${alt >= 0 ? '+' : '−'}${Math.abs(alt).toFixed(0)}<em>°</em>`;
    pAz.innerHTML = `${compassOf(az)}<em>${Math.round(az)}°</em>`;
    pK.innerHTML = `${(K / 1000).toFixed(1)}<em>kK</em>`;

    sUp.classList.toggle('down', alt < 0);
    sUp.querySelector('.i').innerHTML = alt < 0 ? ic('alert', { size: 12 }) : ic('check', { size: 12 });
    sUp.querySelector('.l').textContent = daylightName(alt);
    sUp.querySelector('.n').innerHTML = `${dayPc.toFixed(0)}<em>%</em>`;
    sLx.querySelector('.n').innerHTML = `${lx.toFixed(lx < 10 ? 1 : 0)}<em>klx</em>`;

    numI.textContent = Math.floor(lx);
    numD.textContent = `.${Math.round(lx * 10) % 10}`;
    peakV.textContent = `${(P.intensity ?? 88).toFixed(0)} klx at noon · ${daylightName(alt).toLowerCase()} now`;

    const rise = 6, noon = 12, set = 18;
    times.innerHTML = [['rise', clock(rise)], ['noon', clock(noon)], ['set', clock(set)]]
      .map(([k, v]) => `<div><span class="k">${k}</span><b>${v}</b></div>`).join('');
    const secs = (24 * 3600) / ((P.rate ?? 120) * 6);
    rateNote.textContent = P.animate
      ? `Running · a whole day every ${secs < 90 ? `${secs.toFixed(0)} s` : `${(secs / 60).toFixed(1)} min`}`
      : 'Held at this hour — switch it on to let the day run';

    const up = alt > 0.4;
    const len = up ? 1 / Math.tan(alt * Math.PI / 180) : 0;
    shI.textContent = up ? (len > 99 ? '99' : Math.floor(len)) : '—';
    shD.textContent = up && len < 100 ? `.${Math.round(len * 10) % 10}` : '';
    shV.textContent = up
      ? (P.shadows === false ? 'nothing — casting is off' : `${compassOf((az + 180) % 360)} ${Math.round((az + 180) % 360)}° · ${(P.softness ?? 2.4) < 1 ? 'hard edge' : (P.softness ?? 2.4) < 4 ? 'sun-soft edge' : 'overcast edge'}`)
      : 'nothing while the sun is down';

    kStep._set(K);
    sizeStep._set(P.angular ?? 0.6);
    power._set(P.intensity ?? 88);
    rate._set(P.rate ?? 120);
    soft._set(P.softness ?? 2.4);
    animate._set(!!P.animate);
    castPill._set(P.shadows !== false);
    chip._set && chip._set(P.tint || '#fff0d4');
  }

  syncers.push(paintAll);
  register && register(() => syncers.forEach(f => f()));

  const ro = new ResizeObserver(() => {
    paintSky(); paintPath(); paintDay(); paintChart(); paintRamp(); paintSize(); paintShadow();
    [rate, power, soft].forEach(t => t._paint && t._paint());
  });
  ro.observe(host);
  host._dispose = () => ro.disconnect();

  requestAnimationFrame(paintAll);
  paintAll();
  return host;
}
