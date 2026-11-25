/** Session-only Terrain Studio documents. Pixel readback preserves the visible
 * terrain and height-sculpt stamps while switching between region documents.
 * Stateful surface fields are captured too; particles and moving rocks restart. */
function read(gl, fbo, size) {
  const data = new Float32Array(size * size * 4);
  gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
  gl.readPixels(0, 0, size, size, gl.RGBA, gl.FLOAT, data);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  return data;
}
function upload(gl, texture, size, data) {
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, size, size, gl.RGBA, gl.FLOAT, data);
  gl.bindTexture(gl.TEXTURE_2D, null);
}
export function captureTerrainDocument(engine) {
  const pipe = engine.pipeline, gl = engine.gl, size = engine.gridRes;
  pipe.pauseSimulationForEdit('Terrain document switched');
  const sculpt = [];
  for (const [id, entry] of pipe.sculptStore) sculpt.push({ id, data: read(gl, entry.fbo[entry.idx], size), caveStrokes: structuredClone(entry.caveStrokes) });
  return {
    size, params: structuredClone(engine.params), camera: structuredClone(engine.camera),
    layers: structuredClone(pipe.layers), activePresetId: pipe.activePresetId, projectTitle: pipe.projectTitle,
    paletteId: pipe.paletteId, fabricMode: pipe.fabricMode, shadingMode: pipe.shadingMode, selectedLayerId: pipe.selectedLayerId,
    terrain: read(gl, engine.fboTerrainSingle[engine.terrainIdx], size),
    snow: read(gl, engine.fboSnowClear[engine.snowIdx], size),
    chemical: read(gl, engine.fboChem[engine.chemIdx], size),
    frost: read(gl, engine.fboFrost[engine.frostIdx], size),
    flux: read(gl, engine.fboFlux[engine.fluxIdx], size),
    initial: read(gl, engine.fboInitialTerrain, size),
    clock: { stepCount: engine.stepCount, cryoStepCount: engine.cryoStepCount, climateElapsedYears: engine.climateElapsedYears },
    sculpt,
  };
}
export function restoreTerrainDocument(engine, doc, { restoreTerrain = true } = {}) {
  const pipe = engine.pipeline, gl = engine.gl, size = engine.gridRes;
  if (doc.size !== size) throw new Error('The saved terrain has a different grid resolution.');
  for (const entry of pipe.sculptStore.values()) { for (const t of entry.tex) gl.deleteTexture(t); for (const f of entry.fbo) gl.deleteFramebuffer(f); }
  pipe.sculptStore.clear();
  Object.assign(engine.params, structuredClone(doc.params)); Object.assign(engine.camera, structuredClone(doc.camera));
  pipe.layers = structuredClone(doc.layers); pipe.activePresetId = doc.activePresetId; pipe.projectTitle = doc.projectTitle;
  pipe.paletteId = doc.paletteId; pipe.fabricMode = doc.fabricMode; pipe.shadingMode = doc.shadingMode; pipe.selectedLayerId = doc.selectedLayerId;
  for (const sculpt of doc.sculpt) {
    const entry = pipe._getOrCreateSculptEntry(sculpt.id); upload(gl, entry.tex[entry.idx], size, sculpt.data); entry.caveStrokes.push(...structuredClone(sculpt.caveStrokes));
  }
  pipe.evaluateStack({ reseedPlacements: true });
  if (restoreTerrain) {
    upload(gl, engine.terrainTex[engine.terrainIdx], size, doc.terrain);
    if (doc.snow) upload(gl, engine.snowTex[engine.snowIdx], size, doc.snow);
    if (doc.chemical) upload(gl, engine.chemTex[engine.chemIdx], size, doc.chemical);
    if (doc.frost) upload(gl, engine.frostTex[engine.frostIdx], size, doc.frost);
    if (doc.flux) upload(gl, engine.fluxTex[engine.fluxIdx], size, doc.flux);
    if (doc.initial) upload(gl, engine.initialTerrainTex, size, doc.initial);
    if (doc.clock) Object.assign(engine, doc.clock);
  }
  pipe.pauseSimulationForEdit('Terrain document restored; simulation paused');
}
/** Downsample live bedrock + sediment to a compact height bake. */
export function bakeTerrainDocument(doc, size = 128, mapping = { x: 0, y: 0, width: 1, height: 1 }) {
  const heights = new Float32Array(size * size), r = doc.size;
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const sx = Math.round((mapping.x + x / (size - 1) * mapping.width) * (r - 1));
    const sy = Math.round((mapping.y + y / (size - 1) * mapping.height) * (r - 1));
    const i = (sy * r + sx) * 4;
    heights[y * size + x] = doc.terrain[i] + doc.terrain[i + 1] + doc.terrain[i + 2];
  }
  return { size, heights };
}
