/* ════════════════════════════════════════════════════════════════════════════════════════════
   PROPERTY SHEET
   Builds the cards for one node straight from its type schema, so the docked inspector and the
   floating billboard popup are literally the same surface at two sizes. Every panel registers
   itself for sync, so a value changed in the popup moves in the dock on the same frame.
   ════════════════════════════════════════════════════════════════════════════════════════════ */
import { TYPES, typeOf, isFolder } from './world.js';
import { el, slider, toggle, vec3, colorChip, dropdown, beginRename, swatchRow, repaintSliders } from './kit.js';
import { ic, folderIcon } from './icons.js';
import { bus } from './bus.js';
import { CUSTOM_PANELS } from './panels/index.js';
import { pillToggle, specList } from './panels/controls.js';

export const TINTS = ['#c9a24b', '#ef5350', '#f59e0b', '#22c55e', '#3b82f6', '#8b5cf6', '#ec4899', '#9aa0a6'];

const panels = new Set();
bus.on('propchange', ({ node, src }) => panels.forEach(p => { if (p !== src && p.node === node) p.sync(); }));

export function setProp(node, key, value, src) {
  node.props[key] = value;
  bus.emit('propchange', { node, key, value, src });
}

function row(label, ctl, { wide = false } = {}) {
  const r = el('div', `prow${wide ? ' wide' : ''}`);
  r.append(el('span', 'pl', label), ctl);
  return r;
}

/* one control for one schema entry */
function control(node, def, panel) {
  const val = node.props[def.k];
  switch (def.kind) {
    case 'slider': {
      const c = slider({
        min: def.min, max: def.max, value: val ?? def.def, dec: def.dec, unit: def.unit,
        hi: def.hi, thin: panel.compact,
        onInput: v => setProp(node, def.k, v, panel),
      });
      return c;
    }
    case 'switch': return toggle(val, v => setProp(node, def.k, v, panel));
    case 'vec3': return vec3(val || [0, 0, 0], {
      step: def.step, disabled: node.locked, dec: 2, onChange: v => setProp(node, def.k, v, panel),
    });
    case 'color':
      return def.swatches
        ? swatchRow(val, TINTS, v => setProp(node, def.k, v, panel))
        : colorChip(val, v => setProp(node, def.k, v, panel));
    case 'select': return dropdown(def.options, val, v => setProp(node, def.k, v, panel));
    case 'readout': {
      const s = el('span', 'val', readout(node, def.k));
      s._set = () => { s.textContent = readout(node, def.k); };
      return s;
    }
    default: return el('span', 'val', String(val));
  }
}

const readout = (node, key) => {
  if (key === 'children') {
    const kids = node.kids.length;
    const deep = (function count(l) { return l.reduce((a, n) => a + 1 + count(n.kids), 0); })(node.kids);
    return `${kids} direct · ${deep} total`;
  }
  return String(node.props[key] ?? '—');
};

/* ── the sheet ─────────────────────────────────────────────────────────────────────────────── */
export function buildSheet(node, { compact = false, onDirty = () => {} } = {}) {
  const host = el('div', 'sheet');
  const bound = [];
  const extra = [];                                   /* bespoke panels keep themselves in step */
  const panel = {
    node, compact,
    sync: () => { bound.forEach(b => b.ctl._set && b.ctl._set(node.props[b.def.k])); extra.forEach(f => f()); },
  };
  panels.add(panel);

  const t = typeOf(node);

  /* a type with an instrument gets it first, and keeps whichever schema groups it did not claim */
  const custom = CUSTOM_PANELS[node.type];
  const owned = new Set(custom ? custom.owns || [] : []);
  let customEl = null;
  host._dispose = () => { panels.delete(panel); customEl && customEl._dispose && customEl._dispose(); };

  /* identity — the popup carries its own header, so the strip is only for the dock */
  if (!compact) buildIdentity();
  function buildIdentity() {
  const ident = el('div', 'ident');
  ident.innerHTML =
    `<div class="glyph">${ic(isFolder(node) ? folderIcon(node.name) : t.icon, { size: 18, color: node.props.tint || t.color })}</div>
     <div class="who"><span class="name-edit" title="Double click to rename">${node.name}</span>
       <div class="kind">${t.label}${node.locked ? ' · locked' : ''}${node.vis ? '' : ' · hidden'}</div></div>`;
  const quick = el('div', 'row-actions');
  quick.style.cssText = 'display:flex;gap:6px;';
  const visBtn = el('button', `iconbtn ghost${node.vis ? '' : ' on'}`, ic(node.vis ? 'eye' : 'eyeoff', { size: 13 }));
  visBtn.title = 'Visibility  (H)';
  visBtn.onclick = () => { node.vis = !node.vis; bus.emit('treechange'); onDirty(); };
  const lockBtn = el('button', `iconbtn ghost${node.locked ? ' on' : ''}`, ic(node.locked ? 'lock' : 'unlock', { size: 13 }));
  lockBtn.title = 'Lock  (L)';
  lockBtn.onclick = () => { node.locked = !node.locked; bus.emit('treechange'); onDirty(); };
  quick.append(visBtn, lockBtn);
  ident.appendChild(quick);
  ident.querySelector('.name-edit').ondblclick = e => {
    beginRename(node, e.currentTarget, () => { bus.emit('treechange'); onDirty(); });
  };
  host.appendChild(ident);
  }

  if (custom) {
    host.classList.add('bespoke');            /* the generic cards fall in line with the instrument */
    customEl = custom.build(node, {
      compact,
      setProp: (n, k, v) => setProp(n, k, v, panel),
      register: fn => extra.push(fn),
      onDirty,
    });
    host.appendChild(customEl);
  }

  /* schema cards */
  (t.groups || []).filter(g => !owned.has(g.title)).forEach((group, gi) => {
    const card = el('div', 'pcard');
    const head = el('h4', null, `${group.title}<span class="cw">${ic('chevdown', { size: 12 })}</span>`);
    const body = el('div', 'pbody');
    head.onclick = () => { card.classList.toggle('shut'); if (!card.classList.contains('shut')) repaintSliders(card); };
    card.append(head, body);
    group.props.forEach(def => {
      const ctl = control(node, def, panel);
      bound.push({ def, ctl });
      body.appendChild(row(def.label, ctl));
    });
    host.appendChild(card);
    if (compact && gi > 2) card.classList.add('shut');
  });

  /* per-object state card for anything the schema does not own */
  if (!isFolder(node)) {
    const card = el('div', 'pcard mp-state');
    card.innerHTML = `<h4>Object<span class="cw">${ic('chevdown', { size: 12 })}</span></h4>`;
    const body = el('div', 'pbody');
    card.appendChild(body);
    card.querySelector('h4').onclick = () => card.classList.toggle('shut');
    /* state as words with a light beside them, not a stack of switches */
    const pills = el('div', 'mp-tags');
    pills.append(
      pillToggle('VISIBLE', node.vis, v => { node.vis = v; bus.emit('treechange'); onDirty(); }),
      pillToggle('LOCKED', node.locked, v => { node.locked = v; bus.emit('treechange'); onDirty(); }),
      pillToggle('DYNAMIC', node.dynamic, v => { node.dynamic = v; bus.emit('treechange'); }),
    );
    if (Array.isArray(node.props.pos)) {
      pills.appendChild(pillToggle('PHYSICS', !!node.physics, v => {
        node.physics = v; node.vel = 0; bus.emit('treechange'); bus.emit('physicschange');
      }));
    }
    body.append(pills, specList([
      ['type', TYPES[node.type].label],
      ['id', `#${String(node.id).padStart(3, '0')}`],
    ]));
    host.appendChild(card);
  }

  /* notes */
  const notes = el('div', 'pcard');
  notes.innerHTML = `<h4>Notes<span class="cw">${ic('chevdown', { size: 12 })}</span></h4>`;
  const nbody = el('div', 'pbody');
  const ta = el('textarea', 'notes');
  ta.placeholder = `Notes for ${node.name}…`;
  ta.spellcheck = false;
  ta.value = node.notes || '';
  ta.oninput = () => { node.notes = ta.value; };
  ta.onkeydown = e => e.stopPropagation();
  nbody.appendChild(ta);
  notes.appendChild(nbody);
  notes.querySelector('h4').onclick = e => { if (e.target.closest('textarea')) return; notes.classList.toggle('shut'); };
  if (compact) notes.classList.add('shut');
  host.appendChild(notes);

  return host;
}
