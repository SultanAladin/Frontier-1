// Small DOM kit in the Project Zero language: section headings, rounded panels, pill sliders, segmented controls,
// toggles, XYZ vector rows and a toast. Every builder returns an element; callbacks receive parsed values.
import { icon } from './Icons.js';

export const $ = (id) => document.getElementById(id);

export function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'class') node.className = v;
    else if (k === 'html') node.innerHTML = v;
    else if (k === 'text') node.textContent = v;
    else if (k.startsWith('on')) node.addEventListener(k.slice(2).toLowerCase(), v);
    else if (k === 'style' && typeof v === 'object') Object.assign(node.style, v);
    else if (k === 'dataset') Object.assign(node.dataset, v);
    else node.setAttribute(k, v === true ? '' : v);
  }
  for (const c of children.flat()) if (c !== null && c !== undefined && c !== false) node.append(c.nodeType ? c : document.createTextNode(String(c)));
  return node;
}

export function iconEl(name, size = 16, cls = '') {
  return el('span', { class: `ico ${cls}`.trim(), html: icon(name, size) });
}

export function section(title, extra = null) {
  return el('h4', { class: 'section-head' }, el('span', {}, title), extra);
}

export function panel(...children) {
  return el('div', { class: 'panel' }, ...children);
}

const decimals = (step) => (String(step).includes('.') ? String(step).split('.')[1].length : 0);

export function rangeField({ label, value, min = 0, max = 1, step = 0.01, unit = '', note = '', onInput, onChange, format }) {
  const show = format || ((v) => `${(+v).toFixed(decimals(step))}${unit ? ` ${unit}` : ''}`);
  const out = el('output', {}, show(value));
  const input = el('input', { type: 'range', min, max, step, value });
  const paint = () => input.style.setProperty('--fill', `${((input.value - min) / (max - min || 1)) * 100}%`);
  paint();
  input.addEventListener('input', () => { paint(); out.textContent = show(+input.value); onInput && onInput(+input.value); });
  input.addEventListener('change', () => onChange && onChange(+input.value));
  return el('label', { class: 'field' }, el('span', {}, label), out, input, note ? el('small', {}, note) : null);
}

export function numberField({ label, value, step = 0.01, unit = '', onChange }) {
  const input = el('input', { type: 'number', class: 'num', value, step });
  input.addEventListener('change', () => onChange && onChange(+input.value));
  return el('label', { class: 'field inline' }, el('span', {}, label), el('span', { class: 'num-wrap' }, input, unit ? el('i', {}, unit) : null));
}

export function segmented({ options, value, onChange, cls = '' }) {
  const wrap = el('div', { class: `segmented ${cls}`.trim(), role: 'tablist' });
  for (const o of options) {
    const opt = typeof o === 'string' ? { id: o, label: o } : o;
    const b = el('button', { type: 'button', class: opt.id === value ? 'active' : '', title: opt.title || null, dataset: { value: opt.id } }, opt.icon ? iconEl(opt.icon, 14) : null, opt.label);
    b.addEventListener('click', () => {
      for (const x of wrap.children) x.classList.toggle('active', x === b);
      onChange && onChange(opt.id);
    });
    wrap.append(b);
  }
  return wrap;
}

export function segmentedField({ label, note = '', ...rest }) {
  return el('div', { class: 'field' }, el('span', {}, label), el('span'), el('div', { class: 'field-wide' }, segmented(rest)), note ? el('small', {}, note) : null);
}

export function toggle({ label, value, onChange, note = '' }) {
  const input = el('input', { type: 'checkbox', class: 'switch' });
  input.checked = !!value;
  input.addEventListener('change', () => onChange && onChange(input.checked));
  return el('label', { class: 'toggle-row' }, el('span', {}, label, note ? el('small', {}, note) : null), input);
}

export function selectField({ label, options, value, onChange }) {
  const select = el('select', {});
  for (const o of options) {
    const opt = typeof o === 'string' ? { id: o, label: o } : o;
    const node = el('option', { value: opt.id }, opt.label);
    if (String(opt.id) === String(value)) node.selected = true;
    select.append(node);
  }
  select.addEventListener('change', () => onChange && onChange(select.value));
  return el('label', { class: 'field inline' }, el('span', {}, label), select);
}

export function vec3Field({ label, value, step = 0.01, unit = '', onChange, linkable = false }) {
  const v = value.slice();
  let linked = false;
  const inputs = [];
  const row = el('div', { class: 'vec3' });
  ['X', 'Y', 'Z'].forEach((axis, i) => {
    const input = el('input', { type: 'number', step, value: +(+v[i]).toFixed(4) });
    input.addEventListener('change', () => {
      const n = +input.value;
      if (linked && v[i]) { const k = n / v[i]; for (let j = 0; j < 3; j++) { v[j] = j === i ? n : +(v[j] * k).toFixed(4); inputs[j].value = v[j]; } }
      else v[i] = n;
      onChange && onChange(v.slice());
    });
    inputs.push(input);
    row.append(el('label', { class: `axis axis-${axis.toLowerCase()}` }, el('i', {}, axis), input));
  });
  const head = el('div', { class: 'vec3-head' }, el('span', {}, label), unit ? el('small', {}, unit) : null);
  if (linkable) {
    const lock = el('button', { type: 'button', class: 'mini', title: 'Lock proportions', html: icon('link', 12) });
    lock.addEventListener('click', () => { linked = !linked; lock.classList.toggle('on', linked); });
    head.append(lock);
  }
  return el('div', { class: 'vec3-field' }, head, row);
}

export function colorField({ label, value, onChange }) {
  const input = el('input', { type: 'color', value });
  const code = el('code', {}, value);
  input.addEventListener('input', () => { code.textContent = input.value; onChange && onChange(input.value); });
  return el('label', { class: 'field inline color' }, el('span', {}, label), el('span', { class: 'color-wrap' }, code, input));
}

export function textField({ label, value, placeholder = '', onChange, multiline = false }) {
  const input = multiline ? el('textarea', { placeholder, rows: 3 }) : el('input', { type: 'text', value, placeholder });
  if (multiline) input.value = value;
  input.addEventListener('change', () => onChange && onChange(input.value));
  return el('label', { class: 'field stack' }, el('span', {}, label), input);
}

export function rows(pairs) {
  return el('div', { class: 'rows' }, ...pairs.flatMap(([k, v]) => [el('span', {}, k), el('b', {}, v)]));
}

export function metrics(pairs) {
  return el('div', { class: 'metrics' }, ...pairs.map(([v, k]) => el('div', {}, el('b', {}, v), el('small', {}, k))));
}

let toastTimer = 0;
export function toast(text) {
  const t = $('Toast');
  t.textContent = text;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 2200);
}

export function download(name, data, type = 'application/octet-stream') {
  const url = typeof data === 'string' && data.startsWith('data:') ? data : URL.createObjectURL(new Blob([data], { type }));
  const a = el('a', { href: url, download: name });
  document.body.append(a);
  a.click();
  a.remove();
  if (!url.startsWith('data:')) setTimeout(() => URL.revokeObjectURL(url), 2000);
}
