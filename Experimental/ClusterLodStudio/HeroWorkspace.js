// Hero Workspace: the Outliner tree of one hero and the centre pane it shows. Items are Metadata, Source, Geometry,
// Icon, UV, SDF and Cluster LOD. Cluster LOD is the nested viewport workspace; Metadata and SDF cover it with their own
// panes; UV and Geometry open their overlays. Metadata is stored per source in this browser.
const $ = (id) => document.getElementById(id);
const STORE_KEY = 'frontier.clusterLodStudio.heroes';
export const CATEGORIES = ['Uncategorised', 'Organic', 'Architecture', 'Terrain', 'Prop', 'Abstract', 'Character'];
const TABS = { cluster: 'Cluster LOD', source: 'Cluster LOD', icon: 'Icon', metadata: 'Metadata', sdf: 'SDF', uv: 'Cluster LOD', geometry: 'Cluster LOD' };
const PANES = { metadata: 'HeroPanelMetadata', sdf: 'HeroPanelSdf' };

function readStore() {
  try { return JSON.parse(localStorage.getItem(STORE_KEY)) || {}; } catch (error) { return {}; }
}
function writeStore(store) {
  try { localStorage.setItem(STORE_KEY, JSON.stringify(store)); } catch (error) { /* storage full or blocked */ }
}

export function createHeroWorkspace({ currentSource, getGeometry, onSelect, onMetadataChanged }) {
  let store = readStore();
  let active = 'cluster';

  function metadataFor(key) {
    return { name: '', category: 'Uncategorised', tags: '', info: '', ...(store[key] || {}) };
  }
  function title() {
    const source = currentSource();
    return metadataFor(source.key).name.trim() || source.name;
  }
  function fillForm() {
    const source = currentSource();
    const m = metadataFor(source.key);
    $('MetaName').value = m.name;
    $('MetaName').placeholder = source.name;
    $('MetaCategory').value = m.category;
    $('MetaTags').value = m.tags;
    $('MetaInfo').value = m.info;
    $('MetaNote').textContent = `Saved in this browser for ${source.name}. Icon and source keys are unchanged.`;
  }
  function refresh() {
    const source = currentSource();
    const m = metadataFor(source.key);
    $('HeroName').textContent = title();
    const tags = m.tags.split(',').map((t) => t.trim()).filter(Boolean);
    $('HeroCategory').textContent = [m.category, ...tags.slice(0, 3)].join(' · ');
    const geometry = getGeometry();
    // SDF applies to closed surfaces only; open heroes hide the part.
    $('TreeSdf').hidden = !!geometry && !geometry.closed;
    if (document.activeElement && document.activeElement.closest && document.activeElement.closest('#HeroPanelMetadata')) return;
    fillForm();
  }
  function save() {
    const source = currentSource();
    store[source.key] = {
      name: $('MetaName').value.trim(),
      category: $('MetaCategory').value,
      tags: $('MetaTags').value,
      info: $('MetaInfo').value,
    };
    writeStore(store);
    $('HeroName').textContent = title();
    const tags = $('MetaTags').value.split(',').map((t) => t.trim()).filter(Boolean);
    $('HeroCategory').textContent = [$('MetaCategory').value, ...tags.slice(0, 3)].join(' · ');
    onMetadataChanged();
  }
  function highlight(part) {
    for (const button of document.querySelectorAll('[data-part]')) {
      const on = button.dataset.part === part;
      button.classList.toggle('active', on);
      button.setAttribute('aria-current', on ? 'true' : 'false');
    }
  }
  function showPane(part) {
    const shown = PANES[part] || null;
    for (const id of Object.values(PANES)) $(id).hidden = id !== shown;
    $('CentreTab').textContent = TABS[part] || 'Cluster LOD';
  }
  function select(part) {
    active = part;
    highlight(part);
    if (PANES[part]) showPane(part); else showPane('cluster');
    onSelect(part);
  }

  for (const button of document.querySelectorAll('[data-part]')) {
    button.addEventListener('click', () => select(button.dataset.part));
  }
  for (const id of ['MetaName', 'MetaTags', 'MetaInfo']) $(id).addEventListener('input', save);
  $('MetaCategory').addEventListener('change', save);
  $('MetaCategory').innerHTML = CATEGORIES.map((c) => `<option value="${c}">${c}</option>`).join('');
  highlight(active);

  return {
    refresh, select, title,
    get active() { return active; },
    // Leaves the current part in place and only returns the centre pane to the cluster viewport.
    returnToCluster() { if (PANES[active]) select('cluster'); },
  };
}
