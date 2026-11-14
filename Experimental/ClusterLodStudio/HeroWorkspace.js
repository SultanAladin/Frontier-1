// Hero Workspace: the metadata of the scene's hero object, its thumbnail, and the SDF section's availability. The
// Outliner (left) lists the objects; the Inspector (right) shows every workspace with its preview. Metadata is saved
// per source in this browser.
const $ = (id) => document.getElementById(id);
const HEROES_KEY = 'frontier.clusterLodStudio.heroes';
export const CATEGORIES = ['Uncategorised', 'Organic', 'Architecture', 'Terrain', 'Prop', 'Abstract', 'Character'];

function readHeroes() {
  try { return JSON.parse(localStorage.getItem(HEROES_KEY)) || {}; } catch (error) { return {}; }
}
function writeHeroes(heroes) {
  try { localStorage.setItem(HEROES_KEY, JSON.stringify(heroes)); } catch (error) { /* storage full or blocked */ }
}

export function createHeroWorkspace({ currentSource, getGeometry, getIcon, onMetadataChanged, onOpenSource }) {
  let heroes = readHeroes();

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
    for (const id of ['HeroThumb', 'MetaThumb']) {
      $(id).src = icon || '';
      $(id).hidden = !icon;
    }
    // SDF applies to closed surfaces only; the section is hidden for open heroes.
    const geometry = getGeometry();
    $('PanelSdf').hidden = !!geometry && !geometry.closed;
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

  for (const id of ['MetaName', 'MetaTags', 'MetaInfo']) $(id).addEventListener('input', save);
  $('MetaCategory').addEventListener('change', save);
  $('MetaCategory').innerHTML = CATEGORIES.map((c) => `<option value="${c}">${c}</option>`).join('');
  $('OpenSource').addEventListener('click', onOpenSource);
  // The object row selects the hero: the Inspector returns to its first section.
  document.querySelector('.hero-row').addEventListener('click', () => { $('InspectorScroll').scrollTo({ top: 0, behavior: 'smooth' }); });

  return { refresh, title };
}
