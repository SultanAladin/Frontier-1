/*============================================================================================================================================
                                                          STUDIO KIT
============================================================================================================================================*/
// 🧩 DOM builders in the Project-Zero language: trapezoid silhouettes for the notch and drawer handles, inline stroke icons, field rows
// (range, select, toggle, vector, colour, text), and the canvas instruments the inspector and telemetry drawer draw with.

//------------------------------------------------------------------------------------------------------------------------
//                                                  DOM HELPERS
//------------------------------------------------------------------------------------------------------------------------

export const $ = (id) => document.getElementById(id);

export function El(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (value === undefined || value === null || value === false) continue;
    if (key === 'class') node.className = value;
    else if (key === 'html') node.innerHTML = value;
    else if (key === 'text') node.textContent = value;
    else if (key.startsWith('on')) node.addEventListener(key.slice(2).toLowerCase(), value);
    else if (key === 'style' && typeof value === 'object') Object.assign(node.style, value);
    else if (key === 'dataset') Object.assign(node.dataset, value);
    else node.setAttribute(key, value === true ? '' : value);
  }
  for (const child of children.flat()) {
    if (child === null || child === undefined || child === false) continue;
    node.append(child.nodeType ? child : document.createTextNode(String(child)));
  }
  return node;
}

export const Fragment = (...children) => children.flat().filter((c) => c !== null && c !== undefined && c !== false);

// Trapezoid silhouette used by the notch and drawer handles: [0,h] [slant,0] [w-slant,0] [w,h].
export function TrapezoidPoints(width, height, slant, direction = 'down') {
  const s = Math.min(slant, width * 0.16);
  return direction === 'down'
    ? `0,${height} ${s},0 ${width - s},0 ${width},${height}`
    : `0,0 ${width},0 ${width - s},${height} ${s},${height}`;
}

export function TrapezoidSvg({ width = 240, height = 30, slant = 14, direction = 'down', fill = '#222' }) {
  return `<svg viewBox="0 0 ${width} ${height}" preserveAspectRatio="none" aria-hidden="true"><polygon points="${TrapezoidPoints(width, height, slant, direction)}" fill="${fill}"/></svg>`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INLINE ICONS (24 px grid, 1.5 stroke)
//------------------------------------------------------------------------------------------------------------------------

const PATHS = {
  plus: '<path d="M12 5v14M5 12h14"/>',
  minus: '<path d="M5 12h14"/>',
  search: '<circle cx="11" cy="11" r="6.5"/><path d="m20 20-4.4-4.4"/>',
  eye: '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="2.8"/>',
  eyeOff: '<path d="M3 3l18 18M10.6 5.6A9.6 9.6 0 0 1 12 5.5c6 0 9.5 6.5 9.5 6.5a16.6 16.6 0 0 1-3 3.8M6.4 6.9C3.9 8.6 2.5 12 2.5 12S6 18.5 12 18.5c1.6 0 3-.4 4.2-1"/>',
  trash: '<path d="M4 7h16M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13"/>',
  copy: '<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V5.5A1.5 1.5 0 0 0 14.5 4h-9A1.5 1.5 0 0 0 4 5.5v9A1.5 1.5 0 0 0 5.5 16H8"/>',
  up: '<path d="m6 14 6-6 6 6"/>',
  down: '<path d="m6 10 6 6 6-6"/>',
  play: '<path d="M7 4.5v15l12-7.5z" fill="currentColor" stroke="none"/>',
  pause: '<rect x="6.5" y="5" width="4" height="14" rx="1" fill="currentColor" stroke="none"/><rect x="13.5" y="5" width="4" height="14" rx="1" fill="currentColor" stroke="none"/>',
  rewind: '<path d="M11 6 4 12l7 6z" fill="currentColor" stroke="none"/><path d="M20 6l-7 6 7 6z" fill="currentColor" stroke="none"/>',
  frame: '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>',
  cube: '<path d="M12 2.8 20 7.3v9.4L12 21.2 4 16.7V7.3z"/><path d="M4 7.3l8 4.5 8-4.5M12 11.8v9.4"/>',
  grid: '<path d="M3.5 3.5h17v17h-17zM3.5 9.2h17M3.5 14.8h17M9.2 3.5v17M14.8 3.5v17"/>',
  layers: '<path d="m12 3 9 5-9 5-9-5z"/><path d="m3 13 9 5 9-5"/><path d="m3 17.5 9 3.5 9-3.5" opacity=".6"/>',
  arrows: '<path d="M4 8h11M11.5 5.5 14 8l-2.5 2.5M20 16H9M12.5 13.5 10 16l2.5 2.5"/>',
  wind: '<path d="M3 7.5h9a2.4 2.4 0 1 0-2.3-3"/><path d="M3 12h12.5a2.2 2.2 0 1 1-2 2.8"/><path d="M3 16.5h7.5"/>',
  pulse: '<path d="M2.5 12h4l2.2-6 3.6 12 2.4-8 2 2h5"/>',
  droplet: '<path d="M12 3.2c3.4 4 5.6 6.7 5.6 9.6A5.6 5.6 0 0 1 6.4 12.8c0-2.9 2.2-5.6 5.6-9.6z"/>',
  probe: '<circle cx="12" cy="12" r="2.4"/><path d="M12 2.5v3.2M12 18.3v3.2M2.5 12h3.2M18.3 12h3.2M5.4 5.4l2.3 2.3M16.3 16.3l2.3 2.3M18.6 5.4l-2.3 2.3M7.7 16.3l-2.3 2.3"/>',
  download: '<path d="M12 3.5V15M7 10l5 5 5-5"/><path d="M4 14.5v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4"/>',
  upload: '<path d="M12 15V3.5M7 8.5l5-5 5 5"/><path d="M4 14.5v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4"/>',
  undo: '<path d="M4 9h9.5a6 6 0 0 1 0 12H8"/><path d="M8 5 4 9l4 4"/>',
  redo: '<path d="M20 9h-9.5a6 6 0 0 0 0 12H16"/><path d="m16 5 4 4-4 4"/>',
  reset: '<path d="M4 12a8 8 0 1 0 2.4-5.7"/><path d="M4 3.5V8h4.5"/>',
  help: '<circle cx="12" cy="12" r="8.5"/><path d="M9.8 9.5a2.3 2.3 0 1 1 3.2 2.1c-.6.3-1 .8-1 1.5v.4M12 16.5v.3"/>',
  chevron: '<path d="m9 6 6 6-6 6"/>',
  chevronDown: '<path d="m6 9 6 6 6-6"/>',
  x: '<path d="M6 6l12 12M18 6 6 18"/>',
  check: '<path d="m5 12.5 4.5 4.5L19 7.5"/>',
  warn: '<path d="M12 4 2.8 19.5h18.4z"/><path d="M12 10v4.5M12 17.2v.3"/>',
  sliders: '<path d="M4 8h9M17 8h3M4 16h4M12 16h8"/><circle cx="15" cy="8" r="2"/><circle cx="10" cy="16" r="2"/>',
  camera: '<path d="M4 8h3l1.6-2.5h6.8L17 8h3a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V9a1 1 0 0 1 1-1z"/><circle cx="12" cy="13" r="3.4"/>',
  sparkle: '<path d="M12 3v4M12 17v4M3 12h4M17 12h4M5.6 5.6l2.8 2.8M15.6 15.6l2.8 2.8M5.6 18.4l2.8-2.8M15.6 8.4l2.8-2.8"/>',
  code: '<path d="m8 8-4 4 4 4M16 8l4 4-4 4M13.5 5l-3 14"/>',
  magnet: '<path d="M5 4v8a7 7 0 0 0 14 0V4"/><path d="M5 9h4M15 9h4"/>',
  orbit: '<circle cx="12" cy="12" r="2"/><ellipse cx="12" cy="12" rx="9" ry="4" transform="rotate(-24 12 12)"/><circle cx="19.4" cy="8.2" r="1.6" fill="currentColor"/>',
  chain: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1"/><path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>',
  fan: '<circle cx="12" cy="12" r="2"/><path d="M12 10c0-3 1.6-5 4-5 0 3-1.6 5-4 5zM10 13.5c-2.6 1.6-5 1.4-6.4-.6 2.6-1.6 5-1.4 6.4.6zM13.9 14.2c.4 3-.6 5.4-3 6.2-.4-3 .6-5.4 3-6.2z"/>',
  shield: '<path d="M12 3 4.5 6v5.5c0 4.6 3.2 8.2 7.5 9.5 4.3-1.3 7.5-4.9 7.5-9.5V6z"/>',
  bolt: '<path d="M13 3 6 13h5l-1 8 7-10h-5z"/>',
  snow: '<path d="M12 2.5v19M3.8 7.2l16.4 9.6M20.2 7.2 3.8 16.8"/>',
  waves: '<path d="M2 8c2.5-2.4 5-2.4 7.5 0s5 2.4 7.5 0 3.5-1.6 5-.4"/><path d="M2 14c2.5-2.4 5-2.4 7.5 0s5 2.4 7.5 0 3.5-1.6 5-.4"/>',
  target: '<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="4.6"/><circle cx="12" cy="12" r="1.2" fill="currentColor"/>',
  link: '<path d="M9.5 14.5 14.5 9.5"/><path d="M11 6.5 13 4.5a3.5 3.5 0 0 1 5 5L16 11.5M13 17.5 11 19.5a3.5 3.5 0 0 1-5-5L8 12.5"/>',
  save: '<path d="M5 4.5h10L19.5 9v10.5H5z"/><path d="M8.5 4.5v5h7v-5M8.5 19.5v-6h7v6"/>',
};

export function Icon(name, size = 16) {
  const body = PATHS[name] || PATHS.dot || '<circle cx="12" cy="12" r="3"/>';
  return `<svg viewBox="0 0 24 24" width="${size}" height="${size}" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
}

export function IconEl(name, size = 16, cls = '') {
  return El('span', { class: `ico ${cls}`.trim(), html: Icon(name, size) });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FIELD WIDGETS
//------------------------------------------------------------------------------------------------------------------------

const DecimalsOf = (step) => (String(step).includes('.') ? String(step).split('.')[1].length : 0);

export function RangeField({ Label, Value, Min = 0, Max = 1, Step = 0.01, Unit = '', Hint = '', Format, OnInput, OnCommit }) {
  const decimals = DecimalsOf(Step);
  const show = Format || ((v) => `${(+v).toFixed(decimals)}${Unit ? ` ${Unit}` : ''}`);
  const out = El('output', {}, show(Value));
  const input = El('input', { type: 'range', min: Min, max: Max, step: Step, value: Value, 'aria-label': Label });
  const paint = () => input.style.setProperty('--fill', `${((input.value - Min) / (Max - Min || 1)) * 100}%`);
  paint();
  input.addEventListener('input', () => {
    paint();
    out.textContent = show(+input.value);
    if (OnInput) OnInput(+input.value);
  });
  input.addEventListener('change', () => OnCommit && OnCommit(+input.value));
  return El('label', { class: 'field' }, El('span', {}, Label), out, input, Hint ? El('small', {}, Hint) : null);
}

export function NumberField({ Label, Value, Min = -Infinity, Max = Infinity, Step = 0.1, Unit = '', OnCommit }) {
  const input = El('input', { type: 'number', class: 'num', value: +(+Value).toFixed(4), step: Step, 'aria-label': Label });
  input.addEventListener('change', () => {
    const v = Math.min(Max, Math.max(Min, Number(input.value)));
    if (Number.isFinite(v) && OnCommit) OnCommit(v);
  });
  return El('label', { class: 'field inline' }, El('span', {}, Label), El('span', { class: 'vec3' }, El('label', {}, input), Unit ? El('small', {}, Unit) : null));
}

export function SelectField({ Label, Options, Value, OnChange }) {
  const select = El('select', { 'aria-label': Label });
  for (const option of Options) {
    const opt = typeof option === 'string' ? { Id: option, Label: option } : option;
    const node = El('option', { value: opt.Id }, opt.Label);
    if (String(opt.Id) === String(Value)) node.selected = true;
    select.append(node);
  }
  select.addEventListener('change', () => OnChange(select.value));
  return El('label', { class: 'field inline' }, El('span', {}, Label), select);
}

export function Segmented({ Options, Value, OnChange, Small = false }) {
  const wrap = El('div', { class: `segmented${Small ? ' small' : ''}`, role: 'tablist' });
  const buttons = [];
  for (const option of Options) {
    const opt = typeof option === 'string' ? { Id: option, Label: option } : option;
    const button = El('button', { type: 'button', class: opt.Id === Value ? 'active' : '', title: opt.Title || null }, opt.Label);
    button.addEventListener('click', () => {
      for (const b of buttons) b.classList.toggle('active', b === button);
      OnChange(opt.Id);
    });
    buttons.push(button);
    wrap.append(button);
  }
  return wrap;
}

export function Toggle({ Label, Value, Hint = '', OnChange }) {
  const input = El('input', { type: 'checkbox', 'aria-label': Label });
  input.checked = !!Value;
  input.addEventListener('change', () => OnChange(input.checked));
  return El('label', { class: 'toggle-row' }, El('span', {}, Label, Hint ? El('small', {}, Hint) : null), input);
}

export function VectorField({ Label, Value, Step = 0.1, Unit = '', OnCommit }) {
  const v = Value.slice();
  const inputs = [];
  const row = El('div', { class: 'vec3' });
  ['X', 'Y', 'Z'].forEach((axis, i) => {
    const input = El('input', { type: 'number', step: Step, value: +(+v[i]).toFixed(3), 'aria-label': `${Label} ${axis}` });
    input.addEventListener('change', () => {
      const n = Number(input.value);
      if (!Number.isFinite(n)) return;
      v[i] = n;
      OnCommit(v.slice());
    });
    inputs.push(input);
    row.append(El('label', {}, El('i', {}, axis), input));
  });
  return El('div', { class: 'rows' }, El('div', { class: 'vec3-head' }, El('span', {}, Label), Unit ? El('small', {}, Unit) : null), row);
}

export function TextField({ Label, Value, Placeholder = '', Multiline = false, OnCommit, Class = '' }) {
  const input = Multiline ? El('textarea', { placeholder: Placeholder, rows: 4 }) : El('input', { type: 'text', value: Value, placeholder: Placeholder });
  if (Multiline) input.value = Value;
  input.addEventListener('change', () => OnCommit(input.value));
  return El('label', { class: `field stack ${Class}`.trim() }, El('span', {}, Label), input);
}

export function Rows(pairs) {
  return El('div', { class: 'rows' }, ...pairs.flatMap(([key, value]) => (key === null ? [El('div', {}, El('span', { class: 'verdict' }, value))] : [El('div', {}, El('span', {}, key), El('b', {}, value))])));
}

export function Metrics(pairs) {
  return El('div', { class: 'metrics' }, ...pairs.map(([value, label]) => El('div', {}, El('b', {}, value), El('small', {}, label))));
}

export function Section(title, extra = null) {
  return El('h4', { class: 'section-head' }, El('span', {}, title), extra);
}

export function Panel(...children) {
  return El('div', { class: 'panel' }, ...children.flat());
}

export function IconButton(name, title, onClick, cls = 'mini-btn') {
  const button = El('button', { type: 'button', class: cls, title, 'aria-label': title, html: Icon(name, 14) });
  button.addEventListener('click', (event) => {
    event.stopPropagation();
    onClick(event);
  });
  return button;
}

export function Chip(label, tone = '') {
  return El('span', { class: `chip ${tone}`.trim() }, label);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TOAST
//------------------------------------------------------------------------------------------------------------------------

let toastTimer = 0;
export function Toast(text) {
  const node = $('Toast');
  if (!node) return;
  node.textContent = text;
  node.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => node.classList.remove('show'), 2300);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CANVAS INSTRUMENTS
//------------------------------------------------------------------------------------------------------------------------

// Sizes a canvas to its CSS box at device pixel ratio and returns a 2D context already scaled to CSS pixels.
export function FitCanvas(canvas, height) {
  const width = canvas.clientWidth || canvas.parentElement?.clientWidth || 260;
  const h = height ?? (canvas.clientHeight || 90);
  const dpr = Math.min(devicePixelRatio || 1, 2);
  if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(h * dpr);
  }
  if (height) canvas.style.height = `${height}px`;
  const context = canvas.getContext('2d');
  context.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { Context: context, Width: width, Height: h };
}

export function FormatNumber(value, decimals = 2, unit = '') {
  if (!Number.isFinite(value)) return '—';
  const abs = Math.abs(value);
  const digits = abs >= 1000 ? 0 : abs >= 100 ? 1 : decimals;
  return `${value.toFixed(digits)}${unit ? ` ${unit}` : ''}`;
}
