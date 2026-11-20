/*============================================================================================================================================
                                                         FORCEFIELD STUDIO
============================================================================================================================================*/
// 🧩 Application entry: builds the Project-Zero shell (Outliner · Viewport · Inspector, top notch, bottom telemetry drawer), wires the studio
// state to every panel, owns keyboard shortcuts, import/export and the boot sequence. Panels rebuild on authored changes only; the field itself
// is repainted from the transport clock.

import { El, Icon, IconEl, Toast, $, TrapezoidSvg } from './Ui/Kit.js';
import { CreateStudioState } from './Engine/StudioState.js';
import { CreateOutliner } from './Ui/Outliner.js';
import { CreateViewportPanel } from './Ui/ViewportPanel.js';
import { CreateInspector } from './Ui/Inspector.js';
import { CreateTelemetry } from './Ui/Telemetry.js';
import { CreateNotch } from './Ui/Notch.js';
import { DocumentToJson, DocumentFromJson, DocumentToCascadeText, DocumentToCsv, DefaultDocument } from './Engine/FieldEvaluator.js';
import { EvaluatedQuantities, FORCE_LAWS } from './Engine/ForceFieldCatalog.js';

function Download(name, text, type = 'application/json') {
  const blob = new Blob([text], { type });
  const url = URL.createObjectURL(blob);
  const anchor = El('a', { href: url, download: name });
  document.body.append(anchor);
  anchor.click();
  anchor.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1200);
}

function Boot() {
  const studio = El('div', { class: 'studio' });
  const state = CreateStudioState();
  state.RestorePersisted();

  const outliner = CreateOutliner(state);
  const viewport = CreateViewportPanel(state);
  const inspector = CreateInspector(state);
  studio.append(outliner.Node, viewport.Node, inspector.Node);

  const telemetry = CreateTelemetry(state);
  const drawerHandle = El('button', { class: 'drawer-handle', type: 'button', 'aria-expanded': 'false', title: 'Toggle telemetry (T)' });
  const refreshDrawerHandle = () => {
    const open = telemetry.IsOpen();
    drawerHandle.setAttribute('aria-expanded', open ? 'true' : 'false');
    drawerHandle.innerHTML = `${TrapezoidSvg({ width: 210, height: 38, slant: 16, direction: 'up', fill: '#1b1b1b' })}<span>${Icon(open ? 'chevronDown' : 'pulse', 13)} Telemetry</span>`;
  };

  const actions = {
    Export: (format) => {
      const document_ = state.Document();
      if (format === 'json') {
        Download('frontier-forcefield.json', DocumentToJson(document_));
        Toast('Document exported as JSON');
      } else if (format === 'cascade') {
        Download('frontier-forcefield.toml', DocumentToCascadeText(document_), 'text/plain');
        Toast('Cascade TOML exported');
      } else {
        Download('frontier-forcefield-sources.csv', DocumentToCsv(document_), 'text/csv');
        Toast('Source table exported as CSV');
      }
    },
    Import: () => {
      const input = El('input', { type: 'file', accept: '.json,application/json' });
      input.addEventListener('change', async () => {
        const file = input.files && input.files[0];
        if (!file) return;
        try {
          const text = await file.text();
          const loaded = DocumentFromJson(text);
          state.Load(loaded);
          Toast(`Imported ${loaded.Sources.length} force sources`);
        } catch (error) {
          Toast(`Import failed: ${error.message}`);
        }
      });
      input.click();
    },
  };

  const notch = CreateNotch(state, actions);

  // On narrow viewports the docks collapse out of the grid (see Studio.css). The rail keeps every panel reachable as a
  // slide-in sheet instead of a dead end, which matters most for adding generators on a laptop-sized window.
  const rail = El('div', { class: 'dock-rail', role: 'group', 'aria-label': 'Panels' });
  const railButtons = {
    outliner: El('button', { type: 'button', class: 'rail-btn', title: 'Sources outliner', 'aria-expanded': 'false' }, IconEl('layers', 14), El('span', {}, 'Sources')),
    inspector: El('button', { type: 'button', class: 'rail-btn', title: 'Generator inspector', 'aria-expanded': 'false' }, IconEl('sliders', 14), El('span', {}, 'Generator')),
  };
  const sheet = El('div', { class: 'dock-sheet', hidden: '' });
  // The close control lives outside .sheet-body so swapping panels never destroys it.
  const sheetBody = El('div', { class: 'sheet-body' });
  const sheetClose = El('button', { type: 'button', class: 'sheet-close', title: 'Close panel', 'aria-label': 'Close panel' }, IconEl('x', 13));
  sheet.append(sheetClose, sheetBody);
  const sheets = { outliner: outliner.Node, inspector: inspector.Node };
  const openSheet = (which) => {
    const current = sheet.dataset.open || '';
    const next = current === which ? '' : which;
    for (const node of Object.values(sheets)) node.classList.remove('in-sheet');
    sheetBody.replaceChildren();
    sheet.dataset.open = next;
    sheet.hidden = !next;
    for (const [key, button] of Object.entries(railButtons)) button.setAttribute('aria-expanded', String(key === next));
    if (next) {
      sheets[next].classList.add('in-sheet');
      sheetBody.append(sheets[next]);
    }
  };
  for (const [key, button] of Object.entries(railButtons)) button.addEventListener('click', () => openSheet(key));
  sheetClose.addEventListener('click', () => openSheet(sheet.dataset.open));
  rail.append(railButtons.outliner, railButtons.inspector);

  drawerHandle.addEventListener('click', () => {
    telemetry.Toggle();
    refreshDrawerHandle();
  });
  refreshDrawerHandle();
  document.body.append(studio, rail, sheet, telemetry.Node, drawerHandle, notch.Node);

  outliner.Mount();
  viewport.Mount();
  inspector.Mount();

  // ---------------------------------------------------------------- keyboard
  document.addEventListener('keydown', (event) => {
    const target = event.target;
    const typing = target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.tagName === 'SELECT' || target.isContentEditable);
    const meta = event.ctrlKey || event.metaKey;
    if (meta && event.key.toLowerCase() === 'z') {
      event.preventDefault();
      if (event.shiftKey ? state.Redo() : state.Undo()) Toast(event.shiftKey ? 'Redo' : 'Undo');
      return;
    }
    if (meta && event.key.toLowerCase() === 's') {
      event.preventDefault();
      actions.Export('json');
      return;
    }
    if (meta && event.shiftKey && event.key.toLowerCase() === 'r') {
      event.preventDefault();
      state.Reset();
      Toast('Studio scene reset');
      return;
    }
    if (typing) return;
    if (event.key === ' ') {
      event.preventDefault();
      state.TogglePlay();
    } else if (event.key === 'r' || event.key === 'R') {
      state.ResetTime();
    } else if (event.key === 'f' || event.key === 'F') {
      viewport.FrameDomain?.() || null;
    } else if (event.key === 'a' || event.key === 'A') {
      outliner.OpenAddMenu();
    } else if (event.key === 'q' || event.key === 'Q') {
      const overlay = state.Overlay();
      const index = EvaluatedQuantities.findIndex((q) => q.Id === overlay.Quantity);
      const next = EvaluatedQuantities[(index + 1) % EvaluatedQuantities.length];
      state.SetOverlay({ Quantity: next.Id });
      Toast(`Quantity: ${next.Label} [${next.Unit}]`);
    } else if (event.key === 't' || event.key === 'T') {
      telemetry.Toggle();
      refreshDrawerHandle();
    } else if (event.key === '/') {
      event.preventDefault();
      outliner.FocusSearch();
    } else if (event.key === 'Delete' || event.key === 'Backspace') {
      const selected = state.Selection();
      if (selected) {
        state.RemoveSource(selected);
        Toast('Source removed');
      }
    }
  });

  // ---------------------------------------------------------------- boot overlay
  const boot = $('Boot');
  requestAnimationFrame(() => {
    if (boot) {
      boot.querySelector('small').textContent = `${FORCE_LAWS.length} generators · ${state.Sources().length} sources · ${viewport.IsWebGL() ? 'WebGL2 viewport' : 'software viewport'}`;
      boot.classList.add('done');
      setTimeout(() => boot.remove(), 420);
    }
  });

  // Expose a small, documented surface for the validation harness and console inspection.
  window.ForceFieldStudio = {
    State: state,
    Export: actions.Export,
    Import: actions.Import,
    Viewport: viewport,
    Inspector: inspector,
    Outliner: outliner,
    Telemetry: telemetry,
    Notch: notch,
    DefaultDocument,
    Rail: { Node: rail, OpenSheet: openSheet },
  };
}

if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', Boot);
else Boot();
