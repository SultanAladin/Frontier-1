import { setRenderTargetViewport } from "./renderTargetViewport.js";
import * as THREE from "three";

// A bounded pair of linear lit-history targets, not painted/PBR channel atlases.
export const TEXTURE_OPTICAL_PIXEL_LIMIT = 1024 * 1024;
export const TEXTURE_OPTICAL_EDGE_LIMIT = 2048;
export function textureOpticalTargetSize(width, height) {
  width = Math.max(1, Number.isFinite(width) ? Math.floor(width) : 1);
  height = Math.max(1, Number.isFinite(height) ? Math.floor(height) : 1);
  const scale = Math.min(
    1,
    TEXTURE_OPTICAL_EDGE_LIMIT / Math.max(width, height),
    Math.sqrt(TEXTURE_OPTICAL_PIXEL_LIMIT / (width * height)),
  );
  return [
    Math.max(1, Math.floor(width * scale)),
    Math.max(1, Math.floor(height * scale)),
  ];
}

// Ordinary surfaces share one queue. Each optical surface is a barrier: take
// the lower lit result, then shade that surface, before allowing higher rows.
// Conservative visibility proof: a full, nonoptical higher material covering
// the same component makes a lower optical surface unobservable. Leave every
// partial/masked/alpha case in the history pipeline. Avoid unnecessary optical
// captures when the row is provably buried; ordinary rows still use linear HDR.
export function textureOpticalUncoveredPasses(passes) {
  const covered = new Set();
  return [...passes]
    .sort((a, b) => b.renderOrder - a.renderOrder)
    .filter((mesh) => {
      const parts = mesh.userData.textureStackParts || [];
      const hidden =
        parts.length > 0 && parts.every((part) => covered.has(part));
      if (mesh.userData.textureStackFullyCovers)
        parts.forEach((part) => covered.add(part));
      return !hidden;
    })
    .reverse();
}

export function textureOpticalSteps(passes) {
  const steps = [];
  let pending = [];
  for (const mesh of [...passes].sort(
    (a, b) => a.renderOrder - b.renderOrder,
  )) {
    const materials = Array.isArray(mesh.material)
      ? mesh.material
      : [mesh.material];
    if (materials.some((m) => m.transmission > 0)) {
      if (pending.length) steps.push({ kind: "ordinary", meshes: pending });
      pending = [];
      steps.push({ kind: "optical", meshes: [mesh] });
    } else pending.push(mesh);
  }
  if (pending.length) steps.push({ kind: "ordinary", meshes: pending });
  return steps;
}

export function installTextureOpticalHistory(material, uniforms) {
  if (!(material.transmission > 0) || material.userData.textureOpticalHistory)
    return;
  material.userData.textureOpticalHistory = true;
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, uniforms);
    shader.uniforms.uTextureOpticalUV = material.userData.textureStackUV || {
      value: 0,
    };
    // Keep native transmission, IOR, thickness, attenuation, dispersion and
    // roughness. Only its framebuffer consumer changes to the lower stack.
    let chunk = THREE.ShaderChunk.transmission_pars_fragment
      .replace(/\btransmissionSamplerMap\b/g, "uTextureOpticalHistory")
      .replace(/\btransmissionSamplerSize\b/g, "uTextureOpticalSize");
    chunk = chunk.replace(
      "return textureBicubic( uTextureOpticalHistory, fragCoord.xy, lod );",
      `// A lit UV view is not a perspective refraction framebuffer. Sample the
      // same chart, without pulling another UDIM/chart through camera rays.
      if(uTextureOpticalUV==1)return textureLod(uTextureOpticalHistory,gl_FragCoord.xy/uTextureOpticalSize,0.);
      lod=clamp(lod,0.,max(floor(log2(max(uTextureOpticalSize.x,uTextureOpticalSize.y)))-1.,0.));
      return textureBicubic( uTextureOpticalHistory, fragCoord.xy, lod );`,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <transmission_pars_fragment>",
      "uniform int uTextureOpticalUV;\n" + chunk,
    );
  };
  material.customProgramCacheKey = () =>
    cache() + "|ordered-optical-history-v1";
}

function drawable(object) {
  return object.isMesh || object.isLine || object.isPoints || object.isSprite;
}
function materialsOf(object) {
  return Array.isArray(object.material) ? object.material : [object.material];
}
function hasVisibleAncestors(object) {
  for (let parent = object; parent; parent = parent.parent)
    if (!parent.visible) return false;
  return true;
}
function isOverlay(object, specimen) {
  for (
    let parent = object;
    parent && parent !== specimen;
    parent = parent.parent
  )
    if (parent.userData.textureStackOverlay || parent.renderOrder >= 10000)
      return true;
  return false;
}

// Save all mutable state in one scope. A mesh parent must remain traversable
// when one of its children is a later pass; hide its material, not its subtree.
export function withTextureOpticalState(engine, draw) {
  const r = engine.renderer,
    scene = engine.scene,
    objects = new Map(),
    materials = new Map();
  scene.traverse((object) => {
    if (!drawable(object)) return;
    objects.set(object, {
      visible: object.visible,
      effective: hasVisibleAncestors(object),
    });
    materialsOf(object).forEach((material) => {
      if (!materials.has(material))
        materials.set(material, {
          visible: material.visible,
          side: material.side,
        });
    });
  });
  const saved = {
    target: r.getRenderTarget(),
    cube: r.getActiveCubeFace(),
    mip: r.getActiveMipmapLevel(),
    viewport: r.getViewport(new THREE.Vector4()),
    scissor: r.getScissor(new THREE.Vector4()),
    scissorTest: r.getScissorTest(),
    clear: r.getClearColor(new THREE.Color()),
    alpha: r.getClearAlpha(),
    autoClear: r.autoClear,
    toneMapping: r.toneMapping,
    transmissionScale: r.transmissionResolutionScale,
    background: scene.background,
    shadowAuto: r.shadowMap.autoUpdate,
    shadowNeeds: r.shadowMap.needsUpdate,
  };
  let success = false;
  function activate(selected) {
    const ancestors = new Set();
    selected.forEach((object) => {
      for (let parent = object.parent; parent; parent = parent.parent)
        ancestors.add(parent);
    });
    materials.forEach((_, material) => {
      material.visible = false;
    });
    objects.forEach((state, object) => {
      object.visible =
        state.visible && (selected.has(object) || ancestors.has(object));
    });
    selected.forEach((object) =>
      materialsOf(object).forEach((material) => {
        material.visible = materials.get(material).visible;
      }),
    );
  }
  try {
    const result = draw({ objects, materials, saved, activate });
    success = true;
    return result;
  } finally {
    objects.forEach((state, object) => {
      object.visible = state.visible;
    });
    materials.forEach((state, material) => {
      material.visible = state.visible;
      if (material.side !== state.side) {
        material.side = state.side;
        material.needsUpdate = true;
      }
    });
    scene.background = saved.background;
    r.autoClear = saved.autoClear;
    r.toneMapping = saved.toneMapping;
    r.transmissionResolutionScale = saved.transmissionScale;
    r.shadowMap.autoUpdate = saved.shadowAuto;
    // A successful main pass consumed the pending shadow update. A failed one
    // must leave it pending. UV mode's outer scope restores its own shadow state.
    if (!success) r.shadowMap.needsUpdate = saved.shadowNeeds;
    r.setRenderTarget(saved.target, saved.cube, saved.mip);
    r.setViewport(saved.viewport);
    r.setScissor(saved.scissor);
    r.setScissorTest(saved.scissorTest);
    r.setClearColor(saved.clear, saved.alpha);
  }
}

export function createTextureOpticalStackController(engine) {
  const r = engine.renderer,
    fallback = new THREE.DataTexture(
      new Uint8Array(16 * 16 * 4),
      16,
      16,
      THREE.RGBAFormat,
    ),
    uniforms = {
      uTextureOpticalHistory: { value: fallback },
      uTextureOpticalSize: { value: new THREE.Vector2(16, 16) },
    },
    copyUniforms = { source: { value: fallback } },
    presentUniforms = {
      source: { value: fallback },
      premultiply: { value: 1 },
      displayToneMapping: { value: r.toneMapping },
      toneMappingExposure: { value: r.toneMappingExposure },
      displaySRGB: { value: 1 },
    },
    vertexShader =
      "varying vec2 vUv;void main(){vUv=uv;gl_Position=vec4(position.xy,0.,1.);}",
    copyMaterial = new THREE.ShaderMaterial({
      uniforms: copyUniforms,
      vertexShader,
      fragmentShader:
        "varying vec2 vUv;uniform sampler2D source;void main(){gl_FragColor=texture2D(source,vUv);}",
      depthTest: false,
      depthWrite: false,
      blending: THREE.NoBlending,
      toneMapped: false,
    }),
    presentMaterial = new THREE.ShaderMaterial({
      uniforms: presentUniforms,
      vertexShader,
      fragmentShader: `varying vec2 vUv;uniform sampler2D source;uniform int premultiply;
      uniform int displayToneMapping;uniform int displaySRGB;
      ${THREE.ShaderChunk.tonemapping_pars_fragment}
      void main(){vec4 lit=texture2D(source,vUv);vec3 color=lit.a>1e-6?lit.rgb/lit.a:vec3(0.);
      if(displayToneMapping==1)color=LinearToneMapping(color);
      else if(displayToneMapping==2)color=ReinhardToneMapping(color);
      else if(displayToneMapping==3)color=CineonToneMapping(color);
      else if(displayToneMapping==4)color=ACESFilmicToneMapping(color);
      else if(displayToneMapping==6)color=AgXToneMapping(color);
      else if(displayToneMapping==7)color=NeutralToneMapping(color);
      gl_FragColor=vec4(color,lit.a);
      if(displaySRGB==1)gl_FragColor=sRGBTransferOETF(gl_FragColor);
      if(premultiply==1)gl_FragColor.rgb*=gl_FragColor.a;}`,
      depthTest: false,
      depthWrite: false,
      blending: THREE.NoBlending,
      // Explicit display mapping also applies to the UV target; Three omits
      // tone mapping on ordinary offscreen targets, which would clip HDR there.
      toneMapped: false,
    }),
    plane = new THREE.PlaneGeometry(2, 2),
    blit = new THREE.Mesh(plane, copyMaterial),
    blitScene = new THREE.Scene(),
    blitCamera = new THREE.Camera(),
    targets = new Map();
  fallback.colorSpace = THREE.NoColorSpace;
  fallback.generateMipmaps = true;
  fallback.minFilter = THREE.LinearMipmapLinearFilter;
  fallback.needsUpdate = true;
  blit.frustumCulled = false;
  blitScene.add(blit);
  let disposed = false;
  function releaseTargets() {
    uniforms.uTextureOpticalHistory.value = fallback;
    uniforms.uTextureOpticalSize.value.set(16, 16);
    copyUniforms.source.value = presentUniforms.source.value = fallback;
    targets.forEach((pair) => {
      pair.lit.dispose();
      pair.history.dispose();
    });
    targets.clear();
  }
  function pairFor(key, width, height) {
    const size = textureOpticalTargetSize(width, height);
    let pair = targets.get(key);
    if (!pair) {
      const type =
        r.extensions.has("EXT_color_buffer_float") ||
        r.extensions.has("EXT_color_buffer_half_float")
          ? THREE.HalfFloatType
          : THREE.UnsignedByteType;
      pair = {
        lit: new THREE.WebGLRenderTarget(size[0], size[1], {
          type,
          depthBuffer: true,
          stencilBuffer: false,
          samples: 4,
        }),
        history: new THREE.WebGLRenderTarget(size[0], size[1], {
          type,
          depthBuffer: false,
          stencilBuffer: false,
          generateMipmaps: true,
          minFilter: THREE.LinearMipmapLinearFilter,
        }),
      };
      pair.lit.texture.colorSpace = pair.history.texture.colorSpace =
        THREE.LinearSRGBColorSpace;
      targets.set(key, pair);
    } else {
      pair.lit.setSize(size[0], size[1]);
      pair.history.setSize(size[0], size[1]);
    }
    return pair;
  }
  function render({ uv = false } = {}) {
    if (disposed) return;
    const all = [],
      overlays = [],
      base = [],
      depth = [];
    engine.scene.traverse((object) => {
      if (
        !drawable(object) ||
        !hasVisibleAncestors(object) ||
        !materialsOf(object).some((m) => m.visible)
      )
        return;
      if (object.userData.textureStackPass) all.push(object);
      else if (object.name === "system-layer-0-depth") {
        base.push(object);
        depth.push(object);
      } else if (isOverlay(object, engine.specimen)) overlays.push(object);
      else base.push(object);
    });
    const steps = textureOpticalSteps(textureOpticalUncoveredPasses(all)),
      opticalCount = steps.filter((s) => s.kind === "optical").length,
      canvas = r.domElement,
      role = uv ? "opticalStackUv" : "opticalStack";
    canvas.dataset[`${role}Materials`] = JSON.stringify(
      all
        .filter((mesh) => mesh.material.transmission > 0)
        .map((mesh) => ({
          name: mesh.name,
          transmission: mesh.material.transmission,
          ior: mesh.material.ior,
          thickness: mesh.material.thickness,
          roughness: mesh.material.roughness,
        })),
    );
    if (
      engine.specimen.material.wireframe ||
      overlays.some((o) => o.name === "selected-mask-preview")
    ) {
      releaseTargets();
      canvas.dataset[`${role}Mode`] = engine.specimen.material.wireframe
        ? "wireframe"
        : "selected-mask-preview";
      canvas.dataset[`${role}PassCount`] = String(opticalCount);
      r.render(engine.scene, engine.camera);
      return;
    }
    const originalTarget = r.getRenderTarget(),
      drawingSize = r.getDrawingBufferSize(new THREE.Vector2()),
      pair = pairFor(
        uv ? "uv" : "native",
        originalTarget?.width || drawingSize.x,
        originalTarget?.height || drawingSize.y,
      ),
      w = pair.lit.width,
      h = pair.lit.height;
    withTextureOpticalState(engine, ({ saved, activate }) => {
      r.autoClear = false;
      r.toneMapping = THREE.NoToneMapping;
      // The native auxiliary transmission target is not our sampler. Keep its
      // required allocation small; restore this setting for every other job.
      r.transmissionResolutionScale = 0.0625;
      r.setScissorTest(false);
      r.setRenderTarget(pair.history);
      setRenderTargetViewport(r, w, h);
      blit.material = copyMaterial;
      copyUniforms.source.value = fallback;
      r.render(blitScene, blitCamera);
      uniforms.uTextureOpticalHistory.value = pair.history.texture;
      uniforms.uTextureOpticalSize.value.set(w, h);
      if (
        r.shadowMap.enabled &&
        (r.shadowMap.autoUpdate || r.shadowMap.needsUpdate)
      ) {
        // Prime shadows with ALL original material geometry visible. The color
        // is discarded, so higher rows cannot leak into optical history.
        r.setRenderTarget(pair.lit);
        setRenderTargetViewport(r, w, h);
        r.clear();
        r.render(engine.scene, engine.camera);
      }
      r.shadowMap.autoUpdate = false;
      r.shadowMap.needsUpdate = false;
      r.setRenderTarget(pair.lit);
      setRenderTargetViewport(r, w, h);
      r.clear();
      activate(new Set(base));
      r.render(engine.scene, engine.camera);
      engine.scene.background = null;
      for (const step of steps) {
        if (step.kind === "optical") {
          copyUniforms.source.value = pair.lit.texture;
          blit.material = copyMaterial;
          r.setRenderTarget(pair.history);
          setRenderTargetViewport(r, w, h);
          r.render(blitScene, blitCamera);
          r.setRenderTarget(pair.lit);
          setRenderTargetViewport(r, w, h);
        }
        activate(new Set(step.meshes));
        r.render(engine.scene, engine.camera);
      }
      r.toneMapping = saved.toneMapping;
      r.setRenderTarget(saved.target, saved.cube, saved.mip);
      r.setViewport(saved.viewport);
      r.setScissor(saved.scissor);
      r.setScissorTest(saved.scissorTest);
      r.setClearColor(saved.clear, saved.alpha);
      r.clear();
      blit.material = presentMaterial;
      presentUniforms.source.value = pair.lit.texture;
      presentUniforms.premultiply.value = saved.target ? 0 : 1;
      presentUniforms.displayToneMapping.value = saved.toneMapping;
      presentUniforms.toneMappingExposure.value = r.toneMappingExposure;
      // The canvas needs an explicit transfer. The SRGB8 UV framebuffer does
      // the transfer in hardware; encoding twice would brighten that preview.
      presentUniforms.displaySRGB.value =
        !saved.target && r.outputColorSpace === THREE.SRGBColorSpace ? 1 : 0;
      r.render(blitScene, blitCamera);
      // Populate onscreen depth (the fullscreen presentation writes no depth)
      // and draw UI helpers AFTER display mapping, preserving their own colors.
      if (overlays.length) {
        activate(new Set([...depth, ...overlays]));
        r.render(engine.scene, engine.camera);
      }
    });
    canvas.dataset[`${role}Mode`] = "ordered-linear-history";
    canvas.dataset[`${role}PassCount`] = String(opticalCount);
    canvas.dataset[`${role}Size`] = JSON.stringify([w, h]);
    canvas.dataset[`${role}History`] = JSON.stringify(
      steps.map((s) => ({
        kind: s.kind,
        order: s.meshes.map((m) => m.renderOrder),
        names: s.meshes.map((m) => m.name),
      })),
    );
  }
  return {
    install: (material) => installTextureOpticalHistory(material, uniforms),
    render,
    dispose() {
      disposed = true;
      releaseTargets();
      fallback.dispose();
      copyMaterial.dispose();
      presentMaterial.dispose();
      plane.dispose();
    },
  };
}
