/*==========================================================================================================================================
                                                          BAKINGWORKSPACE.JS
==========================================================================================================================================*/
/* Complete Frontier High/Low Mesh Map & 3D Volume SDF Baking Studio:
   - 18 Surface & Volume Bake Maps (Tangent/World/Object/Bent/Bevel Normals, AO, Secondary AO, Curvature, Convexity, Concavity,
     Thickness, Height, Top Dust, UV Island ID, Part ID, World Position, Wireframe, + 3D Signed Volume SDF)
   - Synchronized 3D Baked Map Mesh Stage + 2D UV Map / Volume SDF Z-Y-X Slice Inspector
   - Built-in High/Low studies + Custom Low/High .OBJ/.STL import + Texture Paint bridge + PNG & .fvol Export. */

import * as THREE from "three";
import { OrbitControls } from "../Vendor/orbit-controls.esm.min.js";
import { OBJLoader } from "../Vendor/addons/OBJLoader.js";
import { STLLoader } from "../Vendor/addons/STLLoader.js";
import {
  BAKE_MAP_CATALOG,
  BAKE_STUDY_SHAPES,
  createBakeStudyPair,
  bakeMeshMaps,
  bakeVolumeSdf,
} from "../Engine/BakingEngine.js";
import { GpuBakeSession } from "../Engine/GpuBakeEngine.js";
import { buildPaintTargetGeometry } from "../Engine/TexturePaintEngine.js";
import { BuildThreeGeometryFromBridgeTransfer } from "../../Shared/UvTextureBridge.js";
import {
  Construct,
  ConstructGlyph,
  SectionCard,
  SliderRow,
  SwitchRow,
  SelectRow,
  SegmentedRow,
  ActionRow,
  MetricTrio,
  SmoothScroll,
  OpenScope,
  CloseScope,
  ReleaseScope,
  Broadcast,
  Toast,
} from "../StudioControls.js";

function downloadUrl(url, filename) {
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();
}

function renderVolumeSdfSliceToCanvas(canvas, sdf, axis = "z", sliceNorm = 0.5) {
  if (!sdf) return;
  const [nx, ny, nz] = sdf.dims;
  const w = axis === "x" ? nz : nx;
  const h = axis === "y" ? nz : ny;
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext("2d");
  const img = ctx.createImageData(w, h);
  const scale = Math.max(0.05, Math.max(Math.abs(sdf.minVal), Math.abs(sdf.maxVal)) * 0.65);

  const sx = Math.min(nx - 1, Math.max(0, Math.round(sliceNorm * (nx - 1))));
  const sy = Math.min(ny - 1, Math.max(0, Math.round(sliceNorm * (ny - 1))));
  const sz = Math.min(nz - 1, Math.max(0, Math.round(sliceNorm * (nz - 1))));

  for (let py = 0; py < h; py++) {
    for (let px = 0; px < w; px++) {
      let x = px, y = h - 1 - py, z = sz;
      if (axis === "y") { x = px; y = sy; z = py; }
      else if (axis === "x") { x = sx; y = h - 1 - py; z = px; }
      const d = sdf.values[(z * ny + y) * nx + x];
      const norm = Math.max(-1, Math.min(1, d / scale));
      const contour = Math.abs(d) < scale * 0.04 ? 1 : 0;

      let r, g, b;
      if (contour) {
        r = 239; g = 244; b = 207; // Bright cream iso-surface!
      } else if (d < 0) {
        // Interior negative distance (warm amber/crimson)
        const t = Math.min(1, Math.abs(norm));
        r = Math.round(45 + t * 185);
        g = Math.round(35 + t * 65);
        b = Math.round(28 + t * 25);
      } else {
        // Exterior positive distance (cool slate-blue rings)
        const t = Math.min(1, norm);
        const wave = 0.8 + 0.2 * Math.sin(d * 24);
        r = Math.round((24 + t * 62) * wave);
        g = Math.round((42 + t * 110) * wave);
        b = Math.round((68 + t * 155) * wave);
      }
      const i4 = (py * w + px) * 4;
      img.data[i4] = r; img.data[i4 + 1] = g; img.data[i4 + 2] = b; img.data[i4 + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
}

export function mountBakingWorkspace(root, appBridge) {
  let state = {
    studyId: "teapot",
    viewMode: "3d", // "3d" (default) | "2d" | "split"
    selectedChannels: new Set(["normalTangent", "ao", "curvature", "height", "volumeSdf"]),
    inspectChannel: "normalTangent",
    resolution: 512,
    samples: 12,
    frontDistance: 0.14,
    rearDistance: 0.14,
    aoDistance: 0.45,
    thicknessRange: 1.8,
    padding: 6,
    normalY: "+Y",
    showHighOverlay: false,
    sdfRes: 36,
    sdfPadding: 0.16,
    sdfSigned: true,
    sdfSliceAxis: "z",
    sdfSlicePos: 0.5,
    baking: false,
    deviceMode: "auto", // "auto" | "gpu" | "cpu"
    lastDevice: "Detecting GPU…",
    lastElapsedMs: 0,
    progress: 0,
    progressText: "Ready to bake",
    bakedSurface: null, // { res, maps, bridgeData }
    bakedSdf: null,
  };

  let studyPair = createBakeStudyPair(state.studyId);
  let inspectorScope = null;
  let rafId = 0;
  let disposed = false;
  let activePopup = null;

  // ----------------------------------------------------------------- LAYOUT
  const grid = Construct("div", "studio-grid");

  // LEFT: MESH STUDY + MAP CHECKLIST
  const leftCol = Construct("aside", "studio-column");
  leftCol.append(
    Construct("div", "panel-heading", [
      Construct("h2", null, "Bake Map Targets"),
      Construct("span", "panel-heading-meta", `${BAKE_MAP_CATALOG.length} MAPS`),
    ]),
  );
  const leftScroll = Construct("div", "panel-scroll");
  const leftInner = Construct("div", "panel-scroll-inner");
  leftScroll.append(leftInner);
  leftCol.append(
    leftScroll,
    Construct("div", "panel-footer", [
      Construct("span", null, "Spatial AABB Raycaster"),
      Construct("span", null, "High → Low Cage"),
    ]),
  );

  // CENTER: TOOLBAR + 3D PREVIEW + 2D INSPECTOR
  const centerCol = Construct("main", "studio-column viewport-column");
  const toolbar = Construct("div", "viewport-toolbar");

  const studySelect = Construct("select", "se-header-select");
  for (const s of BAKE_STUDY_SHAPES) {
    const o = Construct("option", null, s.label);
    o.value = s.id;
    studySelect.append(o);
  }
  const studyWrap = Construct("div", "select-pill-wrap vp-compact-select", [
    studySelect,
    Construct("span", "pill-caret-divider", ConstructGlyph("ChevronDown", "glyph chev")),
  ]);

  const viewSelect = Construct("select", "se-header-select vp-bake-view-select");
  viewSelect.setAttribute("aria-label", "Bake Viewport View");
  viewSelect.title = "Switch Baking Viewport View";
  for (const [label, val] of [
    ["3D Mesh Only", "3d"],
    ["2D Map Only", "2d"],
    ["3D + 2D Split", "split"],
  ]) {
    const o = Construct("option", null, label);
    o.value = val;
    viewSelect.append(o);
  }
  viewSelect.value = state.viewMode;
  const viewWrap = Construct("div", "select-pill-wrap vp-compact-select", [
    viewSelect,
    Construct("span", "pill-caret-divider", ConstructGlyph("ChevronDown", "glyph chev")),
  ]);

  const lowInp = document.createElement("input");
  lowInp.type = "file"; lowInp.accept = ".obj,.stl"; lowInp.hidden = true;
  const highInp = document.createElement("input");
  highInp.type = "file"; highInp.accept = ".obj,.stl"; highInp.hidden = true;

  const btnLoadLow = Construct("button", "pill-btn", [ConstructGlyph("Upload"), Construct("span", null, "Low Mesh")]);
  btnLoadLow.type = "button";
  btnLoadLow.addEventListener("click", () => lowInp.click());

  const btnLoadHigh = Construct("button", "pill-btn", [ConstructGlyph("Upload"), Construct("span", null, "High Mesh")]);
  btnLoadHigh.type = "button";
  btnLoadHigh.addEventListener("click", () => highInp.click());

  const btnBakeNow = Construct("button", "pill-btn accent", [ConstructGlyph("Bake"), Construct("span", null, "Bake Selected Maps")]);
  btnBakeNow.type = "button";

  const btnSendToPaint = Construct("button", "pill-btn warm", [ConstructGlyph("Brush"), Construct("span", null, "Send to Paint")]);
  btnSendToPaint.type = "button";

  const gpuPill = Construct("span", "gpu-badge", [document.createElement("i"), Construct("span", null, "GPU Baker Ready")]);

  toolbar.append(
    studyWrap,
    viewWrap,
    btnLoadLow, lowInp,
    btnLoadHigh, highInp,
    gpuPill,
    Construct("div", "toolbar-spacer"),
    btnSendToPaint,
    btnBakeNow,
  );

  const stage = Construct("div", "viewport-stage");
  const split = Construct("div", "viewport-split-2d");
  const pane3D = Construct("div", "viewport-split-pane");
  pane3D.append(Construct("span", "pane-badge", "3D Low-Poly + Baked Map"));

  const pane2D = Construct("div", "viewport-split-pane");
  pane2D.style.cssText = "display:flex;align-items:center;justify-content:center;padding:18px;background:#0c0d10;";
  const mapBadge2D = Construct("span", "pane-badge", "2D Baked Map / Volume SDF Slice");
  const canvas2D = document.createElement("canvas");
  canvas2D.width = 512;
  canvas2D.height = 512;
  canvas2D.style.cssText = "max-width:100%;max-height:100%;border-radius:14px;border:1px solid var(--line-strong);image-rendering:auto;background:#131418;";
  pane2D.append(mapBadge2D, canvas2D);

  split.append(pane3D, pane2D);
  const hudHelp = Construct("div", "viewport-hud-bottom", "Click any map on the left to inspect · Volume SDF exports binary/JSON .fvol + live slices");
  stage.append(split, hudHelp);
  centerCol.append(toolbar, stage);

  // RIGHT: BAKE SETTINGS + SDF SETTINGS + EXPORT
  const rightCol = Construct("aside", "studio-column");
  rightCol.append(
    Construct("div", "panel-heading", [
      Construct("h2", null, "Baker Parameters"),
      Construct("span", "panel-heading-meta", "SURFACE + VOLUME SDF"),
    ]),
  );
  const rightScroll = Construct("div", "panel-scroll");
  const rightInner = Construct("div", "panel-scroll-inner");
  rightScroll.append(rightInner);
  rightCol.append(
    rightScroll,
    Construct("div", "panel-footer", [
      Construct("span", null, "Edge Dilation Padding Included"),
      Construct("span", null, "Real Float32 SDF"),
    ]),
  );

  grid.append(leftCol, centerCol, rightCol);
  root.append(grid);
  SmoothScroll(leftScroll, leftInner);
  SmoothScroll(rightScroll, rightInner);

  // ----------------------------------------------------------------- THREE.JS PREVIEW SCENE
  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  pane3D.prepend(renderer.domElement);
  const isRealGl = !!renderer.capabilities?.isWebGL2 && renderer.getContext()?.getParameter?.(0x1F01) !== "MockGPU";
  const gpuBaker = new GpuBakeSession(renderer);
  state.lastDevice = isRealGl ? "WebGL2 GPU Ready" : "CPU Fallback";
  const gpuLabelSpan = gpuPill.querySelector("span");
  if (gpuLabelSpan) gpuLabelSpan.textContent = state.lastDevice;
  gpuBaker.ensureWebGPU().then((dev) => {
    if (disposed) return;
    if (dev) {
      state.lastDevice = `WebGPU (${gpuBaker.webgpuAdapterName})`;
      if (gpuLabelSpan) gpuLabelSpan.textContent = "WebGPU Compute";
    }
  });

  const scene = new THREE.Scene();
  scene.background = new THREE.Color("#0b0b0b");
  const camera = new THREE.PerspectiveCamera(36, 1, 0.05, 80);
  camera.position.set(2.9, 1.9, 3.6);
  const orbit = new OrbitControls(camera, renderer.domElement);
  orbit.enableDamping = true;
  orbit.dampingFactor = 0.08;

  scene.add(new THREE.HemisphereLight("#eef3ff", "#202127", 0.9));
  const dl1 = new THREE.DirectionalLight("#fff8eb", 2.0);
  dl1.position.set(3.5, 4.5, 3.5);
  const dl2 = new THREE.DirectionalLight("#a8c4ff", 0.9);
  dl2.position.set(-4, 2, -3);
  scene.add(dl1, dl2);

  let lowGroup = new THREE.Group();
  let highGroup = new THREE.Group();
  scene.add(lowGroup, highGroup);
  let bakedTex = null;

  function rebuildPreviewMeshes() {
    while (lowGroup.children.length) {
      const m = lowGroup.children[0];
      lowGroup.remove(m);
      m.material?.dispose();
    }
    while (highGroup.children.length) {
      const m = highGroup.children[0];
      highGroup.remove(m);
      m.material?.dispose();
    }

    const previewMat = new THREE.MeshStandardMaterial({
      color: "#dadce0",
      roughness: 0.42,
      metalness: 0.08,
    });
    if (bakedTex && state.inspectChannel !== "volumeSdf") {
      if (state.inspectChannel === "normalTangent") {
        previewMat.normalMap = bakedTex;
      } else {
        previewMat.map = bakedTex;
      }
    }

    for (const p of studyPair.low) {
      const m = new THREE.Mesh(p.geometry, previewMat);
      lowGroup.add(m);
    }

    const highWireMat = new THREE.MeshBasicMaterial({
      color: "#e8c27a",
      wireframe: true,
      transparent: true,
      opacity: 0.25,
    });
    for (const p of studyPair.high) {
      const m = new THREE.Mesh(p.geometry, highWireMat);
      highGroup.add(m);
    }
    highGroup.visible = state.showHighOverlay;
  }

  function updateInspectedMapView() {
    const ch = state.inspectChannel;
    const meta = BAKE_MAP_CATALOG.find((m) => m.id === ch);
    mapBadge2D.textContent = meta ? `Inspecting: ${meta.label}` : "2D Map";

    if (ch === "volumeSdf") {
      if (state.bakedSdf) {
        renderVolumeSdfSliceToCanvas(canvas2D, state.bakedSdf, state.sdfSliceAxis, state.sdfSlicePos);
      } else {
        const ctx = canvas2D.getContext("2d");
        canvas2D.width = 512; canvas2D.height = 512;
        ctx.fillStyle = "#101216"; ctx.fillRect(0, 0, 512, 512);
        ctx.fillStyle = "#8a8d96"; ctx.font = "14px sans-serif"; ctx.textAlign = "center";
        ctx.fillText("Click 'Bake Selected Maps' to compute 3D Volume SDF", 256, 256);
      }
      if (bakedTex) { bakedTex.dispose(); bakedTex = null; }
      rebuildPreviewMeshes();
      return;
    }

    const buf = state.bakedSurface?.maps?.[ch];
    if (buf) {
      const res = state.bakedSurface.res;
      canvas2D.width = res;
      canvas2D.height = res;
      const ctx = canvas2D.getContext("2d");
      ctx.putImageData(new ImageData(new Uint8ClampedArray(buf), res, res), 0, 0);

      if (bakedTex) bakedTex.dispose();
      bakedTex = new THREE.DataTexture(new Uint8Array(buf), res, res, THREE.RGBAFormat);
      bakedTex.flipY = true;
      if (!ch.toLowerCase().includes("normal")) bakedTex.colorSpace = THREE.SRGBColorSpace;
      bakedTex.needsUpdate = true;
    } else {
      const ctx = canvas2D.getContext("2d");
      canvas2D.width = 512; canvas2D.height = 512;
      ctx.fillStyle = "#101216"; ctx.fillRect(0, 0, 512, 512);
      ctx.fillStyle = "#8a8d96"; ctx.font = "14px sans-serif"; ctx.textAlign = "center";
      ctx.fillText("Select channels & click 'Bake Selected Maps'", 256, 256);
    }
    rebuildPreviewMeshes();
  }

  // ----------------------------------------------------------------- RUN BAKE
  async function runBake() {
    if (state.baking || disposed) return;
    if (!state.selectedChannels.size) {
      Toast("Check at least one map to bake");
      return;
    }
    state.baking = true;
    state.progress = 0.04;
    state.progressText = "Initializing GPU baker…";
    btnBakeNow.disabled = true;
    Broadcast();

    try {
      const chList = [...state.selectedChannels];
      const surfaceChannels = chList.filter((c) => c !== "volumeSdf");
      const wantSdf = chList.includes("volumeSdf");
      const tStart = performance.now();
      let deviceUsed = "CPU Reference";

      const surfaceCfg = {
        resolution: state.resolution,
        channels: surfaceChannels,
        samples: state.samples,
        frontDistance: state.frontDistance,
        rearDistance: state.rearDistance,
        aoDistance: state.aoDistance,
        thicknessRange: state.thicknessRange,
        padding: state.padding,
        normalY: state.normalY,
      };

      const onSurfProg = (p, msg) => {
        state.progress = wantSdf ? p * 0.65 : p;
        state.progressText = msg;
        updateProgressUi();
      };

      if (surfaceChannels.length) {
        let res = null;
        if (state.deviceMode !== "cpu" && isRealGl) {
          res = await gpuBaker.bakeMeshMapsWebGPU(studyPair.low, studyPair.high, surfaceCfg, onSurfProg);
          if (!res) res = await gpuBaker.bakeMeshMapsWebGL2(studyPair.low, studyPair.high, surfaceCfg, onSurfProg);
        }
        if (!res) {
          res = await bakeMeshMaps(studyPair.low, studyPair.high, surfaceCfg, onSurfProg);
          res.deviceUsed = "CPU Reference";
        }
        deviceUsed = res.deviceUsed || deviceUsed;
        state.bakedSurface = res;
      }

      if (wantSdf) {
        const sdfCfg = {
          nx: state.sdfRes,
          ny: state.sdfRes,
          nz: state.sdfRes,
          padding: state.sdfPadding,
          signMode: state.sdfSigned ? "signed" : "unsigned",
        };
        const onSdfProg = (p, msg) => {
          state.progress = surfaceChannels.length ? 0.65 + p * 0.35 : p;
          state.progressText = msg;
          updateProgressUi();
        };
        let sdfRes = null;
        if (state.deviceMode !== "cpu" && isRealGl) {
          sdfRes = await gpuBaker.bakeVolumeSdfWebGPU(studyPair.high, sdfCfg, onSdfProg);
          if (!sdfRes) sdfRes = await gpuBaker.bakeVolumeSdfWebGL2(studyPair.high, sdfCfg, onSdfProg);
        }
        if (!sdfRes) {
          sdfRes = await bakeVolumeSdf(studyPair.high, sdfCfg, onSdfProg);
          sdfRes.deviceUsed = "CPU Reference";
        }
        deviceUsed = sdfRes.deviceUsed || deviceUsed;
        state.bakedSdf = sdfRes;
      }

      state.lastElapsedMs = Math.round(performance.now() - tStart);
      state.lastDevice = `${deviceUsed} · ${state.lastElapsedMs} ms`;
      if (gpuLabelSpan) gpuLabelSpan.textContent = state.lastDevice;

      // Ensure inspectChannel shows a map that actually baked
      if (!chList.includes(state.inspectChannel)) state.inspectChannel = chList[0];
      updateInspectedMapView();
      renderLeftColumn();
      rebuildInspector();
      Toast(`Baked cleanly via ${state.lastDevice}`);
    } catch (err) {
      Toast(`Bake failed: ${err.message}`);
    } finally {
      state.baking = false;
      btnBakeNow.disabled = false;
      state.progress = 1;
      state.progressText = state.lastElapsedMs ? `Done · ${state.lastDevice}` : "Bake complete";
      updateProgressUi();
      Broadcast();
    }
  }

  let progressBarEl = null;
  let progressLabelEl = null;
  function updateProgressUi() {
    if (progressBarEl) progressBarEl.style.width = `${Math.round(state.progress * 100)}%`;
    if (progressLabelEl) progressLabelEl.textContent = state.progressText;
  }

  // ----------------------------------------------------------------- LEFT COLUMN
  function renderLeftColumn() {
    leftInner.innerHTML = "";

    const quick = Construct("div", "chip-strip");
    for (const [label, list] of [
      ["PBR Core", ["normalTangent", "ao", "curvature", "height"]],
      ["PBR + Volume", ["normalTangent", "ao", "curvature", "height", "volumeSdf"]],
      ["Normals", ["normalTangent", "normalWorld", "bentNormal", "bevelNormal"]],
      ["Select All", BAKE_MAP_CATALOG.map((m) => m.id)],
    ]) {
      const b = Construct("button", "chip-btn", label);
      b.type = "button";
      b.addEventListener("click", () => {
        state.selectedChannels = new Set(list);
        renderLeftColumn();
        Broadcast();
      });
      quick.append(b);
    }
    leftInner.append(quick);

    let lastGroup = "";
    let currentGrid = null;
    for (const item of BAKE_MAP_CATALOG) {
      if (item.group !== lastGroup) {
        lastGroup = item.group;
        const head = Construct("h3", "bk-map-group-title", item.group);
        leftInner.append(head);
        currentGrid = Construct("div", "bk-map-grid");
        leftInner.append(currentGrid);
      }
      const checked = state.selectedChannels.has(item.id);
      const isInspected = state.inspectChannel === item.id;
      const buf = !item.isVolume ? state.bakedSurface?.maps?.[item.id] : null;
      const isDone = item.isVolume ? !!state.bakedSdf : !!buf;

      const swatch = Construct("div", `bk-map-swatch bk-swatch-${item.id}`);
      if (buf && state.bakedSurface?.res) {
        const prevCanvas = document.createElement("canvas");
        const sz = 72;
        prevCanvas.width = sz;
        prevCanvas.height = sz;
        prevCanvas.className = "bk-map-preview-canvas";
        const pCtx = prevCanvas.getContext?.("2d");
        if (pCtx && typeof pCtx.putImageData === "function") {
          const srcRes = state.bakedSurface.res;
          const tmp = document.createElement("canvas");
          tmp.width = srcRes;
          tmp.height = srcRes;
          const tCtx = tmp.getContext?.("2d");
          if (tCtx) {
            tCtx.putImageData(new ImageData(new Uint8ClampedArray(buf), srcRes, srcRes), 0, 0);
            pCtx.drawImage(tmp, 0, 0, sz, sz);
          }
        }
        swatch.append(prevCanvas);
      } else if (item.isVolume) {
        if (state.bakedSdf) {
          const sdfCanvas = document.createElement("canvas");
          sdfCanvas.className = "bk-map-preview-canvas";
          renderVolumeSdfSliceToCanvas(sdfCanvas, state.bakedSdf, state.sdfSliceAxis, state.sdfSlicePos);
          swatch.append(sdfCanvas);
        } else {
          swatch.append(Construct("span", "bk-swatch-tag", "SDF"));
        }
      }

      const checkBtn = Construct("button", `bk-map-check${checked ? " active" : ""}`, ConstructGlyph("Check", "glyph"));
      checkBtn.type = "button";
      checkBtn.setAttribute("aria-pressed", checked ? "true" : "false");
      checkBtn.title = checked ? `Exclude ${item.label} from bake` : `Include ${item.label} in bake`;
      checkBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        if (state.selectedChannels.has(item.id)) state.selectedChannels.delete(item.id);
        else state.selectedChannels.add(item.id);
        renderLeftColumn();
        Broadcast();
      });
      swatch.append(checkBtn);

      const tile = Construct("div", `bk-map-tile${isInspected ? " active is-inspected" : ""}`, [
        swatch,
        Construct("strong", null, item.label),
        Construct("small", null, isDone ? "Ready" : item.short),
      ]);
      tile.setAttribute("role", "button");
      tile.tabIndex = 0;
      tile.dataset.mapId = item.id;
      tile.setAttribute("aria-pressed", checked ? "true" : "false");
      tile.title = `${item.label} — Click to inspect · Top-right check toggles bake output`;
      const inspectThis = () => {
        state.inspectChannel = item.id;
        updateInspectedMapView();
        renderLeftColumn();
        rebuildInspector();
      };
      tile.addEventListener("click", inspectThis);
      tile.addEventListener("keydown", (e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          inspectThis();
        }
      });
      currentGrid.append(tile);
    }
  }

  // ----------------------------------------------------------------- RIGHT INSPECTOR
  function rebuildInspector() {
    if (inspectorScope) ReleaseScope(inspectorScope);
    inspectorScope = OpenScope();
    rightInner.innerHTML = "";

    const lowTris = studyPair.low.reduce((a, p) => a + (p.geometry.index ? p.geometry.index.count / 3 : p.geometry.attributes.position.count / 3), 0);
    const highTris = studyPair.high.reduce((a, p) => a + (p.geometry.index ? p.geometry.index.count / 3 : p.geometry.attributes.position.count / 3), 0);

    rightInner.append(
      Construct("div", "object-heading", [
        Construct("div", "object-symbol", ConstructGlyph("Bake")),
        Construct("div", "object-heading-copy", [
          Construct("strong", null, BAKE_STUDY_SHAPES.find((s) => s.id === state.studyId)?.label || "Custom High/Low Assembly"),
          Construct("span", null, `${Math.round(lowTris)} LOW · ${Math.round(highTris)} HIGH TRIS`),
        ]),
      ]),
    );

    MetricTrio(rightInner, [
      { Label: "Resolution", Highlight: true, Read: () => ({ value: `${state.resolution}px`, sub: `${state.selectedChannels.size} Maps` }) },
      { Label: "Rays / Px", Read: () => ({ value: String(state.samples), sub: "Hemisphere" }) },
      { Label: "Volume SDF", Read: () => ({ value: `${state.sdfRes}³`, sub: state.bakedSdf ? "Baked" : "Pending" }) },
    ]);

    // Bake Progress Box
    const progBox = Construct("div", "property-group");
    progBox.open = true;
    const pInner = Construct("div", "group-content");
    pInner.style.paddingTop = "12px";
    progressLabelEl = Construct("div", "property-label", state.progressText);
    const track = Construct("div", "bake-progress-track");
    progressBarEl = Construct("div", "bake-progress-fill");
    track.append(progressBarEl);
    pInner.append(progressLabelEl, track);
    ActionRow(pInner, [
      { Label: state.baking ? "Baking…" : "Bake Selected Maps", Glyph: "Bake", Tone: "accent", Act: runBake },
    ]);
    progBox.append(pInner);
    rightInner.append(progBox);
    updateProgressUi();

    // 1. Surface Baker Settings
    const sSec = SectionCard(rightInner, { Title: "GPU Device & Surface Ray Settings", Glyph: "Sliders" });
    SegmentedRow(sSec.Inner, {
      Label: "Compute Backend",
      Options: [["Auto GPU", "auto"], ["WebGL2 / WebGPU", "gpu"], ["CPU Ref", "cpu"]],
      Read: () => state.deviceMode,
      Write: (v) => { state.deviceMode = v; },
    });
    SegmentedRow(sSec.Inner, {
      Label: "Map Resolution",
      Options: [["256", 256], ["512", 512], ["1024", 1024], ["2048", 2048]],
      Read: () => state.resolution,
      Write: (v) => { state.resolution = v; },
    });
    SegmentedRow(sSec.Inner, {
      Label: "Tangent Normal Convention",
      Options: [["OpenGL (+Y)", "+Y"], ["DirectX (-Y)", "-Y"]],
      Read: () => state.normalY,
      Write: (v) => { state.normalY = v; },
    });
    SliderRow(sSec.Inner, { Label: "AO / Bent Samples", Minimum: 4, Maximum: 36, Step: 2, Read: () => state.samples, Write: (v) => { state.samples = v; } });
    SliderRow(sSec.Inner, { Label: "Front Cage Offset", Minimum: 0.02, Maximum: 0.8, Step: 0.01, Unit: "m", Read: () => state.frontDistance, Write: (v) => { state.frontDistance = v; } });
    SliderRow(sSec.Inner, { Label: "Rear Cage Offset", Minimum: 0.02, Maximum: 0.8, Step: 0.01, Unit: "m", Read: () => state.rearDistance, Write: (v) => { state.rearDistance = v; } });
    SliderRow(sSec.Inner, { Label: "AO Max Ray Distance", Minimum: 0.1, Maximum: 2.0, Step: 0.02, Unit: "m", Read: () => state.aoDistance, Write: (v) => { state.aoDistance = v; } });
    SliderRow(sSec.Inner, { Label: "UV Edge Padding", Minimum: 0, Maximum: 14, Step: 1, Unit: "px", Read: () => state.padding, Write: (v) => { state.padding = v; } });
    SwitchRow(sSec.Inner, { Label: "Show High-Poly Wire Overlay", Read: () => state.showHighOverlay, Write: (v) => { state.showHighOverlay = v; highGroup.visible = v; } });

    // 2. 3D Volume SDF Settings & Slice Viewer
    const vSec = SectionCard(rightInner, { Title: "3D Volume SDF & Slice Inspector", Glyph: "Volume" });
    SegmentedRow(vSec.Inner, {
      Label: "Voxel Grid Dim",
      Options: [["24³", 24], ["36³", 36], ["48³", 48], ["64³", 64]],
      Read: () => state.sdfRes,
      Write: (v) => { state.sdfRes = v; },
    });
    SwitchRow(vSec.Inner, { Label: "Signed Interior Distance", Read: () => state.sdfSigned, Write: (v) => { state.sdfSigned = v; } });
    SliderRow(vSec.Inner, { Label: "Bounds Padding", Minimum: 0.05, Maximum: 0.45, Step: 0.01, Read: () => state.sdfPadding, Write: (v) => { state.sdfPadding = v; } });
    SegmentedRow(vSec.Inner, {
      Label: "Inspect Slice Axis",
      Options: [["Z Slice", "z"], ["Y Slice", "y"], ["X Slice", "x"]],
      Read: () => state.sdfSliceAxis,
      Write: (v) => { state.sdfSliceAxis = v; state.inspectChannel = "volumeSdf"; updateInspectedMapView(); },
    });
    SliderRow(vSec.Inner, {
      Label: "Slice Depth",
      Minimum: 0, Maximum: 1, Step: 0.01,
      Read: () => state.sdfSlicePos,
      Write: (v) => { state.sdfSlicePos = v; if (state.inspectChannel === "volumeSdf") updateInspectedMapView(); },
    });

    // 3. Export & Paint Bridge
    const eSec = SectionCard(rightInner, { Title: "Export & Texture Paint Bridge", Glyph: "Download" });
    ActionRow(eSec.Inner, [
      {
        Label: "Download Current PNG",
        Glyph: "Download",
        Act: () => {
          downloadUrl(canvas2D.toDataURL("image/png"), `${state.studyId}-${state.inspectChannel}.png`);
          Toast(`Exported ${state.inspectChannel}.png`);
        },
      },
      {
        Label: "Download .fvol SDF",
        Glyph: "Volume",
        Availability: () => ({ Enabled: !!state.bakedSdf, Reason: "Bake Volume SDF first" }),
        Act: () => {
          const sdf = state.bakedSdf;
          const payload = JSON.stringify({
            schema: "frontier.volume-sdf.v1",
            dims: sdf.dims,
            boundsMin: sdf.min,
            boundsMax: sdf.max,
            signed: sdf.signed,
            minDistance: sdf.minVal,
            maxDistance: sdf.maxVal,
            values: Array.from(sdf.values, (v) => Number(v.toFixed(5))),
          });
          const blob = new Blob([payload], { type: "application/json" });
          const u = URL.createObjectURL(blob);
          downloadUrl(u, `${state.studyId}-volume.fvol`);
          setTimeout(() => URL.revokeObjectURL(u), 1000);
          Toast("Exported Frontier .fvol Signed Volume SDF");
        },
      },
    ]);
    ActionRow(eSec.Inner, [
      {
        Label: "Send Baked AO/Curv/Normals to Texture Paint",
        Glyph: "ArrowRight",
        Tone: "warm",
        Availability: () => ({ Enabled: !!state.bakedSurface, Reason: "Bake surface maps first" }),
        Act: () => appBridge.sendBakedMapsToPaint(state.bakedSurface.bridgeData),
      },
    ]);

    CloseScope(inspectorScope);
    Broadcast();
  }

  // ----------------------------------------------------------------- EVENTS & LOADERS
  async function loadCustomFile(file, targetKey) {
    try {
      const ext = file.name.split(".").pop().toLowerCase();
      const parts = [];
      if (ext === "obj") {
        const grp = new OBJLoader().parse(await file.text());
        grp.traverse((ch) => {
          if (ch.isMesh) {
            const g = targetKey === "low" ? buildPaintTargetGeometry("custom", ch.geometry.clone()) : ch.geometry.clone();
            parts.push({ name: ch.name || file.name, geometry: g });
          }
        });
      } else if (ext === "stl") {
        const raw = new STLLoader().parse(await file.arrayBuffer());
        const g = targetKey === "low" ? buildPaintTargetGeometry("custom", raw) : raw;
        parts.push({ name: file.name, geometry: g });
      }
      if (parts.length) {
        // Normalize center & scale consistently if first custom load
        const box = new THREE.Box3();
        parts.forEach((p) => { p.geometry.computeBoundingBox(); box.union(p.geometry.boundingBox); });
        const ctr = box.getCenter(new THREE.Vector3());
        const sz = box.getSize(new THREE.Vector3());
        const scale = 2.4 / Math.max(0.001, Math.max(sz.x, sz.y, sz.z));
        parts.forEach((p) => { p.geometry.translate(-ctr.x, -ctr.y, -ctr.z); p.geometry.scale(scale, scale, scale); });
        studyPair[targetKey] = parts;
        studyPair.studyId = "custom";
        rebuildPreviewMeshes();
        rebuildInspector();
        Toast(`Loaded ${file.name} as ${targetKey.toUpperCase()} mesh`);
      }
    } catch (err) {
      Toast(`Load error: ${err.message}`);
    }
  }

  lowInp.addEventListener("change", () => { if (lowInp.files?.[0]) loadCustomFile(lowInp.files[0], "low"); lowInp.value = ""; });
  highInp.addEventListener("change", () => { if (highInp.files?.[0]) loadCustomFile(highInp.files[0], "high"); highInp.value = ""; });

  studySelect.addEventListener("change", () => {
    state.studyId = studySelect.value;
    studyPair = createBakeStudyPair(state.studyId);
    state.bakedSurface = null;
    state.bakedSdf = null;
    if (bakedTex) { bakedTex.dispose(); bakedTex = null; }
    rebuildPreviewMeshes();
    updateInspectedMapView();
    renderLeftColumn();
    rebuildInspector();
  });

  btnBakeNow.addEventListener("click", runBake);
  btnSendToPaint.addEventListener("click", () => {
    if (!state.bakedSurface) {
      Toast("Run Bake Selected Maps first, then send to Texture Paint!");
      return;
    }
    appBridge.sendBakedMapsToPaint(state.bakedSurface.bridgeData);
  });

  const resizeRenderer3D = () => {
    if (pane3D.hidden) return;
    const w = pane3D.clientWidth || 400;
    const h = pane3D.clientHeight || 400;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  };

  const syncBakeViewportLayout = () => {
    const mode = state.viewMode || "3d";
    if (mode === "2d") {
      pane3D.hidden = true;
      pane2D.hidden = false;
      split.style.gridTemplateColumns = "1fr";
      split.classList.add("bake-single-pane");
    } else if (mode === "3d") {
      pane3D.hidden = false;
      pane2D.hidden = true;
      split.style.gridTemplateColumns = "1fr";
      split.classList.add("bake-single-pane");
    } else {
      pane3D.hidden = false;
      pane2D.hidden = false;
      split.style.gridTemplateColumns = "1fr 1fr";
      split.classList.remove("bake-single-pane");
    }
    viewSelect.value = mode;
    resizeRenderer3D();
  };

  viewSelect.addEventListener("change", () => {
    state.viewMode = viewSelect.value || "3d";
    syncBakeViewportLayout();
  });
  syncBakeViewportLayout();

  const ro = new ResizeObserver(() => {
    resizeRenderer3D();
  });
  ro.observe(stage);

  // ----------------------------------------------------------------- RIGHT-CLICK / Q / TAB QUICK BAKE MENU
  function closeBakePopup() {
    if (activePopup) {
      activePopup.remove();
      activePopup = null;
    }
  }

  function openBakePopup(clientX, clientY) {
    if (activePopup) { closeBakePopup(); return; }
    const box = stage.getBoundingClientRect();
    const x = clientX ?? (box.left + box.width * 0.5 - 160);
    const y = clientY ?? (box.top + 80);
    const pop = Construct("div", "tool-popup-menu");
    pop.style.left = `${Math.max(12, Math.min(x, window.innerWidth - 356))}px`;
    pop.style.top = `${Math.max(12, Math.min(y, window.innerHeight - 380))}px`;

    const closeBtn = Construct("button", "stack-mini-btn", ConstructGlyph("Close"));
    closeBtn.type = "button";
    closeBtn.addEventListener("click", closeBakePopup);
    pop.append(
      Construct("div", "tool-popup-head", [
        ConstructGlyph("Bake"),
        Construct("strong", null, "Baking Quick Menu"),
        Construct("small", null, state.lastDevice),
        closeBtn,
      ]),
    );

    const resRow = Construct("div", "chip-strip");
    for (const r of [256, 512, 1024, 2048]) {
      const b = Construct("button", "chip-btn" + (state.resolution === r ? " active" : ""), `${r}px`);
      b.type = "button";
      b.addEventListener("click", () => { state.resolution = r; closeBakePopup(); rebuildInspector(); });
      resRow.append(b);
    }
    pop.append(Construct("div", "property-label", "Map Resolution"), resRow);

    const actRow = Construct("div", "action-row");
    const runBtn = Construct("button", "action-button accent", [ConstructGlyph("Bake"), Construct("span", null, "Bake Selected Maps")]);
    runBtn.type = "button";
    runBtn.addEventListener("click", () => { closeBakePopup(); runBake(); });
    const bridgeBtn = Construct("button", "action-button warm", [ConstructGlyph("Brush"), Construct("span", null, "Send to Paint")]);
    bridgeBtn.type = "button";
    bridgeBtn.addEventListener("click", () => { closeBakePopup(); btnSendToPaint.click(); });
    actRow.append(runBtn, bridgeBtn);
    pop.append(actRow);

    document.body.append(pop);
    activePopup = pop;
  }

  let rDown = null;
  stage.addEventListener("pointerdown", (e) => {
    if (e.button === 2) rDown = { x: e.clientX, y: e.clientY };
    else if (activePopup && !activePopup.contains(e.target)) closeBakePopup();
  });
  stage.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    if (!rDown || Math.hypot(e.clientX - rDown.x, e.clientY - rDown.y) < 5) {
      openBakePopup(e.clientX, e.clientY);
    }
    rDown = null;
  });

  const onBakeKeyDown = (e) => {
    if (["INPUT", "TEXTAREA", "SELECT"].includes(document.activeElement?.tagName)) return;
    if (e.key === "Escape") { closeBakePopup(); return; }
    if (e.key.toLowerCase() === "q" || e.key === "Tab") {
      e.preventDefault();
      openBakePopup();
    }
  };
  window.addEventListener("keydown", onBakeKeyDown);

  function ensureCustomStudyOption(label = "Imported Study") {
    let opt = Array.from(studySelect.options).find((o) => o.value === "custom");
    if (!opt) {
      opt = Construct("option", null, label);
      opt.value = "custom";
      studySelect.append(opt);
    } else {
      opt.textContent = label;
    }
    studySelect.value = "custom";
  }

  function applyImportedMesh(transfer) {
    if (!transfer) return false;
    const rawGeo = BuildThreeGeometryFromBridgeTransfer(transfer, THREE);
    if (!rawGeo) return false;
    const lowGeo = buildPaintTargetGeometry("custom", rawGeo);
    const highGeo = lowGeo.clone();
    state.studyId = "custom";
    ensureCustomStudyOption(`UV Bridge: ${transfer.name || "Imported Mesh"}`);
    studyPair = {
      studyId: "custom",
      low: [{ name: transfer.name || "Low", geometry: lowGeo }],
      high: [{ name: transfer.name || "High", geometry: highGeo }],
    };
    state.bakedSurface = null;
    state.bakedSdf = null;
    if (bakedTex) { bakedTex.dispose(); bakedTex = null; }
    rebuildPreviewMeshes();
    updateInspectedMapView();
    renderLeftColumn();
    rebuildInspector();
    Toast(`Loaded "${transfer.name}" (${transfer.triCount.toLocaleString()} tris) for baking`);
    return true;
  }

  if (appBridge?.shared?.importedMesh) {
    applyImportedMesh(appBridge.shared.importedMesh);
  } else {
    renderLeftColumn();
    rebuildInspector();
    updateInspectedMapView();
  }

  const tick = () => {
    if (disposed) return;
    if (!pane3D.hidden) {
      orbit.update();
      renderer.render(scene, camera);
    }
    rafId = requestAnimationFrame(tick);
  };
  rafId = requestAnimationFrame(tick);

  function applyAsset(payload) {
    if (!payload) return false;
    if (payload.assetKind === "mesh") {
      const mId = payload.raw?.bakeId || payload.id;
      if (BAKE_STUDY_SHAPES.some((s) => s.id === mId)) {
        state.studyId = mId;
        studySelect.value = mId;
        studyPair = createBakeStudyPair(state.studyId);
        state.bakedSurface = null;
        state.bakedSdf = null;
        if (bakedTex) { bakedTex.dispose(); bakedTex = null; }
        rebuildPreviewMeshes();
        updateInspectedMapView();
        renderLeftColumn();
        rebuildInspector();
        Toast(`Loaded High/Low study "${payload.name}" for baking`);
        return true;
      }
    }
    return false;
  }

  stage.addEventListener("dragover", (e) => {
    e.preventDefault();
    if (e.dataTransfer) e.dataTransfer.dropEffect = "copy";
    stage.classList.add("canvas-drop-active");
  });
  stage.addEventListener("dragleave", () => stage.classList.remove("canvas-drop-active"));
  stage.addEventListener("drop", (e) => {
    e.preventDefault();
    stage.classList.remove("canvas-drop-active");
    let payload = null;
    try {
      const rawJson = e.dataTransfer?.getData("application/x-frontier-asset") || e.dataTransfer?.getData("text/plain");
      if (rawJson) {
        const parsed = JSON.parse(rawJson);
        payload = parsed.raw ? parsed : appBridge.resolveAssetPayload?.(parsed.assetKind, parsed.id);
      }
    } catch {}
    if (!payload) payload = appBridge.activeDragAsset;
    if (payload) applyAsset(payload);
  });

  return {
    name: "Baking",
    applyAsset,
    applyImportedMesh,
    getSnapshot: () => ({
      studyId: state.studyId,
      viewMode: state.viewMode,
      inspectChannel: state.inspectChannel,
      selected: [...state.selectedChannels],
      hasSdf: !!state.bakedSdf,
      hasSurface: !!state.bakedSurface,
    }),
    dispose() {
      disposed = true;
      cancelAnimationFrame(rafId);
      closeBakePopup();
      window.removeEventListener("keydown", onBakeKeyDown);
      ro.disconnect();
      if (inspectorScope) ReleaseScope(inspectorScope);
      orbit.dispose();
      gpuBaker.dispose();
      if (bakedTex) bakedTex.dispose();
      renderer.dispose();
      root.innerHTML = "";
    },
  };
}
