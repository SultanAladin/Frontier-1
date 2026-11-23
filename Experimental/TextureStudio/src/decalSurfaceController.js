import { textureLayerRenderOrder } from "./textureStackEvaluation.js";
import { maskHasPaint } from "./textureMaskPaint.js";
import {
  installMaskPaintCoverage,
} from "./maskPaintRaster.js";
import { createMaskCoverage } from "./maskCoverageFactory.js";
import { installSurfaceCoordinates } from "./textureSurfaceModel.js";
import * as THREE from "three";
import { TransformControls } from "three/addons/controls/TransformControls.js";
import {
  collectDecalRecords,
  normalizeDecal,
  createDecal,
} from "./decalModel.js";
import { textureAncestors, isTextureLayerVisible } from "./textureDocument.js";
import {
  createPointGradientUniforms,
  updatePointGradientUniforms,
  installPointGradient,
  installPointGradientAlpha,
  gradientObjectPosition,
} from "./pointGradientViewport.js";
import {
  decalQuaternion,
  projectDecalGeometry,
  decalSurfaceHit,
  surfaceMaskOwners,
  updateSurfaceMaskUniforms,
  SURFACE_MASK_GLSL,
} from "./decalProjection.js";

async function stampTextures(stamp) {
  const image = new Image();
  image.src = stamp.raster.dataUrl;
  await image.decode();
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = 256;
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  ctx.drawImage(image, 0, 0, 256, 256);
  const pixels = ctx.getImageData(0, 0, 256, 256),
    heights = new Float32Array(256 * 256);
  const coverage = ctx.createImageData(256, 256),
    height = ctx.createImageData(256, 256),
    normal = ctx.createImageData(256, 256);
  for (let i = 0; i < heights.length; i++) {
    const p = i * 4,
      alpha = pixels.data[p + 3];
    heights[i] =
      (((pixels.data[p] * 0.2126 +
        pixels.data[p + 1] * 0.7152 +
        pixels.data[p + 2] * 0.0722) /
        255) *
        alpha) /
      255;
    for (let c = 0; c < 3; c++) {
      coverage.data[p + c] = alpha;
      height.data[p + c] = Math.round(heights[i] * 255);
    }
    coverage.data[p + 3] = height.data[p + 3] = 255;
  }
  const h = (x, y) =>
    heights[
      Math.max(0, Math.min(255, y)) * 256 + Math.max(0, Math.min(255, x))
    ];
  for (let y = 0; y < 256; y++)
    for (let x = 0; x < 256; x++) {
      const v = new THREE.Vector3(
          (h(x - 1, y) - h(x + 1, y)) * 3,
          (h(x, y + 1) - h(x, y - 1)) * 3,
          1,
        ).normalize(),
        p = (y * 256 + x) * 4;
      normal.data[p] = Math.round((v.x * 0.5 + 0.5) * 255);
      normal.data[p + 1] = Math.round((v.y * 0.5 + 0.5) * 255);
      normal.data[p + 2] = Math.round((v.z * 0.5 + 0.5) * 255);
      normal.data[p + 3] = 255;
    }
  const texture = (data, colour = false) => {
    const target = document.createElement("canvas");
    target.width = target.height = 256;
    target.getContext("2d").putImageData(data, 0, 0);
    const t = new THREE.CanvasTexture(target);
    t.colorSpace = colour ? THREE.SRGBColorSpace : THREE.NoColorSpace;
    t.minFilter = THREE.LinearMipmapLinearFilter;
    t.magFilter = THREE.LinearFilter;
    return t;
  };
  return {
    image,
    colour: texture(pixels, true),
    coverage: texture(coverage),
    height: texture(height),
    normal: texture(normal),
  };
}
function decalMaterial(engine, record, textures, masks, owners, pointCoverage) {
  const decal = record.decal,
    on = (id) => decal.channels.includes(id),
    value = decal.channelValues;
  const base =
    engine.surfaceAssignments?.materialAt(
      decal.target?.partId,
      record.layerId,
    ) || engine.specimen.material;
  const material = new THREE.MeshPhysicalMaterial({
    color: on("baseColor") ? "#ffffff" : base.color,
    map: on("baseColor") ? textures.colour : null,
    alphaMap: on("baseColor") ? null : textures.coverage,
    roughness: on("roughness") ? value.roughness : base.roughness,
    metalness: on("metalness") ? value.metalness : base.metalness,
    opacity: decal.opacity * (on("opacity") ? value.opacity : 1),
    transparent: true,
    alphaTest: 0.005,
    depthWrite: false,
    polygonOffset: true,
    polygonOffsetFactor: -4,
    polygonOffsetUnits: -4,
    side: THREE.FrontSide,
    clearcoat: on("clearcoat") ? value.clearcoat : base.clearcoat,
    clearcoatRoughness: base.clearcoatRoughness,
    ior: on("refractionIndex") ? value.refractionIndex : base.ior,
    anisotropy: on("anisotropy") ? value.anisotropy : base.anisotropy,
    anisotropyRotation: on("anisotropyAngle")
      ? (value.anisotropyAngle * Math.PI) / 180
      : base.anisotropyRotation,
    sheen: on("sheen") ? 1 : base.sheen,
    sheenColor: on("sheen") ? value.sheen : base.sheenColor,
    sheenRoughness: 0.8,
    emissive: on("emission") ? value.emission : base.emissive,
    emissiveMap: on("emission") ? textures.colour : null,
    normalMap: on("normal") ? textures.normal : null,
    normalScale: new THREE.Vector2(value.height * 0.4, value.height * 0.4),
    bumpMap: on("height") && !on("normal") ? textures.height : null,
    bumpScale: on("height") ? value.height * 0.045 : 0,
    envMapIntensity: base.envMapIntensity,
  });
  const original = material.onBeforeCompile;
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    shader.vertexShader =
      "varying vec3 vProcPosition;varying vec3 vProcNormal;\n" +
      shader.vertexShader;
    shader.vertexShader = shader.vertexShader.replace(
      "#include <begin_vertex>",
      "#include <begin_vertex>\nvProcPosition=position;vProcNormal=normal;",
    );
    shader.fragmentShader =
      "varying vec3 vProcPosition;varying vec3 vProcNormal;\n" +
      SURFACE_MASK_GLSL +
      shader.fragmentShader;
    Object.assign(shader.uniforms, masks, owners, {
      uDecalAO: { value: on("occlusion") ? value.occlusion : 1 },
      uDecalSubsurface: {
        value: new THREE.Color(on("subsurface") ? value.subsurface : "#000000"),
      },
    });
    shader.fragmentShader =
      "uniform float uDecalAO;uniform vec3 uDecalSubsurface;\n" +
      shader.fragmentShader;
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <alphatest_fragment>",
      "diffuseColor.a*=surfaceStampCoverage(vProcPosition,vProcNormal);\n#include <alphatest_fragment>",
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lights_fragment_end>",
      "#include <lights_fragment_end>\nreflectedLight.indirectDiffuse*=uDecalAO;reflectedLight.indirectSpecular*=uDecalAO;reflectedLight.indirectDiffuse+=uDecalSubsurface*diffuseColor.rgb*.16*pow(1.-abs(dot(normal,geometryViewDir)),2.);",
    );
  };
  // RGB-disabled decals use the selected base-colour field rather than painting
  // a white quad over it. Other PBR channels can still affect their stencil.
  if (!on("baseColor") && engine.pointUniforms)
    installPointGradient(material, engine.pointUniforms);
  installPointGradientAlpha(material, pointCoverage);
  material.customProgramCacheKey = () =>
    `alloy-surface-decal-v2-${on("baseColor") ? "art" : "underlay"}`;
  installSurfaceCoordinates(material);
  return material;
}
export function createSurfaceDecalController(
  engine,
  masks,
  rootOwners,
  editorRef,
  releaseMaterial,
) {
  const group = new THREE.Group();
  group.name = "surface-decals";
  engine.specimen.add(group);
  const projector = new THREE.Object3D();
  projector.name = "decal-projector";
  engine.specimen.add(projector);
  const transform = new TransformControls(
    engine.camera,
    engine.renderer.domElement,
  );
  transform.setSpace("local");
  transform.setSize(0.7);
  transform.enabled = false;
  transform.getHelper().userData.textureStackOverlay = true;
  engine.scene.add(transform.getHelper());
  const meshes = new Map(),
    cache = new Map();
  let doc = null,
    records = [],
    selected = null,
    gesture = null,
    ghost = null,
    destroyed = false,
    ticket = 0,
    pendingHover = null,
    hoverFrame = 0,
    hoverTime = 0,
    atlasStamp = "",
    aimTicket = 0;
  const canvas = engine.renderer.domElement;
  const releaseTextures = (value) => {
    if (value)
      for (const key of ["colour", "coverage", "height", "normal"])
        releaseMaterial(value[key]);
  };
  const textureKey = (stamp) => `${stamp.id}:${stamp.raster.dataUrl}`;
  const textureFor = (stamp) => {
    const key = textureKey(stamp);
    if (!cache.has(key)) {
      const entry = { result: null, retired: false, promise: null };
      entry.promise = stampTextures(stamp).then((value) => {
        if (destroyed || entry.retired) {
          releaseTextures(value);
          return null;
        }
        entry.result = value;
        return value;
      });
      cache.set(key, entry);
    }
    return cache.get(key).promise;
  };
  function retireTextures(keep) {
    for (const [key, entry] of cache)
      if (!keep.has(key)) {
        cache.delete(key);
        entry.retired = true;
        if (entry.result) {
          releaseTextures(entry.result);
          entry.result = null;
        }
      }
  }
  function releaseMesh(value) {
    value.mesh.removeFromParent();
    value.mesh.geometry.dispose();
    releaseMaterial(value.mesh.material);
    value.pointCoverage.uPointGradientData.value.dispose();
    value.paintCoverage?.dispose();
  }
  function clearGhost(cancelPending = true) {
    if (cancelPending) {
      aimTicket++;
      pendingHover = null;
      cancelAnimationFrame(hoverFrame);
      hoverFrame = 0;
    }
    if (ghost) {
      ghost.removeFromParent();
      ghost.geometry.dispose();
      releaseMaterial(ghost.material);
      ghost = null;
    }
    engine.dirty = true;
  }
  function copyProjector(decal) {
    engine.specimen.geometry.computeBoundingSphere();
    const radius = engine.specimen.geometry.boundingSphere.radius;
    projector.position.copy(
      gradientObjectPosition(engine.specimen, decal.position),
    );
    projector.quaternion.copy(decalQuaternion(decal));
    projector.scale.set(
      decal.size[0] * radius,
      decal.size[1] * radius,
      decal.depth * radius,
    );
    projector.updateMatrixWorld();
  }
  function projectorDraft() {
    const sphere = engine.specimen.geometry.boundingSphere;
    return normalizeDecal({
      ...gesture.before,
      position: projector.position
        .clone()
        .sub(sphere.center)
        .divideScalar(sphere.radius)
        .toArray(),
      quaternion: projector.quaternion.toArray(),
      size: [
        projector.scale.x / sphere.radius,
        projector.scale.y / sphere.radius,
      ],
      depth: projector.scale.z / sphere.radius,
      roll: 0,
    });
  }
  function updateMaskData(draft = null) {
    if (!doc) return;
    updateSurfaceMaskUniforms(
      masks,
      draft
        ? records.map((r) =>
            r.decal.id === draft.id ? { ...r, decal: draft } : r,
          )
        : records,
      engine.specimen,
      doc.stamps.map((s) => s.id),
    );
  }
  function cancelGesture() {
    if (!gesture) return;
    const old = gesture;
    gesture = null;
    transform.reset();
    transform.dragging = false;
    transform.axis = null;
    engine.controls.enabled = true;
    copyProjector(old.before);
    const entry = meshes.get(old.before.id);
    if (entry) {
      entry.mesh.geometry.dispose();
      entry.mesh.geometry = projectDecalGeometry(engine.specimen, old.before);
    }
    updateMaskData();
    engine.dirty = true;
  }
  transform.addEventListener("mouseDown", () => {
    const editor = editorRef.current;
    if (!selected || !editor?.active || editor.mode !== "transform") return;
    gesture = {
      before: selected.decal,
      expected: JSON.stringify(selected.decal),
      dirty: false,
    };
    engine.controls.enabled = false;
    clearGhost();
  });
  transform.addEventListener("objectChange", () => {
    if (!gesture) return;
    const draft = projectorDraft();
    gesture.dirty = true;
    const entry = meshes.get(draft.id);
    if (entry) {
      entry.mesh.geometry.dispose();
      entry.mesh.geometry = projectDecalGeometry(engine.specimen, draft);
    }
    updateMaskData(draft);
    engine.dirty = true;
  });
  transform.addEventListener("mouseUp", () => {
    if (!gesture) {
      engine.controls.enabled = true;
      return;
    }
    const active = gesture,
      draft = projectorDraft();
    gesture = null;
    engine.controls.enabled = true;
    if (active.dirty)
      editorRef.current?.onTransform(
        {
          position: draft.position,
          quaternion: draft.quaternion,
          size: draft.size,
          depth: draft.depth,
          roll: draft.roll,
        },
        active.expected,
      );
    // React will publish the accepted snapshot; rejected/no-op gestures restore.
    const entry = meshes.get(active.before.id);
    if (entry) {
      entry.mesh.geometry.dispose();
      entry.mesh.geometry = projectDecalGeometry(
        engine.specimen,
        active.before,
      );
    }
    copyProjector(active.before);
    updateMaskData();
    engine.dirty = true;
  });
  transform.addEventListener("change", () => {
    engine.dirty = true;
  });
  function pointerCancelled() {
    cancelGesture();
  }
  canvas.addEventListener("pointercancel", pointerCancelled);
  canvas.addEventListener("lostpointercapture", pointerCancelled);
  async function update(nextDoc, editor) {
    doc = nextDoc;
    const nextSelected =
      editor?.id &&
      doc.layers
        .find((l) => l.id === editor.layerId)
        ?.decals.find((d) => d.id === editor.id);
    if (
      gesture &&
      (!editor?.active ||
        editor.mode !== "transform" ||
        nextSelected?.id !== gesture.before.id ||
        JSON.stringify(nextSelected) !== gesture.expected)
    )
      cancelGesture();
    const owners = surfaceMaskOwners(doc, editor?.previewLayerId);
    rootOwners.uSurfaceStampOwners.value.set(owners.uSurfaceStampOwners.value);
    rootOwners.uSurfaceStampOwnerCount.value =
      owners.uSurfaceStampOwnerCount.value;
    records = collectDecalRecords(doc, isTextureLayerVisible).filter(
      (r) => r.layer.opacity > 0,
    );
    selected = records.find(
      (r) => r.decal.id === editor?.id && r.layerId === editor?.layerId,
    );
    const gizmo =
      editor?.active &&
      editor.mode === "transform" &&
      selected &&
      selected.decal.space !== "uv" &&
      !selected.decal.locked;
    transform.enabled = !!gizmo;
    if (gizmo) {
      if (!gesture) copyProjector(selected.decal);
      transform.attach(projector);
      transform.setMode(editor.gizmo || "translate");
    } else {
      transform.detach();
      clearGhost();
    }
    canvas.dataset.decalGizmo = gizmo ? editor.gizmo || "translate" : "off";
    const order = doc.stamps.map((s) => s.id),
      stampSignature = JSON.stringify(
        doc.stamps.map((s) => [s.id, s.raster.dataUrl]),
      );
    const job = ++ticket;
    try {
      const resources = await Promise.all(doc.stamps.map(textureFor));
      if (destroyed || job !== ticket) return;
      if (atlasStamp !== stampSignature) {
        const ctx = masks.uSurfaceStampAtlas.value.image.getContext("2d");
        ctx.clearRect(0, 0, 1024, 1024);
        resources.forEach((value, i) => {
          if (value)
            ctx.drawImage(
              value.image,
              (i % 4) * 256 + 1,
              Math.floor(i / 4) * 256 + 1,
              254,
              254,
            );
        });
        masks.uSurfaceStampAtlas.value.needsUpdate = true;
        atlasStamp = stampSignature;
      }
      updateMaskData(gesture ? projectorDraft() : null);
      const ids = new Set(
        records.filter((r) => !r.decal.asMask).map((r) => r.decal.id),
      );
      for (const [id, value] of meshes)
        if (!ids.has(id)) {
          releaseMesh(value);
          meshes.delete(id);
        }
      for (const record of records.filter((r) => !r.decal.asMask)) {
        const ancestors = [
            ...textureAncestors(doc, record.layerId),
            record.layer,
          ],
          opacity = ancestors.reduce((v, l) => (v * l.opacity) / 100, 1);
        const legacyMasks = ancestors
          .map((l) => l.mask)
          .filter(
            (m) =>
              m && !maskHasPaint(m) && ["fill", "gradient"].includes(m.kind),
          );
        const materialSignature = JSON.stringify([
          record.decal.opacity,
          record.decal.channels,
          record.decal.channelValues,
          opacity,
          legacyMasks,
          ancestors.filter((l) => maskHasPaint(l.mask)).map((l) => l.mask),
          ancestors.map((l) => doc.layers.indexOf(l)),
          (
            engine.surfaceAssignments?.materialAt(
              record.decal.target?.partId,
              record.layerId,
            ) || engine.specimen.material
          ).uuid,
        ]);
        const signature = JSON.stringify([
          record.decal,
          opacity,
          legacyMasks,
          ancestors.filter((l) => maskHasPaint(l.mask)).map((l) => l.mask),
          ancestors.map((l) => l.id),
          record.layerIndex,
          record.componentIndex,
          (
            engine.surfaceAssignments?.materialAt(
              record.decal.target?.partId,
              record.layerId,
            ) || engine.specimen.material
          ).uuid,
          doc.layers
            .filter((l) => l.surfaceMaterial)
            .map((l) => [
              l.surfaceTarget,
              l.surfaceMaterial,
              l.visible,
              l.opacity,
            ]),
        ]);
        const previous = meshes.get(record.decal.id);
        if (
          previous?.signature === signature &&
          previous.artwork === record.stamp.raster.dataUrl
        )
          continue;
        if (
          previous?.materialSignature === materialSignature &&
          previous.artwork === record.stamp.raster.dataUrl
        ) {
          previous.mesh.geometry.dispose();
          previous.mesh.geometry = projectDecalGeometry(
            engine.specimen,
            record.decal,
          );
          previous.signature = signature;
          previous.mesh.renderOrder = textureLayerRenderOrder(
            doc,
            record.layerId,
            record.componentIndex,
          );
          continue;
        }
        if (previous) releaseMesh(previous);
        const textures = resources[order.indexOf(record.stamp.id)];
        if (!textures) continue;
        const pointCoverage = createPointGradientUniforms();
        updatePointGradientUniforms(
          pointCoverage,
          { masks: legacyMasks },
          engine.specimen.geometry,
        );
        const material = decalMaterial(
          engine,
          record,
          textures,
          masks,
          surfaceMaskOwners(doc, record.layerId),
          pointCoverage,
        );
        const paintCoverage = createMaskCoverage(
          engine.specimen.geometry,
          engine.renderer,
        );
        await paintCoverage.update(ancestors, doc);
        if (destroyed || job !== ticket) {
          paintCoverage.dispose();
          material.dispose();
          pointCoverage.uPointGradientData.value.dispose();
          continue;
        }
        installMaskPaintCoverage(material, paintCoverage.uniforms);
        engine.meshSupport?.install(material);
        engine.digitalPaint?.install(material);
        engine.stackUV?.install(material);
        material.opacity *= opacity;
        const mesh = new THREE.Mesh(
          projectDecalGeometry(engine.specimen, record.decal),
          material,
        );
        mesh.name = `decal-${record.decal.id}`;
        mesh.receiveShadow = true;
        mesh.userData.textureStackPass = true;
        mesh.renderOrder = textureLayerRenderOrder(
          doc,
          record.layerId,
          record.componentIndex,
        );
        mesh.frustumCulled = false;
        group.add(mesh);
        meshes.set(record.decal.id, {
          mesh,
          signature,
          materialSignature,
          pointCoverage,
          paintCoverage,
          artwork: record.stamp.raster.dataUrl,
        });
      }
      canvas.dataset.decalCount = String(records.length);
      canvas.dataset.decalTriangles = String(
        [...meshes.values()].reduce(
          (n, v) => n + v.mesh.geometry.attributes.position.count / 3,
          0,
        ),
      );
      canvas.dataset.decalMaskCount = String(
        records.filter((r) => r.decal.asMask).length,
      );
      engine.maskPreview?.refreshStamps();
      engine.dirty = true;
      const keep = new Set(doc.stamps.map(textureKey));
      if (editor?.active && editor.preset) {
        keep.add(textureKey(editor.preset));
        await textureFor(editor.preset);
      }
      if (job === ticket && !destroyed) retireTextures(keep);
    } catch (e) {
      if (!destroyed && job === ticket) editorRef.current?.onError(e.message);
    }
  }
  async function drawHover(x, y) {
    const editor = editorRef.current;
    if (
      !editor?.active ||
      editor.brush?.space === "uv" ||
      !["brush", "place"].includes(editor.mode) ||
      !editor.preset
    ) {
      clearGhost();
      return;
    }
    const aim = ++aimTicket;
    const hit = decalSurfaceHit(engine, x, y);
    if (
      !hit ||
      (editor.surfaceTarget &&
        hit.target?.partId !== editor.surfaceTarget.partId)
    ) {
      clearGhost();
      return;
    }
    const preset = editor.preset,
      textures = await textureFor(preset);
    if (
      !textures ||
      destroyed ||
      aim !== aimTicket ||
      editorRef.current !== editor
    )
      return;
    const decal = createDecal(preset, { ...editor.brush, ...hit });
    clearGhost(false);
    ghost = new THREE.Mesh(
      projectDecalGeometry(engine.specimen, decal),
      new THREE.MeshBasicMaterial({
        map: textures.colour,
        color: editor.brush.asMask ? "#dab476" : "#ffffff",
        transparent: true,
        opacity:
          0.55 *
          (editor.brush.opacity ?? 1) *
          (!editor.brush.asMask && preset.project.channels.includes("opacity")
            ? preset.project.channelValues.opacity
            : 1),
        depthWrite: false,
        polygonOffset: true,
        polygonOffsetFactor: -8,
        polygonOffsetUnits: -8,
        side: THREE.FrontSide,
      }),
    );
    ghost.name = "stamp-brush-aim";
    ghost.renderOrder = 10000;
    ghost.frustumCulled = false;
    engine.specimen.add(ghost);
    engine.dirty = true;
  }
  function hover(x, y) {
    pendingHover = { x, y };
    if (hoverFrame) return;
    const tick = (now) => {
      if (now - hoverTime < 70) {
        hoverFrame = requestAnimationFrame(tick);
        return;
      }
      hoverFrame = 0;
      hoverTime = now;
      const point = pendingHover;
      pendingHover = null;
      if (point)
        drawHover(point.x, point.y).catch((e) =>
          editorRef.current?.onError(e.message),
        );
    };
    hoverFrame = requestAnimationFrame(tick);
  }
  function stamp(x, y) {
    const editor = editorRef.current;
    if (!editor?.active || !["brush", "place"].includes(editor.mode))
      return false;
    const hit = decalSurfaceHit(engine, x, y);
    if (hit) editor.onStamp(hit);
    else editor.onError("Aim at the teapot surface to place a stamp.");
    clearGhost();
    return true;
  }
  return {
    update,
    hover,
    stamp,
    cancel: cancelGesture,
    clearGhost,
    dispose() {
      destroyed = true;
      ticket++;
      cancelAnimationFrame(hoverFrame);
      cancelGesture();
      clearGhost();
      transform.detach();
      transform.getHelper().removeFromParent();
      transform.dispose();
      canvas.removeEventListener("pointercancel", pointerCancelled);
      canvas.removeEventListener("lostpointercapture", pointerCancelled);
      for (const value of meshes.values()) releaseMesh(value);
      meshes.clear();
      group.removeFromParent();
      projector.removeFromParent();
      retireTextures(new Set());
      masks.uSurfaceStampData.value.dispose();
      masks.uSurfaceStampAtlas.value.dispose();
    },
  };
}
