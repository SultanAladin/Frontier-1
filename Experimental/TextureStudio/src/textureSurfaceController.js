import { hasDigitalPaint } from "./textureDigitalPaint.js";
import { createTextureSystemBaseMaterial } from "./textureSystemBaseMaterial.js";
import { maskHasPaint } from "./textureMaskPaint.js";
import {
  installMaskPaintCoverage,
} from "./maskPaintRaster.js";
import { createMaskCoverage } from "./maskCoverageFactory.js";
import * as THREE from "three";
import { createMaterial } from "./materials.js";
import {
  evaluateTextureStack,
  textureLayerFullyCovers,
} from "./textureStackEvaluation.js";
import {
  partGeometry,
  installSurfaceCoordinates,
  texturePart,
} from "./textureSurfaceModel.js";
import {
  createPointGradientUniforms,
  updatePointGradientUniforms,
  installPointGradientAlpha,
} from "./pointGradientViewport.js";
import {
  installPartStampCoverage,
  surfaceMaskOwners,
} from "./decalProjection.js";

export function installPartReplacement(material, uniforms) {
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, uniforms);
    shader.fragmentShader =
      "uniform vec4 uPartReplaced;\n" + shader.fragmentShader;
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <alphatest_fragment>",
      "if(vSurfacePart>.5&&vSurfacePart<4.5&&uPartReplaced[int(vSurfacePart+.5)-1]>.5)discard;\n#include <alphatest_fragment>",
    );
  };
  material.customProgramCacheKey = () => cache() + "|part-replacement-v1";
  installSurfaceCoordinates(material);
}
export function createSurfaceAssignmentController(
  engine,
  releaseMaterial,
  editorRef,
) {
  const group = new THREE.Group();
  group.name = "part-materials";
  engine.specimen.add(group);
  const entries = new Map(),
    uniforms = { uPartReplaced: { value: new Float32Array(4) } };
  let epoch = 0,
    disposed = false,
    signature = "",
    geometry = null,
    latestDoc = null,
    highlight = null,
    highlightedId = null;
  const depth = new THREE.MeshDepthMaterial({
      depthPacking: THREE.RGBADepthPacking,
    }),
    distance = new THREE.MeshDistanceMaterial();
  installPartReplacement(depth, uniforms);
  installPartReplacement(distance, uniforms);
  engine.specimen.customDepthMaterial = depth;
  engine.specimen.customDistanceMaterial = distance;
  function release(entry) {
    entry.mesh.removeFromParent();
    entry.mesh.geometry.dispose();
    releaseMaterial(entry.mesh.material);
    entry.coverage.uPointGradientData.value.dispose();
    entry.paintCoverage?.dispose();
  }
  const depthSurface = new THREE.Mesh(
    engine.specimen.geometry,
    new THREE.MeshBasicMaterial({
      colorWrite: false,
      depthWrite: true,
      side: THREE.DoubleSide,
    }),
  );
  depthSurface.name = "system-layer-0-depth";
  depthSurface.renderOrder = -1;
  // A depth-only support pass keeps transparent material/decal layers from
  // painting the back of the teapot over its visible front. It never adds RGB.
  engine.specimen.add(depthSurface);
  let plan = null;
  function publish() {
    uniforms.uPartReplaced.value.fill(0);
    for (const id of plan?.replacedParts || []) {
      const part = texturePart(id);
      if (part) uniforms.uPartReplaced.value[part.index - 1] = 1;
    }
    const canvas = engine.renderer.domElement;
    canvas.dataset.partMaterials = JSON.stringify(
      [...entries.values()].flatMap((entry) =>
        entry.partIds.map((partId) => ({
          partId,
          layerId: entry.layerId,
          materialId: entry.parameters.id,
          name: entry.parameters.name,
        })),
      ),
    );
    canvas.dataset.partMaterialCount = String(
      new Set([...entries.values()].flatMap((e) => e.partIds)).size,
    );
    canvas.dataset.layerPassCount = String(entries.size);
    canvas.dataset.layerPasses = JSON.stringify(
      [...entries.values()].map((e) => ({
        layerId: e.layerId,
        partIds: e.partIds,
        renderOrder: e.mesh.renderOrder,
      })),
    );
    canvas.dataset.systemBaseParts = JSON.stringify(plan?.fallbackParts || []);
    canvas.dataset.layerStackReady = "true";
    canvas.dataset.digitalPaintPassCount = String(
      [...entries.values()].filter((e) => e.mesh.userData.digitalPaintOnly)
        .length,
    );
    engine.renderer.shadowMap.needsUpdate = true;
    engine.dirty = true;
  }
  async function update(doc, wireframe = false) {
    latestDoc = doc;
    const desired = evaluateTextureStack(doc);
    const nextSignature =
      JSON.stringify(
        desired.surfaces.map((r) => [
          r.layer.id,
          r.parameters,
          r.paintOnly,
          hasDigitalPaint(r.layer),
          r.opacity,
          r.partIds,
          r.renderOrder,
          r.ancestors.map((l) => [
            doc.layers.indexOf(l),
            l.mask,
            l.decals
              .filter((d) => d.asMask)
              .map((d) => [d.id, d.visible, d.opacity > 0]),
          ]),
        ]),
      ) + wireframe;
    if (signature === nextSignature && geometry === engine.specimen.geometry)
      return;
    signature = nextSignature;
    geometry = engine.specimen.geometry;
    depthSurface.geometry = geometry;
    const job = ++epoch,
      previous = engine.compileTask || Promise.resolve();
    engine.compileTask = (async () => {
      await previous.catch(() => {});
      if (disposed || job !== epoch) return;
      engine.compiling = true;
      engine.renderer.domElement.dataset.materialReady = "false";
      engine.renderer.domElement.dataset.layerStackReady = "false";
      for (const entry of entries.values()) release(entry);
      entries.clear();
      plan = desired;
      try {
        for (const record of desired.surfaces) {
          const clipped = record.layer.surfaceTarget
            ? partGeometry(
                engine.specimen.geometry,
                record.layer.surfaceTarget.partId,
              )
            : engine.specimen.geometry.clone();
          if (!clipped) continue;
          const parameters = record.parameters,
            material = record.paintOnly
              ? createTextureSystemBaseMaterial()
              : createMaterial(parameters),
            coverage = createPointGradientUniforms(),
            masks = record.ancestors
              .map((l) => l.mask)
              .filter(
                (m) =>
                  m &&
                  !maskHasPaint(m) &&
                  ["fill", "gradient"].includes(m.kind),
              );
          updatePointGradientUniforms(
            coverage,
            { masks },
            engine.specimen.geometry,
          );
          installPointGradientAlpha(material, coverage);
          const paintCoverage = createMaskCoverage(
            engine.specimen.geometry,
            engine.renderer,
          );
          await paintCoverage.update(record.ancestors, doc);
          if (disposed || job !== epoch) {
            clipped.dispose();
            releaseMaterial(material);
            coverage.uPointGradientData.value.dispose();
            paintCoverage.dispose();
            return;
          }
          installMaskPaintCoverage(material, paintCoverage.uniforms);
          installPartStampCoverage(
            material,
            engine.decalMasks,
            surfaceMaskOwners(doc, record.layer.id),
          );
          engine.meshSupport?.install(material);
          engine.digitalPaint?.install(material, record.layer.id);
          engine.stackUV?.install(material);
          engine.opticalStack?.install(material);
          material.opacity *= record.opacity;
          material.wireframe = wireframe;
          // Every authored surface belongs to the same ordered transparent
          // queue as decals, even when alpha=1. Three's separate opaque queue
          // was the reason lower decals always covered upper car-paint layers.
          material.transparent = true;
          material.depthWrite = false;
          material.forceSinglePass = true;
          material.polygonOffset = true;
          material.polygonOffsetFactor = -1;
          material.polygonOffsetUnits = -1;
          material.alphaTest = 0.005;
          const mesh = new THREE.Mesh(clipped, material);
          mesh.name = `layer-material-${record.layer.id}`;
          mesh.userData.textureStackPass = true;
          mesh.userData.digitalPaintOnly = record.paintOnly;
          mesh.userData.textureLayerId = record.layer.id;
          mesh.userData.textureStackParts = record.partIds;
          mesh.userData.textureStackFullyCovers =
            textureLayerFullyCovers(record) &&
            material.opacity === 1 &&
            !material.alphaMap;
          mesh.castShadow = mesh.receiveShadow = true;
          mesh.renderOrder = record.renderOrder;
          group.add(mesh);
          entries.set(record.layer.id, {
            mesh,
            coverage,
            paintCoverage,
            parameters,
            partIds: record.partIds,
            layerId: record.layer.id,
            layerIndex: doc.layers.indexOf(record.layer),
          });
        }
        publish();
        await Promise.all(
          [...entries.values()].map((e) => e.mesh.material.userData.ready),
        );
        if (disposed || job !== epoch) return;
        await engine.renderer.compileAsync(engine.scene, engine.camera);
        if (disposed || job !== epoch) return;
        engine.renderer.domElement.dataset.materialReady = "true";
        engine.decals?.update(latestDoc, editorRef.current);
      } catch (error) {
        signature = "";
        editorRef.current?.onError?.(
          `Layer material could not be prepared: ${error.message}`,
        );
      } finally {
        if (!disposed && job === epoch) {
          engine.compiling = false;
          engine.dirty = true;
        }
      }
    })();
    return engine.compileTask;
  }
  function setHighlight(partId) {
    if (highlightedId === partId) return;
    highlightedId = partId;
    if (highlight) {
      highlight.removeFromParent();
      highlight.geometry.dispose();
      releaseMaterial(highlight.material);
      highlight = null;
    }
    const part = texturePart(partId),
      clipped = part && partGeometry(engine.specimen.geometry, partId);
    if (clipped) {
      highlight = new THREE.LineSegments(
        new THREE.EdgesGeometry(clipped, 20),
        new THREE.LineBasicMaterial({
          color: part.color,
          transparent: true,
          opacity: 0.8,
          depthWrite: false,
        }),
      );
      clipped.dispose();
      highlight.name = "part-target-outline";
      highlight.renderOrder = 10001;
      engine.specimen.add(highlight);
    }
    engine.renderer.domElement.dataset.surfaceTarget = partId || "";
    engine.dirty = true;
  }
  return {
    uniforms,
    update,
    setHighlight,
    installBase(material) {
      installPartReplacement(material, uniforms);
    },
    materialAt(partId, ownerId = null) {
      const ownerIndex =
        latestDoc?.layers.findIndex((l) => l.id === ownerId) ?? -1;
      const candidates = [...entries.values()].filter(
        (e) =>
          (!partId || e.partIds.includes(partId)) &&
          (ownerIndex < 0 || e.layerIndex >= ownerIndex),
      );
      return candidates.at(-1)?.mesh.material || engine.specimen.material;
    },
    dispose() {
      disposed = true;
      epoch++;
      setHighlight(null);
      for (const entry of entries.values()) release(entry);
      entries.clear();
      group.removeFromParent();
      depthSurface.removeFromParent();
      depthSurface.material.dispose();
      releaseMaterial(depth);
      releaseMaterial(distance);
    },
  };
}
