import { litScreenProjection, screenPointer } from "./screenPaintProjection.js";
import { createDigitalBrushCursorController } from "./digitalBrushCursorController.js";
import { digitalCursorLabel } from "./digitalBrushCursor.js";
import { createDigitalPaintSurfaceController } from "./digitalPaintSurface.js";
import { createTextureOpticalStackController } from "./textureOpticalStackController.js";
import { createTextureSystemBaseMaterial } from "./textureSystemBaseMaterial.js";
import { createTextureStackUVController } from "./textureStackUVController.js";
import { createTextureMeshSupportController } from "./textureMeshSupportController.js";
import { createMaskPreviewController } from "./maskPreviewController.js";
import {
  installMaskPaintCoverage,
} from "./maskPaintRaster.js";
import { createMaskCoverage } from "./maskCoverageFactory.js";
import { textureAncestors } from "./textureDocument.js";
import { createSurfaceAssignmentController } from "./textureSurfaceController.js";
import { ensureSurfaceCoordinates } from "./textureSurfaceModel.js";
import { createSurfaceDecalController } from "./decalSurfaceController.js";
import {
  createSurfaceMaskUniforms,
  surfaceMaskOwners,
  installSelectedStampMask,
  decalSurfaceHit,
} from "./decalProjection.js";
import {
  createPointGradientUniforms,
  installPointGradient,
  updatePointGradientUniforms,
  gradientObjectPosition,
  gradientSurfaceHit,
} from "./pointGradientViewport.js";
import { TeapotGeometry } from "three/addons/geometries/TeapotGeometry.js";
import { createLeatherSwatchGeometry } from "./leatherGeometry.js";
import { createBotanicalGeometry } from "./botanicalGeometry.js";
import React, { useEffect, useRef, useState } from "react";
import * as THREE from "three";
import { createDrapedClothGeometry, clothSupport } from "./clothGeometry";
import { CameraNavigation } from "./CameraNavigation.js";
import { MeshSurfaceSampler } from "three/addons/math/MeshSurfaceSampler.js";
import { RoundedBoxGeometry } from "three/addons/geometries/RoundedBoxGeometry.js";
import { createMaterial, createBallGeometry, materials } from "./materials";

// Three's compileAsync polls material.currentProgram. Never dispose a material
// while that poll owns it: removed cloth fibers used to orphan the compile
// promise during rapid textile -> paint/asset switches.
function releaseAfterCompilation(engine, material) {
  if (engine.compiling && engine.compileTask) {
    engine.compileTask.then(
      () => material.dispose(),
      () => material.dispose(),
    );
  } else material.dispose();
}

export function makeEnvironment(renderer, mode = "Studio softbox") {
  // The studio is geometry and light only: no downloaded HDRIs or texture maps.
  const room = new THREE.Scene();
  const warm = mode === "Warm atelier";
  room.background = new THREE.Color(
    mode === "Daylight" ? "#919599" : "#65676a",
  );
  const softbox = (position, width, height, power, color = "#ffffff") => {
    const panel = new THREE.Mesh(
      new THREE.PlaneGeometry(width, height),
      new THREE.MeshBasicMaterial({
        color: new THREE.Color(color).multiplyScalar(power),
        side: THREE.DoubleSide,
      }),
    );
    panel.position.set(...position);
    panel.lookAt(0, 0, 0);
    room.add(panel);
  };
  softbox([-3, 2, 3], 2.2, 4.5, 3.6, warm ? "#ffe4bf" : "#f4f6ff");
  softbox([4, 1, 1], 0.9, 4.7, 2.8, warm ? "#ffc684" : "#e2eafa");
  softbox([0, 5, -1], 3.5, 2.3, 2.1);
  softbox([-2, 1, -4], 1.2, 3.5, 1.7);
  const generator = new THREE.PMREMGenerator(renderer);
  const target = generator.fromScene(room, 0.025);
  room.traverse((o) => {
    o.geometry?.dispose();
    o.material?.dispose();
  });
  generator.dispose();
  return target;
}

export async function renderThumbnails(
  callback,
  onProgress = () => {},
  signal,
  initialResults = {},
  catalog = materials,
) {
  const results = { ...initialResults };
  let done = catalog.filter((p) => results[p.id]).length;
  if (done === catalog.length) {
    onProgress({
      done,
      total: catalog.length,
      phase: "ready",
      name: "Library",
    });
    return;
  }
  const renderer = new THREE.WebGLRenderer({
    antialias: true,
    alpha: true,
    preserveDrawingBuffer: true,
  });
  renderer.setSize(240, 186);
  renderer.setPixelRatio(1);
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.2;
  const scene = new THREE.Scene();
  const env = makeEnvironment(renderer);
  scene.environment = env.texture;
  scene.add(new THREE.AmbientLight(0xffffff, 0.5));
  const light = new THREE.DirectionalLight(0xffffff, 3);
  light.position.set(-3, 5, 4);
  scene.add(light);
  const camera = new THREE.PerspectiveCamera(34, 240 / 186, 0.1, 20);
  camera.position.set(0, 1.1, 5.7);
  camera.lookAt(0, 0, 0);
  const geometry = createBallGeometry();
  const plantGeometries = {
    11: createLeatherSwatchGeometry(),
    30: createLeatherSwatchGeometry(),
    31: createBotanicalGeometry("Leaf"),
    32: createBotanicalGeometry("Grass blade"),
    33: createBotanicalGeometry("Petal"),
    34: new THREE.SphereGeometry(1.35, 64, 48),
    35: createBotanicalGeometry("Cactus"),
    cactus8: createBotanicalGeometry("Cactus", 8),
    36: createBotanicalGeometry("Stem"),
  };
  const mesh = new THREE.Mesh(geometry);
  mesh.rotation.z = -0.3;
  scene.add(mesh);
  let shaderFailure = null;
  renderer.debug.onShaderError = (gl, program, vertex, fragment) => {
    shaderFailure = new Error(gl.getShaderInfoLog(fragment));
  };
  // Yield before each GPU job, and publish real completed-job counts. Browsers
  // with KHR_parallel_shader_compile can keep their UI responsive during linking.
  mesh.material.dispose();
  // Keep owners alive for this batch so Three can reuse linked programs across
  // presets. Disposing each immediately would force recompilation 100 times.
  const retainedMaterials = [];
  try {
    for (const p of catalog) {
      if (results[p.id]) continue;
      if (signal?.aborted) break;
      onProgress({
        done,
        total: catalog.length,
        name: p.name,
        phase: "compiling",
      });
      await new Promise((resolve) => setTimeout(resolve, 24));
      if (signal?.aborted) break;
      mesh.geometry =
        p.type === 35 && p.plantRibs === 8
          ? plantGeometries.cactus8
          : plantGeometries[p.type] || geometry;
      mesh.material = createMaterial(p);
      retainedMaterials.push(mesh.material);
      {
        await mesh.material.userData.ready;
        await renderer.compileAsync(scene, camera);
        if (signal?.aborted) break;
        renderer.render(scene, camera);
        if (shaderFailure) throw shaderFailure;
        results[p.id] = renderer.domElement.toDataURL("image/png");
        callback({ ...results });
        done++;
        onProgress({
          done,
          total: catalog.length,
          name: p.name,
          phase: done === catalog.length ? "ready" : "rendered",
        });
      }
    }
  } finally {
    retainedMaterials.forEach((material) => material.dispose());
    geometry.dispose();
    Object.values(plantGeometries).forEach((g) => g.dispose());
    env.dispose();
    renderer.dispose();
    renderer.forceContextLoss();
  }
}

export default function Viewport({
  params,
  paused = false,
  gridVisible = true,
  studioLayout = false,
  shape,
  environment,
  rotate,
  wireframe,
  resetToken,
  zoom,
  onReady,
  onZoomChange,
  onCompile,
  pointGradient,
  gradientEditor,
  decalDocument,
  decalEditor,
  surfaceGeometry,
  systemBase = false,
  surfaceAPI,
  selectedPart,
  onPartSelect,
  maskPreview,
  maskEditor,
  onMaskAtlas,
  onSurfaceAtlas,
  renderEnabled = true,
}) {
  const host = useRef(null),
    engine = useRef(null),
    latest = useRef(params);
  const [error, setError] = useState(false);
  const [gradientHandles, setGradientHandles] = useState([]),
    handleSignature = useRef(""),
    pointRef = useRef(pointGradient),
    editorRef = useRef(gradientEditor),
    gradientGesture = useRef(null),
    decalEditorRef = useRef(decalEditor),
    partSelectRef = useRef(onPartSelect),
    maskEditorRef = useRef(maskEditor),
    maskPreviewRef = useRef(maskPreview),
    maskGesture = useRef(null),
    brushCursorMarker = useRef(null),
    brushCursorReadout = useRef(null),
    maskAtlasRef = useRef(onMaskAtlas),
    surfaceAtlasRef = useRef(onSurfaceAtlas);
  maskEditorRef.current = maskEditor;
  maskPreviewRef.current = maskPreview;
  maskAtlasRef.current = onMaskAtlas;
  surfaceAtlasRef.current = onSurfaceAtlas;
  partSelectRef.current = onPartSelect;
  pointRef.current = pointGradient;
  editorRef.current = gradientEditor;
  decalEditorRef.current = decalEditor;
  const supportsDecals = decalDocument !== undefined;
  const supportsGradient = pointGradient !== undefined;
  latest.current = params;
  const pausedRef = useRef(paused);
  pausedRef.current = paused;
  const renderEnabledRef = useRef(renderEnabled);
  renderEnabledRef.current = renderEnabled;
  const wireframeRef = useRef(wireframe);
  wireframeRef.current = wireframe;
  const zoomCallback = useRef(onZoomChange);
  zoomCallback.current = onZoomChange;
  useEffect(() => {
    let renderer;
    try {
      renderer = new THREE.WebGLRenderer({
        antialias: true,
        alpha: true,
        preserveDrawingBuffer: true,
        powerPreference: "high-performance",
      });
    } catch {
      setError(true);
      return;
    }
    renderer.debug.onShaderError = (gl, program, vertex, fragment) => {
      console.error("Viewport shader: " + gl.getShaderInfoLog(fragment));
      setError(true);
      onCompile?.({ phase: "error", name: latest.current.name });
    };
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.22;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.shadowMap.autoUpdate = false;
    renderer.shadowMap.needsUpdate = true;
    host.current.appendChild(renderer.domElement);
    const scene = new THREE.Scene();
    scene.fog = new THREE.FogExp2("#191a1c", 0.075);
    const env = makeEnvironment(renderer);
    scene.environment = env.texture;
    const camera = new THREE.PerspectiveCamera(37, 1, 0.001, 200);
    camera.position.set(4.1, 3.25, 6.8);
    const Navigation = new CameraNavigation(camera, renderer.domElement);
    const controls = Navigation.Controls;
    controls.target.set(0, 1.65, 0);
    controls.enableDamping = false;
    controls.minDistance = 0.01;
    controls.maxDistance = 100;
    controls.enableZoom = false; // Use optical macro zoom: never push the camera through the surface.
    controls.screenSpacePanning = true;
    controls.maxPolarAngle = Math.PI - 0.01;
    controls.minPolarAngle = 0.01;
    controls.autoRotateSpeed = 0.7;
    controls.enablePan = true;
    controls.panSpeed = 0.8;
    const ambient = new THREE.AmbientLight("#e0e4ec", 0.5);
    scene.add(ambient);
    const key = new THREE.SpotLight("#f1f6f2", 30, 25, 0.65, 0.8, 1.5);
    key.position.set(-3, 7, 4);
    key.castShadow = true;
    key.shadow.mapSize.set(2048, 2048);
    key.shadow.bias = -0.0003;
    key.shadow.normalBias = 0.03;
    key.shadow.radius = 5;
    key.target.position.set(0, 1, 0);
    scene.add(key, key.target);
    const rim = new THREE.DirectionalLight("#dbe3f3", 1.2);
    rim.position.set(3, 4, -3);
    scene.add(rim);
    const fill = new THREE.DirectionalLight("#e4edf2", 0.45);
    fill.position.set(-4, 2, 1);
    scene.add(fill);
    const floorMaterial = new THREE.MeshStandardMaterial({
      color: "#242528",
      roughness: 0.85,
      metalness: 0.05,
      envMapIntensity: 0.2,
      transparent: true,
      depthWrite: false,
    });
    floorMaterial.onBeforeCompile = (shader) => {
      shader.vertexShader =
        "varying vec3 vGroundPosition;\n" + shader.vertexShader;
      shader.vertexShader = shader.vertexShader.replace(
        "#include <begin_vertex>",
        "#include <begin_vertex>\nvGroundPosition=position;",
      );
      shader.fragmentShader =
        "varying vec3 vGroundPosition;\n" + shader.fragmentShader;
      shader.fragmentShader = shader.fragmentShader.replace(
        "#include <color_fragment>",
        "#include <color_fragment>\ndiffuseColor.a *= 1. - smoothstep(3.,9.,length(vGroundPosition.xy));",
      );
    };
    const floor = new THREE.Mesh(
      new THREE.PlaneGeometry(24, 24),
      floorMaterial,
    );
    floor.rotation.x = -Math.PI / 2;
    floor.receiveShadow = true;
    floor.position.y = -0.015;
    scene.add(floor);
    const grid = new THREE.GridHelper(14, 28, "#35363a", "#2b2c30");
    grid.material.transparent = true;
    grid.material.opacity = studioLayout ? 0.18 : 0.085;
    grid.visible = gridVisible;
    grid.position.y = -0.007;
    scene.add(grid);
    const group = new THREE.Group();
    scene.add(group);
    const material = systemBase
      ? createTextureSystemBaseMaterial()
      : createMaterial(latest.current);
    const pointUniforms = supportsGradient
      ? createPointGradientUniforms()
      : null;
    const decalMasks = supportsDecals ? createSurfaceMaskUniforms() : null;
    const decalOwners = surfaceMaskOwners(
      decalDocument,
      decalEditor?.previewLayerId,
    );
    if (pointUniforms) installPointGradient(material, pointUniforms);
    if (decalMasks && pointUniforms)
      installSelectedStampMask(material, decalMasks, decalOwners);
    const specimen = new THREE.Mesh(createBallGeometry(), material);
    if (supportsDecals) ensureSurfaceCoordinates(specimen.geometry);
    specimen.position.y = 1.65;
    specimen.rotation.z = -0.27;
    specimen.rotation.y = -0.4;
    specimen.castShadow = true;
    specimen.receiveShadow = true;
    group.add(specimen);
    const standMaterial = new THREE.MeshPhysicalMaterial({
      color: "#212226",
      metalness: 0.5,
      roughness: 0.5,
      clearcoat: 0.1,
      envMapIntensity: 0.5,
    });
    const stand = new THREE.Mesh(
      new THREE.CylinderGeometry(1.2, 1.26, 0.18, 128),
      standMaterial,
    );
    stand.position.y = 0.115;
    stand.castShadow = true;
    stand.receiveShadow = true;
    group.add(stand);
    const standBottom = new THREE.Mesh(
      new THREE.CylinderGeometry(1.26, 1.22, 0.055, 128),
      new THREE.MeshStandardMaterial({
        color: "#131417",
        metalness: 0.6,
        roughness: 0.35,
      }),
    );
    standBottom.position.y = 0.032;
    group.add(standBottom);
    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(1.215, 0.012, 12, 128),
      new THREE.MeshStandardMaterial({
        color: "#676a71",
        metalness: 0.85,
        roughness: 0.32,
      }),
    );
    ring.rotation.x = Math.PI / 2;
    ring.position.y = 0.2;
    group.add(ring);
    const supportBall = new THREE.Mesh(
      new THREE.SphereGeometry(clothSupport.radius, 96, 64),
      new THREE.MeshPhysicalMaterial({
        color: "#16191f",
        roughness: 0.4,
        metalness: 0.4,
        clearcoat: 0.25,
      }),
    );
    supportBall.position.fromArray(clothSupport.center);
    supportBall.visible = false;
    supportBall.castShadow = true;
    supportBall.receiveShadow = true;
    scene.add(supportBall);
    if (studioLayout)
      [stand, standBottom, ring].forEach((mesh) => {
        mesh.visible = false;
      });
    const state = {
      pointUniforms,
      decalMasks,
      decalOwners,
      grid,
      supportBall,
      renderer,
      scene,
      camera,
      controls,
      Navigation,
      specimen,
      env,
      key,
      rim,
      ambient,
      frame: 0,
      dirty: true,
      environment: "Studio softbox",
      magnification: 1,
      fitZoom: 0.86,
    };
    state.compiling = true;
    state.compileTicket = 0;
    engine.current = state;
    if (surfaceGeometry) {
      state.brushCursor = createDigitalBrushCursorController(
        state,
        () => maskEditorRef.current,
        (cursor, pointer) => {
          const marker = brushCursorMarker.current,
            readout = brushCursorReadout.current;
          host.current?.classList.toggle("digital-cursor-visible", !!cursor);
          if (marker) marker.hidden = !cursor;
          if (readout) readout.hidden = !cursor;
          if (!cursor || !pointer) return;
          const rect = renderer.domElement.getBoundingClientRect();
          const x = ((pointer.clientX - rect.left) / rect.width) * 100,
            y = ((pointer.clientY - rect.top) / rect.height) * 100;
          if (marker) {
            marker.style.left = `${x}%`;
            marker.style.top = `${y}%`;
          }
          if (readout) {
            readout.textContent = digitalCursorLabel(cursor);
            readout.style.left = `${Math.min(x, 70)}%`;
            readout.style.top = `${Math.min(y, 90)}%`;
          }
        },
      );
      state.opticalStack = createTextureOpticalStackController(state);
      state.stackUV = createTextureStackUVController(
        state,
        (atlas) => surfaceAtlasRef.current?.(atlas),
        (message) => decalEditorRef.current?.onError?.(message),
      );
      state.stackUV.install(material);
      state.digitalPaint = createDigitalPaintSurfaceController(
        state,
        (message) => decalEditorRef.current?.onError?.(message),
      );
      if (decalDocument) state.digitalPaint.update(decalDocument);
      state.meshSupport = createTextureMeshSupportController(state, (message) =>
        decalEditorRef.current?.onError?.(message),
      );
      state.surfaceAssignments = createSurfaceAssignmentController(
        state,
        (m) => releaseAfterCompilation(state, m),
        decalEditorRef,
      );
      state.surfaceAssignments.installBase(material);
      state.meshSupport.install(material);
      state.digitalPaint.install(material);
    }
    if (decalMasks)
      state.decals = createSurfaceDecalController(
        state,
        decalMasks,
        decalOwners,
        decalEditorRef,
        (m) => releaseAfterCompilation(state, m),
      );
    if (surfaceGeometry && decalMasks) {
      state.maskPreview = createMaskPreviewController(state, (atlas) =>
        maskAtlasRef.current?.(atlas),
      );
      state.maskCoverage = createMaskCoverage(
        surfaceGeometry,
        state.renderer,
      );
      installMaskPaintCoverage(
        material,
        state.maskCoverage.uniforms,
        "gradient",
      );
    }
    state.setZoom = (factor) => {
      state.magnification = THREE.MathUtils.clamp(factor, 0.1, 100);
      Navigation.NavigationCamera.zoom = state.fitZoom * state.magnification;
      Navigation.NavigationCamera.updateProjectionMatrix();
      controls.panSpeed = 0.8 / state.magnification;
      state.dirty = true;
      const fibers = state.specimen.getObjectByName("microfibers");
      if (fibers) fibers.visible = state.magnification >= 2;
      const percent = Math.round(state.magnification * 100);
      renderer.domElement.dataset.zoom = String(percent);
      zoomCallback.current?.(percent);
    };
    state.SuspendNavigation = () => {
      Navigation.Suspend();
      const VisibleMagnification = camera.zoom / state.fitZoom;
      if (Math.abs(Math.log(VisibleMagnification / state.magnification)) > 1e-6)
        state.setZoom(VisibleMagnification);
    };
    state.FrameView = () => {
      const e = state;
      if (studioLayout && e.homeTarget) {
        e.Navigation.NavigationCamera.position.set(4.1, 3.25, 6.8);
        e.controls.target.copy(e.homeTarget);
      } else if (e.renderer.domElement.dataset.previewShape === "Rug") {
        e.Navigation.NavigationCamera.position.set(1.6, 2.4, 6.8);
        e.controls.target.set(0, 1.35, 0);
      } else {
        e.Navigation.NavigationCamera.position.set(4.1, 3.25, 6.8);
        e.controls.target.set(0, 1.65, 0);
      }
      e.setZoom(1);
      e.controls.update();
      e.Navigation.Reset();
    };
    Navigation.ConfigureFrame = state.FrameView;
    Navigation.ConfigureZoomDelta = (Displacement) => state.setZoom(state.magnification * Math.exp(-Displacement * 0.006));
    controls.update();
    Navigation.Reset();
    const wheel = (e) => {
      maskPointerEnd({ pointerId: maskGesture.current?.pointerId }, true);
      e.preventDefault();
      e.stopImmediatePropagation();
      state.setZoom(
        state.magnification *
          Math.exp(-Math.max(-100, Math.min(100, e.deltaY)) * 0.006),
      );
    };
    renderer.domElement.addEventListener("wheel", wheel, {
      passive: false,
      capture: true,
    });
    const raycaster = new THREE.Raycaster();
    const inspect = (e) => {
      const rect = renderer.domElement.getBoundingClientRect();
      raycaster.setFromCamera(
        new THREE.Vector2(
          ((e.clientX - rect.left) / rect.width) * 2 - 1,
          (-(e.clientY - rect.top) / rect.height) * 2 + 1,
        ),
        camera,
      );
      const hit = raycaster.intersectObject(specimen, false)[0];
      if (hit) {
        const shift = hit.point.clone().sub(controls.target);
        Navigation.NavigationCamera.position.add(shift);
        controls.target.copy(hit.point);
        state.setZoom(Math.max(8, state.magnification));
        controls.update();
      }
    };
    renderer.domElement.addEventListener("dblclick", inspect);
    let pinch = 0;
    const touchstart = (e) => {
      if (e.touches.length === 2) {
        // Navigation must not alter the camera underneath an unfinished stroke.
        maskPointerEnd({ pointerId: maskGesture.current?.pointerId }, true);
        pinch = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY,
        );
      }
    };
    const touchmove = (e) => {
      if (e.touches.length === 2) {
        e.preventDefault();
        const d = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY,
        );
        if (pinch > 0) state.setZoom((state.magnification * d) / pinch);
        pinch = d;
      }
    };
    renderer.domElement.addEventListener("touchstart", touchstart, {
      passive: true,
    });
    renderer.domElement.addEventListener("touchmove", touchmove, {
      passive: false,
    });
    const observer = new ResizeObserver((entries) => {
      const { width, height } = entries[0].contentRect;
      // A collapsed dock must not create an infinite camera aspect or clear the
      // last visible frame. Refit/redraw once it has a drawable size again.
      state.visible = width > 0 && height > 0;
      if (!state.visible) return;
      if (maskGesture.current)
        maskPointerEnd({ pointerId: maskGesture.current.pointerId }, true);
      renderer.setSize(width, height);
      camera.aspect = width / height;
      Navigation.NavigationCamera.aspect = camera.aspect;
      Navigation.NavigationCamera.zoom = state.fitZoom * state.magnification;
      Navigation.NavigationCamera.updateProjectionMatrix();
      state.fitZoom = Math.min(0.86, width / height / 0.9);
      camera.zoom = state.fitZoom * state.magnification;
      camera.updateProjectionMatrix();
      Navigation.NavigationCamera.zoom = camera.zoom;
      Navigation.NavigationCamera.updateProjectionMatrix();
      state.dirty = true;
    });
    observer.observe(host.current);
    controls.addEventListener("start", () => renderer.domElement.focus({ preventScroll: true }));
    controls.addEventListener("change", () => {
      state.dirty = true;
    });
    let PreviousNavigationTime = performance.now();
    const tick = () => {
      const NavigationTime = performance.now();
      const NavigationSeconds = (NavigationTime - PreviousNavigationTime) / 1000;
      PreviousNavigationTime = NavigationTime;
      state.frame = requestAnimationFrame(tick);
      if (state.visible === false || !renderEnabledRef.current) {
        if (state.dirty && !state.compiling && state.stackUV) {
          state.stackUV.refresh();
          state.dirty = false;
        }
        return;
      }
      // Paused previews still redraw a dirty frame after dock resizing. Clearing
      // the framebuffer on resize and skipping this made a visible teapot blank.
      if (
        pausedRef.current &&
        !state.dirty &&
        !state.cursorDirty &&
        !state.brushCursor?.pending &&
        !Navigation.QueryPending()
      )
        return;
      // enabled=false only gates OrbitControls' DOM events, not damping or
      // auto-rotation in update(). Do not move the camera under a paint gesture.
      if (maskGesture.current || gradientGesture.current || !controls.enabled) state.SuspendNavigation();
      else if (!pausedRef.current || state.dirty || Navigation.QueryPending())
        state.dirty = Navigation.Evaluate(NavigationSeconds, !pausedRef.current) || state.dirty;
      state.cursorDirty =
        state.brushCursor?.update(state.dirty) || state.cursorDirty;
      const editor = editorRef.current;
      if (editor?.active && pointUniforms) {
        state.specimen.updateMatrixWorld();
        state.camera.updateMatrixWorld();
        state.specimen.geometry.computeBoundingSphere();
        const handles = editor.gradient.points.map((p, i) => {
          const world = gradientObjectPosition(state.specimen, p.position)
            .applyMatrix4(state.specimen.matrixWorld)
            .project(state.camera);
          return {
            id: p.id,
            index: i + 1,
            color: p.color,
            x: Number(((world.x * 0.5 + 0.5) * 100).toFixed(3)),
            y: Number(((-world.y * 0.5 + 0.5) * 100).toFixed(3)),
            visible:
              world.z >= -1 &&
              world.z <= 1 &&
              Math.abs(world.x) <= 1.05 &&
              Math.abs(world.y) <= 1.05,
          };
        });
        const signature = JSON.stringify(handles);
        if (signature !== handleSignature.current) {
          handleSignature.current = signature;
          setGradientHandles(handles);
        }
      } else if (handleSignature.current) {
        handleSignature.current = "";
        setGradientHandles([]);
      }
      if ((state.dirty || state.cursorDirty) && !state.compiling) {
        if (state.dirty) state.digitalPaint?.prepare();
        if (state.opticalStack) state.opticalStack.render();
        else renderer.render(scene, camera);
        if (state.dirty) state.stackUV?.refresh();
        state.cursorDirty = false;
        state.dirty = false;
        renderer.domElement.dataset.materialReady = "true";
      }
    };
    tick();
    return () => {
      cancelAnimationFrame(state.frame);
      observer.disconnect();
      state.brushCursor?.dispose();
      state.maskPreview?.dispose();
      state.maskCoverage?.dispose();
      state.surfaceAssignments?.dispose();
      state.meshSupport?.dispose();
      state.digitalPaint?.dispose();
      state.stackUV?.dispose();
      state.opticalStack?.dispose();
      if (surfaceAPI) surfaceAPI.current = null;
      state.decals?.dispose();
      Navigation.Finalize();
      renderer.domElement.removeEventListener("wheel", wheel, true);
      renderer.domElement.removeEventListener("dblclick", inspect);
      renderer.domElement.removeEventListener("touchstart", touchstart);
      renderer.domElement.removeEventListener("touchmove", touchmove);
      engine.current = null;
      renderer.domElement.remove();
      const dispose = () => {
        scene.traverse((o) => {
          o.geometry?.dispose();
          if (Array.isArray(o.material)) o.material.forEach((m) => m.dispose());
          else o.material?.dispose();
        });
        state.env.dispose();
        pointUniforms?.uPointGradientData.value.dispose();
        renderer.dispose();
        renderer.forceContextLoss();
      };
      if (state.compileTask) state.compileTask.then(dispose, dispose);
      else dispose();
    };
  }, []);
  useEffect(() => {
    const e = engine.current;
    if (!e || paused) return;
    const ticket = ++e.compileTicket;
    const previous = e.compileTask || Promise.resolve();
    e.compiling = true;
    e.renderer.domElement.dataset.materialReady = "false";
    onCompile?.({ phase: "compiling", name: params.name });
    e.compileTask = (async () => {
      await new Promise((resolve) => setTimeout(resolve, 32));
      await previous;
      if (engine.current !== e || ticket !== e.compileTicket) return;
      const old = e.specimen.material;
      e.specimen.material = systemBase
        ? createTextureSystemBaseMaterial()
        : createMaterial({
            ...params,
            clothMapping: shape === "Draped cloth",
          });
      if (e.pointUniforms) {
        installPointGradient(e.specimen.material, e.pointUniforms);
        if (e.decalMasks)
          installSelectedStampMask(
            e.specimen.material,
            e.decalMasks,
            e.decalOwners,
          );
        updatePointGradientUniforms(
          e.pointUniforms,
          pointRef.current,
          e.specimen.geometry,
        );
      }
      e.surfaceAssignments?.installBase(e.specimen.material);
      e.meshSupport?.install(e.specimen.material);
      e.digitalPaint?.install(e.specimen.material);
      e.stackUV?.install(e.specimen.material);
      if (e.maskCoverage)
        installMaskPaintCoverage(
          e.specimen.material,
          e.maskCoverage.uniforms,
          "gradient",
        );
      e.specimen.material.wireframe = wireframeRef.current;
      e.scene.background =
        params.type === 5 ? new THREE.Color("#25272b") : null;
      try {
        await e.specimen.material.userData.ready;
        await e.renderer.compileAsync(e.scene, e.camera);
        if (engine.current !== e || ticket !== e.compileTicket) return;
        e.renderer.shadowMap.needsUpdate = true;
        e.dirty = true;
        e.compiling = false;
        setError(false);
        onReady?.();
        onCompile?.({ phase: "ready", name: params.name });
      } catch (error) {
        if (engine.current === e && ticket === e.compileTicket) {
          e.compiling = false;
          onCompile?.({ phase: "error", name: params.name });
          setError(true);
        }
        console.error("Material preparation failed:", error);
      } finally {
        old.dispose();
      }
    })();
  }, [params, shape, paused]);
  useEffect(() => {
    const e = engine.current;
    if (!e) return;
    e.controls.autoRotate = rotate;
  }, [rotate]);
  useEffect(() => {
    if (engine.current) {
      engine.current.renderer.domElement.dataset.wireframe = String(wireframe);
      engine.current.specimen.material.wireframe = wireframe;
      engine.current.maskPreview?.setWireframe(wireframe);
      engine.current.dirty = true;
    }
  }, [wireframe]);
  useEffect(() => {
    const e = engine.current;
    if (!e) return;
    e.dirty = true;
    e.renderer.shadowMap.needsUpdate = true;
    e.specimen.geometry.dispose();
    e.specimen.rotation.set(0, 0, 0);
    e.specimen.position.y = 1.65;
    e.supportBall.visible = shape === "Draped cloth";
    e.renderer.domElement.dataset.previewShape = shape;
    if (shape === "Draped cloth") {
      e.specimen.geometry = createDrapedClothGeometry();
      e.specimen.position.set(0, 0, 0);
    }
    e.specimen.castShadow = shape !== "Foliage card";
    if (["Leaf", "Grass blade", "Petal", "Stem", "Cactus"].includes(shape)) {
      e.specimen.geometry = createBotanicalGeometry(shape, params.plantRibs);
      e.specimen.rotation.y = -0.1;
    }
    if (shape === "Foliage card") {
      e.specimen.geometry = new THREE.PlaneGeometry(2.5, 2.5, 1, 1);
      e.specimen.position.y = 1.5;
    }
    if (shape === "Pipe") {
      const profile = [
        new THREE.Vector2(0.9, -1.2),
        new THREE.Vector2(0.96, -1.2),
        new THREE.Vector2(0.96, 1.2),
        new THREE.Vector2(0.9, 1.2),
        new THREE.Vector2(0.9, -1.2),
      ];
      e.specimen.geometry = new THREE.LatheGeometry(profile, 128);
      e.specimen.position.y = 1.45;
    }
    if (shape === "Sphere") {
      e.specimen.geometry = new THREE.SphereGeometry(1.4, 128, 96);
    }
    if (shape === "Teapot") {
      e.specimen.geometry = surfaceGeometry
        ? surfaceGeometry.clone()
        : new TeapotGeometry(1.05, 20);
      e.specimen.position.y = 1.4;
    }
    if (e.previousShape === "Rug" && shape !== "Rug") {
      e.Navigation.NavigationCamera.position.set(4.1, 3.25, 6.8);
      e.controls.target.set(0, 1.65, 0);
      e.controls.update();
      e.Navigation.Reset();
    }
    if (studioLayout && shape === "Teapot") {
      e.specimen.geometry.computeBoundingBox();
      const bounds = e.specimen.geometry.boundingBox;
      e.specimen.position.y = -bounds.min.y + 0.02;
      e.homeTarget = new THREE.Vector3(
        0,
        (bounds.max.y - bounds.min.y) / 2 + 0.02,
        0,
      );
      e.controls.target.copy(e.homeTarget);
      e.controls.update();
      e.Navigation.Reset();
    }
    e.renderer.domElement.dataset.triangleCount = String(
      (e.specimen.geometry.index?.count ||
        e.specimen.geometry.attributes.position.count) / 3,
    );
    if (supportsDecals) ensureSurfaceCoordinates(e.specimen.geometry);
    if (surfaceAPI)
      surfaceAPI.current = {
        pick: (x, y) => decalSurfaceHit(e, x, y),
        hover: (id) => e.surfaceAssignments?.setHighlight(id),
        canvas: e.renderer.domElement,
        navigate: () => {
          maskPointerEnd({ pointerId: maskGesture.current?.pointerId }, true);
        },
      };
    if (surfaceGeometry)
      e.renderer.domElement.dataset.surfaceParts = JSON.stringify(
        surfaceGeometry.userData.surfaceParts,
      );
    e.previousShape = shape;
    if (shape === "Rug") {
      e.Navigation.NavigationCamera.position.set(1.6, 2.4, 6.8);
      e.controls.target.set(0, 1.35, 0);
      e.controls.update();
      e.Navigation.Reset();
      e.specimen.geometry = createLeatherSwatchGeometry({
        rug: true,
        rugAspect: params.pattern?.designAspect,
      });
      e.specimen.rotation.set(-0.2, -0.16, 0);
      e.specimen.position.y = 1.35;
    }
    if (shape === "Leather swatch") {
      e.specimen.geometry = createLeatherSwatchGeometry();
      e.specimen.rotation.set(-0.08, -0.24, -0.12);
      e.specimen.position.y = 1.5;
    }
    if (shape === "Panel") {
      e.specimen.geometry = new RoundedBoxGeometry(2.8, 2.1, 0.12, 4, 0.04);
      e.specimen.rotation.y = -0.15;
      e.specimen.position.y = 1.35;
    }
    if (shape === "Shader ball") {
      e.specimen.geometry = createBallGeometry();
      e.specimen.rotation.set(0, -0.4, -0.27);
    }
    if (shape === "Rounded cube") {
      e.specimen.geometry = new RoundedBoxGeometry(2.35, 2.35, 2.35, 8, 0.28);
      e.specimen.rotation.y = 0.25;
      e.specimen.position.y = 1.46;
    }
    if (shape === "Torus knot") {
      e.specimen.geometry = new THREE.TorusKnotGeometry(0.92, 0.35, 220, 40);
      e.specimen.position.y = 1.73;
    }
    if (shape === "Brake rotor") {
      const profile = new THREE.Shape();
      profile.absarc(0, 0, 1.45, 0, Math.PI * 2, false);
      const center = new THREE.Path();
      center.absarc(0, 0, 0.36, 0, Math.PI * 2, true);
      profile.holes.push(center);
      for (let ring = 0; ring < 2; ring++)
        for (let i = 0; i < 18; i++) {
          let a = ((i + ring * 0.5) / 18) * Math.PI * 2;
          const hole = new THREE.Path();
          hole.absarc(
            Math.cos(a) * (1.05 + ring * 0.2),
            Math.sin(a) * (1.05 + ring * 0.2),
            0.043,
            0,
            Math.PI * 2,
            true,
          );
          profile.holes.push(hole);
        }
      e.specimen.geometry = new THREE.ExtrudeGeometry(profile, {
        depth: 0.19,
        bevelEnabled: true,
        bevelSegments: 3,
        steps: 1,
        bevelSize: 0.012,
        bevelThickness: 0.012,
        curveSegments: 80,
      });
      e.specimen.geometry.translate(0, 0, -0.095);
      e.specimen.rotation.y = -0.12;
      e.specimen.rotation.x = 0.06;
      e.specimen.position.y = 1.68;
    }
  }, [
    shape,
    params.plantRibs,
    params.pattern?.designAspect,
    studioLayout,
    surfaceGeometry,
  ]);
  useEffect(() => {
    const e = engine.current;
    if (!e) return;
    const old = e.specimen.getObjectByName("microfibers");
    if (old) {
      e.specimen.remove(old);
      old.geometry.dispose();
      releaseAfterCompilation(e, old.material);
    }
    if (params.type === 4 && params.fuzz > 0) {
      let seed = 417;
      const random = () => {
        seed = (Math.imul(1664525, seed) + 1013904223) >>> 0;
        return seed / 4294967296;
      };
      const sampler = new MeshSurfaceSampler(e.specimen)
        .setRandomGenerator(random)
        .build();
      const count = Math.floor(36000 * params.fuzz),
        positions = new Float32Array(count * 18);
      const point = new THREE.Vector3(),
        normal = new THREE.Vector3(),
        tangent = new THREE.Vector3();
      const length = params.fuzzLength / 100;
      let offset = 0;
      for (let i = 0; i < count; i++) {
        sampler.sample(point, normal);
        tangent.set(random() - 0.5, random() - 0.5, random() - 0.5).normalize();
        tangent.addScaledVector(normal, -tangent.dot(normal)).normalize();
        const len = length * (0.35 + random() * 0.85);
        for (let segment = 0; segment < 3; segment++) {
          for (let endpoint = 0; endpoint < 2; endpoint++) {
            const t = (segment + endpoint) / 3;
            const v = point
              .clone()
              .addScaledVector(normal, len * t)
              .addScaledVector(tangent, len * 0.35 * t * t);
            positions.set([v.x, v.y, v.z], offset);
            offset += 3;
          }
        }
      }
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute(
        "position",
        new THREE.BufferAttribute(positions, 3),
      );
      const material = new THREE.LineBasicMaterial({
        color: new THREE.Color(params.sheenColor).multiplyScalar(0.48),
        transparent: true,
        opacity: 0.25 + params.fuzz * 0.4,
        depthWrite: false,
      });
      const fur = new THREE.LineSegments(geometry, material);
      fur.name = "microfibers";
      fur.visible = e.magnification >= 2;
      e.specimen.add(fur);
    }
    e.dirty = true;
    return () => {
      const fur = e.specimen.getObjectByName("microfibers");
      if (fur) {
        e.specimen.remove(fur);
        fur.geometry.dispose();
        releaseAfterCompilation(e, fur.material);
      }
    };
  }, [params.type, params.fuzz, params.fuzzLength, params.sheenColor, shape]);
  useEffect(() => {
    const e = engine.current;
    if (!e || e.environment === environment) return;
    e.environment = environment;
    e.dirty = true;
    e.env.dispose();
    e.env = makeEnvironment(e.renderer, environment);
    e.scene.environment = e.env.texture;
    const warm = environment === "Warm atelier",
      bright = environment === "Daylight";
    e.key.color.set(warm ? "#ffd5a5" : "#f1f6f2");
    e.rim.color.set(warm ? "#f6ba7c" : "#dbe3f3");
    e.renderer.toneMappingExposure = bright
      ? 1.65
      : environment === "Low-key studio"
        ? 0.8
        : 1.22;
  }, [environment]);
  useEffect(() => {
    const e = engine.current;
    if (!e) return;
    e.FrameView();
  }, [resetToken, studioLayout]);
  useEffect(() => {
    const e = engine.current;
    if (!e) return;
    e.grid.visible = gridVisible;
    e.renderer.domElement.dataset.gridVisible = String(gridVisible);
    e.dirty = true;
  }, [gridVisible]);
  useEffect(() => {
    const e = engine.current;
    if (!e || !zoom) return;
    if (zoom.macro) {
      // Center the macro view on a visible surface point instead of the empty object center.
      const ray = new THREE.Raycaster();
      ray.setFromCamera(new THREE.Vector2(0, 0), e.camera);
      const hit = ray.intersectObject(e.specimen, false)[0];
      if (hit) {
        const shift = hit.point.clone().sub(e.controls.target);
        e.Navigation.NavigationCamera.position.add(shift);
        e.controls.target.copy(hit.point);
      }
      e.setZoom(8);
    } else if (zoom.percent) e.setZoom(zoom.percent / 100);
    else e.setZoom(e.magnification * (zoom.direction > 0 ? 1.5 : 1 / 1.5));
    e.controls.update();
  }, [zoom]);
  useEffect(() => {
    const e = engine.current;
    if (!e?.pointUniforms) return;
    updatePointGradientUniforms(
      e.pointUniforms,
      pointGradient,
      e.specimen.geometry,
    );
    e.dirty = true;
    e.renderer.domElement.dataset.gradientPreview = pointGradient
      ? pointGradient.fill
        ? "fill"
        : "mask"
      : "none";
    e.renderer.domElement.dataset.gradientPoints = String(
      gradientEditor?.active ? gradientEditor.gradient.points.length : 0,
    );
  }, [pointGradient, gradientEditor, shape]);
  useEffect(() => {
    if (!gradientEditor?.active && gradientGesture.current) {
      gradientGesture.current = null;
      if (engine.current) engine.current.controls.enabled = true;
      gradientEditor?.onEnd?.(true);
    }
  }, [gradientEditor?.active]);
  function gradientPointerDown(e) {
    const editor = editorRef.current,
      state = engine.current;
    if (!editor?.active || !state || e.button !== 0) return;
    const button = e.target.closest("[data-gradient-handle]");
    if (!button && !editor.placing) return;
    e.preventDefault();
    e.stopPropagation();
    if (!button) {
      const hit = gradientSurfaceHit(state, e.clientX, e.clientY);
      if (hit) editor.onPlace(hit);
      return;
    }
    const id = button.dataset.gradientHandle;
    editor.onSelect(id);
    gradientGesture.current = {
      id,
      pointerId: e.pointerId,
      target: e.currentTarget,
    };
    state.SuspendNavigation();
    state.controls.enabled = false;
    editor.onStart(id);
    e.currentTarget.setPointerCapture(e.pointerId);
  }
  function gradientPointerMove(e) {
    const gesture = gradientGesture.current,
      state = engine.current,
      editor = editorRef.current;
    if (
      !gesture ||
      gesture.pointerId !== e.pointerId ||
      !state ||
      !editor?.active
    )
      return;
    e.preventDefault();
    e.stopPropagation();
    const hit = gradientSurfaceHit(state, e.clientX, e.clientY);
    if (hit) editor.onMove(gesture.id, hit);
  }
  function gradientPointerEnd(e, cancelled = false) {
    const gesture = gradientGesture.current;
    if (!gesture || gesture.pointerId !== e.pointerId) return;
    e.preventDefault();
    e.stopPropagation();
    gradientGesture.current = null;
    if (engine.current) engine.current.controls.enabled = true;
    editorRef.current?.onEnd(cancelled);
  }
  useEffect(() => {
    const e = engine.current;
    if (e?.decals && decalDocument) e.decals.update(decalDocument, decalEditor);
  }, [decalDocument, decalEditor, shape]);
  useEffect(() => {
    const e = engine.current;
    if (e?.surfaceAssignments && decalDocument) {
      e.meshSupport?.update(decalDocument.meshMaps);
      e.digitalPaint?.update(decalDocument);
      e.surfaceAssignments.update(decalDocument, wireframe);
    }
  }, [decalDocument, wireframe, surfaceGeometry]);
  useEffect(() => {
    engine.current?.surfaceAssignments?.setHighlight(selectedPart);
  }, [selectedPart]);
  useEffect(() => {
    const state = engine.current;
    state?.maskPreview
      ?.update(maskPreview)
      .catch((error) =>
        decalEditorRef.current?.onError?.(`Mask preview: ${error.message}`),
      );
    if (state?.maskCoverage && decalDocument) {
      const owner = decalDocument.layers.find(
        (l) => l.id === decalEditor?.previewLayerId,
      );
      state.maskCoverage
        .update(
          owner ? [...textureAncestors(decalDocument, owner.id), owner] : [],
          decalDocument,
        )
        .then(() => {
          state.dirty = true;
        })
        .catch((error) => decalEditorRef.current?.onError?.(error.message));
    }
  }, [maskPreview, decalDocument, decalEditor?.previewLayerId, shape]);
  useEffect(() => {
    if (!maskEditor?.active && maskGesture.current)
      maskPointerEnd({ pointerId: maskGesture.current.pointerId }, true);
  }, [maskEditor?.active]);
  useEffect(() => {
    const cancelMask = () => {
      maskPointerEnd({ pointerId: maskGesture.current?.pointerId }, true);
      engine.current?.brushCursor?.clear();
    };
    const visibility = () => {
      if (document.visibilityState === "hidden") cancelMask();
    };
    const escape = (e) => {
      if (e.key === "Escape") cancelMask();
    };
    window.addEventListener("keydown", escape);
    window.addEventListener("blur", cancelMask);
    document.addEventListener("visibilitychange", visibility);
    return () => {
      window.removeEventListener("keydown", escape);
      window.removeEventListener("blur", cancelMask);
      document.removeEventListener("visibilitychange", visibility);
      cancelMask();
    };
  }, []);
  useEffect(() => {
    if (!maskEditor?.active || !renderEnabled)
      engine.current?.brushCursor?.clear();
    else engine.current?.brushCursor?.invalidate();
  }, [maskEditor, renderEnabled]);
  function paintingPointer(e) {
    const state = engine.current;
    if (
      !state ||
      !maskEditorRef.current?.digital ||
      !Number.isFinite(e.clientX)
    )
      return e;
    const rect = state.renderer.domElement.getBoundingClientRect();
    return screenPointer(e, litScreenProjection(state, rect), rect);
  }
  function maskPointerEnd(e, cancelled = false) {
    const g = maskGesture.current;
    if (!g) {
      if (e.pointerType === "touch") engine.current?.brushCursor?.clear();
      return;
    }
    if (g.pointerId !== e.pointerId) return;
    e.preventDefault?.();
    e.stopPropagation?.();
    maskGesture.current = null;
    if (engine.current) engine.current.controls.enabled = true;
    const editor = maskEditorRef.current;
    const hit =
      !cancelled && editor?.digital && Number.isFinite(e.clientX)
        ? surfaceAPI?.current?.pick(e.clientX, e.clientY)
        : null;
    editor?.onEnd(cancelled, hit, paintingPointer(e));
    if (e.pointerType === "touch" || cancelled)
      engine.current?.brushCursor?.clear();
    else engine.current?.brushCursor?.move(e);
    if (host.current?.hasPointerCapture(g.pointerId))
      host.current.releasePointerCapture(g.pointerId);
  }
  function surfacePointerDown(e) {
    const mask = maskEditorRef.current;
    if (mask?.active && (maskGesture.current || e.isPrimary === false)) {
      e.preventDefault();
      e.stopPropagation();
      return;
    }
    if (e.target.tagName === "CANVAS") engine.current?.brushCursor?.move(e);
    if (
      e.button === 0 &&
      e.target.tagName === "CANVAS" &&
      mask?.active &&
      !e.altKey &&
      !e.ctrlKey &&
      !e.metaKey
    ) {
      e.preventDefault();
      e.stopPropagation();
      e.currentTarget.focus();
      if (
        mask.onStart(
          surfaceAPI?.current?.pick(e.clientX, e.clientY),
          paintingPointer(e),
        )
      ) {
        maskGesture.current = { pointerId: e.pointerId };
        e.currentTarget.setPointerCapture(e.pointerId);
        if (engine.current) engine.current.controls.enabled = false;
      }
      return;
    }
    const editor = decalEditorRef.current;
    if (
      e.button === 0 &&
      e.target.tagName === "CANVAS" &&
      editor?.active &&
      ["brush", "place"].includes(editor.mode)
    ) {
      e.preventDefault();
      e.stopPropagation();
      engine.current?.decals?.stamp(e.clientX, e.clientY);
      return;
    }
    if (
      e.button === 0 &&
      e.target.tagName === "CANVAS" &&
      !editorRef.current?.active &&
      !editor?.active
    ) {
      const hit = surfaceAPI?.current?.pick(e.clientX, e.clientY);
      if (hit?.target) partSelectRef.current?.(hit.target.partId);
    }
    gradientPointerDown(e);
  }
  function surfacePointerMove(e) {
    const g = maskGesture.current;
    // A pen and mouse can both be primary pointers. Gesture ownership, not
    // isPrimary alone, protects both the live dab and its exact-input marker.
    if (g && g.pointerId !== e.pointerId) {
      e.preventDefault();
      e.stopPropagation();
      return;
    }
    if (
      (e.target.tagName === "CANVAS" || g) &&
      (g || e.pointerType !== "touch")
    )
      engine.current?.brushCursor?.move(e);
    else engine.current?.brushCursor?.clear();
    if (g) {
      e.preventDefault();
      e.stopPropagation();
      maskEditorRef.current?.onMove(
        surfaceAPI?.current?.pick(e.clientX, e.clientY),
        paintingPointer(e),
      );
      return;
    }
    const editor = decalEditorRef.current;
    if (
      editor?.active &&
      ["brush", "place"].includes(editor.mode) &&
      e.target.tagName === "CANVAS"
    )
      engine.current?.decals?.hover(e.clientX, e.clientY);
    gradientPointerMove(e);
  }
  return (
    <div
      className="canvas-host"
      ref={host}
      tabIndex={maskEditor ? 0 : undefined}
      onPointerDownCapture={surfacePointerDown}
      onPointerMoveCapture={surfacePointerMove}
      onPointerLeave={() => {
        engine.current?.decals?.clearGhost();
        if (!maskGesture.current) engine.current?.brushCursor?.clear();
      }}
      onPointerUpCapture={(e) => {
        maskPointerEnd(e);
        gradientPointerEnd(e);
      }}
      onPointerCancelCapture={(e) => {
        maskPointerEnd(e, true);
        gradientPointerEnd(e, true);
      }}
      onLostPointerCapture={(e) => {
        maskPointerEnd(e, true);
        gradientPointerEnd(e, true);
      }}
      onKeyDown={(e) => {
        if (e.key === "Escape") {
          maskPointerEnd({ pointerId: maskGesture.current?.pointerId }, true);
          engine.current?.brushCursor?.clear();
          engine.current?.decals?.cancel();
        }
        if (e.key === "Escape" && gradientGesture.current) {
          e.preventDefault();
          e.stopPropagation();
          gradientGesture.current = null;
          if (engine.current) engine.current.controls.enabled = true;
          editorRef.current?.onEnd(true);
        }
      }}
    >
      <span
        ref={brushCursorMarker}
        className="digital-cursor-anchor"
        aria-hidden="true"
        hidden
      />
      <span
        ref={brushCursorReadout}
        className="digital-cursor-readout"
        aria-hidden="true"
        hidden
      />
      {gradientEditor?.active && (
        <div className="point-gradient-handles">
          {gradientHandles
            .filter((p) => p.visible)
            .map((p) => (
              <button
                key={p.id}
                data-gradient-handle={p.id}
                className="point-gradient-handle"
                aria-label={`Viewport gradient point ${p.index}`}
                aria-pressed={gradientEditor.pointId === p.id}
                style={{
                  left: `${p.x}%`,
                  top: `${p.y}%`,
                  "--point-colour": p.color,
                }}
                onClick={() => editorRef.current?.onSelect(p.id)}
                onKeyDown={(e) => {
                  if (
                    [
                      "ArrowLeft",
                      "ArrowRight",
                      "ArrowUp",
                      "ArrowDown",
                    ].includes(e.key)
                  ) {
                    e.preventDefault();
                    e.stopPropagation();
                    editorRef.current?.onKeyMove(p.id, e.key, e.shiftKey);
                  }
                  if (e.key === "Delete" || e.key === "Backspace") {
                    e.preventDefault();
                    e.stopPropagation();
                    editorRef.current?.onDelete(p.id);
                  }
                }}
              >
                {p.index}
              </button>
            ))}
        </div>
      )}

      {error && (
        <div className="webgl-error">
          WebGL is unavailable. Please enable hardware acceleration to preview
          materials.
        </div>
      )}
    </div>
  );
}
