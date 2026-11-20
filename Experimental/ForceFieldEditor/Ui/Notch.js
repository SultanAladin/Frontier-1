/*============================================================================================================================================
                                                          NOTCH
============================================================================================================================================*/
// 🧩 The Project-Zero top notch: document actions, the generator catalogue summary, the keyboard control sheet and the export/import paths, all
// keyboard reachable and announced through the shared toast.

import { El, Icon, IconEl, Toast, TrapezoidSvg, IconButton } from './Kit.js';
import { FORCE_LAWS, ForceDomains } from '../Engine/ForceFieldCatalog.js';
import { DocumentToJson, DocumentFromJson, DocumentToCascadeText, DocumentToCsv } from '../Engine/FieldEvaluator.js';

export function CreateNotch(state, actions) {
  const shell = El('div', { class: 'notch-shell' });
  const handle = El('button', { class: 'notch-handle', type: 'button', 'aria-expanded': 'false', title: 'Open the Control Centre (Ctrl+K)' });
  handle.innerHTML = `${TrapezoidSvg({ width: 240, height: 30, slant: 14, direction: 'up', fill: '#1b1b1b' })}<span>${Icon('sparkle', 12)} ForceField Studio</span>`;
  const shade = El('div', { class: 'notch-shade' });

  const close = () => {
    shell.classList.remove('open');
    handle.setAttribute('aria-expanded', 'false');
  };
  const open = () => {
    shell.classList.add('open');
    handle.setAttribute('aria-expanded', 'true');
    renderSheet();
  };
  handle.addEventListener('click', () => (shell.classList.contains('open') ? close() : open()));
  shade.addEventListener('click', (event) => {
    if (event.target === shade) close();
  });
  shell.append(shade);
  shell.append(handle);

  const sheet = El('div', { class: 'control-sheet' });
  shade.append(sheet);

  function renderSheet() {
    sheet.innerHTML = '';
    const doc = state.Document();
    const lawCounts = {};
    for (const source of doc.Sources) lawCounts[source.Law] = (lawCounts[source.Law] || 0) + 1;
    sheet.append(
      El('header', {}, El('b', {}, 'ForceField Studio'), IconButton('x', 'Close (Esc)', close)),
      El('div', { class: 'rows', style: { fontSize: '12px', gap: '7px', marginBottom: '16px' } },
        El('div', {}, El('span', {}, 'Generators available'), El('b', {}, `${FORCE_LAWS.length} (19 laws + formula)`)),
        El('div', {}, El('span', {}, 'Sources in the document'), El('b', {}, String(doc.Sources.length))),
        El('div', {}, El('span', {}, 'Bounded domain'), El('b', {}, `${doc.Domain.Min.map((v) => v.toFixed(0)).join(' / ')} → ${doc.Domain.Max.map((v) => v.toFixed(0)).join(' / ')} m`)),
        El('div', {}, El('span', {}, 'Simulation clock'), El('b', {}, `${state.Time().toFixed(2)} s ${state.Playing() ? '(running)' : '(paused)'}`)),
      ),
      El('div', { class: 'segmented', style: { marginBottom: '14px' } },
        ...Object.values(ForceDomains).map((domain) => El('button', { type: 'button', class: 'active', style: { pointerEvents: 'none', background: 'transparent', color: domain.Color, fontWeight: '500' } }, `${domain.Label} · ${FORCE_LAWS.filter((l) => l.Domain === domain.Id).length}`)),
      ),
      El('div', { class: 'keys' },
        ...[
          ['Play / pause', 'Space'],
          ['Rewind clock', 'R'],
          ['Frame the domain', 'F'],
          ['Add a force generator', 'A'],
          ['Cycle evaluated quantity', 'Q'],
          ['Undo / redo', 'Ctrl+Z / Ctrl+Shift+Z'],
          ['Reset studio scene', 'Ctrl+Shift+R'],
          ['Export document', 'Ctrl+S'],
          ['Toggle telemetry', 'T'],
          ['Search generators', '/'],
          ['Orbit', 'Drag'],
          ['Pan', 'Right-drag'],
          ['Dolly', 'Wheel'],
          ['Sample the field', 'Probe then click'],
        ].map(([action, key]) => El('div', {}, El('span', {}, action), El('kbd', {}, key))),
      ),
      El('div', { class: 'hop', style: { marginTop: '18px' } },
        El('button', { type: 'button', class: 'tool', onclick: () => actions.Export('json') }, IconEl('download', 13), 'Export JSON'),
        El('button', { type: 'button', class: 'tool', onclick: () => actions.Export('cascade') }, IconEl('download', 13), 'Export TOML'),
        El('button', { type: 'button', class: 'tool', onclick: () => actions.Export('csv') }, IconEl('download', 13), 'Export CSV'),
        El('button', { type: 'button', class: 'tool', onclick: () => actions.Import() }, IconEl('upload', 13), 'Import'),
      ),
      El('p', {}, 'Every generator is a bounded source. The document round-trips through JSON, and exports carry the domain, each source envelope, duration, motion and full parameter block so a pipeline can rebuild the field exactly.'),
    );
  }

  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && shell.classList.contains('open')) close();
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'k') {
      event.preventDefault();
      shell.classList.contains('open') ? close() : open();
    }
  });

  return {
    Node: shell,
    Open: open,
    Close: close,
    IsOpen: () => shell.classList.contains('open'),
  };
}
