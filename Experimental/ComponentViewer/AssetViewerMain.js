// Third, unified-workflow prototype: begin with an empty component tree and imported files only.
import { $, toast } from '../Shared/Studio/Ui.js';
import { createCompileContext } from '../Shared/GeometryCompile/CompileContext.js';
import { runStep } from '../Shared/GeometryCompile/CompileSteps.js';
import { assetFromFile } from '../Shared/GeometryCompile/CompileSources.js';
import { createBlueprint, addComponent } from './Blueprint.js';
import { createComponentViewer } from './AssetViewer.js';
import { worldModels, makeWorldModel } from './WorldMirrorAssets.js';

const assets = new Map();
const { ctx, iconRenderer } = createCompileContext();
let viewer;

async function importFiles(files, target = null) {
  // A placeholder receives one file; importing to the Asset Browser accepts multiple files.
  for (const file of Array.from(files).slice(0, target ? 1 : undefined)) {
    try {
      const asset = await assetFromFile(file);
      // Only prepare geometry for display; UV, LOD, SDF and icon are not baked here.
      await runStep(asset, 'geometry', ctx);
      if (asset.raw.cornerUvs?.length === asset.out.geometry.indices.length * 2) {
        asset.out.uv = { uvs: asset.raw.cornerUvs, layout: null, seams: null, stats: null, authored: true };
        asset.steps.uv.state = 'done'; asset.steps.uv.summary = 'Imported UV coordinates'; asset.revision++;
      }
      if (asset.raw.icon) { asset.out.icon = asset.raw.icon; asset.steps.icon.state = 'done'; asset.steps.icon.summary = 'Imported icon'; }
      assets.set(asset.id, asset);
      if (target && !viewer.assignImported(target, asset)) {
        toast(`${asset.name} imported into the Asset Browser; the placeholder no longer exists`);
      } else {
        viewer.refresh();
        toast(target ? `${asset.name} assigned to ${target.name}` : `${asset.name} imported into the Asset Browser`);
      }
    } catch (error) {
      console.error(error);
      toast(`Could not import ${file.name}: ${error.message || error}`);
    }
  }
}

// Catalog entries are prepared only when dropped; no preset is placed into the scene automatically.
const worldCache = new Map();
async function resolveWorldModel(id) {
  if (!worldCache.has(id)) {
    const loading = makeWorldModel(id, ctx).then((asset) => { assets.set(asset.id, asset); viewer.refresh(); return asset; });
    worldCache.set(id, loading);
    loading.catch(() => worldCache.delete(id));
  }
  return worldCache.get(id);
}
viewer = createComponentViewer({ assets, iconRenderer, importFiles, samples: [], worldModels, resolveWorldModel, compileContext: ctx, openCompile() {} });
viewer.open(createBlueprint('Untitled'));
$('FileInput').addEventListener('change', (e) => { importFiles(e.target.files); e.target.value = ''; });
addEventListener('dragover', (e) => { if ([...e.dataTransfer.types].includes('Files')) e.preventDefault(); });
addEventListener('drop', (e) => {
  if (!e.defaultPrevented && e.dataTransfer.files.length) { e.preventDefault(); importFiles(e.dataTransfer.files); }
});
// Kept for the viewer's existing blueprint helper; this page does not create a preset at startup.
window.FrontierComponentViewer = {
  assets, viewer,
  createBlueprintFor(asset) {
    const bp = createBlueprint(`BP_${asset.name.replace(/[^\w]+/g, '')}`);
    addComponent(bp, 'StaticMesh', { name: asset.name, props: { asset: asset.id } });
    return bp;
  },
};
