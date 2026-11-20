/* ════════════════════════════════════════════════════════════════════════════════════════════
   HEIGHT FOG
   Fog is distance made visible. This instrument replaces four anonymous controls with a calibrated
   sight line, a contrast curve, a vertical extinction profile, and a forward-scatter chamber.
   Every drawing is also a control; there are no sliders hiding beneath the diagrams.
   ════════════════════════════════════════════════════════════════════════════════════════════ */
import { el, colorChip } from '../kit.js';
import { tape, stepper, pillToggle } from './controls.js';
import { ic } from '../icons.js';

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const rgb = c => {
  const n = parseInt((c || '#8fa4bb').slice(1), 16);
  return [n >> 16 & 255, n >> 8 & 255, n & 255];
};
const rgba = (c, a = 1) => `rgba(${c.map(Math.round).join(',')},${a})`;

/* THREE's exponential-squared fog: transmission at distance d. */
export const transmission = (density, d) => Math.exp(-Math.pow(Math.max(0, density) * d, 2));
export const sightRange = density => density <= 0 ? 9999 : Math.sqrt(-Math.log(0.02)) / density;
const atHeight = (density, height, y) => density * Math.exp(-Math.max(0, y) / Math.max(1, height));
const fogClass = range => range > 1000 ? 'Mist' : range > 500 ? 'Light fog' : range > 200 ? 'Moderate fog' : range > 50 ? 'Thick fog' : 'Dense fog';

export function fogPanel(node, ctx) {
  const { compact = false, setProp, register } = ctx;
  const P = node.props;
  const host = el('div', 'mpanel fogpanel');
  const D = () => P.density ?? 0.011;
  const H = () => P.height ?? 42;
  const S = () => P.sunScatter ?? 0.7;
  const C = () => P.color || '#8fa4bb';
  const on = (x, ev, fn) => x.addEventListener(ev, fn);

  /* ── sight-line hero ───────────────────────────────────────────────────────────────────── */
  const hero = el('div', 'pcard mp-hero fg-hero');
  const heroCv = el('canvas');
  heroCv.title = 'Drag across for density; up and down for layer height';
  const cap = el('div', 'mp-cap', `<div class="l"><b>—</b><span class="mp-illum fg-sub">—</span></div><div class="r">—</div>`);
  hero.append(heroCv, cap); host.appendChild(hero);
  let heroMark = null;

  function sizeCanvas(cv, h) {
    const w = cv.clientWidth || 290, dpr = Math.min(devicePixelRatio || 1, 2);
    if (cv.width !== w * dpr || cv.height !== h * dpr) { cv.width = w * dpr; cv.height = h * dpr; }
    cv.style.height = h + 'px';
    const g = cv.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0);
    return [g, w, h];
  }

  function paintHero() {
    const [g, w, h] = sizeCanvas(heroCv, compact ? 138 : 166);
    const col = rgb(C()), horizon = h * .42;
    const bg = g.createLinearGradient(0, 0, 0, h);
    bg.addColorStop(0, '#090b0e'); bg.addColorStop(.42, '#181d22'); bg.addColorStop(1, '#08090a');
    g.fillStyle = bg; g.fillRect(0, 0, w, h);

    /* instrument floor — a coordinate field, not terrain */
    g.strokeStyle = 'rgba(255,255,255,.075)'; g.lineWidth = 1;
    for (let i = -7; i <= 7; i++) {
      const xb = w / 2 + i * w * .13;
      g.beginPath(); g.moveTo(w / 2, horizon); g.lineTo(xb, h); g.stroke();
    }
    for (let z = 1; z <= 12; z++) {
      const t = z / 12, y = horizon + Math.pow(t, 1.75) * (h - horizon);
      g.beginPath(); g.moveTo(0, y); g.lineTo(w, y); g.stroke();
    }

    /* range gates recede into the fog; their contrast obeys the actual transmission curve */
    const ranges = [10, 25, 50, 100, 200, 400];
    ranges.slice().reverse().forEach((m, ri) => {
      const z = ranges.indexOf(m) / (ranges.length - 1), scale = .16 + (1 - z) * .70;
      const x = w / 2, y = horizon + (1 - z) * (h - horizon) * .82;
      const gh = h * .58 * scale, gw = w * .36 * scale;
      const tr = transmission(D(), m);
      g.strokeStyle = `rgba(255,255,255,${.05 + tr * .58})`;
      g.strokeRect(x - gw / 2, y - gh, gw, gh);
      g.fillStyle = `rgba(255,255,255,${.12 + tr * .55})`;
      g.font = '8px ui-sans-serif,system-ui'; g.textAlign = 'center'; g.fillText(`${m} m`, x, y - gh + 10);
      if (ri === 0) g.fillRect(x - 1, y - gh * .48, 2, gh * .48);
    });

    /* luminous air stacked by height falloff */
    for (let y = 0; y < h; y += 3) {
      const metres = Math.max(0, (horizon - y) / Math.max(1, horizon) * 200);
      const local = atHeight(D(), H(), metres);
      const a = clamp(local / .06, 0, 1) * .38 + (y > horizon ? clamp(D() / .06, 0, 1) * .12 : 0);
      g.fillStyle = rgba(col, a); g.fillRect(0, y, w, 3.2);
    }

    const range = sightRange(D());
    const frac = clamp(range / 400, 0, 1);
    const x = 8 + frac * (w - 16);
    g.strokeStyle = 'rgba(255,255,255,.72)'; g.setLineDash([3, 3]);
    g.beginPath(); g.moveTo(x, 9); g.lineTo(x, h - 10); g.stroke(); g.setLineDash([]);
    g.fillStyle = 'rgba(255,255,255,.7)'; g.textAlign = x > w - 45 ? 'right' : 'left';
    g.fillText('2% CONTRAST', x + (x > w - 45 ? -4 : 4), 17);

    if (heroMark) {
      g.strokeStyle = 'rgba(255,255,255,.4)'; g.setLineDash([2, 3]);
      g.beginPath(); g.moveTo(heroMark.x, 0); g.lineTo(heroMark.x, h); g.moveTo(0, heroMark.y); g.lineTo(w, heroMark.y); g.stroke(); g.setLineDash([]);
    }
    cap.querySelector('b').textContent = fogClass(range);
    cap.querySelector('.fg-sub').textContent = `exponential² · ${D().toFixed(4)} per metre`;
    cap.querySelector('.r').textContent = range >= 1000 ? `${(range / 1000).toFixed(1)} km sight` : `${range.toFixed(0)} m sight`;
  }

  const heroFrom = e => {
    const r = heroCv.getBoundingClientRect(), x = clamp((e.clientX - r.left) / r.width, 0, 1), y = clamp((e.clientY - r.top) / r.height, 0, 1);
    heroMark = { x: x * r.width, y: y * r.height };
    setProp(node, 'density', +(x * .06).toFixed(4));
    setProp(node, 'height', Math.round(1 + (1 - y) * 199));
    paintAll();
  };
  on(heroCv, 'pointerdown', e => { heroCv.setPointerCapture(e.pointerId); heroCv.classList.add('grabbing'); heroFrom(e); });
  on(heroCv, 'pointermove', e => { if (heroCv.hasPointerCapture?.(e.pointerId)) heroFrom(e); });
  const heroEnd = () => { heroMark = null; heroCv.classList.remove('grabbing'); paintHero(); };
  on(heroCv, 'pointerup', heroEnd); on(heroCv, 'pointercancel', heroEnd);

  /* ── quick rail ────────────────────────────────────────────────────────────────────────── */
  const rail = el('div', 'mp-rail');
  const railItem = label => { const e = el('div', 'mp-pill', `<b class="v">—</b><span class="k">${label}</span>`); rail.append(e); return e.querySelector('.v'); };
  const rD = railItem('Density'), rR = railItem('Sight'), rH = railItem('Ceiling'), rS = railItem('Scatter');
  host.append(rail);

  const duo = el('div', 'mp-duo');
  const stat = (icon, label) => { const e = el('div', 'pcard mp-stat', `<span class="i">${ic(icon, { size: 12 })}</span><span class="l">${label}</span><b class="n">—</b>`); duo.append(e); return e; };
  const tr50 = stat('eye', 'Contrast at 50 m');
  const classStat = stat('fog', 'WMO character');
  host.append(duo);

  /* ── visibility / contrast plot ────────────────────────────────────────────────────────── */
  const vc = el('div', 'pcard mp-metric fg-vis');
  vc.innerHTML = `<div class="mp-chead"><div class="l"><span class="t">Visibility</span><span class="s">Contrast transmission · 0–400 m</span></div><button class="mp-x">${ic('arrowout', { size: 12 })}</button></div>
    <div class="mp-num"><span class="i">—</span><span class="d"></span><span class="u">m</span></div>
    <div class="mp-k mp-target">Sight range <span class="v">2% contrast threshold</span></div>`;
  const visWrap = el('div', 'mp-chartwrap'); const visCv = el('canvas'); visWrap.append(visCv); vc.append(visWrap); host.append(vc);
  vc.querySelector('.mp-x').onclick = () => { vc.classList.toggle('tall'); paintVisibility(); };

  function paintVisibility() {
    const [g, w, h] = sizeCanvas(visCv, vc.classList.contains('tall') ? 170 : 112);
    g.clearRect(0, 0, w, h); const L = 8, R = 30, T = 8, B = 18, px = d => L + d / 400 * (w - L - R), py = t => T + (1 - t) * (h - T - B);
    g.font = '8px ui-sans-serif,system-ui';
    [.02, .25, .5, .75, 1].forEach(t => { g.strokeStyle = t === .02 ? 'rgba(239,68,68,.45)' : 'rgba(255,255,255,.07)'; g.setLineDash(t === .02 ? [3, 3] : [2, 5]); g.beginPath(); g.moveTo(L, py(t)); g.lineTo(w - R, py(t)); g.stroke(); g.setLineDash([]); g.fillStyle = 'rgba(255,255,255,.28)'; g.textAlign = 'left'; g.fillText(`${Math.round(t * 100)}%`, w - R + 5, py(t) + 3); });
    const fill = g.createLinearGradient(0, T, 0, h - B); fill.addColorStop(0, rgba(rgb(C()), .38)); fill.addColorStop(1, rgba(rgb(C()), .03));
    g.beginPath(); g.moveTo(px(0), py(0)); for (let d = 0; d <= 400; d += 2) g.lineTo(px(d), py(transmission(D(), d))); g.lineTo(px(400), py(0)); g.closePath(); g.fillStyle = fill; g.fill();
    g.beginPath(); for (let d = 0; d <= 400; d += 2) d ? g.lineTo(px(d), py(transmission(D(), d))) : g.moveTo(px(d), py(1)); g.strokeStyle = 'rgba(255,255,255,.9)'; g.lineWidth = 1.5; g.stroke(); g.lineWidth = 1;
    [0, 100, 200, 300, 400].forEach((d, i) => { g.fillStyle = 'rgba(255,255,255,.27)'; g.textAlign = i ? i === 4 ? 'right' : 'center' : 'left'; g.fillText(i === 2 ? '200 m' : d, px(d), h - 3); });
    const range = sightRange(D()), x = px(clamp(range, 0, 400)); g.fillStyle = '#fff'; g.beginPath(); g.arc(x, py(.02), 3, 0, Math.PI * 2); g.fill();
  }
  const densityFrom = e => { const r = visCv.getBoundingClientRect(); setProp(node, 'density', +(clamp((e.clientX - r.left - 8) / Math.max(1, r.width - 38), 0, 1) * .06).toFixed(4)); paintAll(); };
  on(visCv, 'pointerdown', e => { visCv.setPointerCapture(e.pointerId); visCv.classList.add('drag'); densityFrom(e); });
  on(visCv, 'pointermove', e => { if (visCv.hasPointerCapture?.(e.pointerId)) densityFrom(e); });
  on(visCv, 'pointerup', e => { visCv.releasePointerCapture?.(e.pointerId); visCv.classList.remove('drag'); });

  /* ── layer profile ─────────────────────────────────────────────────────────────────────── */
  const lc = el('div', 'pcard mp-light fg-layer');
  lc.innerHTML = `<div class="mp-chead"><div class="l"><span class="t">Layer</span><span class="s">Extinction with altitude</span></div></div>`;
  const lb = el('div', 'pbody'); lc.append(lb);
  const profile = el('div', 'mp-meter fg-profile', '<div class="hd"><span class="k">vertical profile</span></div>');
  const profileCv = el('canvas'); profile.append(profileCv); lb.append(profile);
  const hStep = stepper({ value: H(), min: 1, max: 200, dec: 0, step: 1, unit: 'm', onInput: v => { setProp(node, 'height', v); paintAll(); } });
  profile.querySelector('.hd').append(hStep);
  const dTape = tape({ label: 'Density at datum', min: 0, max: .06, value: D(), dec: 4, step: .0005, marks: [{ t: 0, l: 'CLEAR 0' }, { t: .183, l: 'FOG .011' }, { t: 1, l: 'OPAQUE .060' }], onInput: v => { setProp(node, 'density', +v.toFixed(4)); paintAll(); } });
  const hTape = tape({ label: 'Height falloff', min: 1, max: 200, value: H(), dec: 0, step: 1, unit: 'm', marks: [{ t: 0, l: '1 m' }, { t: .206, l: 'LOW 42' }, { t: 1, l: '200 m' }], onInput: v => { setProp(node, 'height', Math.round(v)); paintAll(); } });
  lb.append(dTape, hTape); host.append(lc);
  lc.querySelector('.mp-chead .l').onclick = () => lc.classList.toggle('shut');

  function paintProfile() {
    const [g, w, h] = sizeCanvas(profileCv, 96); g.clearRect(0, 0, w, h); const L = 32, R = 8, T = 5, B = 14, py = m => T + (1 - m / 200) * (h - T - B), px = d => L + d / .06 * (w - L - R);
    [0, 50, 100, 150, 200].forEach(m => { g.strokeStyle = 'rgba(255,255,255,.07)'; g.beginPath(); g.moveTo(L, py(m)); g.lineTo(w - R, py(m)); g.stroke(); g.fillStyle = 'rgba(255,255,255,.28)'; g.font = '8px ui-sans-serif,system-ui'; g.textAlign = 'right'; g.fillText(m === 100 ? '100 m' : m, L - 5, py(m) + 3); });
    const grad = g.createLinearGradient(L, 0, w - R, 0); grad.addColorStop(0, rgba(rgb(C()), .04)); grad.addColorStop(1, rgba(rgb(C()), .5));
    g.beginPath(); g.moveTo(L, py(0)); for (let m = 0; m <= 200; m += 2) g.lineTo(px(atHeight(D(), H(), m)), py(m)); g.lineTo(L, py(200)); g.closePath(); g.fillStyle = grad; g.fill();
    g.beginPath(); for (let m = 0; m <= 200; m += 2) m ? g.lineTo(px(atHeight(D(), H(), m)), py(m)) : g.moveTo(px(D()), py(0)); g.strokeStyle = 'rgba(255,255,255,.85)'; g.lineWidth = 1.4; g.stroke(); g.lineWidth = 1;
    const y = py(H()); g.setLineDash([3, 3]); g.strokeStyle = 'rgba(255,255,255,.38)'; g.beginPath(); g.moveTo(L, y); g.lineTo(w - R, y); g.stroke(); g.setLineDash([]); g.fillStyle = '#fff'; g.beginPath(); g.arc(px(D() / Math.E), y, 3, 0, Math.PI * 2); g.fill();
  }
  const heightFrom = e => { const r = profileCv.getBoundingClientRect(); setProp(node, 'height', Math.round(clamp((1 - (e.clientY - r.top - 5) / Math.max(1, r.height - 19)) * 200, 1, 200))); paintAll(); };
  on(profileCv, 'pointerdown', e => { profileCv.setPointerCapture(e.pointerId); profileCv.classList.add('drag'); heightFrom(e); }); on(profileCv, 'pointermove', e => { if (profileCv.hasPointerCapture?.(e.pointerId)) heightFrom(e); }); on(profileCv, 'pointerup', e => profileCv.releasePointerCapture?.(e.pointerId));

  /* ── light transport ───────────────────────────────────────────────────────────────────── */
  const sc = el('div', 'pcard mp-light fg-scatter');
  sc.innerHTML = `<div class="mp-chead"><div class="l"><span class="t">Light transport</span><span class="s">Forward sun scatter through the volume</span></div></div>`;
  const sb = el('div', 'pbody'); sc.append(sb);
  const chamber = el('div', 'mp-meter fg-chamber', '<div class="hd"><span class="k">beam chamber</span></div>'); const chamberCv = el('canvas'); chamber.append(chamberCv); sb.append(chamber);
  const scatterTape = tape({ label: 'Sun scatter', min: 0, max: 2, value: S(), dec: 2, step: .02, marks: [{ t: 0, l: 'FLAT 0' }, { t: .35, l: 'NATURAL .70' }, { t: 1, l: 'HALO 2' }], onInput: v => { setProp(node, 'sunScatter', v); paintAll(); } }); sb.append(scatterTape);
  const colourHead = el('div', 'mp-subhead', '<span class="k">volume colour</span>'); const colour = colorChip(C(), v => { setProp(node, 'color', v); paintAll(); }); colourHead.append(colour); sb.append(colourHead);
  const tags = el('div', 'mp-tags'); const enabled = pillToggle('VOLUME ENABLED', P.enabled !== false, v => { setProp(node, 'enabled', v); paintAll(); }); tags.append(enabled); sb.append(tags);
  const note = el('div', 'mp-note'); sb.append(note); host.append(sc); sc.querySelector('.mp-chead .l').onclick = () => sc.classList.toggle('shut');

  function paintChamber() {
    const [g, w, h] = sizeCanvas(chamberCv, 74); g.clearRect(0, 0, w, h); g.fillStyle = '#191919'; g.fillRect(0, 0, w, h);
    const cy = h / 2, col = rgb(C()), strength = S(), layerDensity = atHeight(D(), H(), 25), active = P.enabled !== false, preview = active ? 1 : .84, range = sightRange(layerDensity), rangeX = 12 + clamp(range / 120, 0, 1) * (w - 24), endSpread = 8 + strength * 16;
    g.strokeStyle = 'rgba(255,255,255,.07)'; g.setLineDash([2, 5]);
    [cy - 24, cy, cy + 24].forEach(y => { g.beginPath(); g.moveTo(12, y); g.lineTo(w - 12, y); g.stroke(); }); g.setLineDash([]);
    for (let x = 12; x < w - 10; x += 2) {
      const t = (x - 12) / Math.max(1, w - 24), physical = transmission(layerDensity, t * 120), display = .35 + .65 * Math.pow(physical, .15), spread = 3 + t * t * (12 + strength * 22), a = preview * (.12 + Math.min(2, strength) * .22) * display;
      const grd = g.createLinearGradient(0, cy - spread, 0, cy + spread); grd.addColorStop(0, rgba(col, 0)); grd.addColorStop(.5, rgba(col, a)); grd.addColorStop(1, rgba(col, 0)); g.fillStyle = grd; g.fillRect(x, cy - spread, 2.1, spread * 2);
      g.fillStyle = rgba(col, preview * (.28 + physical * .5)); g.fillRect(x, cy - 1.05, 2.1, 2.1);
    }
    g.strokeStyle = rgba(col, preview * .28); g.beginPath(); g.moveTo(12, cy); g.lineTo(w - 12, cy - endSpread); g.moveTo(12, cy); g.lineTo(w - 12, cy + endSpread); g.stroke();
    g.strokeStyle = 'rgba(239,110,110,.62)'; g.setLineDash([3, 3]); g.beginPath(); g.moveTo(rangeX, 20); g.lineTo(rangeX, h - 18); g.stroke(); g.setLineDash([]);
    g.fillStyle = `rgba(255,247,220,${active ? .95 : .62})`; g.beginPath(); g.arc(12, cy, 4, 0, Math.PI * 2); g.fill();
    g.fillStyle = 'rgba(255,255,255,.36)'; g.font = '8px ui-sans-serif,system-ui'; g.textAlign = 'left'; g.fillText(`${active ? 'LIVE' : 'PREVIEW'} · 25 m LAYER`, 8, 11); g.fillText(`D ${D().toFixed(4)} · H ${H().toFixed(0)} m · S ${strength.toFixed(2)}`, 8, h - 5); g.textAlign = 'right'; g.fillText(`2% · ${range >= 1000 ? (range / 1000).toFixed(1) + ' km' : range.toFixed(0) + ' m'}`, w - 8, 11); g.fillText(`${(transmission(layerDensity, 120) * 100).toFixed(0)}% PHYSICAL AT 120 m`, w - 8, h - 5);
  }

  function paintAll() {
    const range = sightRange(D()), t50 = transmission(D(), 50);
    paintHero(); paintVisibility(); paintProfile(); paintChamber();
    rD.textContent = D().toFixed(4); rR.innerHTML = range >= 1000 ? `${(range / 1000).toFixed(1)}<em>km</em>` : `${range.toFixed(0)}<em>m</em>`; rH.innerHTML = `${H().toFixed(0)}<em>m</em>`; rS.textContent = S().toFixed(2);
    tr50.querySelector('.n').innerHTML = `${(t50 * 100).toFixed(0)}<em>%</em>`; tr50.classList.toggle('down', t50 < .1); tr50.querySelector('.i').innerHTML = ic(t50 < .1 ? 'alert' : 'eye', { size: 12 });
    classStat.querySelector('.n').innerHTML = fogClass(range).replace(' fog', '<em> fog</em>');
    const whole = Math.floor(Math.min(range, 9999)), tenth = Math.round(range * 10) % 10; vc.querySelector('.mp-num .i').textContent = whole; vc.querySelector('.mp-num .d').textContent = `.${tenth}`;
    vc.querySelector('.mp-num .u').textContent = range >= 1000 ? 'm+' : 'm';
    hStep._set(H()); dTape._set(D()); hTape._set(H()); scatterTape._set(S()); enabled._set(P.enabled !== false); colour._set?.(C());
    note.textContent = `${P.enabled === false ? 'The volume is bypassed.' : `${fogClass(range)} is active.`} At one falloff height (${H().toFixed(0)} m), density is 37% of its datum value. Sun scatter is ${S() < .3 ? 'flat' : S() < 1.2 ? 'natural' : 'strong'}.`;
    host.classList.toggle('disabled', P.enabled === false);
  }

  register?.(paintAll);
  const ro = new ResizeObserver(() => { paintHero(); paintVisibility(); paintProfile(); paintChamber(); [dTape, hTape, scatterTape].forEach(t => t._paint?.()); });
  ro.observe(host); host._dispose = () => ro.disconnect();
  requestAnimationFrame(paintAll); paintAll();
  return host;
}
