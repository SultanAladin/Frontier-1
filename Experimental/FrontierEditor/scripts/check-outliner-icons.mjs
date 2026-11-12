import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {build} from 'esbuild';
import {JSDOM} from 'jsdom';
const bundle=await build({entryPoints:['src.jsx'],bundle:true,write:false,format:'iife',loader:{'.css':'empty'},define:{'process.env.NODE_ENV':'"test"'},plugins:[{
 name:'svg-test-loader',setup(b){
  b.onResolve({filter:/\.svg\?(url|raw)$/},args=>({path:path.resolve(args.resolveDir,args.path),namespace:'test-svg'}));
  b.onLoad({filter:/.*/,namespace:'test-svg'},args=>{const [file,query]=args.path.split('?'),raw=fs.readFileSync(file,'utf8');return {contents:'export default '+JSON.stringify(query==='raw'?raw:'data:image/svg+xml;charset=utf-8,'+encodeURIComponent(raw)),loader:'js'}});
 }
}]});
const dom=new JSDOM('<div id="root"></div>',{url:'http://localhost:5173',runScripts:'outside-only',pretendToBeVisual:true});
dom.window.matchMedia=()=>({matches:true,addEventListener(){},removeEventListener(){}});
new vm.Script(bundle.outputFiles[0].text).runInContext(dom.getInternalVMContext());
const tick=()=>new Promise(resolve=>setTimeout(resolve,30));await tick();
const d=dom.window.document;
assert.equal(d.querySelectorAll('.shell>.outliner').length,1);
assert.equal(d.querySelectorAll('.shell>.inspector').length,1);
assert.equal(d.querySelector('.viewport'),null);
assert.ok(d.querySelector('[data-bake-target="sun"]'));
assert.equal(d.querySelector('.bake-quick-area').previousElementSibling.className,'cards sun');
assert.equal(d.querySelector('.bake-quick-area').nextElementSibling.className,'inspector-footer');
assert.ok(d.querySelector('.bake-image-switch.property-switch.is-off'));
assert.equal(d.querySelector('.bake-image-switch .switch-state').textContent,'OFF');
assert.equal(d.querySelector('.bake-image-switch svg').getAttribute('stroke'),'currentColor');
d.querySelector('[aria-label="Bake Sun lighting"]').click();await tick();
const image=kind=>d.querySelector(`.tree img[data-icon-kind="${kind}"]`);
for(const kind of ['sun','moon','stars','sky','cloud','air','forest','terrain','liquid','cube','material','camera','folder','collection','precip'])assert.ok(image(kind),kind+' SVG present');
for(const img of d.querySelectorAll('.outliner img')){
 assert.equal(img.alt,'');assert.equal(img.getAttribute('aria-hidden'),'true');
 assert.ok(img.src.startsWith('data:image/svg+xml'));
 const svg=decodeURIComponent(img.src.split(',').slice(1).join(','));
 assert.ok(!svg.includes('<image'));
 if(img.dataset.iconKind==='folder')assert.ok(!svg.includes('<text'),'no fake gallery counts in real folders');
}
const row=name=>[...d.querySelectorAll('.tree .object-button')].find(b=>b.querySelector('span')?.textContent===name);
// Folder icons are category-specific and consistent across the tree and inspector.
row('Night Sky').click();await tick();
const collectionSource=image('collection').src;
assert.ok(decodeURIComponent(collectionSource).includes('collection-bracketed-objects-title'));
assert.equal(d.querySelector('.object-title img,.object-title svg,.object-icon'),null);
assert.equal(d.querySelector('.organization-emblem .outliner-asset-icon').src,collectionSource);
const folderSources=[];
for(const name of ['Environment','World','Water Bodies','Scene','Materials']){
 const src=row(name).querySelector('img').src;folderSources.push(src);
 row(name).click();await tick();
 assert.equal(d.querySelector('.object-title img,.object-title svg,.object-icon'),null);
 assert.equal(d.querySelector('.organization-emblem .outliner-asset-icon').src,src);
}
assert.equal(new Set(folderSources).size,5);
row('Environment').click();await tick();
assert.ok(d.querySelector('.organization-member-link img[data-icon-kind="sun"]'));
assert.ok(d.querySelector('.organization-member-link img[data-icon-kind="local-fog"]'));
row('Terrain').click();await tick();
assert.equal(d.querySelector('.object-title .eyebrow').textContent,'Landscape');
assert.equal(d.querySelector('.object-title h1').textContent,'Terrain');
assert.equal(d.querySelector('.object-title img,.object-title svg,.object-icon'),null);
assert.ok(image('terrain'));
// Local effects are distinct finite volumes, not children of global Clouds.
for(const [name,kind,width] of [['Local Volumetric Clouds','local-cloud','1000'],['Local Fog','local-fog','200']]){
 assert.ok(image(kind));row(name).click();await tick();assert.equal(d.querySelector('h1').textContent,name);
 assert.equal(d.querySelector('input[aria-label="Width"]').value,width);
 assert.ok(d.querySelector('input[aria-label="Center X"]'));
 assert.equal(d.querySelector('input[aria-label="Volume density"]'),null);
 const expected=kind==='local-cloud'?['clouds','cloudBase','thickness']:['fog'];
 for(const key of expected)assert.ok(d.querySelector(`input[aria-label="${key}"]`));
 assert.equal(d.querySelectorAll('.property-switches button').length,kind==='local-cloud'?2:1);
 assert.ok(!d.querySelector('.cloud-child-link'));
}
row('Clouds').click();await tick();
assert.ok(d.querySelector('.cloud-sky-view svg'));
assert.equal(d.querySelector('.cloud-sky-view canvas'),null);
assert.ok(d.querySelector('.cloud-sky-view').textContent.includes('ILLUSTRATIVE'));
const cloudTitles=[...d.querySelectorAll('.cards .card-heading>span:first-child')].map(el=>el.textContent);
row('Local Volumetric Clouds').click();await tick();
assert.deepEqual([...d.querySelectorAll('.cards .card-heading>span:first-child')].map(el=>el.textContent),[...cloudTitles,'Volume bounds & position']);
row('Local Fog').click();await tick();
d.querySelector('[aria-label="Toggle Fog"]').click();await tick();
assert.ok(d.querySelector('.card.feature-disabled'));
row('Atmosphere').click();await tick();assert.equal(d.querySelector('[aria-label="Toggle Fog"]').getAttribute('aria-pressed'),'true');
row('Local Fog').click();await tick();assert.equal(d.querySelector('[aria-label="Toggle Fog"]').getAttribute('aria-pressed'),'false');
const chooseShape=async shape=>{const select=d.querySelector('[aria-label="Volume shape"]');select.value=shape;select.dispatchEvent(new dom.window.Event('change',{bubbles:true}));await tick()};
for(const shape of ['Sphere','Cylinder','Cone']){
 await chooseShape(shape);assert.ok(d.querySelector('[aria-label="Radius"]'));
 assert.equal(!!d.querySelector('[aria-label="Height"]'),shape!=='Sphere');
 assert.equal(d.querySelector('[aria-label="Width"]'),null);
 assert.ok(d.querySelector(`[aria-label="${shape} volume bounds schematic"]`));
}
await chooseShape('Custom');
const editOutline=async text=>{const input=d.querySelector('[aria-label="Custom footprint vertices"]');Object.getOwnPropertyDescriptor(dom.window.HTMLTextAreaElement.prototype,'value').set.call(input,text);input.dispatchEvent(new dom.window.Event('input',{bubbles:true}));await tick();d.querySelector('.volume-custom button').click();await tick()};
await editOutline('-1, -1\n1, -1\n0, 1');assert.equal(d.querySelector('.volume-custom [role="alert"]'),null);
await editOutline('-1, -1\n1, 1\n-1, 1\n1, -1');assert.ok(d.querySelector('.volume-custom [role="alert"]').textContent.includes('cross'));
await editOutline('-2, 0\n1, 0\n0, 1');assert.ok(d.querySelector('.volume-custom [role="alert"]'));
row('Local Volumetric Clouds').click();await tick();assert.equal(d.querySelector('[aria-label="Volume shape"]').value,'Box');
row('Local Fog').click();await tick();assert.equal(d.querySelector('[aria-label="Volume shape"]').value,'Custom');
assert.equal(d.querySelector('[aria-label="Custom footprint vertices"]').value,'-1, -1\n1, -1\n0, 1');
await chooseShape('Box');
const widthInput=d.querySelector('input[aria-label="Width"]');
Object.getOwnPropertyDescriptor(dom.window.HTMLInputElement.prototype,'value').set.call(widthInput,'350');
widthInput.dispatchEvent(new dom.window.Event('input',{bubbles:true}));await tick();
row('Local Volumetric Clouds').click();await tick();assert.equal(d.querySelector('input[aria-label="Width"]').value,'1000');
row('Local Fog').click();await tick();assert.equal(d.querySelector('input[aria-label="Width"]').value,'350');
row('Precipitation').click();await tick();assert.equal(d.querySelector('h1').textContent,'Precipitation');
const rain=image('precip').src;
assert.ok(decodeURIComponent(rain).includes('compact outliner symbol'));
assert.ok(decodeURIComponent(image('stars').src).includes('compact outliner symbol'));
[...d.querySelectorAll('.weather-segments button')].find(b=>b.textContent==='Snow').click();await tick();assert.equal(image('precip').src,rain);assert.equal([...d.querySelectorAll('.weather-segments button')].find(b=>b.textContent==='Snow').getAttribute('aria-pressed'),'true');
[...d.querySelectorAll('.weather-segments button')].find(b=>b.textContent==='Hail').click();await tick();assert.equal(image('precip').src,rain);assert.equal([...d.querySelectorAll('.weather-segments button')].find(b=>b.textContent==='Hail').getAttribute('aria-pressed'),'true');
d.querySelector('[aria-label="Hide Clouds"]').click();await tick();assert.ok(row('Precipitation').closest('.tree-row').classList.contains('hidden-object'));
d.querySelector('[aria-label="Collapse Environment"]').click();await tick();assert.ok(!row('Sun'),'Sun should disappear when its Environment folder collapses');
d.querySelector('[aria-label="Expand Environment"]').click();await tick();assert.ok(row('Sun'));
d.querySelector('[aria-label="Add scene object"]').click();await tick();
for(const name of ['Local Volumetric Clouds','Local Fog'])assert.ok([...d.querySelectorAll('.add-menu button')].some(b=>b.textContent===name));
const addSun=[...d.querySelectorAll('.add-menu button')].find(b=>b.textContent==='Sun');assert.ok(addSun.querySelector('img'));addSun.click();await tick();assert.ok(row('Sun 2').querySelector('img[data-icon-kind="sun"]'));
// A broken image falls back gracefully without losing selection controls.
image('camera').dispatchEvent(new dom.window.Event('error'));await tick();assert.ok(row('Camera').querySelector('svg.outliner-icon-fallback'));row('Camera').click();await tick();assert.equal(d.querySelector('h1').textContent,'Camera');
const save=d.querySelector('.save-status');save.click();await tick();assert.ok(dom.window.localStorage.getItem('frontier-project'));assert.equal(JSON.parse(dom.window.localStorage.getItem('frontier-project')).values.sun['bake:sun'].request.status,'pending-renderer');
assert.equal(JSON.parse(dom.window.localStorage.getItem('frontier-project')).values['local-fog'].volumeWidth,350);
assert.deepEqual(JSON.parse(dom.window.localStorage.getItem('frontier-project')).values['local-fog'].volumeOutline,[[-1,-1],[1,-1],[0,1]]);
assert.ok(!row('Local Fog').closest('.tree-row').classList.contains('hidden-object'),'global clouds do not hide local fog');
dom.window.close();console.log('PASS: two-panel app, gallery SVGs, clean folder thumbnails, readable precipitation icon and weather-mode controls, selection, collapse, inherited visibility, duplicate entities, error fallback and save.');
