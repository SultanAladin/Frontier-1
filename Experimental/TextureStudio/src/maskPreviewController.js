import { collectDecalRecords } from "./decalModel.js";
import { isTextureLayerVisible } from "./textureDocument.js";
import * as THREE from "three";
import {
  createPointGradientUniforms,
  updatePointGradientUniforms,
  POINT_GRADIENT_GLSL,
} from "./pointGradientViewport.js";
import { SURFACE_MASK_GLSL } from "./decalProjection.js";
import {
  rasterizeMaskPaint,
  rasterizeMaskField,
  maskColourSource,
  readMaskSource,
  MASK_ATLAS_WIDTH,
  MASK_TILE_EDGE,
} from "./maskPaintRaster.js";
import { uvTopology } from "./textureUVGeometry.js";
import { maskRegionContains } from "./textureMaskPaint.js";

// The grayscale material is shared by the 3D mesh and the filled UV readback.
// Always restore its state and the renderer, including when GPU readback fails.
export function withMaskAtlasState(renderer, material, drawUV, target, draw) {
  const previous = {
    target: renderer.getRenderTarget(),
    clear: renderer.getClearColor(new THREE.Color()),
    alpha: renderer.getClearAlpha(),
    drawUV: drawUV.value,
    depthTest: material.depthTest,
    depthWrite: material.depthWrite,
    wireframe: material.wireframe,
  };
  try {
    drawUV.value = 1;
    material.depthTest = false;
    material.depthWrite = false;
    // Wireframe is a geometry-view choice, never a sparse mask atlas.
    material.wireframe = false;
    renderer.setRenderTarget(target);
    renderer.setClearColor("#202124", 1);
    return draw();
  } finally {
    drawUV.value = previous.drawUV;
    material.depthTest = previous.depthTest;
    material.depthWrite = previous.depthWrite;
    material.wireframe = previous.wireframe;
    renderer.setRenderTarget(previous.target);
    renderer.setClearColor(previous.clear, previous.alpha);
  }
}

export function createMaskPreviewController(engine, onAtlas) {
  const gradient = createPointGradientUniforms(),
    paint = new THREE.CanvasTexture(document.createElement("canvas")),
    uniforms = {
      ...gradient,
      ...engine.decalMasks,
      uSurfaceStampOwners: { value: new Int32Array(9) },
      uSurfaceStampOwnerCount: { value: 0 },
      uMaskKind: { value: 0 },
      uMaskStampIndex: { value: -1 },
      uMaskFill: { value: 1 },
      uMaskStrength: { value: 1 },
      uMaskInvert: { value: 0 },
      uMaskEnabled: { value: 1 },
      uMaskDrawUV: { value: 0 },
      uMaskPaint: { value: paint },
    };
  paint.colorSpace = THREE.NoColorSpace;
  paint.minFilter = paint.magFilter = THREE.LinearFilter;
  paint.generateMipmaps = false;
  const material = new THREE.ShaderMaterial({
    uniforms,
    side: THREE.DoubleSide,
    toneMapped: false,
    transparent: true,
    depthWrite: false,
    forceSinglePass: true,
    polygonOffset: true,
    polygonOffsetFactor: -8,
    polygonOffsetUnits: -8,
    vertexShader: `attribute vec2 uvAuthoring;attribute float surfacePart;varying vec2 vAuthoringUv;varying float vSurfacePart;varying vec3 vProcPosition,vProcNormal;uniform int uMaskDrawUV;
    void main(){vAuthoringUv=uvAuthoring;vSurfacePart=surfacePart;vProcPosition=position;vProcNormal=normal;
    gl_Position=uMaskDrawUV==1?vec4(uvAuthoring.x/2.-1.,uvAuthoring.y*2.-1.,0.,1.):projectionMatrix*modelViewMatrix*vec4(position,1.);}`,
    fragmentShader: `varying vec2 vAuthoringUv;varying float vSurfacePart;varying vec3 vProcPosition,vProcNormal;
    uniform int uMaskStampIndex;
    ${POINT_GRADIENT_GLSL}\n${SURFACE_MASK_GLSL.replace("if(i>=uSurfaceStampCount)break;", "if(i>=uSurfaceStampCount)break;if(uMaskStampIndex>=0&&i!=uMaskStampIndex)continue;")}
    uniform int uMaskKind,uMaskEnabled,uMaskInvert;uniform float uMaskFill,uMaskStrength;uniform sampler2D uMaskPaint;
    void main(){float v=uMaskFill;
     if(uMaskKind==2){vec3 c=samplePointGradient((vProcPosition-uPointGradientCentre)/max(uPointGradientRadius,1e-6),0);v=clamp(dot(c,vec3(.2126,.7152,.0722)),0.,1.);}
     if(uMaskKind==5)v=uMaskStampIndex>=0?surfaceStampCoverage(vProcPosition,vProcNormal):1.;
     vec4 ink=texture2D(uMaskPaint,vec2(vAuthoringUv.x/4.,vAuthoringUv.y));v=mix(v,ink.r,ink.a);
     if(uMaskInvert==1)v=1.-v;v=uMaskEnabled==1?mix(1.,v,uMaskStrength):1.;gl_FragColor=vec4(vec3(v),1.);}`,
  });
  const overlay = new THREE.Mesh(engine.specimen.geometry, material);
  overlay.name = "selected-mask-preview";
  overlay.renderOrder = 20000;
  overlay.visible = false;
  engine.specimen.add(overlay);
  const uvScene = new THREE.Scene(),
    uvMesh = new THREE.Mesh(engine.specimen.geometry, material),
    camera = new THREE.Camera(),
    target = new THREE.WebGLRenderTarget(MASK_ATLAS_WIDTH, MASK_TILE_EDGE, {
      depthBuffer: false,
      stencilBuffer: false,
    }),
    canvas = document.createElement("canvas");
  uvMesh.frustumCulled = false;
  uvScene.add(uvMesh);
  canvas.width = MASK_ATLAS_WIDTH;
  canvas.height = MASK_TILE_EDGE;
  let ticket = 0,
    disposed = false,
    outline = null,
    regionKey = "",
    showingStamp = false,
    fieldKey = "",
    fieldGeometry = null,
    atlasVersion = 0,
    stampKey = "";
  function selection(region) {
    const key = JSON.stringify(region);
    if (key === regionKey) return;
    regionKey = key;
    if (outline) {
      outline.removeFromParent();
      outline.geometry.dispose();
      outline.material.dispose();
      outline = null;
    }
    if (!region) return;
    const positions = [],
      source = engine.specimen.geometry.attributes.position;
    for (const t of uvTopology(engine.specimen.geometry).triangles)
      if (maskRegionContains(region, t))
        for (const id of t.ids)
          positions.push(source.getX(id), source.getY(id), source.getZ(id));
    const g = new THREE.BufferGeometry();
    g.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
    const edges = new THREE.EdgesGeometry(g, 10);
    g.dispose();
    outline = new THREE.LineSegments(
      edges,
      new THREE.LineBasicMaterial({
        color: "#ffd08a",
        depthTest: false,
        transparent: true,
        opacity: 0.9,
      }),
    );
    outline.renderOrder = 20001;
    outline.name = "mask-region-outline";
    engine.specimen.add(outline);
  }
  function drawAtlas() {
    if (disposed) return;
    const renderer = engine.renderer;
    withMaskAtlasState(renderer, material, uniforms.uMaskDrawUV, target, () => {
      renderer.clear();
      renderer.render(uvScene, camera);
      const bytes = new Uint8Array(MASK_ATLAS_WIDTH * MASK_TILE_EDGE * 4);
      renderer.readRenderTargetPixels(
        target,
        0,
        0,
        MASK_ATLAS_WIDTH,
        MASK_TILE_EDGE,
        bytes,
      );
      const ctx = canvas.getContext("2d"),
        image = ctx.createImageData(canvas.width, canvas.height),
        stride = canvas.width * 4;
      for (let y = 0; y < canvas.height; y++)
        image.data.set(
          bytes.subarray(
            (canvas.height - 1 - y) * stride,
            (canvas.height - y) * stride,
          ),
          y * stride,
        );
      ctx.putImageData(image, 0, 0);
    });
    atlasVersion++;
    onAtlas?.({ canvas, revision: atlasVersion });
    engine.dirty = true;
  }
  return {
    material,
    setWireframe(value) {
      if (disposed) return;
      material.wireframe = !!value;
      engine.dirty = true;
    },
    async update(state) {
      const job = ++ticket,
        layer = state?.doc.layers.find((l) => l.id === state.layerId),
        mask = state?.decalId ? null : state?.mask || layer?.mask,
        component =
          state?.decalId &&
          layer?.decals.find((d) => d.id === state.decalId && d.asMask);
      showingStamp = !!component;
      overlay.geometry = uvMesh.geometry = engine.specimen.geometry;
      overlay.visible = !!(state?.preview === "mask" && (mask || component));
      selection(state?.region);
      engine.renderer.domElement.dataset.maskPreview = overlay.visible
        ? "mask"
        : "material";
      if (!mask && !component) {
        onAtlas?.(null);
        engine.dirty = true;
        return;
      }
      engine.dirty = true;
      const nextKey = JSON.stringify(
        component
          ? [
              component,
              state.doc.stamps,
              state.doc.layers.map((l) => [
                l.id,
                l.opacity,
                l.visible,
                l.decals.filter((d) => d.asMask),
              ]),
            ]
          : [
              mask,
              mask?.kind === "color"
                ? maskColourSource(layer, state.doc)?.dataUrl
                : null,
            ],
      );
      if (nextKey === fieldKey && fieldGeometry === engine.specimen.geometry) {
        onAtlas?.({ canvas, revision: atlasVersion });
        return;
      }
      let image;
      if (mask?.kind === "color")
        image = rasterizeMaskField(
          mask,
          engine.specimen.geometry,
          await readMaskSource(maskColourSource(layer, state.doc)),
        );
      else image = rasterizeMaskPaint(mask, engine.specimen.geometry);
      if (disposed || job !== ticket) return;
      paint.image = image;
      paint.needsUpdate = true;
      uniforms.uMaskKind.value = component
        ? 5
        : mask.kind === "gradient"
          ? 2
          : 1;
      uniforms.uMaskFill.value = mask?.fill ?? 1;
      // Colour source fields include all mask controls already; other kinds
      // are evaluated on the GPU with exactly the same object-space gradient.
      const precomputed = mask?.kind === "color";
      uniforms.uMaskStrength.value = precomputed
        ? 1
        : (mask?.strength ?? 100) / 100;
      uniforms.uMaskInvert.value = !precomputed && mask?.inverted ? 1 : 0;
      uniforms.uMaskEnabled.value =
        precomputed || mask?.enabled !== false ? 1 : 0;
      uniforms.uMaskStampIndex.value = component
        ? collectDecalRecords(state.doc, isTextureLayerVisible)
            .filter((r) => r.decal.asMask && r.layer.opacity > 0)
            .findIndex((r) => r.decal.id === component.id)
        : -1;
      uniforms.uSurfaceStampOwners.value.fill(0);
      uniforms.uSurfaceStampOwners.value[0] =
        state.doc.layers.indexOf(layer) + 1;
      uniforms.uSurfaceStampOwnerCount.value = component ? 1 : 0;
      updatePointGradientUniforms(
        gradient,
        { fill: mask?.kind === "gradient" ? mask.gradient : null },
        engine.specimen.geometry,
      );
      fieldKey = nextKey;
      fieldGeometry = engine.specimen.geometry;
      drawAtlas();
    },
    refreshStamps() {
      if (showingStamp && !disposed) {
        const key =
          Array.from(uniforms.uSurfaceStampData.value.image.data).join(",") +
          "|" +
          uniforms.uSurfaceStampAtlas.value.version +
          "|" +
          uniforms.uMaskStampIndex.value;
        if (stampKey !== key) {
          stampKey = key;
          drawAtlas();
        }
      }
    },
    dispose() {
      disposed = true;
      ticket++;
      selection(null);
      overlay.removeFromParent();
      material.dispose();
      paint.dispose();
      gradient.uPointGradientData.value.dispose();
      target.dispose();
    },
  };
}
