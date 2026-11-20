/*============================================================================================================================================
                                                        OUTLINER
============================================================================================================================================*/
// 🧩 Force-source outliner: one row per generator, grouped by runtime field, with visibility, solo/mute, reorder, duplicate and delete, a search
// filter and the generator picker that adds new sources. Selecting a row drives the inspector and highlights the source in both viewports.

import { El, Icon, IconEl, IconButton, Toast } from './Kit.js';
import { FORCE_LAWS, ForceDomains, LawById, DomainColor } from '../Engine/ForceFieldCatalog.js';

const DomainOrder = ['VectorField', 'ChannelField', 'TurbulenceField'];

export function CreateOutliner(state) {
  const root = El('section', { class: 'dock left', 'aria-label': 'Force outliner' });
  const tabStrip = El('div', { class: 'tab-strip' },
    El('button', { class: 'document-tab active', type: 'button' }, IconEl('layers', 13), 'Outliner'),
  );

  const heading = El('div', { class: 'outliner-heading' }, El('h1', {}, 'Forces'), El('small', {}, 'field sources'));
  const search = El('input', { type: 'search', placeholder: 'Search generators, laws…', 'aria-label': 'Search force sources' });
  const addButton = El('button', { type: 'button', class: 'add-btn', title: 'Add a force generator (A)' }, IconEl('plus', 14), 'Add force');
  const addRow = El('div', { class: 'add-row' }, addButton, El('label', { class: 'search-wrap' }, IconEl('search', 14), search));
  const stats = El('div', { class: 'stats' });
  const scroll = El('div', { class: 'outliner-scroll', role: 'listbox', 'aria-label': 'Force sources' });
  const menu = El('div', { class: 'add-menu', hidden: true });

  root.append(tabStrip, heading, addRow, stats, scroll, menu);

  let filter = '';
  let menuDomain = 'all';

  // ---------------------------------------------------------------- picker
  function closeMenu() {
    menu.hidden = true;
  }

  function openMenu() {
    menu.hidden = false;
    renderMenu();
  }

  function renderMenu() {
    menu.innerHTML = '';
    const header = El('header', {}, El('b', {}, 'Add a force generator'), IconButton('x', 'Close', closeMenu));
    const domainTabs = El('div', { class: 'segmented small' });
    for (const id of ['all', ...DomainOrder]) {
      const label = id === 'all' ? 'All' : ForceDomains[id].Label;
      const button = El('button', { type: 'button', class: id === menuDomain ? 'active' : '' }, label);
      button.addEventListener('click', () => {
        menuDomain = id;
        renderMenu();
      });
      domainTabs.append(button);
    }
    menu.append(header, domainTabs);
    const list = El('div', { class: 'add-list' });
    for (const domainId of DomainOrder) {
      if (menuDomain !== 'all' && menuDomain !== domainId) continue;
      const laws = FORCE_LAWS.filter((law) => law.Domain === domainId);
      list.append(El('div', { class: 'menu-group', style: { color: DomainColor(domainId) } }, `${ForceDomains[domainId].Label.toUpperCase()} · ${laws.length}`));
      for (const law of laws) {
        const item = El('button', { type: 'button', class: 'menu-item', title: law.Summary },
          El('span', { class: 'entity-icon', html: law.Glyph, style: { color: DomainColor(domainId) } }),
          El('span', {}, El('b', {}, law.Name), El('small', {}, law.Summary)),
          El('small', { style: { color: '#6e6e6e' } }, law.Channel === 'velocity' ? 'm/s' : law.Channel === 'acceleration' ? 'm/s²' : '1/s'),
        );
        item.addEventListener('click', () => {
          const source = state.AddSource(law.Id, { Position: [0, 0, 0] });
          closeMenu();
          Toast(`${law.Name} added — drag its envelope card to place it`);
          state.Select(source.Id);
        });
        list.append(item);
      }
    }
    menu.append(list);
  }

  addButton.addEventListener('click', () => (menu.hidden ? openMenu() : closeMenu()));
  search.addEventListener('input', () => {
    filter = search.value.trim().toLowerCase();
    renderList();
  });
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && !menu.hidden) closeMenu();
  });

  // ---------------------------------------------------------------- rows
  function matches(source) {
    if (!filter) return true;
    const law = LawById[source.Law];
    return `${source.Name} ${law ? `${law.Name} ${law.Domain} ${law.Summary}` : ''}`.toLowerCase().includes(filter);
  }

  function rowFor(source) {
    const law = LawById[source.Law];
    const domain = law ? law.Domain : 'VectorField';
    const row = El('div', {
      class: `entity${source.Id === state.Selection() ? ' active' : ''}${source.Enabled ? '' : ' off'}`,
      role: 'option',
      tabindex: '0',
      'aria-selected': source.Id === state.Selection() ? 'true' : 'false',
      dataset: { id: source.Id },
      title: law ? `${law.Name} — ${law.Summary}` : source.Law,
    });
    const icon = El('span', { class: 'entity-icon', html: law ? law.Glyph : Icon('wind', 18), style: { color: DomainColor(domain) } });
    const label = El('div', {},
      El('b', {}, source.Name),
      El('small', {}, `${law ? law.Name : source.Law} · ${law ? law.Channel : ''}`),
    );
    const tools = El('div', { class: 'row-tools' });
    const eye = El('button', { class: `eye${source.Enabled ? '' : ' off'}`, type: 'button', title: source.Enabled ? 'Hide this source' : 'Show this source', html: Icon(source.Enabled ? 'eye' : 'eyeOff', 14) });
    eye.addEventListener('click', (event) => {
      event.stopPropagation();
      state.SetEnabled(source.Id, !source.Enabled);
    });
    const mute = El('button', { class: `mini-btn${source.Muted ? ' on' : ''}`, type: 'button', title: source.Muted ? 'Unmute' : 'Solo / mute', html: Icon('minus', 12) });
    mute.addEventListener('click', (event) => {
      event.stopPropagation();
      state.UpdateSource(source.Id, { Muted: !source.Muted });
    });
    tools.append(mute, eye);
    row.append(icon, label, tools);
    row.addEventListener('click', () => state.Select(source.Id));
    row.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        state.Select(source.Id);
      }
      if (event.key === 'Delete' || event.key === 'Backspace') state.RemoveSource(source.Id);
    });
    return row;
  }

  function renderList() {
    scroll.innerHTML = '';
    const sources = state.Sources();
    const visible = sources.filter(matches);
    for (const domainId of DomainOrder) {
      const laws = visible.filter((s) => (LawById[s.Law]?.Domain || 'VectorField') === domainId);
      if (!laws.length) continue;
      scroll.append(El('div', { class: 'collection' },
        El('span', { style: { color: DomainColor(domainId) }, html: Icon('layers', 14) }),
        El('span', {}, El('b', {}, ForceDomains[domainId].Label), El('small', {}, ForceDomains[domainId].Subtitle)),
        El('span', { class: 'count' }, String(laws.length)),
      ));
      for (const source of laws) scroll.append(rowFor(source));
    }
    if (!visible.length) {
      scroll.append(El('p', { class: 'muted-note', style: { fontSize: '11px', color: '#6e6e6e', margin: '14px 12px', lineHeight: '1.6' } },
        filter ? 'No generator matches that search.' : 'No force sources yet. Use “Add force” to place one.'));
    }
    const live = sources.filter((s) => s.Enabled && !s.Muted).length;
    stats.innerHTML = '';
    stats.append(
      El('div', { class: 'stat-tile' }, El('small', {}, 'SOURCES'), El('strong', {}, String(sources.length))),
      El('div', { class: 'stat-tile' }, El('small', {}, 'LIVE'), El('strong', {}, String(live))),
      El('div', { class: 'stat-tile' }, El('small', {}, 'DOMAINS'), El('strong', {}, String(new Set(sources.map((s) => LawById[s.Law]?.Domain)).size))),
    );
  }

  function syncSelection() {
    for (const row of scroll.querySelectorAll('.entity')) {
      const active = row.dataset.id === state.Selection();
      row.classList.toggle('active', active);
      row.setAttribute('aria-selected', active ? 'true' : 'false');
      if (active) row.scrollIntoView({ block: 'nearest' });
    }
  }

  state.Subscribe((reason) => {
    if (reason === 'document') renderList();
    if (reason === 'selection') syncSelection();
  });

  return {
    Node: root,
    Mount: () => {
      renderList();
      syncSelection();
      return root;
    },
    FocusSearch: () => search.focus(),
    OpenAddMenu: openMenu,
  };
}
