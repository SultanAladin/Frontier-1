/* ════════════════════════════════════════════════════════════════════════════════════════════
   INSTRUMENT CONTROLS
   Sliders are a UI convention, not an instrument. A tape is: a ruler with the range written on
   it, ticks you can count, and a marker sitting at the value — the same language as the timeline
   strips and the meters everywhere else in these panels. Numbers stay type-in, with a stepper
   either side for the last decimal.
   ════════════════════════════════════════════════════════════════════════════════════════════ */
import { el, clamp, fmt } from '../kit.js';

/* ── the number, with a nudge either side ──────────────────────────────────────────────────── */
export function stepper({ value = 0, min = -Infinity, max = Infinity, dec = 2, step = 0.1, unit = '', onInput }) {
  const wrap = el('div', 'step');
  wrap.innerHTML = `<button class="mn" tabindex="-1">−</button>
    <span class="f"><input value="${fmt(value, dec)}"><i>${unit}</i></span>
    <button class="pl" tabindex="-1">+</button>`;
  const inp = wrap.querySelector('input');
  let v = clamp(value, min, max);
  const paint = () => { if (document.activeElement !== inp) inp.value = fmt(v, dec); };
  const set = (nv, fire = true) => {
    v = clamp(nv, min, max);
    paint();
    if (fire && onInput) onInput(v);
  };
  wrap.querySelector('.mn').onclick = () => set(v - step);
  wrap.querySelector('.pl').onclick = () => set(v + step);
  inp.onkeydown = e => {
    e.stopPropagation();
    if (e.key === 'Enter') inp.blur();
    if (e.key === 'ArrowUp') { e.preventDefault(); set(v + step); }
    if (e.key === 'ArrowDown') { e.preventDefault(); set(v - step); }
  };
  inp.onblur = () => { const n = parseFloat(inp.value); isFinite(n) ? set(n) : paint(); };
  wrap._set = nv => set(nv, false);
  return wrap;
}

/* ── the tape ──────────────────────────────────────────────────────────────────────────────── */
export function tape({
  label, min = 0, max = 1, value = 0, dec = 2, step, unit = '', marks, height = 30,
  toPos, fromPos, tint, onInput,
}) {
  const wrap = el('div', 'tape');
  const head = el('div', 'hd', `<span class="k">${label}</span>`);
  const st = stepper({
    value, min, max, dec, unit,
    step: step ?? +((max - min) / 100).toPrecision(1),
    onInput: v => set(v),
  });
  head.appendChild(st);
  const cv = el('canvas');
  wrap.append(head, cv);

  let v = clamp(value, min, max);
  const pos = x => (toPos ? toPos(x, min, max) : (x - min) / (max - min || 1));
  const val = t => (fromPos ? fromPos(t, min, max) : min + t * (max - min));

  const g = cv.getContext('2d');
  function paint() {
    const w = wrap.clientWidth || 260, h = height;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    if (cv.width !== w * dpr || cv.height !== h * dpr) { cv.width = w * dpr; cv.height = h * dpr; }
    cv.style.height = h + 'px';
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const pad = 8, span = w - pad * 2, base = h - 11;
    g.font = '8px ui-sans-serif, system-ui';

    /* the ruler */
    for (let i = 0; i <= 40; i++) {
      const t = i / 40;
      const major = i % 10 === 0;
      const on = t <= pos(v);
      g.strokeStyle = major ? 'rgba(255,255,255,.30)'
        : `rgba(255,255,255,${on ? 0.22 : 0.09})`;
      g.lineWidth = 1;
      const len = major ? 9 : (i % 5 === 0 ? 6 : 4);
      const x = pad + t * span;
      g.beginPath(); g.moveTo(x, base - len); g.lineTo(x, base); g.stroke();
    }
    g.strokeStyle = 'rgba(255,255,255,.10)';
    g.beginPath(); g.moveTo(pad, base + .5); g.lineTo(pad + span, base + .5); g.stroke();

    /* what the ends and the middle mean */
    const ms = marks || [{ t: 0, l: fmt(min, dec) }, { t: 1, l: fmt(max, dec) }];
    g.fillStyle = 'rgba(255,255,255,.28)';
    const taken = [];                                  /* a label that would collide is dropped */
    ms.forEach(({ t, l }) => {
      const x = pad + t * span, tw = g.measureText(l).width;
      const x0 = t <= 0 ? x : t >= 1 ? x - tw : x - tw / 2;
      const x1 = x0 + tw;
      if (taken.some(([a, b]) => x0 < b + 5 && x1 > a - 5)) return;
      taken.push([x0, x1]);
      g.textAlign = t <= 0 ? 'left' : t >= 1 ? 'right' : 'center';
      g.fillText(l, x, h - 1);
    });

    /* the marker */
    const x = pad + pos(v) * span;
    g.fillStyle = tint || '#fff';
    g.beginPath();
    g.moveTo(x, base - 13); g.lineTo(x + 4, base - 19); g.lineTo(x - 4, base - 19);
    g.closePath(); g.fill();
    g.strokeStyle = tint || 'rgba(255,255,255,.85)';
    g.lineWidth = 1.4;
    g.beginPath(); g.moveTo(x, base - 12); g.lineTo(x, base); g.stroke();
  }

  const set = (nv, fire = true) => {
    v = clamp(nv, min, max);
    st._set(v);
    paint();
    if (fire && onInput) onInput(v);
  };

  const fromEvent = e => {
    const r = cv.getBoundingClientRect();
    const pad = 8, span = Math.max(1, r.width - pad * 2);
    set(val(clamp((e.clientX - r.left - pad) / span, 0, 1)));
  };
  cv.addEventListener('pointerdown', e => {
    cv.setPointerCapture(e.pointerId); cv.classList.add('drag'); fromEvent(e);
  });
  cv.addEventListener('pointermove', e => { if (cv.hasPointerCapture?.(e.pointerId)) fromEvent(e); });
  cv.addEventListener('pointerup', e => { cv.releasePointerCapture?.(e.pointerId); cv.classList.remove('drag'); });
  cv.addEventListener('wheel', e => { e.preventDefault(); set(v - Math.sign(e.deltaY) * (max - min) / 100); }, { passive: false });

  wrap._set = nv => { v = clamp(nv, min, max); st._set(v); paint(); };
  wrap._paint = paint;
  requestAnimationFrame(paint);
  return wrap;
}

/* ── a state, said as a word rather than drawn as a switch ─────────────────────────────────── */
export function pillToggle(label, value, onChange) {
  const b = el('button', `mp-tag state${value ? ' on' : ''}`, `<i></i>${label}`);
  b.onclick = () => {
    const next = !b.classList.contains('on');
    b.classList.toggle('on', next);
    onChange(next);
  };
  b._set = v => b.classList.toggle('on', !!v);
  return b;
}

/* ── the flat facts at the bottom of a card ────────────────────────────────────────────────── */
export function specList(rows) {
  const wrap = el('div', 'mp-spec');
  rows.forEach(([k, v]) => wrap.insertAdjacentHTML('beforeend',
    `<div><span class="k">${k}</span><b>${v}</b></div>`));
  return wrap;
}
