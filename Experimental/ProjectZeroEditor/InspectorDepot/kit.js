/* ════════════════════════════════════════════════════════════════════════════════════════════
   CONTROL KIT
   DOM factories for the Slate primitives — slider, switch, value pill, axis field, dropdown,
   colour chip. Geometry matches ControlKit: the slider fill is a pill that ends under the thumb
   centre, never a hard-edged gradient stop, and every numeric readout is type-in editable.
   ════════════════════════════════════════════════════════════════════════════════════════════ */
import { ic } from './icons.js';

export function el(tag, cls, html) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (html != null) e.innerHTML = html;
  return e;
}
export const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
export const fmt = (v, dec) => Number(v).toFixed(dec ?? 2);

/* ── slider + value pill ───────────────────────────────────────────────────────────────────── */
export function slider({ min = 0, max = 1, value = 0, dec = 2, unit = '', hi = false, thin = false, onInput }) {
  const wrap = el('div', 'ctl-slider', '');
  wrap.style.cssText = 'display:flex;align-items:center;gap:10px;flex:1;min-width:0;';
  const pill = el('div', 'vpill', `<div class="num"></div><div class="unit">${unit || '—'}</div>`);
  const sl = el('div', `kslider${hi ? ' hi' : ''}${thin ? ' thin' : ''}`,
    '<div class="track"></div><div class="kfill"></div><div class="knob"></div>');
  wrap.append(pill, sl);

  let v = clamp(value, min, max);
  const numCell = pill.querySelector('.num');

  const paint = () => {
    const t = (v - min) / (max - min || 1);
    const thumb = thin ? 18 : 24;
    const w = sl.clientWidth || 150;
    const cx = thumb / 2 + t * Math.max(w - thumb, 1);
    sl.querySelector('.knob').style.left = cx + 'px';
    sl.querySelector('.kfill').style.width = Math.max(cx, thumb) + 'px';
    if (!numCell.querySelector('input')) numCell.textContent = fmt(v, dec);
  };
  const set = (nv, fire = true) => {
    v = clamp(nv, min, max); paint();
    if (fire && onInput) onInput(v);
  };
  sl._paint = paint;

  const fromEvent = e => {
    const r = sl.getBoundingClientRect();
    const thumb = thin ? 18 : 24;
    const t = clamp((e.clientX - r.left - thumb / 2) / Math.max(r.width - thumb, 1), 0, 1);
    set(min + t * (max - min));
  };
  sl.addEventListener('pointerdown', e => {
    sl.setPointerCapture(e.pointerId); sl.classList.add('drag'); fromEvent(e);
    const mv = ev => fromEvent(ev);
    const up = () => { sl.classList.remove('drag'); sl.removeEventListener('pointermove', mv); sl.removeEventListener('pointerup', up); };
    sl.addEventListener('pointermove', mv); sl.addEventListener('pointerup', up);
  });
  /* wheel nudges by 1 % of the range — fine adjustment without leaving the pointer */
  sl.addEventListener('wheel', e => { e.preventDefault(); set(v - Math.sign(e.deltaY) * (max - min) / 100); }, { passive: false });

  /* type-in editing on the pill */
  numCell.onclick = () => {
    if (numCell.querySelector('input')) return;
    const before = fmt(v, dec);
    numCell.innerHTML = `<input value="${before}">`;
    const inp = numCell.querySelector('input'); inp.focus(); inp.select();
    let done = false;
    const finish = commit => {
      if (done) return; done = true;
      const nv = parseFloat(inp.value);
      numCell.textContent = before;
      if (commit && isFinite(nv)) set(nv);
      else paint();
    };
    inp.onkeydown = e => { e.stopPropagation(); if (e.key === 'Enter') finish(true); if (e.key === 'Escape') finish(false); };
    inp.onblur = () => finish(true);
  };

  requestAnimationFrame(paint);
  wrap._set = nv => set(nv, false);
  return wrap;
}

/* ── switch ────────────────────────────────────────────────────────────────────────────────── */
export function toggle(value, onChange) {
  const s = el('div', 'switch');
  s.dataset.on = value ? 'true' : 'false';
  s.onclick = () => { s.dataset.on = s.dataset.on === 'true' ? 'false' : 'true'; onChange(s.dataset.on === 'true'); };
  s._set = v => { s.dataset.on = v ? 'true' : 'false'; };
  return s;
}

/* ── axis vector ───────────────────────────────────────────────────────────────────────────── */
export function vec3(value, { step = 0.05, disabled = false, dec = 2, onChange }) {
  const wrap = el('div', 'vec');
  const inputs = [];
  ['x', 'y', 'z'].forEach((a, i) => {
    const f = el('div', `vfield ${a}`,
      `<span class="ax">${a.toUpperCase()}</span><span class="num"><input value="${fmt(value[i], dec)}" ${disabled ? 'disabled' : ''}></span>`);
    const inp = f.querySelector('input');
    inputs.push(inp);
    if (!disabled) {
      let before = inp.value;
      inp.onfocus = () => { before = inp.value; inp.select(); };
      inp.onkeydown = e => {
        e.stopPropagation();
        if (e.key === 'Enter') inp.blur();
        if (e.key === 'Escape') { inp.value = before; inp.blur(); }
        if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
          e.preventDefault();
          const d = (e.key === 'ArrowUp' ? 1 : -1) * step * (e.shiftKey ? 10 : 1);
          inp.value = fmt((parseFloat(inp.value) || 0) + d, dec); commit();
        }
      };
      inp.onblur = () => commit();
      /* horizontal drag scrub on the axis letter */
      const ax = f.querySelector('.ax');
      ax.style.cursor = 'ew-resize';
      ax.addEventListener('pointerdown', e => {
        ax.setPointerCapture(e.pointerId);
        const x0 = e.clientX, v0 = parseFloat(inp.value) || 0;
        const mv = ev => { inp.value = fmt(v0 + (ev.clientX - x0) * step, dec); commit(); };
        const up = () => { ax.removeEventListener('pointermove', mv); ax.removeEventListener('pointerup', up); };
        ax.addEventListener('pointermove', mv); ax.addEventListener('pointerup', up);
      });
    }
    const commit = () => {
      const out = inputs.map(x => { const n = parseFloat(x.value); return isFinite(n) ? n : 0; });
      inputs.forEach((x, j) => { x.value = fmt(out[j], dec); });
      onChange(out);
    };
    wrap.appendChild(f);
  });
  wrap._set = v => inputs.forEach((inp, i) => { if (document.activeElement !== inp) inp.value = fmt(v[i], dec); });
  return wrap;
}

/* ── colour chip ───────────────────────────────────────────────────────────────────────────── */
export function colorChip(value, onChange) {
  const wrap = el('div', 'colorrow');
  const chip = el('div', 'colorchip', `<input type="color" value="${value}">`);
  chip.style.background = value;
  const hex = el('span', 'hexval', value.toUpperCase());
  const input = chip.querySelector('input');
  input.oninput = () => { chip.style.background = input.value; hex.textContent = input.value.toUpperCase(); onChange(input.value); };
  wrap.append(chip, hex);
  wrap._set = v => { input.value = v; chip.style.background = v; hex.textContent = v.toUpperCase(); };
  return wrap;
}

export function swatchRow(value, palette, onChange) {
  const wrap = el('div', 'swatches');
  palette.forEach(c => {
    const s = el('span', `swatch${value === c ? ' on' : ''}`);
    s.style.background = c;
    s.onclick = () => { wrap.querySelectorAll('.swatch').forEach(x => x.classList.remove('on')); s.classList.add('on'); onChange(c); };
    wrap.appendChild(s);
  });
  return wrap;
}

/* ── dropdown ──────────────────────────────────────────────────────────────────────────────── */
export function dropdown(options, value, onChange, { width } = {}) {
  const dd = el('div', 'dd');
  if (width) dd.style.flex = `0 0 ${width}px`; else dd.style.flex = '1';
  dd.innerHTML = `<div class="dd-btn"><div class="cur"></div><div class="caret">${ic('chevdown', { size: 13 })}</div></div>
    <div class="dd-menu"></div>`;
  const cur = dd.querySelector('.cur'), menu = dd.querySelector('.dd-menu');
  const tick = `<span class="tick">${ic('check', { size: 12, width: 3 })}</span>`;
  let val = value;
  const paint = () => {
    cur.textContent = val;
    menu.innerHTML = options.map(o => `<div class="opt${o === val ? ' sel' : ''}" data-v="${o}">${tick}${o}</div>`).join('');
    menu.querySelectorAll('.opt').forEach(o => o.onclick = e => {
      e.stopPropagation(); val = o.dataset.v; paint(); dd.classList.remove('open'); onChange(val);
    });
  };
  paint();
  dd.querySelector('.dd-btn').onclick = e => {
    e.stopPropagation();
    document.querySelectorAll('.dd.open').forEach(x => { if (x !== dd) x.classList.remove('open'); });
    dd.classList.toggle('open');
  };
  dd._set = v => { val = v; paint(); };
  return dd;
}
document.addEventListener('click', e => {
  if (!e.target.closest('.dd')) document.querySelectorAll('.dd.open').forEach(x => x.classList.remove('open'));
});

/* ── inline rename, shared by tree rows and identity strips ────────────────────────────────── */
export function beginRename(node, host, after) {
  if (!host || host.querySelector('input')) return;
  const before = node.name;
  host.innerHTML = `<input value="${before.replace(/"/g, '&quot;')}">`;
  const inp = host.querySelector('input');
  inp.focus(); inp.select();
  let done = false;
  const finish = commit => {
    if (done) return; done = true;
    const v = inp.value.trim();
    node.name = (commit && v) ? v : before;
    after();
  };
  inp.onkeydown = e => { e.stopPropagation(); if (e.key === 'Enter') finish(true); if (e.key === 'Escape') finish(false); };
  inp.onblur = () => finish(true);
  inp.onclick = e => e.stopPropagation();
  inp.ondblclick = e => e.stopPropagation();
}

/* repaint every slider when the layout changes — the fill geometry is width dependent */
addEventListener('resize', () => document.querySelectorAll('.kslider').forEach(s => s._paint && s._paint()));
export const repaintSliders = root =>
  (root || document).querySelectorAll('.kslider').forEach(s => s._paint && s._paint());
