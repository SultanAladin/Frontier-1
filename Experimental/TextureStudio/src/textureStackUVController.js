import { setRenderTargetViewport } from "./renderTargetViewport.js";
import * as THREE from "three";
import { MASK_ATLAS_WIDTH, MASK_TILE_EDGE } from "./maskPaintRaster.js";

export function installTextureStackUV(material, uniform) {
  if (material.userData.textureStackUV) return;
  material.userData.textureStackUV = uniform;
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    shader.uniforms.uTextureStackUV = uniform;
    shader.vertexShader =
      "uniform int uTextureStackUV;\n" + shader.vertexShader;
    shader.vertexShader = shader.vertexShader.replace(
      "#include <project_vertex>",
      "#include <project_vertex>\nif(uTextureStackUV==1)gl_Position=vec4(uvAuthoring.x/2.-1.,uvAuthoring.y*2.-1.,0.,1.);",
    );
  };
  material.customProgramCacheKey = () => cache() + "|ordered-lit-uv-v1";
}
// Render the SAME ordered surface passes into the packed authoring UVs. This
// is a lit appearance preview, not a set of exported/per-channel PBR atlases.
export function withTextureStackUVState(engine, target, uniform, draw) {
  const r = engine.renderer,
    scene = engine.scene,
    viewport = r.getViewport(new THREE.Vector4()),
    scissor = r.getScissor(new THREE.Vector4()),
    clear = r.getClearColor(new THREE.Color()),
    materials = new Map(),
    visibility = new Map();
  const saved = {
    target: r.getRenderTarget(),
    face: r.getActiveCubeFace?.() || 0,
    mip: r.getActiveMipmapLevel?.() || 0,
    scissorTest: r.getScissorTest(),
    alpha: r.getClearAlpha(),
    background: scene.background,
    auto: r.shadowMap.autoUpdate,
    needs: r.shadowMap.needsUpdate,
    value: uniform.value,
  };
  try {
    scene.traverse((object) => {
      if (!object.isMesh && !object.isLine && !object.isSprite) return;
      const ms = Array.isArray(object.material)
        ? object.material
        : [object.material];
      visibility.set(object, {
        visible: object.visible,
        frustumCulled: object.frustumCulled,
      });
      if (!ms.every((m) => m?.userData.textureStackUV)) {
        object.visible = false;
        return;
      }
      // Onscreen camera framing must not remove offscreen charts or stamps
      // from the UV atlas, which projects every supported surface into UVs.
      object.frustumCulled = false;
      ms.forEach((m) => {
        if (!materials.has(m)) {
          materials.set(m, {
            depthTest: m.depthTest,
            depthWrite: m.depthWrite,
            wireframe: m.wireframe,
          });
          m.depthTest = m.depthWrite = m.wireframe = false;
        }
      });
    });
    uniform.value = 1;
    scene.background = null;
    r.shadowMap.autoUpdate = false;
    r.shadowMap.needsUpdate = false;
    r.setRenderTarget(target);
    setRenderTargetViewport(r, target.width, target.height);
    r.setScissorTest(false);
    r.setClearColor(0, 0);
    r.clear();
    return draw();
  } finally {
    uniform.value = saved.value;
    visibility.forEach((state, object) => Object.assign(object, state));
    materials.forEach((state, m) => Object.assign(m, state));
    scene.background = saved.background;
    r.shadowMap.autoUpdate = saved.auto;
    r.shadowMap.needsUpdate = saved.needs;
    r.setRenderTarget(saved.target, saved.face, saved.mip);
    r.setViewport(viewport);
    r.setScissor(scissor);
    r.setScissorTest(saved.scissorTest);
    r.setClearColor(clear, saved.alpha);
  }
}
export function createTextureStackUVController(engine, onAtlas, onError) {
  const uniform = { value: 0 },
    target = new THREE.WebGLRenderTarget(MASK_ATLAS_WIDTH, MASK_TILE_EDGE, {
      depthBuffer: false,
      stencilBuffer: false,
    });
  target.texture.colorSpace = THREE.SRGBColorSpace;
  const canvas = document.createElement("canvas");
  canvas.width = MASK_ATLAS_WIDTH;
  canvas.height = MASK_TILE_EDGE;
  const context = canvas.getContext("2d"),
    bytes = new Uint8Array(MASK_ATLAS_WIDTH * MASK_TILE_EDGE * 4);
  let disposed = false,
    timer = null,
    revision = 0;
  function refresh() {
    if (disposed || timer) return;
    timer = setTimeout(() => {
      timer = null;
      if (disposed) return;
      if (engine.compiling) {
        refresh();
        return;
      }
      try {
        engine.digitalPaint?.prepare();
        withTextureStackUVState(engine, target, uniform, () => {
          if (engine.opticalStack) engine.opticalStack.render({ uv: true });
          else engine.renderer.render(engine.scene, engine.camera);
          engine.renderer.readRenderTargetPixels(
            target,
            0,
            0,
            target.width,
            target.height,
            bytes,
          );
        });
        const image = context.createImageData(canvas.width, canvas.height),
          row = canvas.width * 4;
        for (let y = 0; y < canvas.height; y++)
          image.data.set(
            bytes.subarray(y * row, (y + 1) * row),
            (canvas.height - y - 1) * row,
          );
        context.putImageData(image, 0, 0);
        let hash = 2166136261;
        for (let i = 0; i < bytes.length; i += 16)
          hash = Math.imul(hash ^ bytes[i], 16777619);
        engine.renderer.domElement.dataset.layerAtlasHash = String(hash >>> 0);
        onAtlas?.({ canvas, revision: ++revision });
        engine.renderer.domElement.dataset.layerAtlasReady = "true";
      } catch (e) {
        onError?.(`Layer UV preview: ${e.message}`);
      }
    }, 50);
  }
  return {
    install: (m) => installTextureStackUV(m, uniform),
    withState: (target, draw) =>
      withTextureStackUVState(engine, target, uniform, draw),
    refresh,
    dispose() {
      disposed = true;
      clearTimeout(timer);
      target.dispose();
    },
  };
}
