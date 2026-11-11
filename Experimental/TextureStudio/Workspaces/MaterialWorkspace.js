/*==========================================================================================================================================
                                                         MATERIALWORKSPACE.JS
==========================================================================================================================================*/
/* Preserved Look-Dev Material Studio with full Frontier 50+ procedural PBR shader library,
   real-time 3-point PMREM stage, recipe inspector, Frontier .material TOML export, and Texture Paint bridge. */

import * as THREE from "three";
import { OrbitControls } from "../Vendor/orbit-controls.esm.min.js";
import { RoundedBoxGeometry } from "../Vendor/addons/RoundedBoxGeometry.js";
import { BuildThreeGeometryFromBridgeTransfer } from "../../Shared/UvTextureBridge.js";
import { getBakedMaterialIcon } from "../MaterialIconBaker.js";
import {
  materials as MATERIAL_CATALOG,
  createMaterial,
  normalizeMaterial,
  createBallGeometry,
} from "../Engine/Materials/materials.js";
import { createDrapedClothGeometry, clothSupport } from "../Engine/Materials/clothGeometry.js";
import {
  getRecipe,
  recipeControlValue,
  applyRecipeControl,
  applyRecipeColor,
} from "../Engine/Materials/materialProfiles.js";
import {
  exportMaterialJSON,
  exportMaterialTOML,
  exportMaterialShader,
} from "../Engine/Materials/materialExport.js";
import {
  AuthoringIdentity,
  PbrChannels,
  ChannelAddress,
  DeclareMaterialAuthoring,
  ReadMaterialParameter,
  WriteMaterialParameter,
  DeclareCodedField,
  ResolveAddress,
} from "../Engine/Materials/MaterialSpecification.js";
import { GeneratorDefinitions, CamouflagePalette } from "../Engine/Materials/FieldSpecification.js";
import { RasterChannelPreview } from "../Engine/Materials/FieldProjection.js";
import {
  listPatternStarterCatalog,
  buildMaterialPatternDocument,
} from "../Engine/Patterns/StarterCatalogBridge.js";
import {
  Construct,
  ConstructGlyph,
  SectionCard,
  SliderRow,
  SwitchRow,
  ColorRow,
  TextRow,
  SegmentedRow,
  FilterDropdownPill,
  ChannelMaskRow,
  MultiColourRow,
  ActionRow,
  MetricTrio,
  SmoothScroll,
  OpenScope,
  CloseScope,
  ReleaseScope,
  Broadcast,
  Toast,
} from "../StudioControls.js";

function createStudioEnvTexture(renderer) {
  const pmrem = new THREE.PMREMGenerator(renderer);
  const envScene = new THREE.Scene();
  envScene.background = new THREE.Color("#111318");

  const cardMat1 = new THREE.MeshBasicMaterial({ color: new THREE.Color(3.6, 3.45, 3.2) });
  const cardMat2 = new THREE.MeshBasicMaterial({ color: new THREE.Color(1.4, 1.85, 2.5) });
  const cardMat3 = new THREE.MeshBasicMaterial({ color: new THREE.Color(2.2, 1.85, 1.4) });
  const cardGeo = new THREE.PlaneGeometry(6, 4);

  const c1 = new THREE.Mesh(cardGeo, cardMat1);
  c1.position.set(4.5, 5.2, 4.2);
  c1.lookAt(0, 0, 0);
  const c2 = new THREE.Mesh(cardGeo, cardMat2);
  c2.position.set(-5.8, 2.2, -2.2);
  c2.lookAt(0, 0, 0);
  const c3 = new THREE.Mesh(cardGeo, cardMat3);
  c3.position.set(0.5, -4.5, 3.8);
  c3.lookAt(0, 0, 0);
  envScene.add(c1, c2, c3);

  const rt = pmrem.fromScene(envScene, 0.035);
  cardGeo.dispose();
  cardMat1.dispose();
  cardMat2.dispose();
  cardMat3.dispose();
  pmrem.dispose();
  return rt.texture;
}

function downloadBlob(text, filename, mime = "text/plain") {
  const blob = new Blob([text], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

const MATERIAL_PBR_CHANNEL_SPEC = Object.freeze([
  ["colour", "Colour", "Diffuse / Base Albedo", "albedo", "#9ea2aa"],
  ["roughness", "Roughness", "Microfacet Gloss", "rough", "#9ea2aa"],
  ["metallic", "Metallic", "Conductor Mask", "metal", "#9ea2aa"],
  ["normal", "Normal", "Surface Tangent", "height", "#9ea2aa"],
  ["microNormal", "Micro normal", "Fine Pore / Grain", "height", "#9ea2aa"],
  ["height", "Height / Relief", "Displacement Relief", "height", "#9ea2aa"],
  ["occlusion", "Ambient Occlusion", "Cavity Indirect AO", "rough", "#9ea2aa"],
  ["opacity", "Opacity", "Cutout / Alpha Weight", "albedo", "#9ea2aa"],
  ["coverage", "Coverage", "Layer Mask / Blend", "albedo", "#9ea2aa"],
  ["ior", "IOR", "Fresnel Index", "metal", "#9ea2aa"],
  ["specular", "Specular", "Dielectric Reflection", "metal", "#9ea2aa"],
  ["specularColour", "Specular Colour", "Edge Specular Tint", "albedo", "#9ea2aa"],
  ["coat", "Clearcoat", "Candy / Lacquer Topcoat", "rough", "#9ea2aa"],
  ["coatRoughness", "Coat Roughness", "Clearcoat Polish", "rough", "#9ea2aa"],
  ["coatIor", "Coat IOR", "Topcoat Fresnel IOR", "metal", "#9ea2aa"],
  ["coatNormal", "Coat Normal", "Orange-Peel / Coat Normal", "height", "#9ea2aa"],
  ["sheen", "Sheen", "Velvet / Frost Grazing", "albedo", "#9ea2aa"],
  ["sheenRoughness", "Sheen Roughness", "Grazing Falloff", "rough", "#9ea2aa"],
  ["sheenColour", "Sheen Colour", "Grazing Tint", "albedo", "#9ea2aa"],
  ["anisotropy", "Anisotropy", "Directional Brushing", "metal", "#9ea2aa"],
  ["anisotropyRotation", "Anisotropy Angle", "Grain Direction Angle", "metal", "#9ea2aa"],
  ["iridescence", "Iridescence", "Thin-Film Interference", "albedo", "#9ea2aa"],
  ["iridescenceIor", "Iridescence IOR", "Thin-Film Index", "metal", "#9ea2aa"],
  ["iridescenceThickness", "Iridescence NM", "Film Thickness (nm)", "rough", "#9ea2aa"],
  ["transmission", "Transmission", "Subsurface / Gel Glass", "rough", "#9ea2aa"],
  ["thickness", "Thickness / SSS", "Volume Depth", "rough", "#9ea2aa"],
  ["attenuationColour", "Attenuation Colour", "Syrup / Interior Tint", "albedo", "#9ea2aa"],
  ["emissive", "Emissive", "Self-Glow Colour", "albedo", "#9ea2aa"],
  ["emissiveIntensity", "Emissive Strength", "HDR Glow Intensity", "rough", "#9ea2aa"],
]);

const SOURCE_MODES = Object.freeze([
  { value: "value", label: "Scalar / Constant", sub: "Direct parameter value" },
  { value: "field", label: "Procedural Field", sub: "Noise, Camo, Checker, Cells, Rings" },
  { value: "linear", label: "Linear Gradient", sub: "Directional UV ramp" },
  { value: "radial", label: "Radial Gradient", sub: "Center-to-edge falloff" },
  { value: "conic", label: "Conic Gradient", sub: "Polar sweep ramp" },
  { value: "texture", label: "Custom Image Tile", sub: "Upload ≤256px PNG/JPG" },
  { value: "pattern", label: "Active Pattern", sub: "Sample applied material pattern" },
]);

const PATTERN_MAPPING_OPTIONS = [
  ["UV Map", "uv"],
  ["Triplanar", "object"],
  ["Cylinder", "cylinder"],
];

const PATTERN_AXIS_OPTIONS = [
  ["Both XY", "xy"],
  ["X Only", "x"],
  ["Y Only", "y"],
  ["Single", "none"],
];

export function mountMaterialWorkspace(root, appBridge) {
  const patternCatalog = listPatternStarterCatalog();
  const initialParams = normalizeMaterial(MATERIAL_CATALOG[0]);
  initialParams.Authoring = initialParams.Authoring || DeclareMaterialAuthoring(initialParams);

  let state = {
    selectedId: MATERIAL_CATALOG[0].id,
    params: initialParams,
    selectedLayerId:
      initialParams.Authoring.Layers.find((l) => l.Model === "surface")?.Identity ||
      initialParams.Authoring.Layers[0]?.Identity,
    inspectedChannel: "colour",
    selectedPatternCatalogId: patternCatalog[0]?.id || "",
    category: "All",
    search: "",
    shape: "shaderball",
    backdrop: "softbox",
    inspectorTab: "properties",
    fullscreen: false,
    autoRotate: false,
    lightAzimuth: -37,
    exposure: 1.22,
  };

  const DEFAULT_CAM_POS = new THREE.Vector3(4.1, 3.25, 6.8);
  const DEFAULT_CAM_TARGET = new THREE.Vector3(0, 1.65, 0);
  const DEFAULT_CAM_DIST = DEFAULT_CAM_POS.distanceTo(DEFAULT_CAM_TARGET);

  let dragLayerId = null;

  function ensureAuthoring(params) {
    if (!params.Authoring) {
      params.Authoring = DeclareMaterialAuthoring(params);
    }
    const layers = params.Authoring.Layers;
    if (
      state.selectedLayerId !== "__pattern__" &&
      !layers.some((l) => l.Identity === state.selectedLayerId)
    ) {
      state.selectedLayerId =
        layers.find((l) => l.Model === "surface")?.Identity || layers[0]?.Identity || null;
    }
    return params;
  }

  function mutateParamsPreservingPattern(fn) {
    const savedPattern = state.params.pattern;
    const savedMask = state.params.patternChannelMask;
    const clone = { ...state.params, pattern: undefined, patternChannelMask: savedMask };
    const next = fn(clone);
    if (next) {
      next.pattern = savedPattern;
      if (savedMask) next.patternChannelMask = savedMask;
      state.params = next;
    }
  }

  let inspectorScope = null;
  let rafId = 0;
  let disposed = false;

  const grid = Construct("div", "studio-grid studio-grid-2col");

  // Material Scene & Strata container (housed inside right Material Inspector [Properties | Layer Stack] — nothing on the left!)
  const sceneSummaryWrap = Construct("div", "mw-strata-pane");
  const propPane = Construct("div", "mw-prop-pane");
  const layersPane = Construct("div", "mw-layers-pane");
  layersPane.hidden = true;
  layersPane.append(sceneSummaryWrap);

  // ----------------------------------------------------------------- CENTER: CLEAN LOOK-DEV STAGE (MATCHING IMAGE 3)
  const centerCol = Construct("main", "studio-column viewport-column");

  const shapeOptions = [
    ["Shader Ball", "shaderball"],
    ["Sphere", "sphere"],
    ["Draped Cloth", "cloth"],
    ["Chamfer Box", "box"],
    ["Plane", "plane"],
  ];
  const shapeSelect = Construct("select", "mw-select-pill");
  shapeSelect.setAttribute("aria-label", "Preview Mesh Geometry");
  for (const [label, val] of shapeOptions) {
    const opt = Construct("option", null, label);
    opt.value = val;
    shapeSelect.append(opt);
  }
  shapeSelect.value = state.shape;

  const backdropOptions = [
    ["Studio softbox", "softbox", "#1c1e22"],
    ["Obsidian", "obsidian", "#111215"],
    ["Neutral", "neutral", "#222429"],
    ["Warm", "warm", "#221c16"],
    ["Daylight", "daylight", "#262d36"],
  ];
  const bgSelect = Construct("select", "mw-select-pill");
  bgSelect.setAttribute("aria-label", "Studio Environment");
  for (const [label, val] of backdropOptions) {
    const opt = Construct("option", null, label);
    opt.value = val;
    bgSelect.append(opt);
  }
  bgSelect.value = state.backdrop;

  const fsIconSlot = Construct("span", "mw-fs-icon", ConstructGlyph("Maximize"));
  const fsLabel = Construct("span", "mw-fs-label", "Fullscreen");
  const btnFullscreen = Construct("button", "pill-btn mw-mini-pill mw-fullscreen-btn", [fsIconSlot, fsLabel]);
  btnFullscreen.type = "button";
  btnFullscreen.setAttribute("aria-pressed", "false");
  btnFullscreen.title = "Toggle fullscreen look-dev stage";

  const stage = Construct("div", "viewport-stage mw-stage");

  // Top-left floating editorial collection + large title + description
  const mwHeroKicker = Construct("div", "mw-hero-kicker", "01 / MATERIAL COLLECTION");
  const mwHeroTitle = Construct("h1", "mw-hero-title", state.params.name || "Material");
  const mwHeroDesc = Construct("p", "mw-hero-desc", state.params.description || "Physically based procedural surface shader.");
  const mwStageHero = Construct("div", "mw-stage-hero", [mwHeroKicker, mwHeroTitle, mwHeroDesc]);

  const mwTopActions = Construct("div", "mw-stage-top-actions", [btnFullscreen]);

  // Bottom-center floating zoom capsule + Reset view + selector pills (matching Image 3)
  const btnZoomOut = Construct("button", "mw-zoom-btn", "−");
  btnZoomOut.type = "button";
  btnZoomOut.title = "Zoom out";
  const zoomReadout = Construct("span", "mw-zoom-pct", "62%");
  const btnZoomIn = Construct("button", "mw-zoom-btn", "+");
  btnZoomIn.type = "button";
  btnZoomIn.title = "Zoom in";
  const zoomPill = Construct("div", "mw-zoom-pill", [btnZoomOut, zoomReadout, btnZoomIn]);
  const btnResetCam = Construct("button", "mw-reset-link", "Reset view");
  btnResetCam.type = "button";
  const zoomRow = Construct("div", "mw-zoom-row", [zoomPill, btnResetCam]);

  const wrapSelectPill = (sel) =>
    Construct("div", "select-pill-wrap vp-compact-select mw-stage-select", [
      sel,
      Construct("span", "pill-caret-divider", ConstructGlyph("ChevronDown", "glyph chev")),
    ]);
  const selectorRow = Construct("div", "mw-selector-row", [
    wrapSelectPill(shapeSelect),
    wrapSelectPill(bgSelect),
  ]);
  const bottomDock = Construct("div", "mw-bottom-dock", [zoomRow, selectorRow]);

  stage.append(mwStageHero, mwTopActions, bottomDock);
  centerCol.append(stage);

  function syncStageHeroCopy() {
    const p = state.params;
    const cat = p.category || "Material";
    const catList = MATERIAL_CATALOG.filter((m) => (m.category || "Material") === cat);
    const idx = Math.max(1, catList.findIndex((m) => m.id === p.id) + 1);
    const idxStr = String(idx).padStart(2, "0");
    mwHeroKicker.textContent = `${idxStr} / ${cat} COLLECTION`;
    mwHeroTitle.textContent = p.name || "Procedural Material";
    const rec = getRecipe(p);
    mwHeroDesc.textContent =
      p.description ||
      rec?.description ||
      `${p.label || rec?.title || "PBR"} · Roughness ${(p.roughness ?? 0.4).toFixed(2)}, Metalness ${Math.round((p.metalness ?? 0) * 100)}%.`;
  }

  // ----------------------------------------------------------------- RIGHT: INSPECTOR ([Properties | Layer Stack] matching AlloyStudio)
  const rightCol = Construct("aside", "studio-column");
  rightCol.append(
    Construct("div", "panel-heading", [
      Construct("h2", null, "Material Inspector"),
      Construct("span", "panel-heading-meta", "LOOK-DEV PBR"),
    ]),
  );

  const layerCountBadge = Construct("span", "mw-tab-count", "1");
  const tabPropsBtn = Construct("button", "mw-insp-tab active", "Properties");
  tabPropsBtn.type = "button";
  tabPropsBtn.dataset.inspectorTab = "properties";
  const tabLayersBtn = Construct("button", "mw-insp-tab", [
    Construct("span", null, "Layer Stack"),
    layerCountBadge,
  ]);
  tabLayersBtn.type = "button";
  tabLayersBtn.dataset.inspectorTab = "layers";
  const inspectorTabs = Construct("div", "mw-inspector-tabs", [tabPropsBtn, tabLayersBtn]);

  function setInspectorTab(tab) {
    state.inspectorTab = tab === "layers" ? "layers" : "properties";
    const isProps = state.inspectorTab === "properties";
    tabPropsBtn.classList.toggle("active", isProps);
    tabLayersBtn.classList.toggle("active", !isProps);
    propPane.hidden = !isProps;
    layersPane.hidden = isProps;
  }
  tabPropsBtn.addEventListener("click", () => setInspectorTab("properties"));
  tabLayersBtn.addEventListener("click", () => setInspectorTab("layers"));
  setInspectorTab("properties");

  const summaryHost = Construct("div", "mw-inspector-summary");
  const rightScroll = Construct("div", "panel-scroll");
  const rightInner = Construct("div", "mw-prop-inner");
  propPane.append(rightInner);
  const rightHostInner = Construct("div", "panel-scroll-inner", [summaryHost, inspectorTabs, propPane, layersPane]);
  rightScroll.append(rightHostInner);
  rightCol.append(
    rightScroll,
    Construct("div", "panel-footer", [
      Construct("span", null, "Drag pills to scrub values"),
      Construct("span", null, "Frontier Shading"),
    ]),
  );

  grid.append(centerCol, rightCol);
  root.append(grid);
  SmoothScroll(rightScroll, rightHostInner);

  // ----------------------------------------------------------------- THREE.JS SCENE
  const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = state.exposure;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFShadowMap;
  stage.prepend(renderer.domElement);

  const THEME_BG_BLACK = "#0b0b0b";
  let backdropTex = null;
  function createRadialBackdropTexture(colorA, colorB = THEME_BG_BLACK) {
    const c = document.createElement("canvas");
    c.width = 512;
    c.height = 512;
    const ctx = c.getContext?.("2d");
    if (!ctx || typeof ctx.createRadialGradient !== "function") return null;
    const grad = ctx.createRadialGradient(256, 224, 24, 256, 224, 320);
    grad.addColorStop(0, colorA || "#1c1e22");
    grad.addColorStop(1, colorB || THEME_BG_BLACK);
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, 512, 512);
    const tex = new THREE.CanvasTexture(c);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.needsUpdate = true;
    return tex;
  }

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(THEME_BG_BLACK);
  scene.fog = new THREE.FogExp2(THEME_BG_BLACK, 0.068);
  const envTex = createStudioEnvTexture(renderer);
  scene.environment = envTex;

  function applyStageBackdrop(hexA) {
    const colorA = hexA || "#1c1e22";
    stage.style.background = `radial-gradient(ellipse at 50% 44%, ${colorA} 0%, ${THEME_BG_BLACK} 78%)`;
    const nextTex = createRadialBackdropTexture(colorA, THEME_BG_BLACK);
    backdropTex?.dispose?.();
    backdropTex = nextTex;
    scene.background = nextTex || new THREE.Color(THEME_BG_BLACK);
    if (scene.fog) scene.fog.color.set(THEME_BG_BLACK);
  }
  applyStageBackdrop(backdropOptions[0][2]);

  const camera = new THREE.PerspectiveCamera(37, 1, 0.001, 200);
  camera.position.copy(DEFAULT_CAM_POS);
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.target.copy(DEFAULT_CAM_TARGET);
  controls.update();

  const ambientLight = new THREE.AmbientLight("#e0e4ec", 0.5);
  const keyLight = new THREE.SpotLight("#f1f6f2", 30, 25, 0.65, 0.8, 1.5);
  keyLight.position.set(-3, 7, 4);
  keyLight.target.position.set(0, 1, 0);
  keyLight.castShadow = true;
  keyLight.shadow.mapSize.set(2048, 2048);
  keyLight.shadow.bias = -0.0005;
  const rimLight = new THREE.DirectionalLight("#dbe3f3", 1.2);
  rimLight.position.set(3, 4, -3);
  const fillLight = new THREE.DirectionalLight("#e4edf2", 0.45);
  fillLight.position.set(-4, 2, 1);
  scene.add(ambientLight, keyLight, keyLight.target, rimLight, fillLight);

  // Exact unmodified AlloyStudio grey studio floor fading smoothly into theme black, faint ground grid, 3-part pedestal stand & cloth supportBall
  const floorMaterial = new THREE.MeshStandardMaterial({
    color: "#1b1c1f",
    roughness: 0.85,
    metalness: 0.05,
    envMapIntensity: 0.2,
    transparent: true,
    depthWrite: false,
  });
  floorMaterial.onBeforeCompile = (shader) => {
    shader.vertexShader = "varying vec3 vGroundPosition;\n" + shader.vertexShader;
    shader.vertexShader = shader.vertexShader.replace(
      "#include <begin_vertex>",
      "#include <begin_vertex>\nvGroundPosition=position;",
    );
    shader.fragmentShader = "varying vec3 vGroundPosition;\n" + shader.fragmentShader;
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <color_fragment>",
      "#include <color_fragment>\ndiffuseColor.a *= 1. - smoothstep(3.,9.,length(vGroundPosition.xy));",
    );
  };
  const studioFloor = new THREE.Mesh(new THREE.PlaneGeometry(24, 24), floorMaterial);
  studioFloor.rotation.x = -Math.PI / 2;
  studioFloor.position.y = -0.015;
  studioFloor.receiveShadow = true;
  scene.add(studioFloor);

  const studioGrid = new THREE.GridHelper(14, 28, "#35363a", "#2b2c30");
  studioGrid.material.transparent = true;
  studioGrid.material.opacity = 0.085;
  studioGrid.position.y = -0.007;
  scene.add(studioGrid);

  const standGroup = new THREE.Group();
  const stand = new THREE.Mesh(
    new THREE.CylinderGeometry(1.2, 1.26, 0.18, 128),
    new THREE.MeshPhysicalMaterial({
      color: "#212226",
      metalness: 0.5,
      roughness: 0.5,
      clearcoat: 0.1,
      envMapIntensity: 0.5,
    }),
  );
  stand.position.y = 0.115;
  stand.castShadow = true;
  stand.receiveShadow = true;
  standGroup.add(stand);

  const standBottom = new THREE.Mesh(
    new THREE.CylinderGeometry(1.26, 1.22, 0.055, 128),
    new THREE.MeshStandardMaterial({
      color: "#131417",
      metalness: 0.6,
      roughness: 0.35,
    }),
  );
  standBottom.position.y = 0.032;
  standBottom.castShadow = true;
  standBottom.receiveShadow = true;
  standGroup.add(standBottom);

  const standRing = new THREE.Mesh(
    new THREE.TorusGeometry(1.215, 0.012, 12, 128),
    new THREE.MeshStandardMaterial({
      color: "#676a71",
      metalness: 0.85,
      roughness: 0.32,
    }),
  );
  standRing.rotation.x = Math.PI / 2;
  standRing.position.y = 0.2;
  standGroup.add(standRing);
  scene.add(standGroup);

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
  supportBall.visible = state.shape === "cloth";
  supportBall.castShadow = true;
  supportBall.receiveShadow = true;
  scene.add(supportBall);

  let sampleMesh = null;
  let activeMaterial = null;
  let customBridgeGeo = null;

  function ensureCustomShapeOption(label = "UV Bridge Mesh") {
    let opt = Array.from(shapeSelect.options).find((o) => o.value === "custom");
    if (!opt) {
      opt = Construct("option", null, label);
      opt.value = "custom";
      shapeSelect.append(opt);
    } else {
      opt.textContent = label;
    }
    shapeSelect.value = "custom";
  }

  function syncZoomReadout() {
    const dist = Math.max(1.5, camera.position.distanceTo(controls.target));
    const pct = Math.max(15, Math.min(250, Math.round((DEFAULT_CAM_DIST / dist) * 62)));
    zoomReadout.textContent = `${pct}%`;
  }

  function applyZoomFactor(scale) {
    const dir = camera.position.clone().sub(controls.target);
    const nextDist = Math.max(2.1, Math.min(16, dir.length() * scale));
    dir.setLength(nextDist);
    camera.position.copy(controls.target).add(dir);
    controls.update();
    syncZoomReadout();
  }

  function updateLights() {
    const rad = (state.lightAzimuth * Math.PI) / 180;
    keyLight.position.set(Math.sin(rad) * 5.0, 7.0, Math.cos(rad) * 5.0);
    renderer.toneMappingExposure = state.exposure;
  }

  function buildGeometryForShape(shape) {
    if (shape === "custom" && customBridgeGeo) return customBridgeGeo.clone();
    if (shape === "shaderball") return createBallGeometry();
    if (shape === "cloth") return createDrapedClothGeometry();
    if (shape === "box") return new RoundedBoxGeometry(2.35, 2.35, 2.35, 8, 0.28);
    if (shape === "plane") return new THREE.PlaneGeometry(2.5, 2.5, 64, 64);
    return new THREE.SphereGeometry(1.4, 128, 96);
  }

  function applyImportedMesh(transfer) {
    if (!transfer) return false;
    const rawGeo = BuildThreeGeometryFromBridgeTransfer(transfer, THREE);
    if (!rawGeo) return false;
    rawGeo.computeBoundingBox();
    const bb = rawGeo.boundingBox;
    const sz = bb.getSize(new THREE.Vector3());
    const ctr = bb.getCenter(new THREE.Vector3());
    const scale = 2.45 / Math.max(1e-4, Math.max(sz.x, sz.y, sz.z));
    rawGeo.translate(-ctr.x, -bb.min.y, -ctr.z);
    rawGeo.scale(scale, scale, scale);
    rawGeo.translate(0, 0.21, 0);
    rawGeo.computeBoundingBox();
    rawGeo.computeBoundingSphere();
    customBridgeGeo = rawGeo;
    state.shape = "custom";
    ensureCustomShapeOption(`UV Bridge: ${transfer.name || "Imported Mesh"}`);
    rebuildMeshAndMaterial();
    renderLeftOutliner();
    Toast(`Loaded "${transfer.name}" onto look-dev stage`);
    return true;
  }

  function rebuildMeshAndMaterial() {
    const geo = buildGeometryForShape(state.shape);
    const mat = createMaterial(state.params);
    if (sampleMesh) {
      scene.remove(sampleMesh);
      sampleMesh.geometry.dispose();
    }
    if (activeMaterial) activeMaterial.dispose();
    activeMaterial = mat;
    sampleMesh = new THREE.Mesh(geo, mat);
    sampleMesh.rotation.set(0, 0, 0);
    sampleMesh.position.set(0, 1.65, 0);
    supportBall.visible = state.shape === "cloth";
    if (state.shape === "custom") {
      sampleMesh.position.set(0, 0, 0);
      sampleMesh.rotation.set(0, -0.35, 0);
    } else if (state.shape === "shaderball") {
      sampleMesh.rotation.set(0, -0.4, -0.27);
    } else if (state.shape === "cloth") {
      sampleMesh.position.set(0, 0, 0);
    } else if (state.shape === "box") {
      sampleMesh.rotation.set(0, 0.25, 0);
      sampleMesh.position.set(0, 1.46, 0);
    } else if (state.shape === "plane") {
      sampleMesh.position.set(0, 1.5, 0);
    }
    sampleMesh.castShadow = state.shape !== "plane";
    sampleMesh.receiveShadow = true;
    scene.add(sampleMesh);
    syncStageHeroCopy();
  }

  function syncMaterialLive() {
    ensureAuthoring(state.params);
    state.params = normalizeMaterial(state.params);
    const nextMat = createMaterial(state.params);
    if (sampleMesh) {
      const old = sampleMesh.material;
      sampleMesh.material = nextMat;
      activeMaterial = nextMat;
      if (old && old !== nextMat) old.dispose();
    }
    syncStageHeroCopy();
  }

  function syncPatternUniformsFast() {
    const u = activeMaterial?.userData?.patternUniforms;
    const pat = state.params.pattern;
    if (!u || !pat) {
      syncMaterialLive();
      return;
    }
    const ch = state.params.patternChannelMask || {};
    if (u.uPatternRepeats) u.uPatternRepeats.value = pat.repeats ?? 2;
    if (u.uPatternAngle) u.uPatternAngle.value = ((pat.rotation || 0) * Math.PI) / 180;
    if (u.uPatternObject) {
      u.uPatternObject.value = pat.mapping === "object" ? 1 : pat.mapping === "cylinder" ? 2 : 0;
    }
    if (u.uPatternApplyChannels) {
      u.uPatternApplyChannels.value.set(
        ch.albedo !== false ? 1 : 0,
        ch.roughness !== false ? 1 : 0,
        ch.metalness !== false ? 1 : 0,
        ch.height !== false ? 1 : 0,
      );
    }
    if (u.uPatternApplyExtra) {
      u.uPatternApplyExtra.value.set(
        ch.normal !== false ? 1 : 0,
        ch.occlusion !== false ? 1 : 0,
        ch.coat !== false ? 1 : 0,
        ch.sheen !== false ? 1 : 0,
      );
    }
    if (u.uPatternBeads && pat.beadwork) {
      u.uPatternBeads.value.set(
        pat.beadwork.columns || 1,
        pat.beadwork.rows || 1,
        pat.beadwork.height || 0,
      );
    }
  }

  function applyTileablePatternToMaterial(inputOrEntry, toastMsg = null) {
    const curPat = state.params.pattern;
    const patDoc = buildMaterialPatternDocument(inputOrEntry, {
      repeats: curPat?.repeats ?? 3,
      rotation: curPat?.rotation ?? 0,
      mapping: curPat?.mapping ?? "uv",
      tileAxes: curPat?.tileAxes ?? "xy",
    });
    if (!patDoc) return false;
    state.params = {
      ...state.params,
      pattern: patDoc,
      patternChannelMask: state.params.patternChannelMask || {
        albedo: true,
        roughness: true,
        metalness: true,
        height: true,
        normal: true,
        occlusion: true,
        coat: true,
        sheen: true,
      },
    };
    if (inputOrEntry?.id) state.selectedPatternCatalogId = inputOrEntry.id;
    state.selectedLayerId = "__pattern__";
    syncMaterialLive();
    renderLeftOutliner();
    rebuildInspector();
    Broadcast();
    Toast(toastMsg || `Tiled "${patDoc.name}" onto ${state.params.name}`);
    return true;
  }

  function clearTileablePattern() {
    if (!state.params.pattern) return;
    const next = { ...state.params };
    delete next.pattern;
    state.params = next;
    ensureAuthoring(state.params);
    if (state.selectedLayerId === "__pattern__") {
      state.selectedLayerId =
        state.params.Authoring.Layers.find((l) => l.Model === "surface")?.Identity ||
        state.params.Authoring.Layers[0]?.Identity;
    }
    syncMaterialLive();
    renderLeftOutliner();
    rebuildInspector();
    Broadcast();
    Toast("Removed tileable pattern from material");
  }

  function triggerPatternImageUpload(target = "tile-pattern") {
    const inp = document.createElement("input");
    inp.type = "file";
    inp.accept = "image/png,image/jpeg,image/webp,image/svg+xml";
    inp.addEventListener("change", () => {
      const file = inp.files?.[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        const img = new Image();
        img.onload = () => {
          if (target === "channel-texture") {
            const c = document.createElement("canvas");
            const s = Math.min(256, Math.max(img.width, img.height));
            c.width = s;
            c.height = s;
            c.getContext("2d").drawImage(img, 0, 0, s, s);
            const dataUrl = c.toDataURL("image/png");
            const layer = state.params.Authoring.Layers.find((l) => l.Identity === state.selectedLayerId);
            if (!layer) return;
            const ch = layer.Channels.includes(state.inspectedChannel)
              ? state.inspectedChannel
              : layer.Channels[0] || "colour";
            const addr = ResolveAddress(state.params, ChannelAddress(layer, ch));
            mutateParamsPreservingPattern((p) => {
              const copy = structuredClone(p);
              copy.Authoring.Sources[addr] = {
                ...(copy.Authoring.Sources[addr] || {}),
                Mode: "texture",
                Texture: { name: file.name, dataUrl },
              };
              return copy;
            });
            syncMaterialLive();
            rebuildInspector();
            Toast(`Loaded ${s}×${s} map onto ${layer.Name}.${ch}`);
            return;
          }
          const size = 512;
          const c = document.createElement("canvas");
          c.width = size;
          c.height = size;
          const ctx = c.getContext("2d");
          ctx.drawImage(img, 0, 0, size, size);
          const rgba = ctx.getImageData(0, 0, size, size).data;
          const rough = new Uint8ClampedArray(rgba.length);
          const metal = new Uint8ClampedArray(rgba.length);
          const height = new Uint8ClampedArray(rgba.length);
          const norm = new Uint8ClampedArray(rgba.length);
          for (let i = 0; i < rgba.length; i += 4) {
            const luma = Math.round(0.299 * rgba[i] + 0.587 * rgba[i + 1] + 0.114 * rgba[i + 2]);
            const rVal = Math.round(60 + (255 - luma) * 0.65);
            rough[i] = rough[i + 1] = rough[i + 2] = rVal; rough[i + 3] = 255;
            metal[i] = metal[i + 1] = metal[i + 2] = 0; metal[i + 3] = 255;
            height[i] = height[i + 1] = height[i + 2] = luma; height[i + 3] = 255;
          }
          for (let y = 0; y < size; y++) {
            for (let x = 0; x < size; x++) {
              const xm = (x - 1 + size) % size;
              const xp = (x + 1) % size;
              const ym = (y - 1 + size) % size;
              const yp = (y + 1) % size;
              const dx = (height[(y * size + xm) * 4] - height[(y * size + xp) * 4]) / 128;
              const dy = (height[(ym * size + x) * 4] - height[(yp * size + x) * 4]) / 128;
              const len = Math.hypot(dx, dy, 1);
              const idx = (y * size + x) * 4;
              norm[idx] = Math.round(((dx / len) * 0.5 + 0.5) * 255);
              norm[idx + 1] = Math.round(((dy / len) * 0.5 + 0.5) * 255);
              norm[idx + 2] = Math.round(((1 / len) * 0.5 + 0.5) * 255);
              norm[idx + 3] = 255;
            }
          }
          applyTileablePatternToMaterial(
            {
              schema: "frontier.pattern-surface.v1",
              sourceId: `upload:${file.name}:${Date.now()}`,
              name: file.name.replace(/\.[^.]+$/, ""),
              width: size,
              height: size,
              albedo: rgba,
              roughness: rough,
              metalness: metal,
              heightMap: height,
              normal: norm,
              version: Date.now(),
            },
            `Applied uploaded tileable image "${file.name}"`,
          );
        };
        img.src = reader.result;
      };
      reader.readAsDataURL(file);
    });
    inp.click();
  }

  function addStrataLayer(model = "detail") {
    ensureAuthoring(state.params);
    if (model === "coat") {
      const existing = state.params.Authoring.Layers.find((l) => l.Model === "coat");
      if (existing) {
        existing.Enabled = true;
        if (!existing.Channels.includes("coverage")) existing.Channels.unshift("coverage");
        state.selectedLayerId = existing.Identity;
        if ((state.params.coat ?? 0) < 0.05) state.params.coat = 0.85;
        syncMaterialLive();
        renderLeftOutliner();
        rebuildInspector();
        Toast("Activated Clearcoat strata layer");
        return;
      }
      const coatLayer = {
        Identity: AuthoringIdentity(),
        Name: "Clearcoat",
        Model: "coat",
        Enabled: true,
        Channels: ["coverage", "roughness", "ior"],
      };
      state.params.Authoring.Layers.unshift(coatLayer);
      if ((state.params.coat ?? 0) < 0.05) state.params.coat = 0.85;
      state.selectedLayerId = coatLayer.Identity;
      syncMaterialLive();
      renderLeftOutliner();
      rebuildInspector();
      Toast("Added Clearcoat strata layer");
      return;
    }

    if (state.params.Authoring.Layers.length >= 16) {
      Toast("Material stack supports up to 16 strata layers");
      return;
    }
    const detailNum = state.params.Authoring.Layers.filter((l) => l.Model === "detail").length + 1;
    const newLayer = {
      Identity: AuthoringIdentity(),
      Name: `Detail Strata ${detailNum}`,
      Model: "detail",
      Enabled: true,
      Channels: ["colour", "roughness", "coverage"],
    };
    mutateParamsPreservingPattern((p) => {
      let next = DeclareCodedField(p, "noise", `Detail Field ${detailNum}`, "Detail fields");
      const gen = next.Authoring.Generators[next.Authoring.Generators.length - 1];
      next.Authoring.Layers.unshift(newLayer);
      next.Authoring.Inputs[ChannelAddress(newLayer, "coverage")] = 0.72;
      next.Authoring.Sources[ChannelAddress(newLayer, "colour")] = {
        Mode: "field",
        Generator: gen.Identity,
        Stops: ["#1f2429", "#d6b87c"],
        Direct: false,
      };
      next.Authoring.Sources[ChannelAddress(newLayer, "coverage")] = {
        Mode: "field",
        Generator: gen.Identity,
        Stops: ["#000000", "#ffffff"],
        Direct: true,
      };
      return next;
    });
    state.selectedLayerId = newLayer.Identity;
    state.inspectedChannel = "colour";
    syncMaterialLive();
    renderLeftOutliner();
    rebuildInspector();
    Toast(`Added "${newLayer.Name}" with procedural noise field`);
  }

  function moveAuthoringLayer(layerId, deltaUp) {
    const layers = state.params.Authoring?.Layers;
    if (!layers) return;
    const idx = layers.findIndex((l) => l.Identity === layerId);
    if (idx < 0) return;
    // Visual top = index 0! Moving "up" visually -> deltaUp > 0 -> idx - 1
    const nextIdx = idx + (deltaUp > 0 ? -1 : 1);
    if (nextIdx < 0 || nextIdx >= layers.length) return;
    const [item] = layers.splice(idx, 1);
    layers.splice(nextIdx, 0, item);
    syncMaterialLive();
    renderLeftOutliner();
    rebuildInspector();
  }

  // ----------------------------------------------------------------- UI RENDERERS
  function renderLeftOutliner() {
    sceneSummaryWrap.innerHTML = "";
    ensureAuthoring(state.params);
    const p = state.params;
    const layers = p.Authoring.Layers;
    layerCountBadge.textContent = String(layers.length + (p.pattern ? 1 : 0));

    sceneSummaryWrap.append(
      Construct("div", "mw-strata-head", [
        Construct("span", "mw-strata-title", "Material Scene & Strata"),
        Construct("span", "mw-strata-meta", "LOOK-DEV STAGE"),
      ]),
    );

    // Layer stack creation strip (matching Pattern/Paint stacks)
    const addStrip = Construct("div", "chip-strip");
    const btnAddDetail = Construct("button", "chip-btn", "+ Detail Layer");
    btnAddDetail.type = "button";
    btnAddDetail.addEventListener("click", () => addStrataLayer("detail"));

    const btnAddCoat = Construct("button", "chip-btn", "+ Coat");
    btnAddCoat.type = "button";
    btnAddCoat.addEventListener("click", () => addStrataLayer("coat"));

    const btnTilePat = Construct("button", "chip-btn", p.pattern ? "Inspect Pattern" : "+ Tile Pattern");
    btnTilePat.type = "button";
    btnTilePat.addEventListener("click", () => {
      if (p.pattern) {
        state.selectedLayerId = "__pattern__";
        renderLeftOutliner();
        rebuildInspector();
      } else {
        const defaultEntry =
          patternCatalog.find((e) => e.group === "African Patterns") || patternCatalog[0];
        if (defaultEntry) applyTileablePatternToMaterial(defaultEntry);
      }
    });
    addStrip.append(btnAddDetail, btnAddCoat, btnTilePat);

    sceneSummaryWrap.append(
      Construct("div", "property-label", `Material Layer Stack (${layers.length + (p.pattern ? 1 : 0)})`),
      addStrip,
    );

    const stackBox = Construct("div");

    // 1) Optional Tileable Pattern Overlay row at top of strata stack
    if (p.pattern) {
      const isSelPat = state.selectedLayerId === "__pattern__";
      const patRow = Construct("div", "stack-row" + (isSelPat ? " active" : ""));
      const thumb = Construct("div", "stack-row-thumb", ConstructGlyph("Pattern"));
      thumb.style.background = "linear-gradient(135deg, rgba(232,194,122,0.28), rgba(99,168,255,0.22))";
      const chMap = p.patternChannelMask || {};
      const chCount = ["albedo", "roughness", "metalness", "height", "normal", "occlusion", "coat", "sheen"].filter((k) => chMap[k] !== false).length;
      const info = Construct("div", "stack-row-info", [
        Construct("span", "stack-row-title", p.pattern.name || "Tileable Pattern"),
        Construct(
          "span",
          "stack-row-sub",
          `PATTERN · ${(p.pattern.repeats ?? 2).toFixed(1)}× · ${(p.pattern.mapping || "uv").toUpperCase()} · ${chCount}ch`,
        ),
      ]);
      const rmBtn = Construct("button", "stack-mini-btn", ConstructGlyph("Trash"));
      rmBtn.type = "button";
      rmBtn.title = "Remove pattern overlay";
      rmBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        clearTileablePattern();
      });
      patRow.append(thumb, info, rmBtn);
      patRow.addEventListener("click", () => {
        state.selectedLayerId = "__pattern__";
        renderLeftOutliner();
        rebuildInspector();
      });
      stackBox.append(patRow);
    }

    // 2) Material Authoring Strata Stack (top-to-bottom)
    layers.forEach((layer, idx) => {
      const isSelected = state.selectedLayerId === layer.Identity;
      const row = Construct("div", "stack-row" + (isSelected ? " active" : "") + (layer.Enabled ? "" : " unavailable"));
      row.draggable = true;
      row.dataset.layerId = layer.Identity;

      const handle = Construct("span", "stack-drag-handle", ConstructGlyph("Grip"));
      handle.title = "Drag to reorder strata";

      const thumb = Construct("div", "stack-row-thumb");
      if (layer.Model === "surface") {
        thumb.classList.add("is-baked-mat");
        const img = document.createElement("img");
        img.className = "mw-mat-baked-thumb";
        img.alt = p.name || "Material";
        img.src = getBakedMaterialIcon(p, (upgradedUrl) => {
          if (img.isConnected) img.src = upgradedUrl;
        });
        thumb.append(img);
      } else if (layer.Model === "coat") {
        thumb.append(ConstructGlyph("Sparkles"));
      } else if (layer.Model === "flakes") {
        thumb.append(ConstructGlyph("Grid"));
      } else {
        thumb.append(ConstructGlyph("Layers"));
      }

      const activeFieldCount = layer.Channels.filter((ch) => {
        const addr = ResolveAddress(p, ChannelAddress(layer, ch));
        const src = p.Authoring.Sources[addr];
        return src && src.Mode !== "value";
      }).length;

      const subParts = [
        layer.Model.toUpperCase(),
        `${layer.Channels.length} ch`,
      ];
      if (activeFieldCount > 0) subParts.push(`${activeFieldCount} field`);

      const info = Construct("div", "stack-row-info", [
        Construct("span", "stack-row-title", layer.Name || "Layer"),
        Construct("span", "stack-row-sub", subParts.join(" · ")),
      ]);

      const orderBox = Construct("div", "stack-order-btns");
      const upBtn = Construct("button", "stack-order-btn", ConstructGlyph("ArrowUp"));
      upBtn.type = "button";
      upBtn.disabled = idx === 0;
      upBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        moveAuthoringLayer(layer.Identity, +1);
      });
      const dnBtn = Construct("button", "stack-order-btn", ConstructGlyph("ArrowDown"));
      dnBtn.type = "button";
      dnBtn.disabled = idx === layers.length - 1;
      dnBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        moveAuthoringLayer(layer.Identity, -1);
      });
      orderBox.append(upBtn, dnBtn);

      const visBtn = Construct(
        "button",
        "stack-mini-btn" + (layer.Enabled ? " on" : ""),
        ConstructGlyph(layer.Enabled ? "Eye" : "EyeOff"),
      );
      visBtn.type = "button";
      visBtn.title = "Toggle layer visibility";
      visBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        layer.Enabled = !layer.Enabled;
        syncMaterialLive();
        renderLeftOutliner();
        rebuildInspector();
      });

      row.append(handle, thumb, info, orderBox, visBtn);

      if (layer.Model !== "surface" && layers.length > 1) {
        const delBtn = Construct("button", "stack-mini-btn", ConstructGlyph("Trash"));
        delBtn.type = "button";
        delBtn.title = "Remove layer";
        delBtn.addEventListener("click", (e) => {
          e.stopPropagation();
          p.Authoring.Layers = p.Authoring.Layers.filter((l) => l.Identity !== layer.Identity);
          if (state.selectedLayerId === layer.Identity) {
            state.selectedLayerId = p.Authoring.Layers[0]?.Identity || null;
          }
          syncMaterialLive();
          renderLeftOutliner();
          rebuildInspector();
        });
        row.append(delBtn);
      }

      row.addEventListener("dragstart", (e) => {
        dragLayerId = layer.Identity;
        row.classList.add("dragging");
        if (e.dataTransfer) {
          e.dataTransfer.effectAllowed = "move";
          e.dataTransfer.setData("text/plain", layer.Identity);
        }
      });
      row.addEventListener("dragend", () => {
        dragLayerId = null;
        row.classList.remove("dragging", "drop-above", "drop-below");
      });
      row.addEventListener("dragover", (e) => {
        if (!dragLayerId || dragLayerId === layer.Identity) return;
        e.preventDefault();
        const r = row.getBoundingClientRect();
        const above = e.clientY < r.top + r.height * 0.5;
        row.classList.toggle("drop-above", above);
        row.classList.toggle("drop-below", !above);
      });
      row.addEventListener("dragleave", () => row.classList.remove("drop-above", "drop-below"));
      row.addEventListener("drop", (e) => {
        e.preventDefault();
        const src = dragLayerId || e.dataTransfer?.getData("text/plain");
        const r = row.getBoundingClientRect();
        const above = e.clientY < r.top + r.height * 0.5;
        row.classList.remove("drop-above", "drop-below");
        if (!src || src === layer.Identity) return;
        const srcIdx = layers.findIndex((l) => l.Identity === src);
        const tgtIdx = layers.findIndex((l) => l.Identity === layer.Identity);
        if (srcIdx < 0 || tgtIdx < 0) return;
        const [item] = layers.splice(srcIdx, 1);
        const newTgtIdx = layers.findIndex((l) => l.Identity === layer.Identity);
        layers.splice(above ? newTgtIdx : newTgtIdx + 1, 0, item);
        syncMaterialLive();
        renderLeftOutliner();
        rebuildInspector();
      });

      row.addEventListener("click", () => {
        state.selectedLayerId = layer.Identity;
        if (!layer.Channels.includes(state.inspectedChannel)) {
          state.inspectedChannel = layer.Channels[0] || "colour";
        }
        renderLeftOutliner();
        rebuildInspector();
      });
      stackBox.append(row);

      if (layer.Channels?.length) {
        const chStrip = Construct("div", "mw-outliner-ch-strip");
        for (const ch of layer.Channels) {
          const isCur = isSelected && state.inspectedChannel === ch;
          const label = PbrChannels[ch]?.Caption || ch;
          const chBtn = Construct("button", "mw-outliner-ch-pill" + (isCur ? " active" : ""), label);
          chBtn.type = "button";
          chBtn.title = `Inspect ${label} channel on ${layer.Name}`;
          chBtn.addEventListener("click", (e) => {
            e.stopPropagation();
            state.selectedLayerId = layer.Identity;
            state.inspectedChannel = ch;
            renderLeftOutliner();
            rebuildInspector();
          });
          chStrip.append(chBtn);
        }
        stackBox.append(chStrip);
      }
    });

    sceneSummaryWrap.append(stackBox);

    FilterDropdownPill(sceneSummaryWrap, {
      Label: "Preview Mesh Geometry",
      Prefix: "Mesh:",
      Transient: true,
      Placeholder: "Filter preview meshes...",
      Options: shapeOptions.map(([label, id]) => ({
        value: id,
        label,
        sub: id === "shaderball" ? "Multi-cavity look-dev target" : id === "cloth" ? "Woven drape surface" : id === "box" ? "Chamfered hard-edge cube" : "Smooth spherical UV sample",
      })),
      Read: () => state.shape,
      Write: (id) => {
        state.shape = id;
        shapeSelect.value = id;
        rebuildMeshAndMaterial();
        renderLeftOutliner();
      },
    });

    sceneSummaryWrap.append(Construct("div", "property-label", "Studio Lighting Backdrop"));
    const envStrip = Construct("div", "chip-strip");
    for (const [label, val, hex] of backdropOptions) {
      const cb = Construct("button", "chip-btn" + (state.backdrop === val ? " active" : ""), label);
      cb.type = "button";
      cb.addEventListener("click", () => {
        state.backdrop = val;
        bgSelect.value = val;
        applyStageBackdrop(hex);
        renderLeftOutliner();
      });
      envStrip.append(cb);
    }
    sceneSummaryWrap.append(envStrip);

    const browseStrip = Construct("div", "chip-strip");
    browseStrip.style.marginTop = "10px";
    const browseMatBtn = Construct("button", "chip-btn", `Materials (${MATERIAL_CATALOG.length})`);
    browseMatBtn.type = "button";
    browseMatBtn.addEventListener("click", () => appBridge.focusAssetCategory?.("materials"));
    const browsePatBtn = Construct("button", "chip-btn", `Patterns (${patternCatalog.length})`);
    browsePatBtn.type = "button";
    browsePatBtn.addEventListener("click", () => appBridge.focusAssetCategory?.("patterns", "African Patterns"));
    browseStrip.append(browseMatBtn, browsePatBtn);
    sceneSummaryWrap.append(browseStrip);
  }

  function applyAsset(payload) {
    if (!payload) return false;
    if (payload.assetKind === "material") {
      const item = payload.raw || MATERIAL_CATALOG.find((m) => m.id === payload.id);
      if (!item) return false;
      state.selectedId = item.id;
      const next = normalizeMaterial(item);
      if (state.params.pattern) {
        next.pattern = state.params.pattern;
        next.patternChannelMask = state.params.patternChannelMask;
      }
      ensureAuthoring(next);
      state.params = next;
      state.selectedLayerId =
        next.Authoring.Layers.find((l) => l.Model === "surface")?.Identity ||
        next.Authoring.Layers[0]?.Identity;
      rebuildInspector();
      syncMaterialLive();
      renderLeftOutliner();
      Broadcast();
      Toast(`Applied "${item.name}" onto Material Stage`);
      return true;
    }
    if (payload.assetKind === "pattern") {
      const entry =
        payload.raw ||
        patternCatalog.find((e) => e.id === payload.id || e.name === payload.name) ||
        payload;
      return applyTileablePatternToMaterial(entry);
    }
    if (payload.assetKind === "mesh") {
      const mId = payload.id;
      const mapped = shapeOptions.some(([, id]) => id === mId) ? mId : "shaderball";
      state.shape = mapped;
      shapeSelect.value = mapped;
      rebuildMeshAndMaterial();
      renderLeftOutliner();
      Toast(`Previewing ${mapped} geometry`);
      return true;
    }
    return false;
  }

  function renderPatternSection(parent, openByDefault = false) {
    const pat = state.params.pattern;
    const patSec = SectionCard(parent, {
      Title: "Tileable Pattern Texture",
      Glyph: "Pattern",
      Badge: pat ? pat.name : "NONE",
      Open: openByDefault || !!pat,
    });

    const patternOptions = patternCatalog.map((entry) => ({
      value: entry.id,
      label: entry.name,
      sub: entry.group || "Pattern",
      entry,
    }));

    FilterDropdownPill(patSec.Inner, {
      Label: "Apply Pattern",
      Prefix: "Pattern:",
      Placeholder: "Filter patterns (African, Perlin, Twill...)",
      Options: patternOptions,
      Read: () => state.selectedPatternCatalogId || patternOptions[0]?.value,
      Write: (id, opt) => {
        state.selectedPatternCatalogId = id;
        if (opt?.entry) applyTileablePatternToMaterial(opt.entry);
      },
    });

    ActionRow(patSec.Inner, [
      {
        Label: "+ Upload Image Tile",
        Glyph: "Image",
        Tone: "warm",
        Act: () => triggerPatternImageUpload("tile-pattern"),
      },
      {
        Label: pat ? "Clear Pattern" : "Browse African",
        Glyph: pat ? "Close" : "Grid",
        Act: () => {
          if (pat) clearTileablePattern();
          else appBridge.focusAssetCategory?.("patterns", "African Patterns");
        },
      },
    ]);

    if (!pat) {
      patSec.Inner.append(
        Construct(
          "p",
          "property-hint",
          "Pick a pattern above, click any Pattern in the bottom Asset Browser, drag one onto the 3D stage, or click 'Tile on Material' in Pattern Studio.",
        ),
      );
      return;
    }

    SliderRow(patSec.Inner, {
      Label: "Tile Repeats",
      Minimum: 0.5,
      Maximum: 16,
      Step: 0.25,
      Unit: "×",
      Read: () => state.params.pattern?.repeats ?? 3,
      Write: (v) => {
        if (!state.params.pattern) return;
        state.params.pattern.repeats = v;
        syncPatternUniformsFast();
        renderLeftOutliner();
      },
    });
    SliderRow(patSec.Inner, {
      Label: "Pattern Rotation",
      Minimum: -180,
      Maximum: 180,
      Step: 1,
      Unit: "°",
      Read: () => state.params.pattern?.rotation ?? 0,
      Write: (v) => {
        if (!state.params.pattern) return;
        state.params.pattern.rotation = v;
        syncPatternUniformsFast();
      },
    });
    SegmentedRow(patSec.Inner, {
      Label: "Projection",
      Options: PATTERN_MAPPING_OPTIONS,
      Read: () => state.params.pattern?.mapping || "uv",
      Write: (v) => {
        if (!state.params.pattern) return;
        state.params.pattern.mapping = v;
        syncPatternUniformsFast();
        renderLeftOutliner();
      },
    });
    SegmentedRow(patSec.Inner, {
      Label: "Tile Axes",
      Options: PATTERN_AXIS_OPTIONS,
      Read: () => state.params.pattern?.tileAxes || "xy",
      Write: (v) => {
        if (!state.params.pattern) return;
        state.params.pattern.tileAxes = v;
        syncMaterialLive();
        renderLeftOutliner();
      },
    });
    ChannelMaskRow(patSec.Inner, {
      Label: "Pattern PBR Channels",
      Channels: [
        ["albedo", "Albedo", "Pattern Base Colour", "albedo", "#9ea2aa"],
        ["roughness", "Roughness", "Pattern Surface Gloss", "rough", "#9ea2aa"],
        ["metalness", "Metalness", "Pattern Conductor", "metal", "#9ea2aa"],
        ["height", "Height", "Pattern Relief / Displacement", "height", "#9ea2aa"],
        ["normal", "Normal", "Tangent Normal Map", "height", "#9ea2aa"],
        ["occlusion", "Occlusion", "Cavity Indirect AO", "rough", "#9ea2aa"],
        ["coat", "Clearcoat", "Ceramic / Candy Glaze", "rough", "#9ea2aa"],
        ["sheen", "Sheen", "Wool / Velvet Grazing", "albedo", "#9ea2aa"],
      ],
      Read: () => state.params.patternChannelMask || { albedo: true, roughness: true, metalness: true, height: true, normal: true, occlusion: true, coat: true, sheen: true },
      Write: (next) => {
        state.params.patternChannelMask = next;
        syncPatternUniformsFast();
        renderLeftOutliner();
      },
    });
    SliderRow(patSec.Inner, {
      Label: "Beadwork Relief",
      Minimum: 0,
      Maximum: 0.012,
      Step: 0.0005,
      Digits: 4,
      Read: () => state.params.pattern?.beadwork?.height ?? 0,
      Write: (v) => {
        if (!state.params.pattern) return;
        if (!state.params.pattern.beadwork) {
          state.params.pattern.beadwork = { columns: 96, rows: 72, height: v };
        } else {
          state.params.pattern.beadwork.height = v;
        }
        syncPatternUniformsFast();
      },
    });
  }

  function rebuildInspector() {
    if (inspectorScope) ReleaseScope(inspectorScope);
    inspectorScope = OpenScope();
    summaryHost.innerHTML = "";
    rightInner.innerHTML = "";

    ensureAuthoring(state.params);
    const p = state.params;
    const recipe = getRecipe(p);
    const layers = p.Authoring.Layers;
    const isPatternSelected = state.selectedLayerId === "__pattern__" && !!p.pattern;
    const selectedLayer = !isPatternSelected
      ? layers.find((l) => l.Identity === state.selectedLayerId) ||
        layers.find((l) => l.Model === "surface") ||
        layers[0]
      : null;

    summaryHost.append(
      Construct("div", "object-heading", [
        Construct("div", "object-symbol", ConstructGlyph(isPatternSelected ? "Pattern" : "Material")),
        Construct("div", "object-heading-copy", [
          Construct(
            "strong",
            null,
            isPatternSelected
              ? p.pattern.name || "Tileable Pattern"
              : `${p.name || "Material"} · ${selectedLayer?.Name || "Surface"}`,
          ),
          Construct(
            "span",
            null,
            isPatternSelected
              ? "TILEABLE PATTERN LAYER INSPECTOR"
              : `${(selectedLayer?.Model || "surface").toUpperCase()} STRATA · ${p.label || recipe.title}`,
          ),
        ]),
      ]),
    );

    MetricTrio(summaryHost, [
      { Label: "Roughness", Highlight: true, Read: () => ({ value: Number(state.params.roughness ?? 0.3).toFixed(2), sub: "Microfacet" }) },
      { Label: "Metalness", Read: () => ({ value: `${Math.round((state.params.metalness ?? 0) * 100)}%`, sub: "Conductor" }) },
      {
        Label: isPatternSelected ? "Tile" : "Strata",
        Read: () =>
          isPatternSelected
            ? { value: `${(state.params.pattern?.repeats ?? 2).toFixed(1)}×`, sub: (state.params.pattern?.mapping || "uv").toUpperCase() }
            : { value: `${layers.filter((l) => l.Enabled).length}/${layers.length}`, sub: `${selectedLayer?.Channels?.length || 0} ch` },
      },
    ]);

    if (isPatternSelected) {
      renderPatternSection(rightInner, true);
    }

    // Dedicated Layer Stack Inspector for the Selected Material Layer!
    if (selectedLayer) {
      const layerSec = SectionCard(rightInner, {
        Title: `Layer Inspector: ${selectedLayer.Name}`,
        Glyph: "Layers",
        Badge: selectedLayer.Model.toUpperCase(),
        Open: true,
      });

      TextRow(layerSec.Inner, {
        Label: "Layer Name",
        Read: () => selectedLayer.Name,
        Write: (v) => {
          selectedLayer.Name = String(v || "Layer").slice(0, 80);
          renderLeftOutliner();
        },
      });
      SwitchRow(layerSec.Inner, {
        Label: "Layer Enabled",
        Read: () => selectedLayer.Enabled,
        Write: (v) => {
          selectedLayer.Enabled = !!v;
          syncMaterialLive();
          renderLeftOutliner();
        },
      });

      // Layer PBR Channels — Filterable Dropdown -> Removable Pills!
      ChannelMaskRow(layerSec.Inner, {
        Label: "Layer PBR Channels",
        AddLabel: "Channel",
        StrictPresence: true,
        Channels: MATERIAL_PBR_CHANNEL_SPEC,
        Read: () => Object.fromEntries(selectedLayer.Channels.map((c) => [c, true])),
        Write: (mapObj) => {
          const nextList = MATERIAL_PBR_CHANNEL_SPEC.map(([k]) => k).filter((k) => mapObj[k] === true);
          if (!nextList.length) nextList.push("colour");
          selectedLayer.Channels = nextList;
          if (!nextList.includes(state.inspectedChannel)) {
            state.inspectedChannel = nextList[0];
          }
          syncMaterialLive();
          renderLeftOutliner();
          rebuildInspector();
        },
      });

      if (selectedLayer.Model === "detail" || selectedLayer.Model === "flakes") {
        SliderRow(layerSec.Inner, {
          Label: "Layer Coverage",
          Minimum: 0,
          Maximum: 1,
          Step: 0.01,
          Read: () => ReadMaterialParameter(state.params, ChannelAddress(selectedLayer, "coverage")) ?? 1,
          Write: (v) => {
            mutateParamsPreservingPattern((cur) =>
              WriteMaterialParameter(cur, ChannelAddress(selectedLayer, "coverage"), v),
            );
            syncMaterialLive();
          },
        });
      }

      // Per-Channel Source & Procedural Field Generator Inspector inside the Selected Layer!
      const chOptions = selectedLayer.Channels.map((ch) => ({
        value: ch,
        label: PbrChannels[ch]?.Caption || ch,
        sub: ChannelAddress(selectedLayer, ch),
      }));
      if (!selectedLayer.Channels.includes(state.inspectedChannel)) {
        state.inspectedChannel = selectedLayer.Channels[0] || "colour";
      }
      const chKey = state.inspectedChannel;
      const chSpec = PbrChannels[chKey];
      const chAddr = ResolveAddress(p, ChannelAddress(selectedLayer, chKey));
      const chSource = p.Authoring.Sources[chAddr] || { Mode: "value" };

      FilterDropdownPill(layerSec.Inner, {
        Label: "Inspect Channel Field",
        Prefix: "Ch:",
        Placeholder: "Filter layer channels...",
        Options: chOptions,
        Read: () => chKey,
        Write: (nextCh) => {
          state.inspectedChannel = nextCh;
          rebuildInspector();
        },
      });

      if (chSpec?.Type === "colour") {
        ColorRow(layerSec.Inner, {
          Label: `${chSpec.Caption} Value`,
          Read: () => ReadMaterialParameter(state.params, chAddr) ?? chSpec.Default ?? "#ffffff",
          Write: (hex) => {
            mutateParamsPreservingPattern((cur) => WriteMaterialParameter(cur, chAddr, hex));
            syncMaterialLive();
          },
        });
      } else if (chSpec?.Type === "scalar") {
        const span = (chSpec.Maximum ?? 1) - (chSpec.Minimum ?? 0);
        const step = span > 100 ? 5 : span > 4 ? 0.05 : 0.01;
        SliderRow(layerSec.Inner, {
          Label: `${chSpec.Caption} Value`,
          Minimum: chSpec.Minimum ?? 0,
          Maximum: chSpec.Maximum ?? 1,
          Step: step,
          Digits: span > 100 ? 0 : 2,
          Read: () => ReadMaterialParameter(state.params, chAddr) ?? chSpec.Default ?? chSpec.Minimum ?? 0,
          Write: (v) => {
            mutateParamsPreservingPattern((cur) => WriteMaterialParameter(cur, chAddr, v));
            syncMaterialLive();
          },
        });
      }

      FilterDropdownPill(layerSec.Inner, {
        Label: `${PbrChannels[chKey]?.Caption || chKey} Source`,
        Prefix: "Src:",
        Placeholder: "Filter source modes...",
        Options: SOURCE_MODES,
        Read: () => chSource.Mode || "value",
        Write: (mode) => {
          mutateParamsPreservingPattern((cur) => {
            let next = structuredClone(cur);
            const curSrc = next.Authoring.Sources[chAddr] || {};
            if (mode === "value") {
              delete next.Authoring.Sources[chAddr];
              return next;
            }
            if (mode === "field") {
              let gen =
                curSrc.Generator &&
                next.Authoring.Generators.find((g) => g.Identity === curSrc.Generator && g.Coded);
              if (!gen) {
                next = DeclareCodedField(next, "noise", `${selectedLayer.Name} ${chKey}`, "Layer field");
                gen = next.Authoring.Generators[next.Authoring.Generators.length - 1];
              }
              next.Authoring.Sources[chAddr] = {
                Mode: "field",
                Generator: gen.Identity,
                Stops: curSrc.Stops || ["#0b0b0b", "#e8c27a"],
                Direct: curSrc.Direct ?? false,
              };
            } else {
              next.Authoring.Sources[chAddr] = {
                Mode: mode,
                Stops: curSrc.Stops || ["#0b0b0b", "#e8c27a"],
                Direct: curSrc.Direct ?? false,
                ...(mode === "texture" && curSrc.Texture ? { Texture: curSrc.Texture } : {}),
              };
            }
            return next;
          });
          syncMaterialLive();
          renderLeftOutliner();
          rebuildInspector();
        },
      });

      if (chSource.Mode && chSource.Mode !== "value") {
        // Channel preview badge + live field preview thumbnail
        const prevRow = Construct("div", "property-row");
        const prevImg = document.createElement("img");
        prevImg.alt = `${chKey} field preview`;
        prevImg.style.cssText =
          "width:44px;height:44px;border-radius:0;border:1px solid var(--line);background:#0f1115;object-fit:cover;";
        const prevMeta = Construct(
          "span",
          "property-hint",
          `Live ${chSource.Mode.toUpperCase()} projection on "${selectedLayer.Name}"`,
        );
        prevRow.append(Construct("span", "property-label", "Field Preview"), Construct("div", "chip-strip", [prevImg, prevMeta]));
        layerSec.Inner.append(prevRow);
        RasterChannelPreview(state.params, selectedLayer.Identity, chKey, 48)
          .then((url) => { if (url && prevImg.isConnected) prevImg.src = url; })
          .catch(() => {});

        if (chSource.Mode === "field") {
          const gen = p.Authoring.Generators.find((g) => g.Identity === chSource.Generator && g.Coded);
          if (gen) {
            FilterDropdownPill(layerSec.Inner, {
              Label: "Field Generator",
              Prefix: "Gen:",
              Placeholder: "Filter procedural generators...",
              Options: GeneratorDefinitions.map((d) => ({ value: d.identity, label: d.label, sub: d.description })),
              Read: () => gen.Procedure,
              Write: (proc) => {
                mutateParamsPreservingPattern((cur) => {
                  const next = structuredClone(cur);
                  const targetGen = next.Authoring.Generators.find((g) => g.Identity === gen.Identity);
                  if (!targetGen) return next;
                  targetGen.Procedure = proc;
                  const keys = ["scale", "seed", "angle", "amount", ...(proc.startsWith("camouflage") ? ["colour0", "colour1", "colour2", "colour3", "softness", "pixelSize"] : [])];
                  targetGen.Inputs = keys.map((k) => `field:${targetGen.Identity}:${k}`);
                  if (proc.startsWith("camouflage")) {
                    CamouflagePalette.forEach((col, i) => {
                      next.Authoring.Inputs[`field:${targetGen.Identity}:colour${i}`] ??= col;
                    });
                    next.Authoring.Inputs[`field:${targetGen.Identity}:softness`] ??= 0.012;
                    next.Authoring.Inputs[`field:${targetGen.Identity}:pixelSize`] ??= 4;
                  }
                  return next;
                });
                syncMaterialLive();
                rebuildInspector();
              },
            });

            for (const [k, label, min, max, step, unit] of [
              ["scale", "Field Scale", 1, 64, 0.5, "×"],
              ["seed", "Random Seed", 0, 200, 1, "—"],
              ["angle", "Rotation Angle", -180, 180, 1, "°"],
              ["amount", "Field Contrast", 0, 1, 0.02, "—"],
            ]) {
              const addr = `field:${gen.Identity}:${k}`;
              SliderRow(layerSec.Inner, {
                Label: label,
                Minimum: min,
                Maximum: max,
                Step: step,
                Unit: unit,
                Read: () => ReadMaterialParameter(state.params, addr) ?? (k === "scale" ? 8 : k === "seed" ? 12 : k === "amount" ? 1 : 0),
                Write: (v) => {
                  mutateParamsPreservingPattern((cur) => WriteMaterialParameter(cur, addr, v));
                  syncMaterialLive();
                },
              });
            }

            if (gen.Procedure.startsWith("camouflage")) {
              MultiColourRow(layerSec.Inner, {
                Label: "Camo Palette",
                Read: () => [0, 1, 2, 3].map((i) => ReadMaterialParameter(state.params, `field:${gen.Identity}:colour${i}`) || CamouflagePalette[i]),
                Write: (cols) => {
                  mutateParamsPreservingPattern((cur) => {
                    let next = cur;
                    cols.slice(0, 6).forEach((hex, idx) => {
                      next = WriteMaterialParameter(next, `field:${gen.Identity}:colour${idx}`, hex);
                    });
                    return next;
                  });
                  syncMaterialLive();
                },
              });
            } else {
              MultiColourRow(layerSec.Inner, {
                Label: "Field Gradient Stops",
                Read: () => p.Authoring.Sources[chAddr]?.Stops || ["#0b0b0b", "#e8c27a"],
                Write: (cols) => {
                  mutateParamsPreservingPattern((cur) => {
                    const next = structuredClone(cur);
                    if (next.Authoring.Sources[chAddr]) next.Authoring.Sources[chAddr].Stops = cols.slice(0, 8);
                    return next;
                  });
                  syncMaterialLive();
                },
              });
            }
          }
        } else if (["linear", "radial", "conic"].includes(chSource.Mode)) {
          MultiColourRow(layerSec.Inner, {
            Label: "Gradient Ramp Stops",
            Read: () => p.Authoring.Sources[chAddr]?.Stops || ["#1a1d24", "#e8c27a"],
            Write: (cols) => {
              mutateParamsPreservingPattern((cur) => {
                const next = structuredClone(cur);
                if (next.Authoring.Sources[chAddr]) next.Authoring.Sources[chAddr].Stops = cols.slice(0, 8);
                return next;
              });
              syncMaterialLive();
            },
          });
        } else if (chSource.Mode === "texture") {
          ActionRow(layerSec.Inner, [
            { Label: "+ Upload 256px Map", Glyph: "Image", Tone: "warm", Act: () => triggerPatternImageUpload("channel-texture") },
          ]);
        }

        SwitchRow(layerSec.Inner, {
          Label: "Direct Value Override",
          Read: () => !!p.Authoring.Sources[chAddr]?.Direct,
          Write: (v) => {
            mutateParamsPreservingPattern((cur) => {
              const next = structuredClone(cur);
              if (next.Authoring.Sources[chAddr]) next.Authoring.Sources[chAddr].Direct = !!v;
              return next;
            });
            syncMaterialLive();
          },
        });
      }
    }

    // Layer Model-Specific Optical & Surface Recipe Controls
    if (!selectedLayer || selectedLayer.Model === "surface") {
      const baseSec = SectionCard(rightInner, { Title: "Surface Albedo & Microfacet", Glyph: "Palette", Badge: p.category || "Surface" });
      ColorRow(baseSec.Inner, {
        Label: "Base Albedo",
        Read: () => state.params.color || "#888888",
        Write: (hex) => {
          state.params = applyRecipeColor(state.params, "color", hex);
          syncMaterialLive();
        },
      });
      SliderRow(baseSec.Inner, {
        Label: "Roughness",
        Minimum: 0.02, Maximum: 1, Step: 0.01,
        Read: () => state.params.roughness ?? 0.35,
        Write: (v) => { state.params.roughness = v; syncMaterialLive(); },
      });
      SliderRow(baseSec.Inner, {
        Label: "Metalness",
        Minimum: 0, Maximum: 1, Step: 0.01,
        Read: () => state.params.metalness ?? 0,
        Write: (v) => { state.params.metalness = v; syncMaterialLive(); },
      });
      SliderRow(baseSec.Inner, {
        Label: "Specular / IOR",
        Minimum: 1, Maximum: 2.8, Step: 0.02,
        Read: () => state.params.ior ?? 1.5,
        Write: (v) => { state.params.ior = v; syncMaterialLive(); },
      });

      // Dynamic Procedural Recipe Controls
      const groups = new Map();
      for (const ctrl of recipe.controls) {
        if (ctrl.key === "color") continue;
        const g = ctrl.group || "surface";
        if (!groups.has(g)) groups.set(g, []);
        groups.get(g).push(ctrl);
      }
      for (const [gName, ctrls] of groups.entries()) {
        const title = gName.charAt(0).toUpperCase() + gName.slice(1) + " Controls";
        const sec = SectionCard(rightInner, { Title: title, Glyph: "Sliders", Open: gName !== "iridescence" });
        for (const ctrl of ctrls) {
          if (ctrl.color) continue;
          const min = Number.isFinite(ctrl.min) ? ctrl.min : 0;
          const max = Number.isFinite(ctrl.max) ? ctrl.max : 1;
          const step = Number.isFinite(ctrl.step) ? ctrl.step : (max - min) > 10 ? 0.5 : 0.01;
          SliderRow(sec.Inner, {
            Label: ctrl.label || ctrl.id,
            Minimum: min,
            Maximum: max,
            Step: step,
            Unit: ctrl.unit || "—",
            Read: () => {
              const val = recipeControlValue(state.params, ctrl);
              return Number.isFinite(val) ? val : min;
            },
            Write: (v) => {
              state.params = applyRecipeControl(state.params, ctrl, v);
              syncMaterialLive();
            },
          });
        }
        if (recipe.supportsMetalScratches && gName === "finish") {
          SwitchRow(sec.Inner, {
            Label: "Metal Micro-Scratches",
            Read: () => !!state.params.metalScratches,
            Write: (v) => {
              state.params = { ...state.params, metalScratches: v };
              rebuildInspector();
              syncMaterialLive();
            },
          });
        }
      }
    } else if (selectedLayer.Model === "coat") {
      const coatSec = SectionCard(rightInner, { Title: "Clearcoat Optical Properties", Glyph: "Sparkles", Open: true });
      SliderRow(coatSec.Inner, {
        Label: "Clearcoat Weight",
        Minimum: 0, Maximum: 1, Step: 0.01,
        Read: () => state.params.coat ?? 0,
        Write: (v) => { state.params.coat = v; syncMaterialLive(); },
      });
      SliderRow(coatSec.Inner, {
        Label: "Coat Roughness",
        Minimum: 0.01, Maximum: 1, Step: 0.01,
        Read: () => state.params.coatRoughness ?? 0.08,
        Write: (v) => { state.params.coatRoughness = v; syncMaterialLive(); },
      });
      SliderRow(coatSec.Inner, {
        Label: "Coat IOR",
        Minimum: 1, Maximum: 2.5, Step: 0.02,
        Read: () => state.params.coatIor ?? 1.5,
        Write: (v) => { state.params.coatIor = v; syncMaterialLive(); },
      });
      SliderRow(coatSec.Inner, {
        Label: "Orange Peel Relief",
        Minimum: 0, Maximum: 1, Step: 0.02,
        Read: () => state.params.orangePeel ?? 0,
        Write: (v) => { state.params.orangePeel = v; syncMaterialLive(); },
      });
    } else if (selectedLayer.Model === "flakes") {
      const flSec = SectionCard(rightInner, { Title: "Metallic Flake Dispersion", Glyph: "Grid", Open: true });
      SliderRow(flSec.Inner, {
        Label: "Flake Density",
        Minimum: 0, Maximum: 1, Step: 0.01,
        Read: () => state.params.flakes ?? 0,
        Write: (v) => { state.params.flakes = v; syncMaterialLive(); },
      });
      SliderRow(flSec.Inner, {
        Label: "Flake Frequency",
        Minimum: 4, Maximum: 120, Step: 1,
        Read: () => state.params.flakeScale ?? 24,
        Write: (v) => { state.params.flakeScale = v; syncMaterialLive(); },
      });
      SliderRow(flSec.Inner, {
        Label: "Flake Tilt",
        Minimum: 0, Maximum: 1, Step: 0.02,
        Read: () => state.params.flakeTilt ?? 0.2,
        Write: (v) => { state.params.flakeTilt = v; syncMaterialLive(); },
      });
      MultiColourRow(flSec.Inner, {
        Label: "Flake Palette",
        Read: () => state.params.colors || [state.params.color || "#e8c27a", "#ffffff"],
        Write: (cols) => {
          state.params.colors = cols.slice(0, 8);
          state.params.color = cols[0] || state.params.color;
          syncMaterialLive();
        },
      });
    } else if (selectedLayer.Model === "detail") {
      const detSec = SectionCard(rightInner, { Title: "Detail Strata Presets", Glyph: "Sliders", Open: true });
      const applyDetailPreset = (proc, stops) => {
        mutateParamsPreservingPattern((cur) => {
          let next = structuredClone(cur);
          const curSrc = next.Authoring.Sources[ChannelAddress(selectedLayer, "colour")] || {};
          let gen =
            curSrc.Generator &&
            next.Authoring.Generators.find((g) => g.Identity === curSrc.Generator && g.Coded);
          if (!gen) {
            next = DeclareCodedField(next, proc, `${selectedLayer.Name} Field`, "Detail fields");
            gen = next.Authoring.Generators[next.Authoring.Generators.length - 1];
          } else {
            gen.Procedure = proc;
          }
          next.Authoring.Sources[ChannelAddress(selectedLayer, "colour")] = {
            Mode: "field",
            Generator: gen.Identity,
            Stops: stops,
            Direct: false,
          };
          return next;
        });
        state.inspectedChannel = "colour";
        syncMaterialLive();
        rebuildInspector();
      };
      ActionRow(detSec.Inner, [
        { Label: "Fractal Scuffs", Glyph: "Noise", Act: () => applyDetailPreset("noise", ["#14161a", "#c9b89b"]) },
        { Label: "Camo Patches", Glyph: "Sparkles", Tone: "warm", Act: () => applyDetailPreset("camouflage-smooth", CamouflagePalette) },
      ]);
      ActionRow(detSec.Inner, [
        { Label: "Voronoi Cells", Glyph: "Grid", Act: () => applyDetailPreset("cells", ["#22252b", "#d8aa5b"]) },
        { Label: "Directional Grain", Glyph: "Sliders", Act: () => applyDetailPreset("stripes", ["#1a1d22", "#a2abb8"]) },
      ]);
    }

    if (!isPatternSelected) {
      renderPatternSection(rightInner, false);
    }

    // Look-dev Lighting & Export
    const lightSec = SectionCard(rightInner, { Title: "Stage Lighting", Glyph: "Sun", Open: false });
    SwitchRow(lightSec.Inner, {
      Label: "Auto-Rotate Turntable",
      Read: () => !!state.autoRotate,
      Write: (v) => { state.autoRotate = !!v; },
    });
    SliderRow(lightSec.Inner, {
      Label: "Key Light Azimuth",
      Minimum: -180, Maximum: 180, Step: 1, Unit: "°",
      Read: () => state.lightAzimuth,
      Write: (v) => { state.lightAzimuth = v; updateLights(); },
    });
    SliderRow(lightSec.Inner, {
      Label: "Exposure",
      Minimum: 0.4, Maximum: 2.2, Step: 0.02, Unit: "EV",
      Read: () => state.exposure,
      Write: (v) => { state.exposure = v; updateLights(); },
    });

    const exportSec = SectionCard(rightInner, { Title: "Export & Paint Bridge", Glyph: "Download" });
    ActionRow(exportSec.Inner, [
      {
        Label: ".material TOML",
        Glyph: "Download",
        Tone: "accent",
        Act: () => {
          const slug = (state.params.name || "material").toLowerCase().replace(/[^a-z0-9]+/g, "-");
          downloadBlob(exportMaterialTOML(state.params), `${slug}.material`);
          Toast("Exported Frontier .material TOML");
        },
      },
      {
        Label: "JSON / JS",
        Glyph: "Download",
        Act: () => {
          const slug = (state.params.name || "material").toLowerCase().replace(/[^a-z0-9]+/g, "-");
          downloadBlob(exportMaterialJSON(state.params), `${slug}.json`, "application/json");
          downloadBlob(exportMaterialShader(state.params), `${slug}.js`, "text/javascript");
          Toast("Exported Material JSON + JS module");
        },
      },
    ]);
    ActionRow(exportSec.Inner, [
      {
        Label: "Use in Texture Paint",
        Glyph: "ArrowRight",
        Tone: "warm",
        Act: () => appBridge.sendMaterialToPaint(state.params),
      },
    ]);

    CloseScope(inspectorScope);
    Broadcast();
  }

  // ----------------------------------------------------------------- EVENTS, DROP & LOOP
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

  shapeSelect.addEventListener("change", () => {
    state.shape = shapeSelect.value;
    rebuildMeshAndMaterial();
    renderLeftOutliner();
  });
  bgSelect.addEventListener("change", () => {
    state.backdrop = bgSelect.value;
    const found = backdropOptions.find(([, val]) => val === state.backdrop);
    const hex = found ? found[2] : "#1c1e22";
    applyStageBackdrop(hex);
    renderLeftOutliner();
  });

  const resizeStage = () => {
    const w = stage.clientWidth || 600;
    const h = stage.clientHeight || 480;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  };

  function syncFullscreenButton() {
    const active = !!state.fullscreen;
    grid.classList.toggle("mw-fullscreen", active);
    btnFullscreen.classList.toggle("active", active);
    btnFullscreen.setAttribute("aria-pressed", active ? "true" : "false");
    fsIconSlot.innerHTML = "";
    fsIconSlot.append(ConstructGlyph(active ? "Minimize" : "Maximize"));
    fsLabel.textContent = active ? "Exit Fullscreen" : "Fullscreen";
    resizeStage();
  }

  btnFullscreen.addEventListener("click", () => {
    const next = !state.fullscreen;
    state.fullscreen = next;
    syncFullscreenButton();
    try {
      if (next && !document.fullscreenElement && typeof stage.requestFullscreen === "function") {
        Promise.resolve(stage.requestFullscreen()).catch(() => {});
      } else if (!next && document.fullscreenElement === stage && typeof document.exitFullscreen === "function") {
        Promise.resolve(document.exitFullscreen()).catch(() => {});
      }
    } catch {}
  });

  const onFsChange = () => {
    if (!document.fullscreenElement && state.fullscreen) {
      // Keep CSS fullscreen only if user didn't initiate exit via Escape from native fullscreen
      if (onFsChange._wasNative) {
        state.fullscreen = false;
        syncFullscreenButton();
      }
    }
    onFsChange._wasNative = document.fullscreenElement === stage;
    resizeStage();
  };
  document.addEventListener("fullscreenchange", onFsChange);

  btnZoomOut.addEventListener("click", () => applyZoomFactor(1.16));
  btnZoomIn.addEventListener("click", () => applyZoomFactor(0.86));
  btnResetCam.addEventListener("click", () => {
    camera.position.copy(DEFAULT_CAM_POS);
    controls.target.copy(DEFAULT_CAM_TARGET);
    controls.update();
    syncZoomReadout();
  });
  controls.addEventListener?.("change", syncZoomReadout);

  const resizeObserver = new ResizeObserver(resizeStage);
  resizeObserver.observe(stage);

  updateLights();
  if (appBridge?.shared?.importedMesh) {
    applyImportedMesh(appBridge.shared.importedMesh);
  } else {
    rebuildMeshAndMaterial();
  }
  syncZoomReadout();
  renderLeftOutliner();
  rebuildInspector();

  const tick = () => {
    if (disposed) return;
    if (state.autoRotate && sampleMesh) sampleMesh.rotation.y += 0.008;
    controls.update();
    renderer.render(scene, camera);
    rafId = requestAnimationFrame(tick);
  };
  rafId = requestAnimationFrame(tick);

  return {
      name: "Material",
    applyAsset,
    applyImportedMesh,
    getSnapshot: () => ({
      id: state.params.id,
      name: state.params.name,
      shape: state.shape,
      fullscreen: !!state.fullscreen,
      selectedLayerId: state.selectedLayerId,
      layerCount: state.params.Authoring?.Layers?.length || 0,
      hasPattern: !!state.params.pattern,
      patternName: state.params.pattern?.name || null,
    }),
    dispose() {
      disposed = true;
      cancelAnimationFrame(rafId);
      document.removeEventListener("fullscreenchange", onFsChange);
      resizeObserver.disconnect();
      if (inspectorScope) ReleaseScope(inspectorScope);
      controls.dispose();
      backdropTex?.dispose?.();
      envTex.dispose();
      if (customBridgeGeo) customBridgeGeo.dispose();
      if (sampleMesh) sampleMesh.geometry.dispose();
      if (activeMaterial) activeMaterial.dispose();
      studioFloor.geometry.dispose();
      studioFloor.material.dispose();
      studioGrid.geometry.dispose();
      studioGrid.material.dispose();
      stand.geometry.dispose();
      stand.material.dispose();
      standBottom.geometry.dispose();
      standBottom.material.dispose();
      standRing.geometry.dispose();
      standRing.material.dispose();
      supportBall.geometry.dispose();
      supportBall.material.dispose();
      renderer.dispose();
      root.innerHTML = "";
    },
  };
}
