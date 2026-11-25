import { WorldPreview } from './world-preview.js';
import { loadWorld, saveWorld } from '../world/world-storage.js';
import { bakeTerrainDocument } from './world-terrain-session.js';
import { MASK_RESOLUTION, addRegion, addShapeLayer, createWorld, migrateWorld, normalizeBounds, paintMask, regionMask, terrainImportMapping, visibleTileKeys } from '../world/world-map.js';

/** World layout owns only a 2D mask and bounded baked terrain patches. */
export class WorldLayout {
  constructor(root, onEdit, onChange = () => {}) {
    this.root = root;
    this.onEdit = onEdit;
    this.onChange = onChange;
    this.world = createWorld();
    this.selectedLayer = addShapeLayer(this.world).id;
    this.selectedRegion = null;
    this.tool = 'paint';
    this.brush = .045;
    this.view = { x: .5, y: .5, pixelsPerTile: 320 };
    this.tileCanvasCache = new WeakMap();
    this.previewSize = 64;
    this.ready = false;
    this.saveQueue = Promise.resolve();
    this.root.innerHTML = `<aside class="world-panel"><div class="world-heading"><div><small>WORLD LAYOUT</small><h2>Shapes &amp; terrains</h2></div><button id="world-add-layer" title="Add shape layer">+ Shape</button></div><p>Paint a flat land mask, then drag a box to assign a terrain. The box is edited in Terrain Studio.</p><div id="world-layer-list" class="world-list"></div><h3>Terrain bounds</h3><div id="world-region-list" class="world-list"></div><p class="world-note">Sparse map tiles expand as you paint. Saved in this browser. The 3D terrain is a low-resolution mesh; caves and objects are not included. Water is a visual shader, not a simulation.</p></aside><main class="world-main"><div class="world-toolbar"><div class="world-tools"><button data-world-tool="paint" class="active">Paint land</button><button data-world-tool="erase">Erase</button><button data-world-tool="bounds">Draw bounds</button><button data-world-tool="pan">Pan</button><button id="world-map-fit" title="Fit all painted tiles and bounds">Fit map</button><label>Brush <input id="world-brush" type="range" min="1" max="20" value="4.5" step="0.5"></label></div><div class="world-tools"><button id="world-view-map" class="active">2D map</button><button id="world-view-preview">3D world</button><label title="Mesh vertices per painted tile">Mesh/tile <select id="world-preview-res"><option value="32">32²</option><option value="64" selected>64²</option><option value="128">128²</option><option value="256">256²</option><option value="512">512²</option></select></label><label title="Visual water only"><input id="world-water" type="checkbox" checked> Water</label><label title="Water surface elevation relative to terrain heights">Sea level <input id="world-sea-level" type="range" min="-20" max="80" value="0" step="1"><output id="world-sea-value">0</output></label><button id="world-preview-fit" title="Frame the combined world in 3D">Fit 3D</button></div></div><div class="world-stage"><div class="world-map-wrap"><canvas id="world-map" aria-label="Paint land and draw terrain bounds"></canvas></div><div id="world-preview" class="world-preview hidden-panel"></div></div><div class="world-footer" id="world-status">Paint on the map, then choose Draw bounds and drag a rectangle.</div></main>`;
    this.canvas = root.querySelector('#world-map');
    this.ctx = this.canvas.getContext('2d');
    this.canvas.width = this.canvas.height = 768;
    this.resizeObserver = new ResizeObserver(() => {
      const rect = this.canvas.getBoundingClientRect();
      if (rect.width && rect.height) { this.canvas.width = Math.max(1, Math.round(rect.width)); this.canvas.height = Math.max(1, Math.round(rect.height)); this.drawMap(); }
    });
    this.resizeObserver.observe(this.canvas);
    this.canvas.addEventListener('wheel', event => this.zoomMap(event), { passive: false });
    this.canvas.addEventListener('contextmenu', event => event.preventDefault());
    window.addEventListener('keydown', event => { if (event.code === 'Space' && !/INPUT|TEXTAREA|SELECT/.test(event.target?.tagName || '') && !this.root.classList.contains('hidden-panel')) { this.spacePan = true; event.preventDefault(); } });
    window.addEventListener('keyup', event => { if (event.code === 'Space') this.spacePan = false; });
    window.addEventListener('blur', () => { this.spacePan = false; });
    this.canvas.addEventListener('pointerdown', event => this.pointerDown(event));
    this.canvas.addEventListener('pointermove', event => this.pointerMove(event));
    this.canvas.addEventListener('pointerup', event => this.pointerUp(event));
    this.canvas.addEventListener('pointercancel', event => this.pointerUp(event));
    root.querySelector('#world-add-layer').onclick = () => { if (!this.ready) return; this.selectedLayer = addShapeLayer(this.world).id; this.render(); this.save(); };
    root.querySelectorAll('[data-world-tool]').forEach(button => button.onclick = () => {
      this.tool = button.dataset.worldTool;
      root.querySelectorAll('[data-world-tool]').forEach(el => el.classList.toggle('active', el === button));
      this.status(this.tool === 'bounds' ? 'Drag a rectangle on the selected shape layer.' : this.tool === 'pan' ? 'Drag to pan. Wheel zooms around the pointer.' : 'Paint or erase. Middle-drag or hold Space to pan; wheel zooms.');
    });
    root.querySelector('#world-brush').oninput = event => { this.brush = Number(event.target.value) / 100; };
    root.querySelector('#world-map-fit').onclick = () => this.fitMap();
    root.querySelector('#world-preview-fit').onclick = () => this.updatePreview(true);
    const updateWater = () => { const visible = root.querySelector('#world-water').checked; const level = Number(root.querySelector('#world-sea-level').value); root.querySelector('#world-sea-value').textContent = String(level); this.preview?.setWater(visible, level); };
    root.querySelector('#world-water').onchange = updateWater; root.querySelector('#world-sea-level').oninput = updateWater;
    root.querySelector('#world-view-map').onclick = () => this.showPreview(false);
    root.querySelector('#world-view-preview').onclick = () => this.showPreview(true);
    root.querySelector('#world-preview-res').onchange = event => {
      this.previewSize = Number(event.target.value);
      // Re-bake from each saved full-resolution terrain, not from its old
      // low-resolution preview. The world grid can then be scaled independently.
      for (const region of this.world.regions) if (region.document) region.bake = bakeTerrainDocument(region.document, this.previewSize, terrainImportMapping(region));
      this.save(); this.render();
    };
    this.render();
    this.status('Loading saved world…');
    loadWorld().then(saved => {
      if (saved) { this.world = migrateWorld(saved); if (!this.world.layers.length) addShapeLayer(this.world); this.selectedLayer = this.world.layers[0].id; this.fitMap(); }
      this.ready = true; this.render(); this.status('Paint land; middle-drag or Space-drag to pan, and use the wheel to zoom. World saves locally.');
    }).catch(err => { this.ready = true; this.storageFailed = true; this.render(); this.status(`World could not be loaded: ${err.message}. Autosave disabled to protect existing data.`); });
  }
  save() {
    if (!this.ready || this.storageFailed) return;
    const snapshot = structuredClone(this.world);
    this.saveQueue = this.saveQueue.catch(() => {}).then(() => saveWorld(snapshot)).catch(err => { console.error('World save failed', err); this.status(`World could not be saved: ${err.message}`); });
  }
  status(text) { this.root.querySelector('#world-status').textContent = text; }
  point(event) {
    const rect = this.canvas.getBoundingClientRect();
    const sx = (event.clientX - rect.left) * this.canvas.width / rect.width;
    const sy = (event.clientY - rect.top) * this.canvas.height / rect.height;
    return { x: this.view.x + (sx - this.canvas.width / 2) / this.view.pixelsPerTile,
      y: this.view.y + (sy - this.canvas.height / 2) / this.view.pixelsPerTile, sx, sy };
  }
  zoomMap(event) {
    event.preventDefault();
    const before = this.point(event);
    this.view.pixelsPerTile = Math.max(.001, Math.min(4096, this.view.pixelsPerTile * Math.exp(-event.deltaY * .0015)));
    const after = this.point(event);
    this.view.x += before.x - after.x; this.view.y += before.y - after.y;
    this.drawMap();
  }
  fitMap() {
    const tiles = visibleTileKeys(this.world).map(key => key.split(',').map(Number));
    const bounds = this.world.regions.map(r => r.bounds);
    if (!tiles.length && !bounds.length) { this.view = { x: .5, y: .5, pixelsPerTile: 320 }; this.drawMap(); return; }
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (const [x, y] of tiles) { minX = Math.min(minX, x); minY = Math.min(minY, y); maxX = Math.max(maxX, x + 1); maxY = Math.max(maxY, y + 1); }
    for (const b of bounds) { minX = Math.min(minX, b.x); minY = Math.min(minY, b.y); maxX = Math.max(maxX, b.x + b.width); maxY = Math.max(maxY, b.y + b.height); }
    this.view = { x: (minX + maxX) / 2, y: (minY + maxY) / 2, pixelsPerTile: Math.max(.001, Math.min(1024, .85 * Math.min(this.canvas.width / Math.max(maxX - minX, .1), this.canvas.height / Math.max(maxY - minY, .1)))) };
    this.drawMap();
  }
  pointerDown(event) {
    if (!this.ready || (event.button !== 0 && event.button !== 1)) return;
    event.preventDefault();
    const p = this.point(event), pan = event.button === 1 || this.spacePan || this.tool === 'pan';
    if (!pan && this.tool !== 'bounds' && this.view.pixelsPerTile < 24) { this.status('Zoom in before painting, to avoid accidentally drawing across thousands of tiles.'); return; }
    this.pointer = { id: event.pointerId, start: p, last: p, pan };
    this.canvas.setPointerCapture(event.pointerId);
    if (!pan && this.tool !== 'bounds') this.paint(p, p);
  }
  pointerMove(event) {
    if (!this.pointer || event.pointerId !== this.pointer.id) return;
    let p = this.point(event);
    if (this.pointer.pan) {
      this.view.x -= (p.sx - this.pointer.last.sx) / this.view.pixelsPerTile;
      this.view.y -= (p.sy - this.pointer.last.sy) / this.view.pixelsPerTile;
      p = this.point(event);
    } else if (this.tool !== 'bounds') this.paint(this.pointer.last, p);
    this.pointer.last = p; this.drawMap();
  }
  pointerUp(event) {
    if (!this.pointer || event.pointerId !== this.pointer.id) return;
    if (!this.pointer.pan && this.tool === 'bounds' && event.type !== 'pointercancel') {
      const b = normalizeBounds(this.pointer.start.x, this.pointer.start.y, this.pointer.last.x, this.pointer.last.y);
      if (b) { const region = addRegion(this.world, this.selectedLayer, b); this.selectedRegion = region.id; this.status('Terrain bounds assigned. Open in Terrain Studio to start from the shape mask.'); }
    }
    const changed = !this.pointer.pan; this.pointer = null; this.render(); if (changed) this.save();
  }
  paint(a, b) {
    const layer = this.world.layers.find(item => item.id === this.selectedLayer);
    if (layer) paintMask(layer, a.x, a.y, b.x, b.y, this.brush, this.tool === 'erase');
  }
  render() {
    const list = this.root.querySelector('#world-layer-list'); list.replaceChildren();
    for (const layer of this.world.layers) {
      const row = document.createElement('div'); row.className = `world-row ${layer.id === this.selectedLayer ? 'selected' : ''}`;
      const select = document.createElement('button'); select.textContent = layer.name; select.onclick = () => { this.selectedLayer = layer.id; this.render(); };
      const visibility = document.createElement('button'); visibility.textContent = layer.visible ? 'Visible' : 'Hidden'; visibility.onclick = () => { layer.visible = !layer.visible; this.render(); this.save(); };
      const rename = document.createElement('button'); rename.textContent = '✎'; rename.title = 'Rename shape'; rename.onclick = () => { const value = prompt('Shape name', layer.name); if (value?.trim()) { layer.name = value.trim().slice(0, 80); this.render(); this.save(); } };
      const remove = document.createElement('button'); remove.textContent = '×'; remove.title = 'Delete shape and its terrains'; remove.onclick = () => {
        if (!confirm(`Delete ${layer.name} and all terrain bounds assigned to it?`)) return;
        this.world.layers = this.world.layers.filter(item => item.id !== layer.id);
        this.world.regions = this.world.regions.filter(item => item.layerId !== layer.id);
        if (!this.world.layers.length) addShapeLayer(this.world);
        this.selectedLayer = this.world.layers[0].id; this.selectedRegion = null; this.render(); this.save();
      };
      row.append(select, visibility, rename, remove); list.append(row);
    }
    const regions = this.root.querySelector('#world-region-list'); regions.replaceChildren();
    for (const region of this.world.regions) {
      const row = document.createElement('div'); row.className = `world-region ${region.id === this.selectedRegion ? 'selected' : ''}`;
      const title = document.createElement('strong'); title.textContent = region.name;
      const details = document.createElement('small'); details.textContent = `${this.world.layers.find(item => item.id === region.layerId)?.name || 'Shape'} · ${region.bake ? `baked ${region.bake.size}²` : 'not baked'} · ${(region.bounds.width / region.bounds.height).toFixed(2)}:1 bounds`;
      const edit = document.createElement('button'); edit.textContent = 'Open in Terrain Studio'; edit.onclick = () => { this.selectedRegion = region.id; this.onEdit(region, regionMask(this.world, region)); this.render(); };
      const actions = document.createElement('div'); actions.className = 'world-region-actions';
      const select = document.createElement('button'); select.textContent = 'Select'; select.onclick = () => { this.selectedRegion = region.id; this.selectedLayer = region.layerId; const b = region.bounds; this.view = { x: b.x + b.width / 2, y: b.y + b.height / 2, pixelsPerTile: Math.max(.001, Math.min(1024, .7 * Math.min(this.canvas.width / b.width, this.canvas.height / b.height))) }; this.render(); };
      const rename = document.createElement('button'); rename.textContent = 'Rename'; rename.onclick = () => { const value = prompt('Terrain name', region.name); if (value?.trim()) { region.name = value.trim().slice(0, 80); this.render(); this.save(); } };
      const remove = document.createElement('button'); remove.textContent = 'Delete'; remove.onclick = () => { if (!confirm(`Delete ${region.name} and its saved terrain?`)) return; this.world.regions = this.world.regions.filter(item => item.id !== region.id); if (this.selectedRegion === region.id) this.selectedRegion = null; this.render(); this.save(); };
      actions.append(select, rename, remove);
      row.append(title, details, edit, actions); regions.append(row);
    }
    if (!this.world.regions.length) regions.textContent = 'No terrain bounds yet.';
    this.drawMap();
    if (this.previewVisible) this.updatePreview();
    this.onChange();
  }
  drawMap() {
    const ctx = this.ctx, width = this.canvas.width, height = this.canvas.height, scale = this.view.pixelsPerTile;
    const screenX = x => (x - this.view.x) * scale + width / 2;
    const screenY = y => (y - this.view.y) * scale + height / 2;
    const left = this.view.x - width / (2 * scale), top = this.view.y - height / (2 * scale);
    ctx.fillStyle = '#142b30'; ctx.fillRect(0, 0, width, height);
    for (const layer of this.world.layers) if (layer.visible) for (const [key, tile] of Object.entries(layer.tiles)) {
      const [x, y] = key.split(',').map(Number), px = screenX(x), py = screenY(y);
      if (px > width || py > height || px + scale < 0 || py + scale < 0) continue;
      let cached = this.tileCanvasCache.get(tile);
      if (!cached || cached.revision !== layer.revision) {
        const canvas = cached?.canvas || document.createElement('canvas'); canvas.width = canvas.height = MASK_RESOLUTION;
        const temp = canvas.getContext('2d'), image = temp.createImageData(MASK_RESOLUTION, MASK_RESOLUTION);
        for (let i = 0; i < tile.length; i++) { image.data[i*4] = 91; image.data[i*4+1] = 136; image.data[i*4+2] = 102; image.data[i*4+3] = tile[i]; }
        temp.putImageData(image, 0, 0); cached = { canvas, revision: layer.revision }; this.tileCanvasCache.set(tile, cached);
      }
      ctx.drawImage(cached.canvas, px, py, scale, scale);
    }
    const step = Math.pow(2, Math.ceil(Math.log2(48 / scale)));
    ctx.strokeStyle = 'rgba(190,225,220,.13)'; ctx.lineWidth = 1; ctx.beginPath();
    for (let x = Math.floor(left / step) * step; x <= left + width / scale; x += step) { const px = screenX(x); ctx.moveTo(px, 0); ctx.lineTo(px, height); }
    for (let y = Math.floor(top / step) * step; y <= top + height / scale; y += step) { const py = screenY(y); ctx.moveTo(0, py); ctx.lineTo(width, py); }
    ctx.stroke();
    ctx.fillStyle = 'rgba(190,225,220,.45)'; ctx.font = '11px sans-serif';
    if (scale >= 50) for (let x = Math.ceil(left); x <= left + width / scale; x++) for (let y = Math.ceil(top); y <= top + height / scale; y++) ctx.fillText(`${x}, ${y}`, screenX(x) + 5, screenY(y) + 14);
    const rect = (b, selected, label) => { const x=screenX(b.x), y=screenY(b.y); ctx.strokeStyle = selected ? '#ffcf79' : '#80cbe3'; ctx.lineWidth = selected ? 3 : 2; ctx.strokeRect(x,y,b.width*scale,b.height*scale); if (label) {ctx.fillStyle=ctx.strokeStyle; ctx.font='bold 13px sans-serif'; ctx.fillText(label,x+5,y+18);} };
    for (const region of this.world.regions) rect(region.bounds, region.id === this.selectedRegion, region.name);
    if (this.pointer && !this.pointer.pan && this.tool === 'bounds') { const b = normalizeBounds(this.pointer.start.x,this.pointer.start.y,this.pointer.last.x,this.pointer.last.y); if (b) rect(b,true,''); }
  }
  showPreview(show) {
    this.previewVisible = show;
    this.root.querySelector('.world-map-wrap').classList.toggle('hidden-panel', show);
    this.root.querySelector('#world-preview').classList.toggle('hidden-panel', !show);
    this.root.querySelector('#world-view-map').classList.toggle('active', !show);
    this.root.querySelector('#world-view-preview').classList.toggle('active', show);
    if (this.preview) this.preview.active = show;
    if (show) this.updatePreview();
    else requestAnimationFrame(() => { const rect = this.canvas.getBoundingClientRect(); if (rect.width && rect.height) { this.canvas.width = Math.round(rect.width); this.canvas.height = Math.round(rect.height); } this.drawMap(); });
  }
  updatePreview(fit = false) {
    if (!this.previewVisible) return;
    const el = this.root.querySelector('#world-preview');
    if (!this.preview) this.preview = new WorldPreview(el);
    this.preview.active = true;
    const info = this.preview.rebuild(this.world, this.previewSize, { water: this.root.querySelector('#world-water').checked, seaLevel: Number(this.root.querySelector('#world-sea-level').value), fit });
    this.status(`World mesh: ${info.tileCount} tiles at ${info.resolution}² each. Water is a visual shader; drag to orbit, wheel to zoom.`);
  }
}
