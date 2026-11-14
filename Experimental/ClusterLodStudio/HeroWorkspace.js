// Hero Workspace: the Outliner tree on the right. The hero row is the source (its centre pane lists the presets and
// imports). Its children are the workspaces: Geometry Editor, Cluster LOD Studio, Icon, UV Workspace and SDF Baker.
// Metadata for the hero is edited in the left Inspector and saved per source in this browser.
const $ = (id) => document.getElementById(id);
const HEROES_KEY = 'frontier.clusterLodStudio.heroes';
export const CATEGORIES = ['Uncategorised', 'Organic', 'Architecture', 'Terrain', 'Prop', 'Abstract', 'Character'];

function readHeroes() {
  try { return JSON.parse(localStorage.getItem(HEROES_KEY)) || {}; } catch (error) { return {}; }
}
function writeHeroes(heroes) {
  try { localStorage.setItem(HEROES_KEY, JSON.stringify(heroes)); } catch (error) { /* storage full or blocked */ }
}

export function createHeroWorkspace({ currentSource, getGeometry, getIcon, onSelect, onMetadataChanged }) {
  let heroes = readHeroes();
  let active = 'geometry';

  function metadataFor(key) {
    return { name: '', category: 'Uncategorised', tags: '', info: '', ...(heroes[key] || {}) };
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
    const icon = getIcon ? getIcon() : null;
    $('HeroThumb').src = icon || '';
    $('HeroThumb').hidden = !icon;
    const geometry = getGeometry();
    // SDF applies to closed surfaces only; open heroes hide the workspace row.
    $('TreeSdf').hidden = !!geometry && !geometry.closed;
    if (document.activeElement && document.activeElement.closest && document.activeElement.closest('#PanelHero')) return;
    fillForm();
  }
  function save() {
    const source = currentSource();
    heroes[source.key] = {
      name: $('MetaName').value.trim(),
      category: $('MetaCategory').value,
      tags: $('MetaTags').value,
      info: $('MetaInfo').value,
    };
    writeHeroes(heroes);
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
  function setExpanded(open) {
    $('TreeChildren').hidden = !open;
    document.querySelector('[data-tree-toggle]').setAttribute('aria-expanded', open ? 'true' : 'false');
  }
  function select(part) {
    active = part;
    highlight(part);
    onSelect(part);
  }

  for (const button of document.querySelectorAll('[data-part]')) {
    button.addEventListener('click', () => {
      if (button.dataset.part === 'source') setExpanded(true);
      select(button.dataset.part);
    });
  }
  for (const button of document.querySelectorAll('[data-open-part]')) {
    button.addEventListener('click', () => select(button.dataset.openPart));
  }
  document.querySelector('[data-tree-toggle]').addEventListener('click', (event) => {
    event.stopPropagation();
    setExpanded($('TreeChildren').hidden);
  });
  for (const id of ['MetaName', 'MetaTags', 'MetaInfo']) $(id).addEventListener('input', save);
  $('MetaCategory').addEventListener('change', save);
  $('MetaCategory').innerHTML = CATEGORIES.map((c) => `<option value="${c}">${c}</option>`).join('');
  highlight(active);

  return {
    refresh, select, title,
    get active() { return active; },
  };
}
