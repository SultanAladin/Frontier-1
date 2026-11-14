/*==========================================================================================================================================
                                                     TEXTURESTUDIOVALIDATION.MJS
==========================================================================================================================================*/
/* Comprehensive Automated Validation Suite for Frontier Experimental Remastered Texture Studio.
   Run via:
     node TextureStudioValidation.mjs http://127.0.0.1:8088 */

import assert from "node:assert/strict";
import { register } from "node:module";
import { pathToFileURL } from "node:url";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const threeUrl = pathToFileURL(resolve(HERE, "Vendor/three.esm.min.js")).href;
register(
  "data:text/javascript," +
    encodeURIComponent(
      `export async function resolve(s, c, n) { if (s === "three") return { shortCircuit: true, url: "${threeUrl}" }; return n(s, c); }`,
    ),
);

const THREE = await import("./Vendor/three.esm.min.js");
const {
  materials,
  normalizeMaterial,
  createMaterial,
  createBallGeometry,
} = await import("./Engine/Materials/materials.js");
const { createDrapedClothGeometry } = await import("./Engine/Materials/clothGeometry.js");
const { createLeatherSwatchGeometry } = await import("./Engine/Materials/leatherGeometry.js");
const { createBotanicalGeometry } = await import("./Engine/Materials/botanicalGeometry.js");
const { getRecipe } = await import("./Engine/Materials/materialProfiles.js");
const { exportMaterialTOML, exportMaterialJSON } = await import("./Engine/Materials/materialExport.js");

const {
  createArtworkDocument,
  createArtworkLayer,
  createDefaultModifier,
  artworkDocumentToSvgString,
  evaluateArtworkDocument,
} = await import("./Engine/Patterns/ArtworkStackEngine.js");
const {
  listPatternStarterCatalog,
  listStampStarterCatalog,
  loadStarterAsArtworkDocument,
  getArtworkPreviewSvg,
} = await import("./Engine/Patterns/StarterCatalogBridge.js");

const {
  PAINT_PBR_CHANNELS,
  EXTENDED_PBR_KEYS,
  createDefaultExtendedValues,
  createStampInstance,
  generateProceduralMask,
  featherMaskPixels,
  lookupSurfaceAtUv,
  buildPaintTargetGeometry,
  buildMeshPaintIndex,
  buildDefaultMeshCues,
  createPaintDocument,
  createPaintLayer,
  createStrokeAccumulator,
  applyBrushDab3D,
  applyBrushDabUV,
  commitStrokeAccumulator,
  applyFloodFill,
  compositePaintDocument,
  sampleCompositedAtUV,
  CURVE_PRESETS,
  COLOUR_DYNAMIC_MODES,
  COLOUR_GRADIENT_PRESETS,
  OPACITY_DYNAMIC_MODES,
  sampleCurve,
  getToolChannels,
  resolveDabVariation,
  rgbaPixelsToSurfaceMaps,
} = await import("./Engine/TexturePaintEngine.js");

const {
  BAKE_MAP_CATALOG,
  createBakeStudyPair,
  bakeMeshMaps,
  bakeVolumeSdf,
} = await import("./Engine/BakingEngine.js");

const results = [];
async function suite(name, fn) {
  const t0 = performance.now();
  await fn();
  const ms = Math.round(performance.now() - t0);
  results.push({ name, ms, ok: true });
  console.log(`PASS [${ms}ms] ${name}`);
}

await suite("1. Material Library, Look-Dev Geometries, Recipes & TOML Export", async () => {
  assert(materials.length >= 40, `Expected >=40 materials, got ${materials.length}`);
  for (const m of materials.slice(0, 15)) {
    const norm = normalizeMaterial(m);
    const recipe = getRecipe(norm);
    assert(recipe.controls.length >= 1, `Missing recipe controls for ${m.name}`);
    const threeMat = createMaterial(norm);
    assert(threeMat.isMaterial, `Invalid Three.js material for ${m.name}`);
    threeMat.dispose();
  }
  const toml = exportMaterialTOML(normalizeMaterial(materials[0]));
  assert(toml.includes("Format = 1") && toml.includes("[[Strata]]"), "Invalid .material TOML export");
  const json = JSON.parse(exportMaterialJSON(normalizeMaterial(materials[0])));
  assert.equal(json.schema, "alloy.material.v6");

  const geos = [
    createBallGeometry(),
    createDrapedClothGeometry(),
    createLeatherSwatchGeometry({ kind: "swatch" }),
    createBotanicalGeometry("Leaf", 10),
  ];
  geos.forEach((g) => {
    assert(g.attributes.position.count > 100);
    g.dispose();
  });
});

await suite("2. Unified Pattern Stack + Noise-Scale Modifier + 108 Starters", async () => {
  const patCatalog = listPatternStarterCatalog();
  assert(patCatalog.length >= 108, `Expected >=108 pattern starters, got ${patCatalog.length}`);

  const noiseDoc = createArtworkDocument("pattern");
  noiseDoc.layers = [
    createArtworkLayer("motif", {
      name: "Hex Matrix",
      transform: { x: 256, y: 256, width: 38, height: 38, rotation: 0 },
      shape: { motifType: "hexagon", color: "#eff4cf" },
      surface: { finish: "ceramic", roughness: 0.25, metalness: 0.1, height: 0.3, enableAlbedo: true, enableRoughness: true, enableMetalness: true, enableHeight: true },
      modifiers: [
        { ...createDefaultModifier("array-grid"), columns: 8, rows: 8 },
        { ...createDefaultModifier("noise-scale"), minScale: 0.25, maxScale: 1.75, frequency: 4, seed: 19 },
      ],
    }),
  ];

  const ev = evaluateArtworkDocument(noiseDoc, 256);
  assert.equal(ev.width, 256);
  assert.equal(ev.albedo.length, 256 * 256 * 4);
  assert(ev.stats.instances >= 64, `Expected >=64 grid instances, got ${ev.stats.instances}`);
  assert(
    ev.stats.maxScale > ev.stats.minScale * 1.4,
    `Expected strong noise-driven scale variance, got ${ev.stats.minScale}..${ev.stats.maxScale}`,
  );
  let roughChanged = false;
  for (let i = 0; i < ev.roughness.length; i += 4) {
    if (ev.roughness[i] !== 220) {
      roughChanged = true;
      break;
    }
  }
  assert(roughChanged, "Pattern roughness channel did not evaluate");
});

await suite("3. Unified Stamp Stack + Transparent Decal Cutout + 5 Starters", async () => {
  const stampCatalog = listStampStarterCatalog();
  assert(stampCatalog.length >= 5, `Expected >=5 stamp starters, got ${stampCatalog.length}`);
  const stampDoc = loadStarterAsArtworkDocument(stampCatalog[0]);
  const ev = evaluateArtworkDocument(stampDoc, 256);
  let transparentCount = 0;
  let opaqueCount = 0;
  for (let i = 3; i < ev.albedo.length; i += 4) {
    if (ev.albedo[i] < 10) transparentCount++;
    else if (ev.albedo[i] > 200) opaqueCount++;
  }
  assert(transparentCount > 5000 && opaqueCount > 1000, "Stamp cutout alpha balance failed");
});

await suite("4. Texture Paint Engine: Packed UVs, 3D Strokes, 2D UV Strokes, Flood Fill, Generators, Decals & Masks", async () => {
  for (const meshId of ["teapot", "shaderball", "box", "sphere", "cloth"]) {
    const g = buildPaintTargetGeometry(meshId);
    const uv = g.attributes.uv;
    for (let i = 0; i < uv.count; i++) {
      const u = uv.getX(i), v = uv.getY(i);
      assert(u >= 0 && u <= 1 && v >= 0 && v <= 1, `${meshId} UV out of [0..1] tile: (${u},${v})`);
    }
    g.dispose();
  }

  const res = 128;
  const geo = buildPaintTargetGeometry("teapot");
  const idx = buildMeshPaintIndex(geo);
  assert.deepEqual(idx.partNames, ["Body", "Lid", "Handle", "Spout"]);
  const cues = buildDefaultMeshCues(geo, idx, res);
  const doc = createPaintDocument(res);
  const paintLayer = doc.layers[1];

  // 3D Ray Stroke
  const tri = idx.tris[0];
  const hitPos = new THREE.Vector3(...tri.p0).add(new THREE.Vector3(...tri.p1)).add(new THREE.Vector3(...tri.p2)).multiplyScalar(1 / 3);
  const brush = { tool: "paint", tip: "round", color: "#ff5533", roughness: 0.25, metalness: 0.8, height: 0.4, opacity: 1, flow: 1, hardness: 0.8 };
  const accum3D = createStrokeAccumulator(res);
  applyBrushDab3D({ strokeAccum: accum3D, meshIndex: idx, cues, hitPos, hitNormal: new THREE.Vector3(...tri.faceNormal), worldRadius: 0.4, brush });
  assert.equal(commitStrokeAccumulator(paintLayer, accum3D, brush, false), true);

  // 2D UV Stroke
  const accumUV = createStrokeAccumulator(res);
  applyBrushDabUV({ strokeAccum: accumUV, cues, u: 0.5, v: 0.5, pixelRadius: 10, brush });
  assert.equal(commitStrokeAccumulator(paintLayer, accumUV, brush, false), true);

  // Generator Layer (Edge Wear) + Decal Layer + Mask
  const genLayer = createPaintLayer("generator", { res, generator: { preset: "edge-wear", color: "#ffffff", threshold: 0.2, softness: 0.3, noiseMix: 0.2, noiseScale: 5 } });
  doc.layers.push(genLayer);

  const comp = compositePaintDocument(doc, cues);
  const sample = sampleCompositedAtUV(comp, 0.5, 0.5);
  assert(/^#[0-9a-f]{6}$/.test(sample.color), `Invalid sampled color ${sample.color}`);

  // Curve sampling, Colour Dynamics, Jitter, Custom Image Surface Maps & Selective PBR Channels
  assert(Math.abs(sampleCurve(CURVE_PRESETS.linear, 0.5) - 0.5) < 0.05, "Linear curve failed midpoint");
  assert(sampleCurve(CURVE_PRESETS.bell, 0.5) > sampleCurve(CURVE_PRESETS.bell, 0.05), "Bell curve should peak near center");

  const dynBrush = {
    tool: "paint",
    tip: "round",
    color: "#cc4422",
    roughness: 0.3,
    metalness: 0.2,
    height: 0.8,
    opacity: 1,
    flow: 0.9,
    angle: 15,
    jitter: { size: 0.5, angle: 45, scatter: 0.4, flow: 0.3 },
    colourDynamics: { hueJitter: 0.6, satJitter: 0.3, valJitter: 0.25, strokeGradient: true },
    curves: {
      falloff: CURVE_PRESETS.ease,
      sizeOverStroke: CURVE_PRESETS.bell,
      opacityOverStroke: CURVE_PRESETS.punch,
      hueOverStroke: [[0, 0.2], [1, 0.9]],
    },
  };
  const v0 = resolveDabVariation(dynBrush, 0.1, 101, 25);
  const v1 = resolveDabVariation(dynBrush, 0.9, 102, 25);
  assert.notEqual(v0.color, v1.color, "Colour Dynamics should produce color variation along stroke");
  assert(v0.sizeScale !== v1.sizeScale, "Size Jitter / Stroke Curve should vary dab scale");

  // Stroke Gradient, Multicolour Palette, Rainbow, Temperature & Changing Opacity Dynamics
  assert(COLOUR_DYNAMIC_MODES.length >= 6 && COLOUR_GRADIENT_PRESETS.length >= 8 && OPACITY_DYNAMIC_MODES.length >= 6);
  const gradBrush = {
    tool: "paint", tip: "round", color: "#ffffff", flow: 0.9,
    colourDynamics: {
      mode: "stroke-gradient",
      gradientStops: ["#ff0000", "#0000ff"],
      gradientLength: 20,
      gradientWrap: "clamp",
      opacityMode: "fade-out",
      opacityMin: 0.1,
    },
  };
  const gStart = resolveDabVariation(gradBrush, 0, 1, 0, { dabIndex: 0 });
  const gEnd = resolveDabVariation(gradBrush, 1, 2, 0, { dabIndex: 20 });
  assert.equal(gStart.color, "#ff0000", "Stroke Gradient start must match first stop");
  assert.equal(gEnd.color, "#0000ff", "Stroke Gradient end must match last stop");
  assert(gStart.flow > gEnd.flow + 0.5, "Opacity Fade-Out should reduce dab flow/opacity along stroke");

  const multiBrush = {
    tool: "paint", tip: "round", color: "#ffffff", flow: 0.8,
    colourDynamics: {
      mode: "multicolour-cycle",
      paletteColors: ["#ff2244", "#22eeaa", "#4488ff"],
      opacityMode: "pulse",
      opacityMin: 0.2,
      opacityPulseFreq: 0.5,
    },
  };
  assert.equal(resolveDabVariation(multiBrush, 0, 1, 0, { dabIndex: 0 }).color, "#ff2244");
  assert.equal(resolveDabVariation(multiBrush, 0, 2, 0, { dabIndex: 1 }).color, "#22eeaa");
  assert.equal(resolveDabVariation(multiBrush, 0, 3, 0, { dabIndex: 2 }).color, "#4488ff");

  const customImgRgba = new Uint8ClampedArray(16 * 16 * 4);
  for (let i = 0; i < 16 * 16; i++) {
    customImgRgba[i * 4] = 240; customImgRgba[i * 4 + 1] = 180; customImgRgba[i * 4 + 2] = 90;
    customImgRgba[i * 4 + 3] = i % 2 === 0 ? 255 : 0;
  }
  const imgMaps = rgbaPixelsToSurfaceMaps(customImgRgba, 16, 16, { name: "Test Alpha Image" });
  assert.equal(imgMaps.tipAlpha.length, 16 * 16 * 4);

  // Height-only paint layer test: Albedo MUST stay untouched (no black color bleed!)
  const chDoc = createPaintDocument(res);
  const baseOnly = compositePaintDocument(chDoc, cues);
  const hLayer = chDoc.layers[1];
  const hOnlyBrush = {
    tool: "paint",
    tip: "custom",
    customTipData: imgMaps,
    color: "#000000",
    roughness: 0.1,
    metalness: 1.0,
    height: 1.0,
    opacity: 1,
    flow: 1,
    hardness: 0.8,
    paintChannels: { albedo: false, roughness: false, metalness: false, height: true },
  };
  assert.equal(getToolChannels(hOnlyBrush).height, true);
  assert.equal(getToolChannels(hOnlyBrush).albedo, false);
  const hAcc = createStrokeAccumulator(res);
  applyBrushDabUV({ strokeAccum: hAcc, cues, u: 0.5, v: 0.5, pixelRadius: 12, brush: hOnlyBrush });
  assert.equal(commitStrokeAccumulator(hLayer, hAcc, hOnlyBrush, false), true);
  const afterHeight = compositePaintDocument(chDoc, cues);
  const cMid = (Math.floor(0.5 * res) * res + Math.floor(0.5 * res)) * 4;
  assert.equal(afterHeight.albedo[cMid], baseOnly.albedo[cMid], "Height-only brush must not darken base albedo");

  // Full color paint then selective height-only erase test
  const fullBrush = { tool: "paint", tip: "round", color: "#44aa88", roughness: 0.2, metalness: 0.1, height: 0.9, opacity: 1, flow: 1, paintChannels: { albedo: true, roughness: true, metalness: true, height: true } };
  const fAcc = createStrokeAccumulator(res);
  applyBrushDabUV({ strokeAccum: fAcc, cues, u: 0.25, v: 0.25, pixelRadius: 8, brush: fullBrush });
  commitStrokeAccumulator(hLayer, fAcc, fullBrush, false);
  const pCheck = (Math.floor(0.75 * res) * res + Math.floor(0.25 * res)) * 4;
  const albBeforeErase = hLayer.pixels.albedo[pCheck + 3];
  const eraseHeightOnly = { tool: "erase", tip: "round", opacity: 1, flow: 1, eraseChannels: { albedo: false, roughness: false, metalness: false, height: true } };
  const eAcc = createStrokeAccumulator(res);
  applyBrushDabUV({ strokeAccum: eAcc, cues, u: 0.25, v: 0.25, pixelRadius: 8, brush: eraseHeightOnly });
  commitStrokeAccumulator(hLayer, eAcc, eraseHeightOnly, false);
  assert.equal(hLayer.pixels.albedo[pCheck + 3], albBeforeErase, "Height-only erase must preserve albedo coverage");
  assert(hLayer.pixels.heightAlpha[Math.floor(0.75 * res) * res + Math.floor(0.25 * res)] < 10, "Height-only erase must erase height alpha");

  // Part Flood Fill
  assert.equal(applyFloodFill({ layer: paintLayer, cues, hitTriIndex: 0, scope: "part", brush }), true);
  geo.dispose();
});

await suite("5. Baking Engine: 17 Surface Maps + Signed 3D Volume SDF", async () => {
  assert(BAKE_MAP_CATALOG.length >= 18);
  const study = createBakeStudyPair("box");
  const allSurface = BAKE_MAP_CATALOG.filter((m) => !m.isVolume).map((m) => m.id);
  const baked = await bakeMeshMaps(study.low, study.high, {
    resolution: 128,
    channels: allSurface,
    samples: 6,
    padding: 3,
  });
  for (const ch of allSurface) {
    assert(baked.maps[ch]?.length === 128 * 128 * 4, `Baked buffer missing or wrong size for ${ch}`);
  }
  assert(baked.bridgeData?.ao?.length === 128 * 128);
  assert(baked.bridgeData?.curvature?.length === 128 * 128);

  const sdf = await bakeVolumeSdf(study.high, { nx: 16, ny: 16, nz: 16, signMode: "signed" });
  assert.equal(sdf.values.length, 16 * 16 * 16);
  assert(sdf.minVal < -0.05 && sdf.maxVal > 0.05, `Expected signed interior/exterior range, got ${sdf.minVal}..${sdf.maxVal}`);
});

const baseUrl = process.argv[2];
if (baseUrl) {
  await suite(`6. Live HTTP Endpoints (${baseUrl})`, async () => {
    const paths = [
      "/Experimental/",
      "/Experimental/TextureStudio/",
      "/Experimental/TextureStudio/TextureStudio.css",
      "/Experimental/TextureStudio/TextureStudio.js",
      "/Experimental/TextureStudio/AssetBrowser.js",
      "/Experimental/TextureStudio/StudioControls.js",
      "/Experimental/TextureStudio/Workspaces/MaterialWorkspace.js",
      "/Experimental/TextureStudio/Workspaces/LayerStackKit.js",
      "/Experimental/TextureStudio/Workspaces/ViewportHeaderKit.js",
      "/Experimental/TextureStudio/Workspaces/ArtworkStackWorkspace.js",
      "/Experimental/TextureStudio/Workspaces/PatternWorkspace.js",
      "/Experimental/TextureStudio/Workspaces/StampWorkspace.js",
      "/Experimental/TextureStudio/Workspaces/TexturePaintWorkspace.js",
      "/Experimental/TextureStudio/Workspaces/BakingWorkspace.js",
      "/Experimental/TextureStudio/Engine/NoiseField.js",
      "/Experimental/TextureStudio/Engine/WebGpuCompute.js",
      "/Experimental/TextureStudio/Engine/TexturePaintEngine.js",
      "/Experimental/TextureStudio/Engine/BakingEngine.js",
      "/Experimental/TextureStudio/Engine/Patterns/ArtworkStackEngine.js",
      "/Experimental/TextureStudio/Engine/Patterns/StarterCatalogBridge.js",
      "/Experimental/TextureStudio/Vendor/three.esm.min.js",
      "/Experimental/TextureStudio/Vendor/orbit-controls.esm.min.js",
      "/Experimental/TextureStudio/Vendor/addons/TeapotGeometry.js",
      "/Experimental/TextureStudio/Vendor/addons/RoundedBoxGeometry.js",
      "/Experimental/TextureStudio/Vendor/addons/OBJLoader.js",
      "/Experimental/TextureStudio/Vendor/addons/STLLoader.js",
      "/Experimental/TextureStudio/Assets/draped-cloth.js",
      "/Experimental/TextureStudio/Fonts/dm-sans-latin-400-normal.woff2",
    ];
    for (const p of paths) {
      const r = await fetch(baseUrl.replace(/\/$/, "") + p);
      assert.equal(r.status, 200, `Expected 200 for ${p}, got ${r.status}`);
    }
  });
}

await suite("7. Full TextureStudio Shell & All 5 Workspaces Mount & Switch Cleanly", async () => {
  function makeWebGLMock() {
    const gl = {
      VERSION: 0x1F02, SHADING_LANGUAGE_VERSION: 0x8B8C, VENDOR: 0x1F00, RENDERER: 0x1F01,
      SCISSOR_BOX: 0x0C10, VIEWPORT: 0x0BA2, MAX_TEXTURE_SIZE: 0x0D33, MAX_CUBE_MAP_TEXTURE_SIZE: 0x851C,
      MAX_COMBINED_TEXTURE_IMAGE_UNITS: 0x8B4D, MAX_VERTEX_TEXTURE_IMAGE_UNITS: 0x8B4C, MAX_TEXTURE_IMAGE_UNITS: 0x8872,
      MAX_VERTEX_ATTRIBS: 0x8869, MAX_VARYING_VECTORS: 0x8DFC, MAX_VERTEX_UNIFORM_VECTORS: 0x8DFB,
      MAX_FRAGMENT_UNIFORM_VECTORS: 0x8DFD, MAX_SAMPLES: 0x8D57,
      ACTIVE_UNIFORMS: 0x8B86, ACTIVE_ATTRIBUTES: 0x8B89, LINK_STATUS: 0x8B82, COMPILE_STATUS: 0x8B81,
      getExtension: () => ({}),
      getSupportedExtensions: () => ["EXT_color_buffer_float", "OES_texture_float_linear"],
      getParameter: (p) => {
        if (p === 0x1F02) return "WebGL 2.0 (OpenGL ES 3.0 Chromium)";
        if (p === 0x8B8C) return "WebGL GLSL ES 3.00";
        if (p === 0x1F00 || p === 0x1F01) return "MockGPU";
        if (p === 0x0C10 || p === 0x0BA2) return new Int32Array([0, 0, 640, 480]);
        if (p === 0x0D33 || p === 0x851C) return 4096;
        return 32;
      },
      getShaderPrecisionFormat: () => ({ precision: 23, rangeMin: 127, rangeMax: 127 }),
      getContextAttributes: () => ({ alpha: true, antialias: true }),
      getProgramParameter: (prog, p) => (p === 0x8B86 || p === 0x8B89) ? 0 : true,
      getShaderParameter: () => true,
      getShaderInfoLog: () => "",
      getProgramInfoLog: () => "",
      getActiveUniform: () => ({ name: "uMock", size: 1, type: 0x1406 }),
      getActiveAttrib: () => ({ name: "position", size: 1, type: 0x8B51 }),
      checkFramebufferStatus: () => 36053,
      isContextLost: () => false,
      getUniformLocation: () => ({}),
      getAttribLocation: () => 0,
      getUniformBlockIndex: () => 0,
      createTexture: () => ({}), createBuffer: () => ({}), createFramebuffer: () => ({}),
      createRenderbuffer: () => ({}), createVertexArray: () => ({}), createProgram: () => ({}),
      createShader: () => ({}),
    };
    return new Proxy(gl, {
      get(target, prop) {
        if (prop in target) return target[prop];
        if (typeof prop === "string" && prop === prop.toUpperCase()) return 1;
        return () => {};
      },
    });
  }

  function makeEl(tag = "div") {
    const listeners = new Map();
    const attrs = new Map();
    const el = {
      tagName: String(tag).toUpperCase(),
      className: "",
      style: { setProperty() {}, cssText: "", height: "" },
      classList: {
        toggle(c, f) {
          const set = new Set(String(el.className || "").split(/\s+/).filter(Boolean));
          const on = f !== undefined ? !!f : !set.has(c);
          if (on) set.add(c); else set.delete(c);
          el.className = [...set].join(" ");
        },
        add(...cs) { cs.forEach((c) => el.classList.toggle(c, true)); },
        remove(...cs) { cs.forEach((c) => el.classList.toggle(c, false)); },
        contains: (c) => String(el.className || "").split(/\s+/).includes(c),
      },
      dataset: {},
      children: [],
      parentNode: null,
      options: [{ text: "Default" }, { text: "Opt1" }],
      selectedIndex: 0,
      getRootNode: () => globalThis.document,
      get ownerDocument() { return globalThis.document; },
      get textContent() {
        return (el._text || "") + (el.children || []).map((c) => (typeof c === "string" ? c : c.textContent || "")).join("");
      },
      set textContent(v) {
        el._text = v == null ? "" : String(v);
        el.children = [];
      },
      append(...args) {
        for (const a of args) {
          if (typeof a === "string") el._text = (el._text || "") + a;
          else if (a && typeof a === "object") { a.parentNode = el; el.children.push(a); }
        }
      },
      prepend(...args) {
        for (let i = args.length - 1; i >= 0; i--) {
          const a = args[i];
          if (typeof a === "string") el._text = a + (el._text || "");
          else if (a && typeof a === "object") { a.parentNode = el; el.children.unshift(a); }
        }
      },
      remove() {
        if (el.parentNode?.children) {
          el.parentNode.children = el.parentNode.children.filter((c) => c !== el);
          el.parentNode = null;
        }
      },
      contains(target) {
        if (target === el) return true;
        return el.children.some((c) => typeof c.contains === "function" && c.contains(target));
      },
      closest(sel) {
        let cur = el;
        while (cur) {
          if (sel.startsWith(".") && cur.classList?.contains(sel.slice(1))) return cur;
          if (sel.startsWith("[")) {
            const attrName = sel.slice(1, -1).split("=")[0];
            const camel = attrName.replace(/^data-/, "").replace(/-([a-z])/g, (_, c) => c.toUpperCase());
            if (cur.hasAttribute?.(attrName) || cur.dataset?.[camel] !== undefined) return cur;
          }
          cur = cur.parentNode;
        }
        return null;
      },
      querySelector(sel) { return el.querySelectorAll(sel)[0] || null; },
      querySelectorAll(sel) {
        const parts = String(sel || "").trim().split(/\s+/).filter(Boolean);
        if (parts.length > 1) {
          let pool = [el];
          for (const p of parts) {
            const next = [];
            for (const root of pool) {
              for (const hit of root.querySelectorAll(p)) if (!next.includes(hit)) next.push(hit);
            }
            pool = next;
          }
          return pool;
        }
        const one = parts[0] || "";
        const out = [];
        const walk = (node) => {
          for (const ch of node.children || []) {
            const cls = String(ch.className || "");
            const match = one.startsWith(".")
              ? cls.split(/\s+/).includes(one.slice(1))
              : one.startsWith("[")
                ? ch.hasAttribute?.(one.slice(1, -1).split("=")[0])
                : String(ch.tagName).toLowerCase() === one.toLowerCase();
            if (match) out.push(ch);
            walk(ch);
          }
        };
        walk(el);
        return out;
      },
      setAttribute(k, v) {
        attrs.set(k, String(v));
        if (k === "class") el.className = String(v);
        if (k.startsWith("data-")) {
          const camel = k.slice(5).replace(/-([a-z])/g, (_, c) => c.toUpperCase());
          el.dataset[camel] = String(v);
        }
      },
      getAttribute(k) { return attrs.get(k) ?? null; },
      hasAttribute(k) { return attrs.has(k); },
      removeAttribute(k) { attrs.delete(k); },
      addEventListener(type, fn) {
        if (!listeners.has(type)) listeners.set(type, []);
        listeners.get(type).push(fn);
      },
      removeEventListener(type, fn) {
        if (!listeners.has(type)) return;
        listeners.set(type, listeners.get(type).filter((f) => f !== fn));
      },
      dispatchEvent(evt) {
        const ev = Object.assign({ target: el, preventDefault() {}, stopPropagation() {}, clientX: 200, clientY: 200 }, evt);
        for (const fn of [...(listeners.get(ev.type) || [])]) fn(ev);
        return true;
      },
      click() { el.dispatchEvent({ type: "click" }); },
      setPointerCapture() {}, releasePointerCapture() {},
      toDataURL: () => "data:image/png;base64,MOCK",
      getContext: (kind) => (kind && kind.includes("webgl")) ? makeWebGLMock() : {
        fillStyle: "", strokeStyle: "", lineWidth: 1, globalAlpha: 1, globalCompositeOperation: "source-over",
        font: "", textAlign: "center", textBaseline: "middle",
        fillRect() {}, clearRect() {}, beginPath() {}, moveTo() {}, lineTo() {}, closePath() {}, stroke() {},
        fill() {}, ellipse() {}, arc() {}, rect() {}, roundRect() {}, quadraticCurveTo() {}, bezierCurveTo() {},
        drawImage() {}, save() {}, restore() {}, scale() {}, translate() {}, rotate() {}, fillText() {}, strokeText() {},
        setLineDash() {},
        createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4), width: w, height: h }),
        getImageData: (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4), width: w, height: h }),
        putImageData() {},
        createLinearGradient: () => ({ addColorStop() {} }),
        createRadialGradient: () => ({ addColorStop() {} }),
      },
      getBoundingClientRect: () => ({
        left: 0, top: 0, width: 640, height: parseFloat(el.style.height) || 480,
      }),
      clientWidth: 640, clientHeight: 480, scrollTop: 0, scrollHeight: 480,
      set innerHTML(v) { el._html = v; if (v === "") el.children = []; },
      get innerHTML() { return el._html || ""; },
    };
    return el;
  }

  const mainEl = makeEl("main");
  const browserHostEl = makeEl("div");
  const winListeners = new Map();
  globalThis.window = {
    location: { hash: "#material" },
    innerWidth: 1440,
    innerHeight: 900,
    devicePixelRatio: 1,
    addEventListener(t, fn) { if (!winListeners.has(t)) winListeners.set(t, []); winListeners.get(t).push(fn); },
    removeEventListener(t, fn) { if (winListeners.has(t)) winListeners.set(t, winListeners.get(t).filter((f) => f !== fn)); },
    dispatchEvent(evt) { for (const fn of [...(winListeners.get(evt.type) || [])]) fn(Object.assign({ preventDefault() {}, stopPropagation() {} }, evt)); },
  };
  globalThis.document = {
    getElementById: (id) => id === "studio-main" ? mainEl : id === "asset-browser-host" ? browserHostEl : makeEl("div"),
    querySelector: (sel) => globalThis.document.body.querySelector(sel) || mainEl.querySelector(sel) || browserHostEl.querySelector(sel),
    querySelectorAll: (sel) => [...mainEl.querySelectorAll(sel), ...browserHostEl.querySelectorAll(sel), ...globalThis.document.body.querySelectorAll(sel)],
    createElement: (tag) => makeEl(tag),
    createElementNS: (ns, tag) => makeEl(tag),
    addEventListener() {}, removeEventListener() {},
    body: makeEl("body"),
    activeElement: null,
  };
  globalThis.history = { replaceState() {} };
  globalThis.ResizeObserver = class { observe() {} disconnect() {} };
  globalThis.requestAnimationFrame = () => 1;
  globalThis.cancelAnimationFrame = () => {};
  globalThis.ImageData = class { constructor(d, w, h) { this.data = d; this.width = w; this.height = h; } };

  await import("./TextureStudio.js");
  for (const ws of ["material", "pattern", "stamp", "paint", "bake"]) {
    globalThis.window.__frontierTextureStudio.switchWorkspace(ws);
    const snap = globalThis.window.__frontierTextureStudio.getSnapshot();
    assert(snap && typeof snap === "object", `Workspace ${ws} did not mount cleanly`);
  }
});

// ----------------------------------------------------------------- 8. GPU BAKE / GPU PAINT / TOOL POPUPS / DRAG-DROP STACKS
await suite("8. GPU Bake/Paint Shaders, Q/Tab/Right-Click Tool Popups & Draggable/Up-Down Layer Stacks", async () => {
  const gpuBake = await import("./Engine/GpuBakeEngine.js");
  const gpuPaintMod = await import("./Engine/GpuPaintEngine.js");
  const bakeEng = await import("./Engine/BakingEngine.js");

  assert(gpuBake.BAKE_GLSL3_SHADERS.CORE_RAY_FRAG.includes("uGridMetaTex"), "Missing GLSL3 ray bake shader");
  assert(gpuBake.BAKE_GLSL3_SHADERS.DERIVE_CHANNEL_FRAG.includes("calcCurvature"), "Missing GLSL3 derive channel shader");
  assert(gpuBake.BAKE_WGSL_SHADERS.MESH_RAY_WGSL.includes("@compute"), "Missing WebGPU compute ray bake shader");
  assert(gpuBake.BAKE_WGSL_SHADERS.SDF_WGSL.includes("@compute"), "Missing WebGPU compute SDF shader");
  assert(gpuPaintMod.PAINT_GLSL3_SHADERS.DAB_COMMON.includes("tipCoverage("), "Missing GPU Paint dab shader");
  assert(gpuPaintMod.PAINT_GLSL3_SHADERS.COMPOSITE_LAYER_FRAG.includes("blendRgb("), "Missing GPU Paint composite shader");

  const studyPair = bakeEng.createBakeStudyPair("teapot");
  const packed = gpuBake.packBakeGeometryForGpu(studyPair.low, studyPair.high);
  assert(packed.triCount > 100 && packed.gridMeta.length === 16 * 16 * 16 * 2, "GPU geometry spatial grid packing failed");
  const gbuf = gpuBake.rasterizeLowMeshGBuffer(studyPair.low, 64);
  assert(gbuf.posCov.length === 64 * 64 * 4 && gbuf.normPart.length === 64 * 64 * 4, "Low-poly G-buffer rasterization failed");

  // Exercise GpuPaintSession WebGL2 mesh-cue upload, stamp upload & pattern fill upload against real buildDefaultMeshCues output!
  const mockGpuRenderer = {
    capabilities: { isWebGL2: true },
    extensions: { get: () => ({}) },
    getContext: () => ({ getParameter: () => "ValidatedWebGL2" }),
    autoClear: true,
    getRenderTarget: () => null,
    getScissorTest: () => false,
    setScissorTest() {},
    setRenderTarget() {},
    render() {},
    getClearColor: (c) => c.set(0, 0, 0),
    getClearAlpha: () => 1,
    setClearColor() {},
    clear() {},
  };
  const gpuSess = new gpuPaintMod.GpuPaintSession(mockGpuRenderer, 64);
  assert.equal(gpuSess.isActive, true, "Mock WebGL2 GpuPaintSession should activate");
  const testGeo = buildPaintTargetGeometry("teapot");
  const testIdx = buildMeshPaintIndex(testGeo);
  const testCues = buildDefaultMeshCues(testGeo, testIdx, 64);
  gpuSess.uploadMeshCues(testCues, null);
  assert(gpuSess.posPartTex && gpuSess.auxCueTex, "uploadMeshCues must upload worldPos/worldNormal without throwing");
  assert(gpuSess.auxCueTex.image.data.some((v) => v > 100), "auxCueTex AO/Curvature bytes should be normalized 0..255");

  // Upload baked Float32 (CPU bake) and Uint8 (GPU bake) bridgeData
  gpuSess.uploadMeshCues(testCues, { res: 64, ao: new Float32Array(64 * 64).fill(0.75), curvature: new Float32Array(64 * 64).fill(0.6) });
  gpuSess.uploadMeshCues(testCues, { res: 64, ao: new Uint8ClampedArray(64 * 64).fill(180), curvature: new Uint8ClampedArray(64 * 64).fill(140) });

  // Verify Stamp & Pattern Fill texture uploads work with ArtworkStackEngine .albedo buffers
  const stampEval = evaluateArtworkDocument(createArtworkDocument("stamp"), 64);
  gpuSess.ensureStampTexture(stampEval);
  assert.equal(gpuSess.hasStamp, true, "ensureStampTexture must accept ArtworkStackEngine .albedo buffer");

  const patEval = evaluateArtworkDocument(createArtworkDocument("pattern"), 64);
  const patLayer = createPaintLayer("fill", { res: 64, fill: { color: "#ffffff", roughness: 0.4, metalness: 0.1, height: 0, patternRepeats: 2, patternData: patEval } });
  const patGpu = gpuSess.ensureLayerTargets(patLayer);
  assert(patGpu.patternTex, "ensureLayerTargets must upload Pattern Fill .albedo buffer");

  // Verify GPU Brush Curves, Roundness, Custom Tip & Selective PBR Channel Commit
  const gpuPaintLyr = createPaintLayer("paint", { res: 64 });
  gpuSess.beginStroke();
  gpuSess.dabStroke({
    mode: "2d",
    uv: { x: 0.5, y: 0.5 },
    uvRadius: 0.15,
    brush: {
      tool: "paint",
      tip: "custom",
      customTipData: stampEval,
      color: "#d48842",
      roundness: 0.65,
      angle: 22,
      curves: { falloff: CURVE_PRESETS.bell },
      paintChannels: { albedo: true, roughness: false, metalness: false, height: true },
    },
  });
  assert.equal(gpuSess.commitStrokeToLayer(gpuPaintLyr, { tool: "paint", paintChannels: { albedo: true, roughness: false, metalness: false, height: true }, opacity: 1 }, false), true);
  gpuSess.dispose();
  testGeo.dispose();

  // Switch back to Texture Paint & exercise Q/Tab/Right-click popup + Stamp Previews + Curves + PBR Channels + layer stack drag/up/down
  globalThis.window.__frontierTextureStudio.switchWorkspace("paint");
  assert.equal(globalThis.document.querySelectorAll(".pbr-chan-pill").length, 4, "Layer Inspector must display strictly the selected layer's 4 active PBR channel pills");
  assert.equal(globalThis.document.querySelectorAll(".curve-editor-svg").length, 0, "Right-side Layer Inspector must NOT be cluttered with brush curves");
  const paintRowsBefore = globalThis.document.querySelectorAll(".stack-row");
  assert(paintRowsBefore.length >= 2, "Expected paint workspace stack rows");
  assert(paintRowsBefore.every((r) => r.draggable === true), "All paint stack rows must be draggable");

  const firstTitleBefore = paintRowsBefore[0].querySelector(".stack-row-title")?.textContent;
  const secondTitleBefore = paintRowsBefore[1].querySelector(".stack-row-title")?.textContent;
  const orderBtns = paintRowsBefore[0].querySelectorAll(".stack-order-btn");
  assert(orderBtns.length === 2, "Expected Up/Down reorder buttons on each paint layer row");
  orderBtns[1].click(); // Move top visual layer down 1 slot
  const paintRowsAfterBtn = globalThis.document.querySelectorAll(".stack-row");
  assert(
    paintRowsAfterBtn[0].querySelector(".stack-row-title")?.textContent === secondTitleBefore &&
    paintRowsAfterBtn[1].querySelector(".stack-row-title")?.textContent === firstTitleBefore,
    "Up/Down button did not reorder paint layer stack",
  );

  // Test Drag-and-Drop reordering back
  const dragSrc = paintRowsAfterBtn[1];
  const dragTgt = paintRowsAfterBtn[0];
  dragSrc.dispatchEvent({ type: "dragstart", dataTransfer: { effectAllowed: "", setData() {} } });
  dragTgt.dispatchEvent({ type: "dragover", clientY: 5 }); // top half -> drop-above
  dragTgt.dispatchEvent({ type: "drop", clientY: 5, dataTransfer: { getData: () => dragSrc.dataset.layerId } });
  const paintRowsAfterDrag = globalThis.document.querySelectorAll(".stack-row");
  assert(
    paintRowsAfterDrag[0].querySelector(".stack-row-title")?.textContent === firstTitleBefore,
    "Drag-and-drop did not reorder paint layer stack back",
  );

  // Test Q hotkey & Right-click popup menu
  globalThis.window.dispatchEvent({ type: "keydown", key: "q" });
  let popups = globalThis.document.body.querySelectorAll(".tool-popup-menu");
  assert(popups.length === 1, "Pressing Q did not open Active Tool Popup menu");
  globalThis.window.dispatchEvent({ type: "keydown", key: "Escape" });
  popups = globalThis.document.body.querySelectorAll(".tool-popup-menu");
  assert(popups.length === 0, "Escape did not close Active Tool Popup menu");

  globalThis.window.dispatchEvent({ type: "keydown", key: "Tab" });
  popups = globalThis.document.body.querySelectorAll(".tool-popup-menu");
  assert(popups.length === 1, "Pressing Tab did not open Active Tool Popup menu");
  globalThis.window.dispatchEvent({ type: "keydown", key: "Tab" });
  assert(globalThis.document.body.querySelectorAll(".tool-popup-menu").length === 0, "Pressing Tab again did not toggle popup closed");

  const splitPanes = globalThis.document.querySelectorAll(".viewport-split-pane");
  assert(splitPanes.length >= 1, "Missing paint viewport split panes");
  splitPanes[0].dispatchEvent({ type: "pointerdown", button: 2, clientX: 220, clientY: 180 });
  splitPanes[0].dispatchEvent({ type: "contextmenu", clientX: 221, clientY: 181 });
  let rightPop = globalThis.document.body.querySelectorAll(".tool-popup-menu")[0];
  assert(rightPop, "Right-click on paint viewport did not open Active Tool Popup");
  assert(rightPop.querySelectorAll(".pbr-chan-pill").length === 4, "Right-click Brush popup must display active PBR channel pills");

  // Verify PBR Pill Removal -> Moves Back Into Filterable Dropdown -> Re-Add Returns as Pill!
  const popupPillsBefore = rightPop.querySelectorAll(".pbr-chan-pill");
  const roughPill = popupPillsBefore.find((p) => p.dataset.channel === "roughness");
  assert(roughPill, "Expected Roughness active pill");
  roughPill.click(); // Remove Roughness pill!
  assert.equal(rightPop.querySelectorAll(".pbr-chan-pill").length, 3, "Removing PBR pill must decrease active pills");
  const addTrigger = rightPop.querySelector(".pbr-add-trigger");
  assert(addTrigger, "Missing + Channel dropdown trigger in popup");
  addTrigger.click(); // Open dropdown!
  const chanMenuItems = rightPop.querySelectorAll(".pbr-chan-menu .menu-item");
  assert(chanMenuItems.length >= 16, `Expected >=16 available channels (removed Roughness + 15 extended PBR channels), got ${chanMenuItems.length}`);
  assert(chanMenuItems.some((m) => m.dataset.channel === "roughness"), "Removed channel (Roughness) must move back into Add Channel dropdown");
  assert(chanMenuItems.some((m) => m.dataset.channel === "clearcoat") && chanMenuItems.some((m) => m.dataset.channel === "emissive"), "Add Channel dropdown must offer extended PBR channels (clearcoat, emissive, etc.)");
  // Filter search in dropdown
  const chanSearch = rightPop.querySelector(".pbr-chan-menu .pill-menu-search-input");
  chanSearch.value = "rough";
  chanSearch.dispatchEvent({ type: "input" });
  const filteredRoughItems = rightPop.querySelectorAll(".pbr-chan-menu .menu-item");
  assert(filteredRoughItems.some((m) => m.dataset.channel === "roughness"), "Dropdown filter should retain Roughness");
  filteredRoughItems.find((m) => m.dataset.channel === "roughness").click(); // Re-add!
  assert.equal(rightPop.querySelectorAll(".pbr-chan-pill").length, 4, "Re-adding from dropdown must restore pill");

  const subTabs = rightPop.querySelectorAll(".tool-popup-subtab");
  assert.equal(subTabs.length, 3, "Right-click Brush popup must expose Tip & PBR / Colour Dynamics / Curves subtabs");
  subTabs[1].click(); // Colour Dynamics subtab
  rightPop = globalThis.document.body.querySelectorAll(".tool-popup-menu")[0];
  const cdDropdowns = rightPop.querySelectorAll(".pill-dropdown");
  assert(cdDropdowns.length >= 2, "Colour Dynamics subtab must render filterable Colour Mode & Opacity dropdown chips");
  const cdTrigger = cdDropdowns[0].querySelector(".pill-trigger");
  cdTrigger.click();
  const cdModes = cdDropdowns[0].querySelectorAll(".menu-item");
  assert(cdModes.some((m) => m.dataset.optionId === "stroke-gradient") && cdModes.some((m) => m.dataset.optionId === "multicolour"), "Colour Dynamics dropdown must offer Stroke Gradient & Multicolour");
  cdModes.find((m) => m.dataset.optionId === "stroke-gradient").click();
  rightPop = globalThis.document.body.querySelectorAll(".tool-popup-menu")[0];
  assert(rightPop.querySelectorAll(".colour-stop-pill").length >= 2, "Selecting Stroke Gradient must expose editable multi-colour gradient stop pills");

  rightPop.querySelectorAll(".tool-popup-subtab")[2].click(); // Curves subtab
  rightPop = globalThis.document.body.querySelectorAll(".tool-popup-menu")[0];
  assert(rightPop.querySelectorAll(".curve-editor-svg").length === 1, "Curves subtab in right-click popup must render SVG curve editor");

  // Switch to Stamp tab inside right-click popup and verify visual Stamp Previews!
  const popupToolTabs = rightPop.querySelectorAll(".tool-popup-tab");
  const stampPopupTab = popupToolTabs[3]; // Stamp
  assert(stampPopupTab, "Missing Stamp tab inside right-click popup");
  stampPopupTab.click();
  rightPop = globalThis.document.body.querySelectorAll(".tool-popup-menu")[0];
  const popupStampThumbs = rightPop.querySelectorAll(".stamp-preview-thumb");
  assert(popupStampThumbs.length >= 5, `Right-click Stamp tool must display stamp thumbnail previews, got ${popupStampThumbs.length}`);
  assert(popupStampThumbs[0]._html?.includes("<svg") || popupStampThumbs[0].children?.length >= 1, "Stamp preview thumbnail in right-click popup must render artwork SVG or image");
  globalThis.window.dispatchEvent({ type: "keydown", key: "Escape" });

  // Verify Pattern Studio layer drag-drop & popup as well
  globalThis.window.__frontierTextureStudio.switchWorkspace("pattern");
  const artRows = globalThis.document.querySelectorAll(".stack-row");
  assert(artRows.length >= 2 && artRows.every((r) => r.draggable === true), "Pattern Studio stack rows must be draggable");
  globalThis.window.dispatchEvent({ type: "keydown", key: "q" });
  assert(globalThis.document.body.querySelectorAll(".tool-popup-menu").length === 1, "Q did not open Pattern Studio Quick Menu");
  globalThis.window.dispatchEvent({ type: "keydown", key: "Escape" });

  // Verify Baking Studio Q popup as well
  globalThis.window.__frontierTextureStudio.switchWorkspace("bake");
  globalThis.window.dispatchEvent({ type: "keydown", key: "q" });
  assert(globalThis.document.body.querySelectorAll(".tool-popup-menu").length === 1, "Q did not open Baking Studio Quick Menu");
  globalThis.window.dispatchEvent({ type: "keydown", key: "Escape" });
});

// ----------------------------------------------------------------- 9. AFRICAN PATTERNS & SVG VECTOR EXPORT
await suite("9. African Patterns Catalog (36+ Studies), SVG Paths & Vector Rendering", async () => {
  const patCatalog = listPatternStarterCatalog();
  const african = patCatalog.filter((p) => p.group === "African Patterns");
  assert(african.length >= 34, `Expected >=34 African Patterns, got ${african.length}`);
  const keyNames = ["Asante Kente Blocks", "Ndebele Painted Chevrons", "Bamana Bogolanfini Dashes", "Kuba Raffia Steps", "Yoruba Adire Rings", "African Diamond Carpet"];
  for (const kn of keyNames) {
    assert(african.some((a) => a.name === kn), `Missing African Pattern: ${kn}`);
  }
  for (const entry of african) {
    const doc = loadStarterAsArtworkDocument(entry);
    assert(doc.layers.length >= 1, `Empty layers in ${entry.name}`);
    const svg = artworkDocumentToSvgString(doc, { width: 512, height: 512 });
    assert(svg.includes("<svg") && (svg.includes("<path") || svg.includes("<polygon") || svg.includes("<rect") || svg.includes("<ellipse")), `Invalid SVG output for ${entry.name}`);
    const thumb = getArtworkPreviewSvg(entry, "pattern", 60);
    assert(thumb.includes("<svg"), `Missing preview SVG for ${entry.name}`);
  }
});

// ----------------------------------------------------------------- 10. INTERACTIVE SVG CANVAS + ASSET BROWSER DRAWER + AUTO-TAB + CANVAS DRAG-DROP
await suite("10. Interactive SVG Vector Canvas, Bottom Resizable Asset Browser & Auto-Category Drop", async () => {
  const api = globalThis.window.__frontierTextureStudio;

  // 10a. Verify decluttered Layer Stacks (preset grids moved out of workspace left columns into Asset Browser)
  for (const ws of ["material", "pattern", "stamp"]) {
    api.switchWorkspace(ws);
    const leftCards = globalThis.document.querySelectorAll(".preset-card");
    assert.equal(leftCards.length, 0, `Workspace ${ws} should keep asset cards in bottom Asset Browser, not left stack`);
  }

  // 10b. Verify Workspace -> Asset Browser auto-category switching unless user specifies otherwise
  api.switchWorkspace("material");
  assert.equal(api.getAssetBrowser().activeCategory, "materials");
  api.switchWorkspace("pattern");
  assert.equal(api.getAssetBrowser().activeCategory, "patterns");
  api.switchWorkspace("stamp");
  assert.equal(api.getAssetBrowser().activeCategory, "stamps");
  api.switchWorkspace("bake");
  assert.equal(api.getAssetBrowser().activeCategory, "meshes");

  // User manually selects Patterns tab while on Bake -> should stay pinned when switching workspaces!
  const patCatBtn = globalThis.document.querySelectorAll(".cb-cat-tab").find((b) => b.dataset.category === "patterns");
  assert(patCatBtn, "Missing Patterns tab in Asset Browser");
  patCatBtn.click();
  assert.equal(api.getAssetBrowser().userSpecifiedCategory, true);
  api.switchWorkspace("material");
  assert.equal(api.getAssetBrowser().activeCategory, "patterns", "User-specified category should remain pinned across workspace switches");

  // Clicking Auto Reset button restores automatic category matching
  const autoResetBtn = globalThis.document.querySelector(".cb-auto-pill");
  assert(autoResetBtn, "Missing Auto/Pinned toggle pill");
  autoResetBtn.click();
  assert.equal(api.getAssetBrowser().userSpecifiedCategory, false);
  assert.equal(api.getAssetBrowser().activeCategory, "materials");

  // 10c. Verify draggable notch resize + collapse/expand
  const notch = globalThis.document.querySelector(".cb-resize");
  assert(notch, "Missing Asset Browser resize notch (.cb-resize)");
  const initialH = api.getAssetBrowser().heightPx;
  notch.dispatchEvent({ type: "pointerdown", button: 0, clientY: 600 });
  globalThis.window.dispatchEvent({ type: "pointermove", clientY: 480 }); // drag UP +120px
  globalThis.window.dispatchEvent({ type: "pointerup" });
  assert(api.getAssetBrowser().heightPx > initialH + 60, `Expected drawer height increase after dragging notch up, got ${api.getAssetBrowser().heightPx}`);
  notch.dispatchEvent({ type: "dblclick" });
  assert.equal(api.getAssetBrowser().collapsed, true, "Double-click on notch should collapse Asset Browser");
  notch.dispatchEvent({ type: "dblclick" });
  assert.equal(api.getAssetBrowser().collapsed, false, "Second double-click should expand Asset Browser");

  // 10d. Interactive SVG Vector Canvas + Toolbar + Direct Move / Resize / Rotate + Draw Tool in Pattern & Stamp Studio
  api.switchWorkspace("pattern");
  const patSnap = api.getSnapshot();
  assert.equal(patSnap.viewChannel, "vector", "Pattern Studio should open in Interactive Vector SVG mode");
  assert.equal(patSnap.showGrid, true, "Vector grid should be enabled by default");
  const svgCanvas = globalThis.document.querySelector(".se-canvas");
  assert(svgCanvas && svgCanvas.innerHTML.includes('id="se-grid"'), "SVG canvas missing grid defs");
  assert(svgCanvas.innerHTML.includes('data-handle="rotate"'), "SVG canvas missing rotation handle");
  assert(svgCanvas.innerHTML.includes('data-handle="se"'), "SVG canvas missing SE resize handle");

  // Drop an African Pattern directly onto the SVG viewport!
  const seViewport = globalThis.document.querySelector(".se-viewport");
  const kentePayload = JSON.stringify({ assetKind: "pattern", id: "legacy:Asante Kente Blocks", name: "Asante Kente Blocks" });
  seViewport.dispatchEvent({
    type: "drop",
    clientX: 256,
    clientY: 256,
    dataTransfer: { getData: (t) => (t === "application/x-frontier-asset" ? kentePayload : "") },
  });
  assert.equal(api.getSnapshot().name, "Asante Kente Blocks", "Dropping Asante Kente Blocks onto Pattern canvas should load it");
  assert(globalThis.document.querySelector(".se-canvas").innerHTML.includes("<path"), "African Kente SVG paths should render directly in .se-canvas");

  // Draw a brand-new Diamond vector shape right on the SVG canvas using the Diamond toolbar tool!
  const diamondBtn = globalThis.document.querySelectorAll(".se-tool-btn").find((b) => b.dataset.vectorTool === "diamond");
  assert(diamondBtn, "Missing Diamond vector tool button in toolbar");
  diamondBtn.click();
  const layersBeforeDraw = api.getSnapshot().layers;
  svgCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 160, clientY: 160 });
  svgCanvas.dispatchEvent({ type: "pointermove", clientX: 280, clientY: 280 });
  svgCanvas.dispatchEvent({ type: "pointerup", clientX: 280, clientY: 280 });
  assert.equal(api.getSnapshot().layers, layersBeforeDraw + 1, "Drawing Diamond shape on SVG canvas should create a new layer");

  // Directly move, resize & rotate that selected layer on the SVG canvas!
  const beforeMove = api.getSnapshot().selectedLayer;
  const layerGroupMock = globalThis.document.createElementNS("http://www.w3.org/2000/svg", "g");
  layerGroupMock.dataset = { layerId: beforeMove.id };
  svgCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 200, clientY: 200, target: layerGroupMock });
  svgCanvas.dispatchEvent({ type: "pointermove", clientX: 264, clientY: 232 });
  svgCanvas.dispatchEvent({ type: "pointerup", clientX: 264, clientY: 232 });
  const afterMove = api.getSnapshot().selectedLayer;
  assert(afterMove.x !== beforeMove.x || afterMove.y !== beforeMove.y, "Dragging SVG layer should update its (x, y) coordinates");

  const resizeHandleMock = globalThis.document.createElementNS("http://www.w3.org/2000/svg", "rect");
  resizeHandleMock.dataset = { handle: "se" };
  svgCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 280, clientY: 280, target: resizeHandleMock });
  svgCanvas.dispatchEvent({ type: "pointermove", clientX: 340, clientY: 340 });
  svgCanvas.dispatchEvent({ type: "pointerup", clientX: 340, clientY: 340 });
  assert(api.getSnapshot().selectedLayer.width !== afterMove.width, "Dragging SE handle should resize active layer width");

  const rotHandleMock = globalThis.document.createElementNS("http://www.w3.org/2000/svg", "circle");
  rotHandleMock.dataset = { handle: "rotate" };
  svgCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 300, clientY: 100, target: rotHandleMock });
  svgCanvas.dispatchEvent({ type: "pointermove", clientX: 420, clientY: 240 });
  svgCanvas.dispatchEvent({ type: "pointerup", clientX: 420, clientY: 240 });
  assert(api.getSnapshot().selectedLayer.rotation !== beforeMove.rotation, "Dragging rotate handle should rotate active layer");

  // 10e. Verify Stamp Studio interactive SVG canvas & Material/Bake Asset Drops
  api.switchWorkspace("stamp");
  assert.equal(api.getSnapshot().viewChannel, "vector", "Stamp Studio should also use interactive Vector SVG canvas");
  api.switchWorkspace("material");
  const matStage = globalThis.document.querySelector(".viewport-stage");
  const matDropPayload = JSON.stringify({ assetKind: "material", id: materials[2].id, name: materials[2].name });
  matStage.dispatchEvent({
    type: "drop",
    dataTransfer: { getData: (t) => (t === "application/x-frontier-asset" ? matDropPayload : "") },
  });
  assert.equal(api.getSnapshot().id, materials[2].id, "Dropping Material onto Material 3D stage should apply it");

  api.switchWorkspace("paint");
  const paintPane = globalThis.document.querySelector(".viewport-split-pane");
  assert(paintPane, "Missing paint pane for asset drop");
  const paintLayersBefore = api.getSnapshot().layers;
  paintPane.dispatchEvent({
    type: "drop",
    dataTransfer: { getData: (t) => (t === "application/x-frontier-asset" ? kentePayload : "") },
  });
  assert.equal(api.getSnapshot().layers, paintLayersBefore + 1, "Dropping African Pattern onto Paint viewport should add Pattern Fill layer");

  api.switchWorkspace("bake");
  const bakeStage = globalThis.document.querySelector(".viewport-stage");
  const meshDropPayload = JSON.stringify({ assetKind: "mesh", id: "torus", name: "Torus Knot Study" });
  bakeStage.dispatchEvent({
    type: "drop",
    dataTransfer: { getData: (t) => (t === "application/x-frontier-asset" ? meshDropPayload : "") },
  });
  assert.equal(api.getSnapshot().studyId, "torus", "Dropping Bake Study Mesh onto Baking viewport should load it");
});

// ----------------------------------------------------------------- 11. MATERIAL LAYER STACK INSPECTOR, TILEABLE PATTERNS ON MATERIALS & PAINT LAYER INSPECTORS
await suite("11. Layer Stack Inspectors & Tileable Patterns Applied to Materials", async () => {
  const { RasterPatternChannel } = await import("./Engine/Patterns/PatternSurfaceChannels.js");
  const { buildMaterialPatternDocument } = await import("./Engine/Patterns/StarterCatalogBridge.js");
  const { EnumerateMaterialFields, RasterChannelPreview } = await import("./Engine/Materials/FieldProjection.js");
  const { DeclareMaterialAuthoring } = await import("./Engine/Materials/MaterialSpecification.js");

  const api = globalThis.window.__frontierTextureStudio;
  assert(api, "TextureStudio API not found on window");

  // 11a. Material Layer Stack + Per-Layer Dedicated Inspector
  api.switchWorkspace("material");
  const snap0 = api.getSnapshot();
  assert(snap0.layerCount >= 1, `Expected Material strata layers >= 1, got ${snap0.layerCount}`);

  const stackChips = [...globalThis.document.querySelectorAll(".chip-btn")];
  const btnDetail = stackChips.find((b) => b.textContent.includes("+ Detail Layer"));
  assert(btnDetail, "Missing '+ Detail Layer' button in Material layer stack");
  btnDetail.click();

  const snap1 = api.getSnapshot();
  assert.equal(snap1.layerCount, snap0.layerCount + 1, "'+ Detail Layer' should add a strata layer to Authoring.Layers");

  // Verify the Dedicated Layer Inspector card reflects the newly selected Detail layer
  const titlesAfterDetail = [...globalThis.document.querySelectorAll(".section-title")].map((el) => el.textContent);
  assert(
    titlesAfterDetail.some((t) => t.startsWith("Layer Inspector: Detail Strata")),
    `Expected Layer Inspector heading for Detail Strata, got ${titlesAfterDetail.join(", ")}`,
  );

  // Verify Layer PBR Channels pills + Filterable Dropdown for adding removed channels back
  const layerChPills = [...globalThis.document.querySelectorAll(".pbr-chan-pill")];
  assert(layerChPills.some((p) => p.dataset.channel === "colour"), "Material layer inspector should show Colour pill");
  const metallicPill = layerChPills.find((p) => p.dataset.channel === "metallic");
  if (!metallicPill) {
    // Open Material layer's + Channel dropdown & add Metallic!
    const trigger = globalThis.document.querySelector(".pbr-add-trigger");
    assert(trigger, "Missing PBR + Channel dropdown trigger in Material Layer Inspector");
    trigger.click();
    const metallicOpt = [...globalThis.document.querySelectorAll(".pbr-chan-menu .menu-item")].find((m) => m.dataset.channel === "metallic");
    assert(metallicOpt, "Metallic should be available to add inside Material layer Channel dropdown");
    metallicOpt.click();
    assert(
      [...globalThis.document.querySelectorAll(".pbr-chan-pill")].some((p) => p.dataset.channel === "metallic"),
      "Clicking Metallic in dropdown should add it as an active pill on the Material layer",
    );
  }

  // Test Authoring Field Projection + RasterPatternChannel cleanly in pure JS (no missing .jsx import!)
  const authMat = normalizeMaterial(materials[0]);
  authMat.Authoring = DeclareMaterialAuthoring(authMat);
  const surfLayer = authMat.Authoring.Layers.find((l) => l.Model === "surface");
  assert(surfLayer, "Missing surface layer in DeclareMaterialAuthoring");
  const patSurfaceDoc = buildMaterialPatternDocument(
    listPatternStarterCatalog().find((e) => e.id.startsWith("noise-")),
    { repeats: 4, rotation: 30, mapping: "object", tileAxes: "xy" },
  );
  assert.equal(patSurfaceDoc.schema, "frontier.pattern-surface.v1", "Noise-scaled starter should build frontier.pattern-surface.v1");
  authMat.pattern = patSurfaceDoc;
  authMat.Authoring.Sources[surfLayer.Channels[0] === "colour" ? "color" : surfLayer.Channels[0]] = { Mode: "pattern" };
  const enumRows = EnumerateMaterialFields(authMat);
  assert(enumRows.some((r) => r.Source.Mode === "pattern"), "EnumerateMaterialFields should include pattern-sourced channel");
  const patChanCanvas = await RasterPatternChannel(patSurfaceDoc, "Color");
  assert(patChanCanvas && patChanCanvas.width >= 16, "RasterPatternChannel must raster frontier.pattern-surface.v1 cleanly");
  const previewUrl = await RasterChannelPreview(authMat, surfLayer.Identity, "colour", 32);
  assert(typeof previewUrl === "string" && previewUrl.startsWith("data:image/png"), "RasterChannelPreview must render pattern channel preview without .jsx error");

  // 11b. Apply Pattern directly onto Material Workspace (Asset Drop + Live Pattern Studio Bridge + Texture Uniforms)
  const kenteMatPayload = JSON.stringify({
    assetKind: "pattern",
    id: "legacy:Asante Kente Blocks",
    name: "Asante Kente Blocks",
  });
  const matStage = globalThis.document.querySelector(".viewport-stage");
  matStage.dispatchEvent({
    type: "drop",
    dataTransfer: { getData: (t) => (t === "application/x-frontier-asset" ? kenteMatPayload : "") },
  });
  const afterDropSnap = api.getSnapshot();
  assert.equal(afterDropSnap.hasPattern, true, "Dropping Pattern onto Material stage should attach it to the Material");
  assert.equal(afterDropSnap.patternName, "Asante Kente Blocks");

  // Also test bridging live Pattern Studio artwork ("Tile on Material") -> produces frontier.pattern-surface.v1 with shader uniforms!
  api.switchWorkspace("pattern");
  const noiseEval = evaluateArtworkDocument(
    loadStarterAsArtworkDocument(listPatternStarterCatalog().find((e) => e.id === "noise-polka-wave")),
    128,
  );
  api.sendPatternToMaterial({ ...noiseEval, name: "Perlin Polka Wave", repeats: 5 });
  // Run RAF flush isn't automatic in mock, so trigger apply directly as Material handles active mount
  api.switchWorkspace("material");
  const directMat = createMaterial({
    ...normalizeMaterial(materials[1]),
    pattern: buildMaterialPatternDocument({ ...noiseEval, name: "Perlin Polka Wave", repeats: 5, rotation: 45, mapping: "cylinder" }),
    patternChannelMask: { albedo: true, roughness: true, metalness: false, height: true },
  });
  assert(directMat.userData.patternUniforms, "createMaterial with tileable pattern must expose userData.patternUniforms");
  assert.equal(directMat.userData.patternUniforms.uPatternRepeats.value, 5);
  assert.equal(directMat.userData.patternUniforms.uPatternObject.value, 2);
  assert.equal(directMat.userData.patternUniforms.uPatternApplyChannels.value.z, 0, "Disabled metalness channel must set uPatternApplyChannels.z to 0");
  directMat.dispose();

  // Also test Material drop when Tileable Pattern is applied (dropping a pattern in UI triggers Tileable Pattern Texture section)
  const matStageActive = globalThis.document.querySelector(".viewport-stage");
  matStageActive.dispatchEvent({
    type: "drop",
    dataTransfer: { getData: (t) => (t === "application/x-frontier-asset" ? JSON.stringify({ assetKind: "pattern", id: "noise-polka-wave", name: "Perlin Polka Wave" }) : "") },
  });
  assert.equal(api.getSnapshot().hasPattern, true);
  assert.equal(api.getSnapshot().patternName, "Perlin Polka Wave");
  const patTitleEls = [...globalThis.document.querySelectorAll(".section-title")].map((el) => el.textContent);
  assert(patTitleEls.some((t) => t.includes("Tileable Pattern Texture")), "Tileable Pattern Texture Inspector section must appear");

  // 11c. Verify Per-Layer Inspector for ALL Texture Paint Layer Stack kinds (+ Fill, + Material, + Wear Gen, + Folder)
  api.switchWorkspace("paint");
  for (const btnLabel of ["+ Material", "+ Wear Gen", "+ Folder", "+ Fill"]) {
    const addBtn = [...globalThis.document.querySelectorAll(".chip-btn")].find((b) => b.textContent === btnLabel);
    assert(addBtn, `Missing Texture Paint stack button ${btnLabel}`);
    addBtn.click();
    const titles = [...globalThis.document.querySelectorAll(".section-title")].map((el) => el.textContent);
    assert(titles.some((t) => t.startsWith("Layer Inspector:")), `Expected dedicated Layer Inspector for ${btnLabel}`);
  }
});

await suite("12. Candy Materials, Expanded Patterns, Full 29 PBR Channels & Mesh Preview Dropdown", async () => {
  const { PbrChannels, DeclareMaterialAuthoring } = await import("./Engine/Materials/MaterialSpecification.js");
  const { RasterPbrFields } = await import("./Engine/Materials/FieldProjection.js");
  const api = globalThis.window.__frontierTextureStudio;

  // 12a. Candy-like materials catalog & procedural kernels uType 39..42
  const candyItems = materials.filter((m) => m.category === "Candy");
  assert(candyItems.length >= 16, `Expected >=16 Candy materials, got ${candyItems.length}`);
  for (const id of ["sour-strawberry-sugar", "peppermint-candy-cane", "peach-ring-gummy", "cosmic-jawbreaker-hologram"]) {
    const item = materials.find((m) => m.id === id);
    assert(item, `Missing Candy material ${id}`);
    const recipe = getRecipe(item);
    assert(recipe.controls.length >= 3, `Candy recipe missing controls for ${id}`);
    const threeMat = createMaterial(item);
    const shaderMock = {
      uniforms: THREE.UniformsUtils.clone(THREE.ShaderLib.physical.uniforms),
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    threeMat.onBeforeCompile(shaderMock);
    assert(shaderMock.fragmentShader.includes("candyCrystalMask"), `Candy crystal shader hook missing for ${id}`);
    threeMat.dispose();
  }

  // 12b. Expanded Pattern Starters (Candy & Confectionery, Art Deco, Tech, Noise)
  const patCatalog = listPatternStarterCatalog();
  assert(patCatalog.length >= 190, `Expected >=190 pattern starters, got ${patCatalog.length}`);
  for (const id of ["candy-peppermint-stripe", "candy-sprinkle-confetti", "deco-peacock-fans", "tech-brass-knurling"]) {
    const entry = patCatalog.find((e) => e.id === id);
    assert(entry, `Missing expanded pattern starter ${id}`);
    const svg = getArtworkPreviewSvg(entry, "pattern", 64);
    assert(svg.includes("<svg"), `SVG failed for pattern ${id}`);
  }

  // 12c. Full PBR Channels (~20+ -> 29 OpenPBR channels) across MaterialSpecification + FieldProjection + MaterialWorkspace
  const chanKeys = Object.keys(PbrChannels);
  assert(chanKeys.length >= 24, `Expected >=24 full PBR channels, got ${chanKeys.length}`);
  for (const key of ["height", "occlusion", "opacity", "specularColour", "coat", "coatRoughness", "coatIor", "coatNormal", "sheenColour", "anisotropyRotation", "iridescence", "iridescenceIor", "iridescenceThickness", "attenuationColour", "emissive", "emissiveIntensity"]) {
    assert(PbrChannels[key], `Missing full PBR channel ${key} in PbrChannels`);
  }

  const allChanMat = normalizeMaterial(materials.find((m) => m.id === "sour-strawberry-sugar"));
  allChanMat.Authoring = DeclareMaterialAuthoring(allChanMat);
  allChanMat.Authoring.Layers[allChanMat.Authoring.Layers.length - 1].Channels = chanKeys;
  const fullRaster = await RasterPbrFields(allChanMat, 32);
  assert(fullRaster.Rows.length >= 1, "Expected RasterPbrFields rows with full PBR channels");

  // 12d. Material Workspace UI: No Leather Swatch or Botanical previews + Preview Mesh Geometry is a dropdown
  api.switchWorkspace("material");
  const allOptionTexts = [...globalThis.document.querySelectorAll("option")].map((o) => o.textContent);
  assert(!allOptionTexts.includes("Leather Swatch"), "Leather Swatch option must be removed");
  assert(!allOptionTexts.includes("Botanical"), "Botanical option must be removed");

  const stackTitles = [...globalThis.document.querySelectorAll(".stack-row-title")].map((el) => el.textContent);
  assert(!stackTitles.includes("Leather Swatch") && !stackTitles.includes("Botanical"), "Mesh preview list rows must not exist");
  assert(!stackTitles.includes("Chamfer Box") && !stackTitles.includes("Shader Ball"), "Preview Mesh Geometry must be a dropdown, not stack-row list items");

  const meshLabels = [...globalThis.document.querySelectorAll(".property-label")].map((el) => el.textContent);
  assert(meshLabels.includes("Preview Mesh Geometry"), "Preview Mesh Geometry dropdown label must exist");

  const triggers = [...globalThis.document.querySelectorAll(".pill-trigger")];
  const meshTrigger = triggers.find((el) => el.textContent.includes("Mesh:") && el.textContent.includes("Shader Ball"));
  assert(meshTrigger, "Preview Mesh Geometry dropdown trigger must display Mesh: Shader Ball");

  meshTrigger.click();
  const meshItem = [...globalThis.document.querySelectorAll(".pill-menu .menu-item")].find((el) => el.textContent.includes("Chamfer Box"));
  assert(meshItem, "Preview Mesh Geometry dropdown menu must contain Chamfer Box");
  meshItem.click();
  const updatedTrigger = [...globalThis.document.querySelectorAll(".pill-trigger")].find((el) => el.textContent.includes("Mesh:"));
  assert(updatedTrigger?.textContent.includes("Chamfer Box"), "Clicking Chamfer Box in mesh dropdown must update preview geometry");

  // Select a Material authoring layer (Surface body / Clearcoat) and verify all 29 PBR channels in its Layer Inspector
  const matLayerRow = [...globalThis.document.querySelectorAll(".stack-row")].find((r) => !r.textContent.includes("PATTERN"));
  assert(matLayerRow, "Expected an authoring layer row in Material Layer Stack");
  matLayerRow.click();
  const layerChanTriggers = [...globalThis.document.querySelectorAll(".pbr-add-trigger")];
  assert(layerChanTriggers.length >= 1, "Material Layer Inspector must render + Channel dropdown trigger");
  layerChanTriggers[layerChanTriggers.length - 1].click();
  const menuChanItems = globalThis.document.querySelectorAll(".pbr-chan-menu .menu-item");
  const activeChanPills = globalThis.document.querySelectorAll(".pbr-chan-pill");
  const totalLayerPbrChannels = menuChanItems.length + activeChanPills.length;
  assert(totalLayerPbrChannels >= 24, `Expected >=24 PBR channels rendered in Material Layer Inspector, got ${totalLayerPbrChannels}`);
});

await suite("13. Engineering Materials, Removal Check, Preview Plane, Basic Stamps & Asset Inspector", async () => {
  const api = globalThis.window.__frontierTextureStudio;

  // 13a. Verify Leather, Rose Petal, and Leaves are cleanly removed from browsable materials
  assert.equal(materials.filter((m) => m.category === "Leather").length, 0, "All Leather category materials must be removed");
  for (const removedId of [
    "cognac-leather",
    "nappa-leather",
    "bull-grain-saddle-leather",
    "bull-grain-black-leather",
    "crocodile-belly-leather",
    "rose-petal",
    "broadleaf-green",
    "autumn-leaf",
  ]) {
    assert(!materials.some((m) => m.id === removedId), `Removed material ${removedId} must not remain in catalog`);
  }

  // 13b. Verify new Engineering / Technical materials compile cleanly across PCB, Wafer, OLED, Heat Sink, RF Mesh, Solar & LED
  const expectedEngIds = [
    "emerald-pcb-copper-circuit",
    "stealth-matte-black-pcb",
    "euv-silicon-lithography-wafer",
    "tandem-oled-subpixel-matrix",
    "cnc-anodized-fin-heat-sink",
    "optical-fiber-waveguide",
    "perforated-rf-shielding-mesh",
    "optical-diffraction-grating",
    "perovskite-tandem-solar",
    "amber-avionics-led-matrix",
  ];
  for (const id of expectedEngIds) {
    const m = materials.find((x) => x.id === id);
    assert(m, `Missing engineering material ${id}`);
    assert.equal(m.category, "Technical", `Expected ${id} to be categorized as Technical`);
    assert(getRecipe(m).controls.length >= 3, `Missing recipe controls for ${id}`);
    const threeMat = createMaterial(m);
    const shaderMock = {
      uniforms: THREE.UniformsUtils.clone(THREE.ShaderLib.physical.uniforms),
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    threeMat.onBeforeCompile(shaderMock);
    assert(shaderMock.fragmentShader.includes("engMetalMask"), `Missing engineering kernel hook in ${id}`);
    threeMat.dispose();
  }

  // 13c. Verify Flat Plane option in Material Previewer (toolbar + FilterDropdownPill)
  api.switchWorkspace("material");
  const allOptionTexts = [...globalThis.document.querySelectorAll("option")].map((o) => o.textContent);
  assert(allOptionTexts.includes("Plane"), "Material toolbar select must include Plane geometry");
  const meshTrigger = [...globalThis.document.querySelectorAll(".pill-trigger")].find((el) => el.textContent.includes("Mesh:"));
  assert(meshTrigger, "Missing Preview Mesh Geometry dropdown trigger");
  meshTrigger.click();
  const planeMenuItem = [...globalThis.document.querySelectorAll(".pill-menu .menu-item")].find((el) => el.textContent.includes("Plane"));
  assert(planeMenuItem, "Preview Mesh Geometry menu must offer Plane");
  planeMenuItem.click();
  const updatedTrigger = [...globalThis.document.querySelectorAll(".pill-trigger")].find((el) => el.textContent.includes("Mesh:"));
  assert(updatedTrigger?.textContent.includes("Plane"), "Clicking Plane in dropdown must activate Plane preview");

  // 13d. Verify Basic Stamp Presets in StarterCatalogBridge
  const stamps = listStampStarterCatalog();
  assert(stamps.length >= 17, `Expected >=17 stamps including basic presets, got ${stamps.length}`);
  for (const basicId of [
    "stamp-basic-circle",
    "stamp-basic-ring",
    "stamp-basic-square",
    "stamp-basic-triangle",
    "stamp-basic-star",
    "stamp-basic-hexagon",
    "stamp-basic-cross",
    "stamp-basic-arrow-chevron",
    "stamp-basic-rivet-grid",
  ]) {
    const st = stamps.find((s) => s.id === basicId);
    assert(st, `Missing basic stamp preset ${basicId}`);
    assert(st.group === "Basic Shapes" || st.group === "Basic Stencils", `Unexpected group for ${basicId}: ${st.group}`);
    const svg = getArtworkPreviewSvg(st, "stamp", 64);
    assert(svg.includes("<svg"), `Basic stamp SVG render failed for ${basicId}`);
    const doc = loadStarterAsArtworkDocument(st);
    const evalOut = evaluateArtworkDocument(doc, 128);
    assert.equal(evalOut.width, 128);
  }

  // 13e. Verify Right-Side Asset Inspector inside Bottom Asset Browser Drawer
  const insp = globalThis.document.querySelector(".cb-inspector");
  assert(insp, "Missing right-side .cb-inspector pane in AssetBrowser");
  assert(insp.textContent.includes("Asset Inspector"), "Right-side Asset Inspector heading missing");

  // Select a specific Engineering Material card and verify inspector updates
  const pcbCard = [...globalThis.document.querySelectorAll(".cb-card")].find((c) => c.dataset.assetId === "emerald-pcb-copper-circuit");
  if (!pcbCard) {
    // Switch family or search if grid capped at 120 items
    const techBtn = [...globalThis.document.querySelectorAll(".cb-family-btn")].find((b) => b.dataset.family === "Technical");
    assert(techBtn, "Missing Technical family button in AssetBrowser");
    techBtn.click();
  }
  const targetPcbCard = [...globalThis.document.querySelectorAll(".cb-card")].find((c) => c.dataset.assetId === "emerald-pcb-copper-circuit");
  assert(targetPcbCard, "Missing Emerald PCB Copper Circuit card in AssetBrowser");
  targetPcbCard.click();
  assert.equal(api.getAssetBrowser().inspectorItemId, "emerald-pcb-copper-circuit");
  assert(globalThis.document.querySelector(".cb-insp-title")?.textContent.includes("Emerald PCB Copper Circuit"), "Inspector must display clicked Material title");
  assert(globalThis.document.querySelectorAll(".cb-insp-prop").length >= 4, "Inspector must render material PBR property cards");

  // Switch to Stamps tab in AssetBrowser, click a Basic Stamp, and confirm Inspector updates!
  const stampTab = [...globalThis.document.querySelectorAll(".cb-cat-tab")].find((t) => t.dataset.category === "stamps");
  assert(stampTab, "Missing Stamps tab in AssetBrowser");
  stampTab.click();
  const starStampCard = [...globalThis.document.querySelectorAll(".cb-card")].find((c) => c.dataset.assetId === "stamp-basic-star");
  assert(starStampCard, "Missing stamp-basic-star card in AssetBrowser");
  starStampCard.click();
  assert.equal(api.getAssetBrowser().inspectorItemId, "stamp-basic-star");
  assert(globalThis.document.querySelector(".cb-insp-title")?.textContent.includes("Five-Point Badge Star"), "Inspector must update to clicked Basic Stamp");
  const applyAction = [...globalThis.document.querySelectorAll(".cb-insp-btn")].find((b) => b.textContent === "Apply to Stage");
  assert(applyAction, "Inspector must show Apply to Stage action button");
  applyAction.click();
  assert.equal(api.getActiveId(), "stamp", "Clicking Apply to Stage on stamp in inspector should route to Stamp Studio cleanly");
});

await suite("14. Texture Paint Clean Layer Inspector, Right-Click Tool-Specific Brush Inspector & Per-Channel Mixing", async () => {
  const api = globalThis.window.__frontierTextureStudio;
  api.switchWorkspace("paint");

  // 14a. Right column is strictly Layer Inspector (no brush/dynamics/curves/stamps clutter)
  const panelTitles = [...globalThis.document.querySelectorAll(".panel-heading h2")].map((el) => el.textContent);
  assert(panelTitles.includes("Layer Inspector"), "Right-side heading must read 'Layer Inspector'");
  assert(!panelTitles.includes("Brush & Layer Inspector"), "Legacy 'Brush & Layer Inspector' heading must be replaced");

  const secTitles = [...globalThis.document.querySelectorAll(".section-title")].map((el) => el.textContent);
  assert(!secTitles.some((t) => t.includes("Brush & Tool") || t.includes("Colour Dynamics") || t.includes("Brush & Stroke Curves") || t.includes("Stamps & Custom Images")),
    "Right-side Layer Inspector must not be cluttered with brush/dynamics/curves/stamp sections");

  // 14b. Per-layer PBR Channel pills & Inspect Channel per-channel weight/blend controls
  const inspectLabels = [...globalThis.document.querySelectorAll(".segmented-track")];
  const chanInspSeg = inspectLabels.find((seg) => [...seg.querySelectorAll("button")].some((b) => b.textContent === "Rough"));
  assert(chanInspSeg, "Layer Inspector must provide 'Inspect Channel' segmented tabs (All | Albedo | Rough | Metal | Height)");
  const roughBtn = [...chanInspSeg.querySelectorAll("button")].find((b) => b.textContent === "Rough");
  roughBtn.click();
  const sliderLabelsAfterRough = [...globalThis.document.querySelectorAll(".property-label")].map((el) => el.textContent);
  assert(sliderLabelsAfterRough.some((t) => t.includes("Roughness Channel Weight")), "Inspecting Roughness channel must show Roughness Channel Weight");
  assert(sliderLabelsAfterRough.some((t) => t.includes("Roughness Blend Override")), "Inspecting Roughness channel must show Roughness Blend Override");

  // Verify removing Height channel from Layer removes its Inspect Channel tab cleanly
  const layerPills = [...globalThis.document.querySelectorAll(".pbr-chan-pill")];
  const heightPill = layerPills.find((p) => p.dataset.channel === "height");
  assert(heightPill, "Layer Inspector must render Height PBR pill");
  heightPill.click();
  assert.equal(globalThis.document.querySelectorAll(".pbr-chan-pill").length, 3, "Removing Height pill must leave 3 active layer channels");
  const allChBtn = [...globalThis.document.querySelectorAll(".segmented-track button")].find((b) => b.textContent === "All");
  allChBtn?.click();

  // Re-add Height via Layer's + Channel dropdown
  const addChanTrig = globalThis.document.querySelector(".pbr-add-trigger");
  addChanTrig?.click();
  const hMenu = [...globalThis.document.querySelectorAll(".pbr-chan-menu .menu-item")].find((el) => el.dataset.channel === "height");
  assert(hMenu, "Removed Height channel must appear in Layer Inspector + Channel dropdown");
  hMenu.click();
  assert.equal(globalThis.document.querySelectorAll(".pbr-chan-pill").length, 4, "Re-adding Height must restore 4 Layer PBR channel pills");

  // 14c. Verify Right-Click on viewport opens Tool-Specific Brush Inspector and isolates irrelevant controls per tool
  const pane = globalThis.document.querySelector(".viewport-split-pane");
  pane.dispatchEvent({ type: "contextmenu", clientX: 250, clientY: 200 });
  let pop = globalThis.document.body.querySelector(".tool-popup-menu");
  assert(pop, "Right-click must open Active Tool / Brush Inspector popup");
  assert(pop.querySelector(".tool-popup-head strong")?.textContent === "Brush Inspector", "Default popup title should read 'Brush Inspector'");

  // Switch to Erase tab inside popup -> verify ONLY eraser controls (NO ink swatches, NO Roughness/Metalness/Height Relief sliders, NO Colour Dynamics subtabs)
  const eraseTab = [...pop.querySelectorAll(".tool-popup-tab")].find((t) => t.textContent.includes("Erase"));
  assert(eraseTab, "Missing Erase tab in right-click Tool Inspector");
  eraseTab.click();
  pop = globalThis.document.body.querySelector(".tool-popup-menu");
  assert.equal(pop.querySelector(".tool-popup-head strong")?.textContent, "Eraser Inspector");
  assert.equal(pop.querySelectorAll(".tool-popup-swatch").length, 0, "Eraser Inspector must NOT show paint ink swatches");
  assert.equal(pop.querySelectorAll(".tool-popup-subtab").length, 0, "Eraser Inspector must NOT clutter UI with paint Colour Dynamics/Curves tabs");
  const eraseLabels = [...pop.querySelectorAll(".property-label")].map((el) => el.textContent);
  assert(!eraseLabels.some((l) => l.includes("Roughness") || l.includes("Metalness") || l.includes("Height Relief")), "Eraser Inspector must NOT show paint PBR value sliders");

  // Switch to Fill tab -> verify Flood Scope & Fill controls without brush curves
  const fillTab = [...pop.querySelectorAll(".tool-popup-tab")].find((t) => t.textContent.includes("Fill"));
  fillTab.click();
  pop = globalThis.document.body.querySelector(".tool-popup-menu");
  assert.equal(pop.querySelector(".tool-popup-head strong")?.textContent, "Fill Inspector");
  assert(pop.textContent.includes("Flood Fill Scope"), "Fill Inspector must expose Flood Fill Scope");
  assert(pop.querySelectorAll(".tool-popup-swatch").length > 0, "Fill Inspector must expose Fill Color swatches");

  // Switch to Eyedropper tab -> verify clean Sampler card
  const pickTab = [...pop.querySelectorAll(".tool-popup-tab")].find((t) => t.textContent.includes("Pick"));
  pickTab.click();
  pop = globalThis.document.body.querySelector(".tool-popup-menu");
  assert.equal(pop.querySelector(".tool-popup-head strong")?.textContent, "Sample Inspector");

  // Switch back to Brush before closing and test right-click directly on Layer Inspector column background!
  const brushTab = [...pop.querySelectorAll(".tool-popup-tab")].find((t) => t.textContent.includes("Brush"));
  brushTab.click();
  globalThis.window.dispatchEvent({ type: "keydown", key: "Escape" });
  const cols = globalThis.document.querySelectorAll(".studio-column");
  const rightAside = cols[cols.length - 1];
  rightAside.dispatchEvent({ type: "contextmenu", clientX: 900, clientY: 320 });
  assert(globalThis.document.body.querySelector(".tool-popup-menu"), "Right-clicking Layer Inspector panel must also open Brush/Tool Inspector");
  globalThis.window.dispatchEvent({ type: "keydown", key: "Escape" });

  // 14d. Verify Engine-level Per-Channel Opacity, Per-Channel Blend, Rotated Fill Pattern, and Triplanar Grunge + Invert Generator
  const r2 = 32;
  const cuesGeo = new THREE.SphereGeometry(1, 24, 16);
  const cuesIdx = buildMeshPaintIndex(cuesGeo);
  const testCues = buildDefaultMeshCues(cuesGeo, cuesIdx, r2);
  const doc14 = createPaintDocument(r2);
  const baseFill = doc14.layers[0];
  baseFill.fill.color = "#808080";
  baseFill.fill.roughness = 0.8;
  baseFill.fill.metalness = 0.0;
  baseFill.fill.height = 0.0;

  const solidChLayer = createPaintLayer("fill", {
    res: r2,
    fill: { color: "#ffffff", roughness: 0.2, metalness: 1.0, height: 0.6 },
    channels: { albedo: true, roughness: true, metalness: true, height: true },
    channelOpacity: { albedo: 1.0, roughness: 0.0, metalness: 0.5, height: 1.0 },
    channelBlends: { albedo: "Multiply", roughness: "Inherit", metalness: "Normal", height: "Screen" },
  });
  doc14.layers.push(solidChLayer);
  const comp14 = compositePaintDocument(doc14, testCues);
  const midOff = Math.floor(r2 * 0.5) * r2 + Math.floor(r2 * 0.5);
  assert(Math.abs(comp14.roughness[midOff] - Math.round(0.8 * 255)) <= 3, "Zero roughness channelOpacity must keep underlying roughness untouched");
  assert(comp14.metalness[midOff] > 90 && comp14.metalness[midOff] < 200, "0.5 metalness channelOpacity must half-blend metalness");

  const patEval14 = evaluateArtworkDocument(createArtworkDocument("pattern"), 32);
  const topFill = createPaintLayer("fill", {
    res: r2,
    fill: {
      color: "#ffffff",
      roughness: 0.2,
      metalness: 0.9,
      height: 0.3,
      patternData: patEval14,
      patternRepeats: 3,
      patternRotation: 35,
      patternHeightStrength: 1.5,
    },
  });

  // Generator Triplanar Grunge + Invert test
  const genGr = createPaintLayer("generator", {
    res: r2,
    generator: { preset: "triplanar-grunge", color: "#554433", roughness: 0.9, metalness: 0.0, height: -0.3, threshold: 0.4, softness: 0.35, noiseMix: 0.6, noiseScale: 8, invert: false },
  });
  const docGen = createPaintDocument(r2);
  docGen.layers.push(genGr);
  const compGenA = compositePaintDocument(docGen, testCues);
  genGr.generator.invert = true;
  const compGenB = compositePaintDocument(docGen, testCues);
  let diffSum = 0;
  for (let i = 0; i < r2 * r2; i++) diffSum += Math.abs(compGenA.albedo[i * 4] - compGenB.albedo[i * 4]);
  assert(diffSum > 100, "Inverting triplanar-grunge generator must visibly invert mask coverage");

  // Verify GpuPaintSession patternOrmhTex upload + composite uniforms cleanly execute end-to-end
  const { GpuPaintSession } = await import("./Engine/GpuPaintEngine.js");
  const mockGpuRenderer14 = {
    capabilities: { isWebGL2: true },
    extensions: { get: () => ({}) },
    getContext: () => ({ getParameter: () => "ValidatedWebGL2" }),
    autoClear: true,
    getRenderTarget: () => null,
    getScissorTest: () => false,
    setScissorTest() {},
    setRenderTarget() {},
    render() {},
    getClearColor: (c) => c.set(0, 0, 0),
    getClearAlpha: () => 1,
    setClearColor() {},
    clear() {},
  };
  const gpu14 = new GpuPaintSession(mockGpuRenderer14, 32);
  gpu14.uploadMeshCues(testCues, null);
  gpu14.ensureLayerTargets(topFill);
  assert(topFill._gpu.patternOrmhTex, "GpuPaintSession must generate packed ORM+H DataTexture when fill patternData includes roughness/metalness/height");
  assert(gpu14.compMat.uniforms.uLayerChanOpacity && gpu14.compMat.uniforms.uLayerChanBlends && gpu14.compMat.uniforms.uGenScale && gpu14.compMat.uniforms.uGenInvert, "GpuPaintSession composite shader must expose channel opacity, channel blend, and generator uniforms");
  doc14.layers.push(topFill, genGr);
  gpu14.composite(doc14);
  assert.equal(gpu14.compMat.uniforms.uGenInvert.value, 1, "Composite pass must forward generator invert flag to GPU");
  gpu14.dispose();
  cuesGeo.dispose();
});

// ----------------------------------------------------------------- 15. FULL 19-CHANNEL PBR, FOLDERS, PROCEDURAL MASKS & MOVABLE STAMPS
await suite("15. Full 19-Channel PBR, Folder & Procedural Masks, Movable/Scalable/Rotatable Stamps & GLSL3 Sampler Guard", async () => {
  assert.equal(PAINT_PBR_CHANNELS.length, 19, "Expected 19 PBR channels in PAINT_PBR_CHANNELS");
  assert.equal(EXTENDED_PBR_KEYS.length, 15, "Expected 15 extended PBR keys");

  const r = 48;
  const geo = new THREE.SphereGeometry(1, 24, 16);
  const idx = buildMeshPaintIndex(geo);
  const cues = buildDefaultMeshCues(geo, idx, r);

  // 15a. Extended PBR Fill + Paint Stroke + Mask Luminance + Procedural Mask + Feathering
  const doc = createPaintDocument(r);
  const extFill = createPaintLayer("fill", {
    res: r,
    fill: {
      color: "#d4af37",
      roughness: 0.22,
      metalness: 0.9,
      height: 0.25,
      extValues: createDefaultExtendedValues({ clearcoat: 0.85, coatRoughness: 0.06, emissive: "#ff4422", emissiveIntensity: 1.5, iridescence: 0.75, anisotropy: 0.65 }),
    },
    channels: {
      albedo: true, roughness: true, metalness: true, height: true,
      clearcoat: true, coatRoughness: true, emissive: true, emissiveIntensity: true, iridescence: true, anisotropy: true,
    },
  });
  doc.layers.push(extFill);

  const paintExt = createPaintLayer("paint", {
    res: r,
    channels: { albedo: true, roughness: true, metalness: true, height: true, sheen: true, transmission: true, opacity: true },
  });
  doc.layers.push(paintExt);

  const acc = createStrokeAccumulator(r);
  const strokeBrush = {
    tool: "paint",
    tip: "round",
    color: "#33aaee",
    opacity: 0.9,
    flow: 0.85,
    hardness: 0.7,
    roughness: 0.4,
    metalness: 0.1,
    height: 0.2,
    paintChannels: { albedo: true, roughness: true, metalness: true, height: true, sheen: true, transmission: true, opacity: true },
    extValues: { sheen: 0.9, transmission: 0.7, opacity: 0.5 },
  };
  applyBrushDabUV({ strokeAccum: acc, cues, u: 0.5, v: 0.5, pixelRadius: 10, brush: strokeBrush });
  assert(commitStrokeAccumulator(paintExt, acc, strokeBrush, false), "Extended PBR paint stroke must commit");

  const compExt = compositePaintDocument(doc, cues);
  const midPx = Math.floor(r * 0.5) * r + Math.floor(r * 0.5);
  assert(compExt.ext.clearcoat[midPx] > 180, "Composited extended clearcoat buffer must preserve clearcoat value");
  assert(compExt.ext.emissive[midPx * 3] > 180, "Composited extended emissive RGB buffer must preserve glow color");
  assert(compExt.ext.anisotropy[midPx] > 120, "Composited extended anisotropy buffer must preserve anisotropy value");
  assert(compExt.ext.sheen[midPx] > 150, "Painted sheen channel must appear in composited extended output");
  assert(compExt.ext.transmission[midPx] > 120, "Painted transmission channel must appear in composited extended output");

  // 15b. Folder visibility, opacity & parent mask cascade + Procedural mask + Feathering + Luminance stroke
  const folder = createPaintLayer("folder", { res: r, name: "Armor Group", opacity: 0.5 });
  folder.mask = { enabled: true, invert: false, opacity: 1.0, pixels: new Uint8ClampedArray(r * r).fill(255) };
  // Zero out top-left quadrant of folder mask
  for (let y = 0; y < r / 2; y++) for (let x = 0; x < r / 2; x++) folder.mask.pixels[y * r + x] = 0;
  featherMaskPixels(folder.mask.pixels, r, 1);

  const child = createPaintLayer("fill", {
    res: r,
    name: "Red Accent Inside Folder",
    parentId: folder.id,
    fill: { color: "#ff0000", roughness: 0.5, metalness: 0.0, height: 0 },
  });
  // Test procedural mask on child layer + partial luminance mask stroke
  child.mask = { enabled: true, invert: false, opacity: 0.8, pixels: new Uint8ClampedArray(r * r).fill(255) };
  generateProceduralMask(child.mask, cues, { preset: "edge-wear", threshold: 0.35, softness: 0.25, noiseScale: 6, noiseMix: 0.2 });
  const mAcc = createStrokeAccumulator(r);
  applyBrushDabUV({ strokeAccum: mAcc, cues, u: 0.75, v: 0.25, pixelRadius: 8, brush: { tool: "paint", tip: "round", color: "#808080", opacity: 1, flow: 1, hardness: 0.9 } });
  commitStrokeAccumulator(child, mAcc, { tool: "paint", color: "#808080", opacity: 1 }, true);
  assert(child.mask.pixels[Math.floor(r * 0.75) * r + Math.floor(r * 0.75)] < 200, "Grey luminance mask stroke must paint intermediate alpha values onto mask");

  const folderDoc = createPaintDocument(r);
  folderDoc.layers[0].fill.color = "#303030";
  folderDoc.layers.push(folder, child);
  const folderCompA = compositePaintDocument(folderDoc, cues);
  const hiddenCornerPx = 2 * r + 2;
  const visibleCornerPx = (r - 4) * r + (r - 4);
  assert(folderCompA.albedo[hiddenCornerPx * 4] < 80, "Folder mask zero region must suppress nested child layer");
  folder.visible = false;
  const folderCompHidden = compositePaintDocument(folderDoc, cues);
  assert.equal(folderCompHidden.albedo[visibleCornerPx * 4], 48, "Hiding parent folder must hide nested child layers completely");

  // 15c. Unified Movable / Scalable / Rotatable Stamp Subcomponents (UV & 3D)
  const sw = 32;
  const sAlb = new Uint8ClampedArray(sw * sw * 4);
  const sRough = new Uint8ClampedArray(sw * sw * 4);
  const sMetal = new Uint8ClampedArray(sw * sw * 4);
  const sHeight = new Uint8ClampedArray(sw * sw * 4);
  for (let y = 0; y < sw; y++) {
    for (let x = 0; x < sw; x++) {
      const i4 = (y * sw + x) * 4;
      const d = Math.hypot((x + 0.5) / sw - 0.5, (y + 0.5) / sw - 0.5);
      if (d <= 0.44) {
        sAlb[i4] = 240; sAlb[i4 + 1] = 110; sAlb[i4 + 2] = 40; sAlb[i4 + 3] = 255;
        sRough[i4] = 80; sRough[i4 + 3] = 255;
        sMetal[i4] = 200; sMetal[i4 + 3] = 255;
        sHeight[i4] = 210; sHeight[i4 + 3] = 255;
      }
    }
  }
  const patEval = { width: sw, heightPx: sw, albedo: sAlb, roughness: sRough, metalness: sMetal, height: sHeight };
  const stampDoc = createPaintDocument(r);
  const stLayer = createPaintLayer("decal", { res: r, name: "Movable Decals" });
  const st = createStampInstance({
    name: "Test Emblem",
    projection: "uv",
    u: 0.25,
    v: 0.75,
    scale: 0.16,
    rotation: 0,
    opacity: 1,
    stampData: patEval,
  });
  stLayer.stamps.push(st);
  stampDoc.layers.push(stLayer);
  const compStampA = compositePaintDocument(stampDoc, cues);
  st.u = 0.75;
  st.v = 0.25;
  st.scale = 0.28;
  st.rotation = 45;
  const compStampB = compositePaintDocument(stampDoc, cues);
  let stampMoveDiff = 0;
  for (let i = 0; i < compStampA.albedo.length; i += 4) stampMoveDiff += Math.abs(compStampA.albedo[i] - compStampB.albedo[i]);
  assert(stampMoveDiff > 300, "Moving/scaling/rotating stamp subcomponent must re-composite pixels at new position");

  // Test 3D projected stamp instance via lookupSurfaceAtUv
  const surf = lookupSurfaceAtUv(cues, 0.5, 0.5);
  assert(surf && surf.pos.length === 3 && surf.normal.length === 3, "lookupSurfaceAtUv must recover valid 3D position & normal");
  st.projection = "3d";
  st.anchorPos = surf.pos;
  st.anchorNormal = surf.normal;
  const compStamp3D = compositePaintDocument(stampDoc, cues);
  assert(compStamp3D.albedo.length === r * r * 4, "3D stamp projection compositing must succeed");

  // 15d. GPU Shader Sampler Limit Guard (WebGL2 limit is 16 textures/stage; every shader must have <= 15 sampler2D)
  const { PAINT_GLSL3_SHADERS, GpuPaintSession } = await import("./Engine/GpuPaintEngine.js");
  for (const [sName, src] of Object.entries(PAINT_GLSL3_SHADERS)) {
    const count = (src.match(/uniform\s+sampler2D\b/g) || []).length;
    assert(count <= 15, `GLSL shader ${sName} exceeds safe WebGL2 sampler cap (${count} > 15)`);
  }
  const mockGpu15 = {
    capabilities: { isWebGL2: true },
    extensions: { get: () => ({}) },
    getContext: () => ({ getParameter: () => "ValidatedWebGL2" }),
    autoClear: true,
    getRenderTarget: () => null,
    getScissorTest: () => false,
    setScissorTest() {},
    setRenderTarget() {},
    render() {},
    readRenderTargetPixels(rt, x, y, w, h, out) { out.fill(180); },
    getClearColor: (c) => c.set(0, 0, 0),
    getClearAlpha: () => 1,
    setClearColor() {},
    clear() {},
  };
  const gpu15 = new GpuPaintSession(mockGpu15, r);
  gpu15.uploadMeshCues(cues, null);
  gpu15.composite(doc, { viewChannel: "emissive" });
  const exp15 = gpu15.readExportChannels({ emissive: true, clearcoat: true, anisotropy: true });
  assert(exp15.ext.emissive && exp15.ext.clearcoat && exp15.ext.anisotropy, "GpuPaintSession readExportChannels must extract requested extended PBR maps");
  gpu15.dispose();

  // 15e. Live UI Integration: Move Tool (V), 19-Channel Inspector, Layer Stamps & Layer Mask Sections
  globalThis.window.__frontierTextureStudio.switchWorkspace("paint");
  globalThis.window.dispatchEvent({ type: "keydown", key: "v" });
  const activeMoveBtn = [...globalThis.document.querySelectorAll(".toolbar-group .pill-btn")].find((b) => b.classList.contains("active"));
  assert(activeMoveBtn && /Transform Stamp/i.test(activeMoveBtn.title || ""), "Pressing V must activate Transform Stamp (Move) tool");

  const cols15 = [...globalThis.document.querySelectorAll(".studio-column")];
  const rightAside15 = cols15[cols15.length - 1];
  const chanTrigger = rightAside15.querySelector(".pbr-add-trigger");
  assert(chanTrigger, "Layer Inspector must expose + Channel dropdown for 19 PBR channels");
  chanTrigger.click();
  const clearcoatItem = rightAside15
    .querySelectorAll(".pbr-chan-menu .menu-item")
    .find((m) => m.dataset.channel === "clearcoat");
  assert(clearcoatItem, "Layer Inspector + Channel dropdown must offer Clearcoat");
  clearcoatItem.click();
  assert(
    rightAside15
      .querySelectorAll(".pbr-chan-pill")
      .some((p) => p.dataset.channel === "clearcoat"),
    "Adding Clearcoat must add Clearcoat pill to Active PBR Channels",
  );
  const sectionTitles15 = [...rightAside15.querySelectorAll(".section-title")].map((s) => s.textContent);
  assert(sectionTitles15.some((t) => /Layer Mask/i.test(t)), "Layer Inspector must expose unconditional Layer Mask section");

  // Add a Stamp layer via + Stamp and confirm Layer Stamps section + Rough tab
  const addStampLayerBtn = [...globalThis.document.querySelectorAll(".studio-column .chip-btn")].find((b) => /\+ Stamp/i.test(b.textContent || ""));
  assert(addStampLayerBtn, "Left layer panel must expose + Stamp button");
  addStampLayerBtn.click();
  const titlesAfterStamp = [...rightAside15.querySelectorAll(".section-title")].map((s) => s.textContent);
  assert(titlesAfterStamp.some((t) => /Layer Stamps/i.test(t)), "Selecting a Stamp layer must expose Layer Stamps section in inspector");
  const roughInspectBtn = [...rightAside15.querySelectorAll(".segmented-track button")].find((b) => /Rough/i.test(b.textContent || ""));
  assert(roughInspectBtn, "Inspect Channel SegmentedRow must retain Rough tab");

  // Confirm Right-click popup also exposes Move tab at [5] and Stamp tab at [3]
  globalThis.window.dispatchEvent({ type: "keydown", key: "q" });
  const popTabs15 = globalThis.document.body.querySelectorAll(".tool-popup-menu .tool-popup-tab");
  assert.equal(popTabs15.length, 6, "Right-click/Q popup must offer all 6 tools including Move");
  assert(/Stamp/i.test(popTabs15[3].textContent || ""), "Right-click popup tab[3] must remain Stamp");
  assert(/Move/i.test(popTabs15[5].textContent || ""), "Right-click popup tab[5] must be Move");
  globalThis.window.dispatchEvent({ type: "keydown", key: "Escape" });

  geo.dispose();
});

// ----------------------------------------------------------------- 16. UNIFIED 2-TIER ALLOYSTUDIO HEADER + LAYER STACK + LIVE THUMBNAIL PREVIEWS
await suite("16. Unified AlloyStudio Viewport Header ([Canvas; Size; View; Undo/Redo; Save][Tools]), Full Layer Stack & Live Previews", async () => {
  const api = globalThis.window.__frontierTextureStudio;

  // 16a. Pattern & Stamp Unified 2-Band Header + Band 2 Tools Capsule + Undo/Redo + Editable Name + Save Menu
  for (const ws of ["pattern", "stamp"]) {
    api.switchWorkspace(ws);
    const header = globalThis.document.querySelector(".vp-doc-bar");
    const toolsBar = globalThis.document.querySelector(".vp-tools-bar");
    assert(header && header.classList.contains("se-header-bar"), `Missing Band 1 .vp-doc-bar (.se-header-bar) in ${ws}`);
    assert(toolsBar && toolsBar.classList.contains("tp-authoring-bar"), `Missing Band 2 .vp-tools-bar (.tp-authoring-bar) in ${ws}`);
    assert.equal(header.querySelectorAll(".se-tool-btn").length, 0, `${ws} Band 1 header must NOT contain tool button clutter`);
    assert.equal(header.querySelectorAll(".vp-undo-btn, .vp-redo-btn, .se-save-btn, .vp-view-select").length, 0, `${ws} Band 1 header must strictly stay [Title     Resolution]`);
    assert(!globalThis.document.querySelector(".se-canvas-dock"), `${ws} must not render redundant floating .se-canvas-dock over viewport`);

    assert(header.querySelector(".tp-scene-icon"), `${ws} Band 1 must render .tp-scene-icon`);
    const docPill = header.querySelector(".se-doc-pill");
    const nameInput = docPill?.querySelector(".vp-name-input");
    assert(docPill && nameInput && docPill.querySelector(".vp-mode-badge"), `${ws} Band 1 must render editable .vp-name-input + .vp-mode-badge inside .se-doc-pill`);
    nameInput.value = `${ws}-CustomTitle`;
    nameInput.dispatchEvent({ type: "change" });
    assert.equal(api.getSnapshot().name, `${ws}-CustomTitle`, `${ws} editing .vp-name-input must update document name`);

    const toolGroup = toolsBar.querySelector(".tp-tool-group");
    assert(toolGroup && toolGroup.querySelectorAll(".se-tool-btn").length >= 10, `${ws} Band 2 .tp-tool-group capsule must house >=10 vector tools`);

    const resInput = header.querySelector(".vp-res-input");
    const resSelect = header.querySelector(".se-res-select");
    assert(resInput && resSelect, `${ws} Band 1 must include editable Resolution input + preset dropdown selector`);
    const resOpts = resSelect.children.map((o) => o.value);
    assert.deepEqual(resOpts, ["512", "1024", "2048", "4096"], `${ws} header Resolution selector must offer 512/1024/2048/4096`);
    resInput.value = "768";
    resInput.dispatchEvent({ type: "change" });
    assert.equal(api.getSnapshot().resolution, 768, `${ws} typing custom resolution in .vp-res-input must update state.resolution`);
    resSelect.value = "2048";
    resSelect.dispatchEvent({ type: "change" });
    assert.equal(api.getSnapshot().resolution, 2048, `${ws} switching Resolution selector to 2048 must update state.resolution`);
    assert.equal(resInput.value, "2048", `${ws} selecting preset must sync .vp-res-input`);

    // Undo / Redo buttons in Band 2: add a layer -> Undo -> Redo
    const undoBtn = toolsBar.querySelector(".vp-undo-btn");
    const redoBtn = toolsBar.querySelector(".vp-redo-btn");
    assert(undoBtn && redoBtn, `${ws} Band 2 must expose Undo & Redo buttons`);
    const layersBefore = api.getSnapshot().layers;
    const dockColPre = globalThis.document.querySelector(".tp-layer-dock");
    dockColPre.querySelector(".tp-add-toggle").click();
    dockColPre.querySelector(".tp-add-item").click();
    assert.equal(api.getSnapshot().layers, layersBefore + 1, `${ws} adding layer must increment count`);
    assert.equal(undoBtn.disabled, false, `${ws} Undo button must enable after adding layer`);
    undoBtn.click();
    assert.equal(api.getSnapshot().layers, layersBefore, `${ws} clicking Undo must revert added layer`);
    assert.equal(redoBtn.disabled, false, `${ws} Redo button must enable after Undo`);
    redoBtn.click();
    assert.equal(api.getSnapshot().layers, layersBefore + 1, `${ws} clicking Redo must reapply added layer`);

    const saveBtn = toolsBar.querySelector(".se-save-btn");
    const saveMenu = toolsBar.querySelector(".se-save-menu");
    assert(saveBtn && saveMenu, `${ws} Band 2 must include Save button + dropdown menu`);
    assert.equal(saveMenu.hidden, true, `${ws} Save menu starts closed`);
    saveBtn.click();
    assert.equal(saveMenu.hidden, false, `Clicking Save button in ${ws} must open Save menu`);
    const saveItems = saveMenu.querySelectorAll(".se-save-item");
    assert(saveItems.length >= 5, `${ws} Save menu must expose Project JSON, PNG, SVG, PBR Maps & Open Project actions`);
    assert(saveItems.some((it) => /2048px/.test(it.textContent || "")), `${ws} Save menu Export PNG label must reflect selected 2048px resolution`);
    saveBtn.click();
    assert.equal(saveMenu.hidden, true, `Clicking Save button again in ${ws} must close Save menu`);

    // 16b. Full Layer Stack + Live Per-Layer SVG Thumbnails
    const dockCol = globalThis.document.querySelector(".tp-layer-dock");
    assert(dockCol, `${ws} must render full .tp-layer-dock panel`);
    assert(dockCol.querySelector(".tp-pane-heading"), `${ws} missing .tp-pane-heading`);
    assert(dockCol.querySelector(".tp-new-folder") && dockCol.querySelector(".tp-add-toggle"), `${ws} missing Folder+ / Add+ header buttons`);
    assert.equal(dockCol.querySelectorAll(".tp-stat-card").length, 2, `${ws} must show Visible / Hidden filter stat cards`);
    assert(dockCol.querySelector(".tp-search-input"), `${ws} must expose layer search bar`);
    assert(dockCol.querySelector(".tp-system-base") && dockCol.querySelector(".tp-pane-footer"), `${ws} missing system base tile & footer metrics`);

    const thumbs = dockCol.querySelectorAll(".tp-layer-row .tp-layer-thumbnail");
    assert(thumbs.length >= 1, `${ws} layer rows must include .tp-layer-thumbnail containers`);
    assert(thumbs.some((th) => String(th.innerHTML || "").includes("<svg")), `${ws} layer thumbnails must contain live isolated <svg> layer previews`);

    // Test lock toggle + mask subrow
    const lockBtn = dockCol.querySelector(".tp-lock-btn");
    assert(lockBtn, `${ws} missing per-layer lock button`);
    lockBtn.click();
    assert(dockCol.querySelectorAll(".tp-layer-row").some((r) => r.classList.contains("is-locked")), `${ws} clicking lock button must mark layer .is-locked`);
    lockBtn.click();

    const actMask = [...dockCol.querySelectorAll(".tp-action-btn")][3];
    assert(actMask, `${ws} missing bottom layer toolbar mask button`);
    actMask.click();
    assert(dockCol.querySelector(".tp-mask-row .tp-mask-thumb"), `${ws} toggling mask must display .tp-mask-row with live mask thumbnail`);
  }

  // 16c. Texture Paint Unified 2-Band Header (Same Pattern/Stamp Style) + Live Size Resize + Save Menu + Full Layer Stack
  api.switchWorkspace("paint");
  const paintHeader = globalThis.document.querySelector(".vp-doc-bar");
  const paintToolsBar = globalThis.document.querySelector(".vp-tools-bar");
  assert(paintHeader && paintHeader.classList.contains("se-header-bar"), "Texture Paint must render Band 1 .vp-doc-bar (.se-header-bar)");
  assert(paintToolsBar && paintToolsBar.classList.contains("tp-authoring-bar"), "Texture Paint must render Band 2 .vp-tools-bar (.tp-authoring-bar)");
  assert.equal(paintHeader.querySelectorAll(".vp-undo-btn, .vp-redo-btn, .se-save-btn, .vp-view-select").length, 0, "Texture Paint Band 1 header must strictly stay [Title     Resolution]");

  const paintNameInput = paintHeader.querySelector(".vp-name-input");
  assert(paintNameInput && paintHeader.querySelector(".vp-mode-badge"), "Texture Paint Band 1 must display editable canvas name + mode badge");
  paintNameInput.value = "Painted Relic";
  paintNameInput.dispatchEvent({ type: "change" });
  assert.equal(api.getSnapshot().name, "Painted Relic", "Texture Paint renaming canvas must update document name");

  const paintResInput = paintHeader.querySelector(".vp-res-input");
  const paintResSel = paintHeader.querySelector(".se-res-select");
  assert(paintResInput && paintResSel, "Texture Paint Band 1 must include editable Resolution input + dropdown");
  paintResInput.value = "768";
  paintResInput.dispatchEvent({ type: "change" });
  assert.equal(api.getSnapshot().resolution, 768, "Texture Paint typing custom resolution 768 must update canvas export resolution");
  paintResSel.value = "1024";
  paintResSel.dispatchEvent({ type: "change" });
  assert.equal(api.getSnapshot().resolution, 1024, "Texture Paint changing Size to 1024 must resize canvas resolution");
  assert.equal(api.getSnapshot().liveResolution, 1024, "Texture Paint 1024 must set live session resolution to 1024");
  paintResSel.value = "4096";
  paintResSel.dispatchEvent({ type: "change" });
  assert.equal(api.getSnapshot().resolution, 4096, "Texture Paint changing Size to 4096 must set exportResolution=4096");
  assert.equal(api.getSnapshot().liveResolution, 2048, "Texture Paint 4096 must safely cap live buffer at 2048px while exporting 4096px");
  paintResSel.value = "512";
  paintResSel.dispatchEvent({ type: "change" });

  const paintSaveBtn = paintToolsBar.querySelector(".se-save-btn");
  const paintSaveMenu = paintToolsBar.querySelector(".se-save-menu");
  assert(paintSaveBtn && paintSaveMenu, "Texture Paint Band 2 must include Save dropdown menu");
  paintSaveBtn.click();
  assert.equal(paintSaveMenu.hidden, false, "Texture Paint Save button must open Save menu");
  assert(paintSaveMenu.querySelectorAll(".se-save-item").length >= 5, "Texture Paint Save menu must expose .paint.json Save/Open + Base/Normal/All PBR exports");
  paintSaveBtn.click();

  const paintToolCaps = paintToolsBar.querySelector(".tp-tool-group");
  assert(paintToolCaps && paintToolCaps.querySelectorAll(".pill-btn").length === 8, "Texture Paint Band 2 .tp-tool-group must house the 8 paint tool pills (incl. 3D Curve & 3D Gradient)");
  assert(paintToolsBar.querySelector(".gpu-badge"), "Texture Paint Band 2 trailing strip must keep GPU status badge");

  const paintDock = globalThis.document.querySelector(".tp-layer-dock");
  assert(paintDock, "Texture Paint must render .tp-layer-dock");
  const paintThumbs = paintDock.querySelectorAll(".tp-layer-row .tp-layer-thumbnail");
  assert(paintThumbs.length >= 2, "Texture Paint must render thumbnails for each stack layer");
  assert(paintThumbs.every((th) => th.querySelector("canvas")), "Texture Paint non-folder layer thumbnails must render live <canvas> previews");

  // Add a Stamp layer -> verify nested .tp-decal-tree-row & .tp-decal-tree-thumb render in layer stack
  const addStampChip = [...paintDock.querySelectorAll(".chip-btn")].find((b) => /\+ Stamp/i.test(b.textContent || ""));
  addStampChip.click();
  assert(paintDock.querySelector(".tp-decal-tree-row .tp-decal-tree-thumb canvas"), "Placed stamp on layer must render nested .tp-decal-tree-row with live stamp canvas preview");

  // Verify Search filtering and Visible/Hidden stat card filtering
  const searchInp = paintDock.querySelector(".tp-search-input");
  const countBeforeFilter = paintDock.querySelectorAll(".tp-layer-row").length;
  searchInp.value = "Stamp";
  searchInp.dispatchEvent({ type: "input" });
  const countAfterSearch = paintDock.querySelectorAll(".tp-layer-row").length;
  assert(countAfterSearch >= 1 && countAfterSearch < countBeforeFilter, `Search filter should narrow layers (${countAfterSearch} < ${countBeforeFilter})`);
  paintDock.querySelector(".tp-clear-filter").click();
  assert.equal(paintDock.querySelectorAll(".tp-layer-row").length, countBeforeFilter, "Show all should restore full layer count");

  // Verify GpuPaintSession.readLayerPreviewRgba & readMaskPreviewRgba with mock WebGL2 renderer
  const { GpuPaintSession } = await import("./Engine/GpuPaintEngine.js");
  const mockGpu16 = {
    capabilities: { isWebGL2: true },
    extensions: { get: () => ({}) },
    getContext: () => ({ getParameter: () => "ValidatedWebGL2" }),
    autoClear: true,
    getRenderTarget: () => null,
    getScissorTest: () => false,
    setScissorTest() {},
    setRenderTarget() {},
    render() {},
    readRenderTargetPixels(rt, x, y, w, h, out) {
      for (let py = 0; py < h; py++) {
        for (let px = 0; px < w; px++) {
          const i = (py * w + px) * 4;
          out[i] = py === 0 ? 220 : 40;
          out[i + 1] = 160;
          out[i + 2] = 90;
          out[i + 3] = 255;
        }
      }
    },
    getClearColor: (c) => c.set(0, 0, 0),
    getClearAlpha: () => 1,
    setClearColor() {},
    clear() {},
  };
  const gpu16 = new GpuPaintSession(mockGpu16, 32);
  const pLyr = createPaintLayer("paint", { res: 32 });
  pLyr.mask = { enabled: true, invert: false, pixels: new Uint8ClampedArray(32 * 32).fill(200) };
  gpu16.ensureLayerTargets(pLyr);
  const layerThumbRgba = gpu16.readLayerPreviewRgba(pLyr, 16);
  const maskThumbRgba = gpu16.readMaskPreviewRgba(pLyr, 16);
  assert.equal(layerThumbRgba.length, 16 * 16 * 4, "readLayerPreviewRgba must return 16x16 RGBA preview");
  assert.equal(layerThumbRgba[(15 * 16) * 4], 220, "readLayerPreviewRgba must Y-flip WebGL bottom-up rows to top-down canvas order");
  assert.equal(maskThumbRgba.length, 16 * 16 * 4, "readMaskPreviewRgba must return 16x16 RGBA preview");
  gpu16.dispose();
});

await suite("17. Hard-Surface Decal Stamps, Baked Transparent Material Icons & Floating 3-Pane Asset Browser", async () => {
  const api = globalThis.window.__frontierTextureStudio;
  const { renderAnalyticalSpherePixels, getBakedMaterialIcon } = await import("./MaterialIconBaker.js");
  const fs = await import("node:fs");

  // 17a. Verify all 12 Hard Surface Decal Stamps in StarterCatalogBridge
  const stamps = listStampStarterCatalog();
  const hardSurfaceStamps = stamps.filter((s) => s.group === "Hard Surface");
  assert.equal(hardSurfaceStamps.length, 12, `Expected 12 Hard Surface decal stamps, got ${hardSurfaceStamps.length}`);
  const expectedHardIds = [
    "stamp-hs-hex-bolt",
    "stamp-hs-countersunk-screw",
    "stamp-hs-access-hatch",
    "stamp-hs-louver-vent",
    "stamp-hs-honeycomb-grille",
    "stamp-hs-cam-latch",
    "stamp-hs-serial-dataplate",
    "stamp-hs-hydraulic-port",
    "stamp-hs-rivet-seam",
    "stamp-hs-warning-placard",
    "stamp-hs-gusset-bracket",
    "stamp-hs-knurled-dial",
  ];
  for (const id of expectedHardIds) {
    const entry = hardSurfaceStamps.find((s) => s.id === id);
    assert(entry, `Missing Hard Surface decal stamp ${id}`);
    const doc = loadStarterAsArtworkDocument(entry);
    assert(doc.layers.length >= 2, `Expected multi-layer hard-surface stack for ${id}`);
    const svg = getArtworkPreviewSvg(entry, "stamp", 64);
    assert(svg.includes("<svg") && !svg.includes('width="512" height="512" fill="#'), `Stamp preview ${id} must remain background-free transparent SVG`);
    const evalOut = evaluateArtworkDocument(doc, 128);
    assert.equal(evalOut.width, 128);
  }

  // 17b. Verify Baked Transparent PBR Material Sphere pixels (alpha === 0 at corners, alpha === 255 at center)
  const testMat = materials.find((m) => m.id === "emerald-pcb-copper-circuit") || materials[0];
  const spherePx = renderAnalyticalSpherePixels(testMat, 64);
  assert.equal(spherePx.data.length, 64 * 64 * 4);
  assert.equal(spherePx.data[3], 0, "Corner pixel (0,0) of baked sphere icon must be 100% transparent (alpha=0)");
  assert.equal(spherePx.data[(63 * 64 + 63) * 4 + 3], 0, "Bottom-right corner pixel of baked sphere icon must be transparent (alpha=0)");
  const centerIdx = (32 * 64 + 32) * 4;
  assert.equal(spherePx.data[centerIdx + 3], 255, "Center pixel of baked sphere icon must be opaque (alpha=255)");
  assert(spherePx.data[centerIdx] + spherePx.data[centerIdx + 1] + spherePx.data[centerIdx + 2] > 20, "Center pixel must contain shaded RGB output");
  const bakedUrl = getBakedMaterialIcon(testMat);
  assert(typeof bakedUrl === "string" && bakedUrl.startsWith("data:image/png"), "getBakedMaterialIcon must return PNG data URL");

  // 17c. Verify Floating 3-Pane Asset Browser Structure, Square No-Box Cards, Grid/List Toggle & Tile Inspector
  api.switchWorkspace("material");
  const drawer = globalThis.document.querySelector(".asset-browser");
  assert(drawer, "Missing .asset-browser element");
  if (api.getAssetBrowser().userSpecifiedCategory) {
    drawer.querySelector(".cb-auto-pill")?.click();
  }
  assert.equal(api.getAssetBrowser().activeCategory, "materials", "Materials category should be active after switching to Material workspace");

  // Left Pane: TreeView with expandable branches, folder buttons (.cb-family-btn) & attribute quick filters
  const treePane = drawer.querySelector(".cb-tree-pane");
  assert(treePane, "Missing left .cb-tree-pane TreeView");
  assert(treePane.querySelectorAll(".cb-tree-branch").length >= 4, "TreeView must display category branches");
  assert(treePane.querySelectorAll(".cb-family-btn").length >= 5, "TreeView must render nested category folders");
  const filterPills = treePane.querySelectorAll(".cb-filter-pill");
  assert(filterPills.length >= 3, "TreeView must provide Quick Attribute Filters");

  // Center Pane: Grid Cards must have .cb-asset-icon (NO .cb-thumb box) and .cb-card-name
  assert.equal(drawer.querySelectorAll(".cb-thumb").length, 0, "Grid cards must NOT have thumbnail background boxes (.cb-thumb)");
  const gridEl = drawer.querySelector(".cb-grid");
  assert(gridEl, "Asset Browser must open in .cb-grid view mode");
  const firstMatCard = gridEl.querySelector(".cb-card");
  assert(firstMatCard && firstMatCard.querySelector(".cb-asset-icon") && firstMatCard.querySelector(".cb-card-name"), "Grid card must render transparent .cb-asset-icon + .cb-card-name");
  const matImgEl = firstMatCard.querySelector(".cb-mat-baked-img");
  assert(matImgEl && String(matImgEl.tagName).toLowerCase() === "img", "Material grid card must render baked <img> sphere icon");

  // Toggle List <-> Grid View
  const viewBtns = [...drawer.querySelectorAll(".cb-view-btn")];
  const listBtn = viewBtns.find((b) => b.dataset.view === "list");
  const gridBtn = viewBtns.find((b) => b.dataset.view === "grid");
  assert(listBtn && gridBtn, "Missing Grid/List view toggle buttons");
  listBtn.click();
  assert(drawer.querySelector(".cb-list"), "Clicking List toggle must switch center pane to .cb-list");
  assert.equal(api.getAssetBrowser().viewMode, "list");
  gridBtn.click();
  assert(drawer.querySelector(".cb-grid"), "Clicking Grid toggle must restore .cb-grid");
  assert.equal(api.getAssetBrowser().viewMode, "grid");

  // Right Pane: MetricTrio-style value tiles (.cb-inspector .cb-insp-tiles -> .cb-insp-prop)
  const inspTiles = drawer.querySelector(".cb-inspector .cb-insp-tiles");
  assert(inspTiles, "Asset Inspector must render Material-style .kpi-trio.cb-insp-tiles");
  const propTiles = inspTiles.querySelectorAll(".cb-insp-prop");
  assert(propTiles.length >= 4, "Asset Inspector must render >=4 MetricTrio-style value tiles");
  assert(propTiles[0].className.includes("kpi-card"), "Asset Inspector tiles must share .kpi-card styling");
  assert(propTiles[0].querySelector(".kpi-label") && propTiles[0].querySelector(".kpi-val") && propTiles[0].querySelector(".kpi-sub"),
    "Each Asset Inspector tile must render .kpi-label, .kpi-val & .kpi-sub matching Material Inspector");

  // Filter Stamps -> Hard Surface folder via TreeView & inspect a Hard Surface Decal Stamp
  const stampCatBtn = [...drawer.querySelectorAll(".cb-cat-tab")].find((b) => b.dataset.category === "stamps");
  assert(stampCatBtn, "Missing stamps category tab");
  stampCatBtn.click();
  const hardFolderBtn = [...drawer.querySelectorAll(".cb-family-btn")].find((b) => b.dataset.family === "Hard Surface");
  assert(hardFolderBtn, "TreeView must include Hard Surface folder under Decal Stamps");
  hardFolderBtn.click();
  const visibleStampCards = drawer.querySelectorAll(".cb-grid .cb-card");
  assert.equal(visibleStampCards.length, 12, `Selecting Hard Surface folder should show 12 stamps, got ${visibleStampCards.length}`);
  const boltCard = [...visibleStampCards].find((c) => c.dataset.assetId === "stamp-hs-hex-bolt");
  assert(boltCard, "Hex Bolt stamp card missing from Hard Surface folder");
  boltCard.click();
  assert(drawer.querySelector(".cb-insp-title")?.textContent.includes("Hex Bolt & Machined Washer"), "Inspector must display clicked Hard Surface stamp");

  // 17d. Verify CSS Floating Overlay + Square Card rules in TextureStudio.css
  const cssText = fs.readFileSync(new URL("./TextureStudio.css", import.meta.url), "utf8");
  assert(cssText.includes("position: fixed;") && cssText.includes(".asset-browser"), "CSS must position .asset-browser as a fixed floating pull-up overlay");
  assert(cssText.includes("aspect-ratio: 1 / 1;"), "CSS must keep .cb-card 1:1 square");
  assert(cssText.includes(".cb-asset-icon") && cssText.includes("background: transparent;"), "CSS must keep .cb-asset-icon transparent with no box behind icon");
});

await suite("18. Reference UI Alignment: Material Look-Dev Stage, Shader Ball Pedestal Icons & Circular Olive Studio", async () => {
  const api = globalThis.window.__frontierTextureStudio;
  const { createShaderBallPedestalGeometry, renderAnalyticalSpherePixels } = await import("./MaterialIconBaker.js");
  const fs = await import("node:fs");

  // 18a. Material Workspace Stage (Image 3 / image-5.png)
  api.switchWorkspace("material");
  const mwStage = globalThis.document.querySelector(".mw-stage");
  assert(mwStage, "Material workspace must render .mw-stage look-dev viewport");
  assert(!globalThis.document.querySelector(".mw-stage .viewport-toolbar"), "Material stage must NOT render cluttered top .viewport-toolbar");
  const mwHero = mwStage.querySelector(".mw-stage-hero");
  const mwKicker = mwHero?.querySelector(".mw-hero-kicker");
  const mwTitle = mwHero?.querySelector(".mw-hero-title");
  const mwDesc = mwHero?.querySelector(".mw-hero-desc");
  assert(mwHero && mwKicker && mwTitle && mwDesc, "Material stage must display floating top-left kicker, title & description");
  assert(/^\d{2}\s+\/\s+.+COLLECTION/i.test(mwKicker.textContent), `Expected 'NN / <CAT> COLLECTION' kicker, got '${mwKicker.textContent}'`);

  const zoomPct = mwStage.querySelector(".mw-zoom-pct");
  const zoomBtns = mwStage.querySelectorAll(".mw-zoom-btn");
  const resetBtn = mwStage.querySelector(".mw-reset-link");
  assert(zoomPct && zoomBtns.length === 2 && resetBtn, "Material bottom dock must render [- pct +] zoom pill and Reset view");
  const initialPct = parseInt(zoomPct.textContent, 10);
  zoomBtns[1].click();
  assert(parseInt(zoomPct.textContent, 10) > initialPct, "Zoom + button must increase viewport zoom readout");
  resetBtn.click();
  assert.equal(zoomPct.textContent, "62%", "Reset view must restore default 62% hero camera framing");

  const selectPills = mwStage.querySelectorAll(".mw-select-pill");
  assert.equal(selectPills.length, 2, "Material bottom dock must provide geometry & studio environment selector pills");
  assert(selectPills[1].querySelectorAll("option").some((o) => o.textContent === "Studio softbox"), "Default backdrop list must include 'Studio softbox'");

  const pedGeo = createShaderBallPedestalGeometry(1.52);
  assert(pedGeo?.attributes?.position?.count > 100, "createShaderBallPedestalGeometry must generate beveled pedestal lathe geometry");
  const px = renderAnalyticalSpherePixels(materials[0], 88);
  const pedRow = Math.round(88 * 0.445 + 88 * 0.375 * 0.94);
  const pedCenterAlpha = px.data[(pedRow * 88 + 44) * 4 + 3];
  assert(pedCenterAlpha > 200, "Analytical Shader Ball baker must render beveled pedestal silhouette beneath sphere");

  // 18b. Asset Browser Filter Chips, Collection Bar & Shader Ball Check Cards (Image 2 / image-4.png)
  const drawer = globalThis.document.querySelector(".asset-browser");
  if (api.getAssetBrowser().userSpecifiedCategory) drawer.querySelector(".cb-auto-pill")?.click();
  const famStrip = drawer.querySelector(".cb-family-pills");
  assert(famStrip && famStrip.querySelectorAll(".cb-family-chip").length >= 4, "Asset Browser must render rounded family filter pills strip");
  const colBar = drawer.querySelector(".cb-collection-bar");
  assert(colBar?.querySelector(".cb-collection-title")?.textContent === "THE ESSENTIAL COLLECTION", "Asset Browser must show THE ESSENTIAL COLLECTION header");
  const matCard = drawer.querySelector(".cb-grid .cb-card");
  assert(matCard?.querySelector(".cb-card-check") && matCard?.querySelector(".cb-card-sub"), "Material card must render selection check badge and category subtitle");

  // 18c. Pattern, Stamp & Texture Paint Circular Tool Dock, Clean Header (No Clutter Text) & Floating Viewport Zoom
  for (const ws of ["pattern", "stamp", "paint"]) {
    api.switchWorkspace(ws);
    const metaLine = globalThis.document.querySelector(".vp-hero-meta-line");
    const toolsBar = globalThis.document.querySelector(".vp-tools-bar");
    const moreBtn = toolsBar?.querySelector(".vp-more-tools-btn");
    const moreMenu = toolsBar?.querySelector(".vp-more-tools-menu");
    const stageShell = globalThis.document.querySelector(".se-stage-shell");
    assert(!metaLine || metaLine.hidden === true, `${ws} header must omit unnecessary eyebrow/draft clutter`);
    assert(!stageShell?.querySelector(".se-stage-topbar"), `${ws} stage must not render unnecessary VECTOR/PAINT CANVAS topbar clutter`);
    assert(toolsBar.querySelectorAll(".vp-tool-circle").length >= 6, `${ws} Band 2 must render circular icon tool buttons`);
    assert(moreBtn && moreMenu && moreMenu.hidden === true, `${ws} Band 2 must include collapsed 'More tools' popover`);
    moreBtn.click();
    assert.equal(moreMenu.hidden, false, `Clicking 'More tools' in ${ws} must expand secondary tool menu`);
    moreBtn.click();
    assert(stageShell?.querySelector(".se-stage-footer"), `${ws} stage must render bottom shortcut/selection footer`);
    if (ws !== "paint") {
      const seVp = stageShell?.querySelector(".se-viewport");
      const zoomToolbar = seVp?.querySelector(".se-view-controls");
      assert(zoomToolbar, `${ws} must mount zoom controls (.se-view-controls) directly on .se-viewport`);
    }
  }

  // Verify viewport + circular tool + look-dev stage CSS selectors exist
  const cssText = fs.readFileSync(new URL("./TextureStudio.css", import.meta.url), "utf8");
  for (const token of [".mw-stage", ".mw-bottom-dock", ".vp-tool-circle", ".vp-more-tools-btn", ".se-view-controls", ".se-stage-footer", ".cb-family-pills", ".cb-collection-bar"]) {
    assert(cssText.includes(token), `TextureStudio.css missing required selector ${token}`);
  }
});

await suite("19. Look-Dev Polish: Right-Tabbed Strata, Seated Cloth/Pedestal + Grey Floor, Hero Asset Card & Sharp Layer Stacks", async () => {
  const api = globalThis.window.__frontierTextureStudio;
  const fs = await import("node:fs");

  // 19a. Material Workspace: NOTHING on the left + [Properties | Layer Stack] tabs in right Material Inspector
  api.switchWorkspace("material");
  const grid = globalThis.document.querySelector(".studio-grid");
  assert(grid?.classList.contains("studio-grid-2col"), "Material Workspace must use .studio-grid-2col with no left sidebar");
  const cols = grid ? grid.children.filter((c) => c.classList.contains("studio-column")) : [];
  assert.equal(cols.length, 2, `Material Workspace must have exactly 2 columns (viewport + right inspector), got ${cols.length}`);
  assert(cols[0].classList.contains("viewport-column"), "First column in Material Workspace must be the full-bleed viewport (no left panel)");

  const rightInspector = cols[1];
  const summaryHost = rightInspector.querySelector(".mw-inspector-summary");
  const tabsHost = rightInspector.querySelector(".mw-inspector-tabs");
  const scrollInner = rightInspector.querySelector(".panel-scroll .panel-scroll-inner");
  assert(summaryHost && tabsHost && scrollInner, "Material Inspector must expose .mw-inspector-summary above .mw-inspector-tabs");
  assert(summaryHost.querySelector(".kpi-trio") && summaryHost.querySelector(".object-heading"), "Material Inspector summary must hold .object-heading and .kpi-trio cards");
  assert(scrollInner.children.indexOf(summaryHost) < scrollInner.children.indexOf(tabsHost), "Material Inspector [Properties | Layer Stack] tabs must sit BELOW the summary KPI card");
  const inspTabs = rightInspector.querySelectorAll(".mw-insp-tab");
  const propTab = inspTabs[0];
  const layersTab = inspTabs[1];
  const propPane = rightInspector.querySelector(".mw-prop-pane");
  const layersPane = rightInspector.querySelector(".mw-layers-pane");
  assert(propTab && layersTab && propPane && layersPane, "Material Inspector must expose [Properties | Layer Stack] tabs and panes");
  assert.equal(propPane.hidden, false, "Properties tab pane must be visible initially");
  assert.equal(layersPane.hidden, true, "Layer Stack tab pane must start hidden until selected");
  assert(layersPane.querySelector(".mw-strata-title")?.textContent.includes("Material Scene & Strata"), "Layer Stack tab must house Material Scene & Strata");

  layersTab.click();
  assert.equal(layersPane.hidden, false, "Clicking Layer Stack tab must reveal Material Scene & Strata");
  assert.equal(propPane.hidden, true, "Clicking Layer Stack tab must hide Properties pane");
  propTab.click();
  assert.equal(propPane.hidden, false, "Clicking Properties tab must switch back to Properties pane");

  // Verify switching to Draped Cloth fits cleanly above the pedestal without error
  const selectPills = globalThis.document.querySelectorAll(".mw-select-pill");
  selectPills[0].value = "cloth";
  selectPills[0].dispatchEvent({ type: "change" });
  assert.equal(api.getSnapshot().shape, "cloth", "Selecting cloth must update workspace geometry to Draped Cloth");
  selectPills[0].value = "shaderball";
  selectPills[0].dispatchEvent({ type: "change" });

  // 19b. Asset Browser Inspector: NO color palette swatches + Large Hero Preview Card above tiles
  const drawer = globalThis.document.querySelector(".asset-browser");
  const insp = drawer?.querySelector(".cb-inspector");
  assert(insp, "Asset Browser must render right-hand .cb-inspector");
  assert(!insp.querySelector(".cb-insp-swatches"), "Asset Browser Inspector must NOT render .cb-insp-swatches color palette section");
  const heroCard = insp.querySelector(".cb-insp-hero");
  const heroStage = heroCard?.querySelector(".cb-insp-hero-stage");
  assert(heroCard && heroStage, "Asset Browser Inspector must render large .cb-insp-hero showcase card");
  const tiles = insp.querySelector(".cb-insp-tiles");
  assert(tiles && insp.children.indexOf(heroCard) < insp.children.indexOf(tiles), "Hero preview card must sit directly above .cb-insp-tiles");

  // 19c. Sharp Square Layer Stacks + Exact Unmodified AlloyStudio 3-part Pedestal, Support Ball & Grey Fading Floor checks
  const mwSrc = fs.readFileSync(new URL("./Workspaces/MaterialWorkspace.js", import.meta.url), "utf8");
  assert(mwSrc.includes("CylinderGeometry(1.2, 1.26, 0.18, 128)") && mwSrc.includes("standBottom") && mwSrc.includes("standRing"), "MaterialWorkspace must use AlloyStudio 3-part pedestal (stand + standBottom + standRing)");
  assert(mwSrc.includes("supportBall") && mwSrc.includes("clothSupport"), "MaterialWorkspace must include the cloth inner supportBall from clothSupport");
  assert(mwSrc.includes("studioFloor") && mwSrc.includes("PlaneGeometry"), "MaterialWorkspace 3D scene must include a fading grey PlaneGeometry studioFloor");

  const cssText = fs.readFileSync(new URL("./TextureStudio.css", import.meta.url), "utf8");
  for (const rule of [".stack-row", ".stack-row-thumb", ".tp-layer-row", ".tp-layer-thumbnail", ".tp-mask-row", ".tp-mask-thumb", ".tp-decal-tree-row", ".tp-decal-tree-thumb"]) {
    const idx = cssText.indexOf(`${rule} {`);
    const altIdx = idx !== -1 ? idx : cssText.indexOf(`${rule},\n`);
    assert(altIdx !== -1, `Expected rule ${rule} in TextureStudio.css`);
    const block = cssText.slice(altIdx, cssText.indexOf("}", altIdx) + 1);
    assert(block.includes("border-radius: 0;"), `${rule} must have crisp square corners (border-radius: 0;)`);
  }
});

await suite("20. Pattern & Stamp Curve/Pen/Multi-Stop Gradients, Texture Paint Shift-Line/3D Curve/3D Gradient & Material Cloth/Support Ball", async () => {
  const {
    automaticPathNodes,
    pathNodesToSvgD,
    nodesToNormalizedCurveData,
    normalizedCurveToCanvasNodes,
    normalizeGradientStops,
    createArtworkDocument,
    createArtworkLayer,
    artworkDocumentToSvgString,
    evaluateArtworkDocument,
  } = await import("./Engine/Patterns/ArtworkStackEngine.js");
  const {
    createPaintDocument,
    buildMeshPaintIndex,
    buildDefaultMeshCues,
    sampleMeshSurfaceSpline3D,
    normalizePointGradient3D,
    evaluatePointGradient3DAt,
    applyPointGradient3DToLayer,
  } = await import("./Engine/TexturePaintEngine.js");
  const { clothSupport } = await import("./Engine/Materials/clothGeometry.js");
  const studio = globalThis.window.__frontierTextureStudio;

  // 20a. Engine-level Centripetal Catmull-Rom Curve + Multi-Stop Gradient SVG/PBR verification
  const rawNodes = automaticPathNodes([
    { x: 100, y: 120 },
    { x: 260, y: 80 },
    { x: 380, y: 240 },
    { x: 180, y: 360 },
  ], true, 0.5);
  assert.equal(rawNodes.length, 4);
  const svgD = pathNodesToSvgD(rawNodes, true);
  assert(svgD.startsWith("M ") && svgD.includes(" C ") && svgD.endsWith(" Z"), "Curve path must emit closed smooth cubic Bezier SVG path");
  const normCurve = nodesToNormalizedCurveData(rawNodes, true, 0.5);
  assert.equal(normCurve.closedPath, true);
  assert.equal(normCurve.pathNodes.length, 4);
  const backNodes = normalizedCurveToCanvasNodes(normCurve.pathNodes, normCurve.transform);
  assert.equal(backNodes.length, 4);

  const normStops = normalizeGradientStops([
    { id: "s0", offset: 0, color: "#ff3366", opacity: 1 },
    { id: "s1", offset: 0.45, color: "#ffdd44", opacity: 0.9 },
    { id: "s2", offset: 1, color: "#1177ff", opacity: 1 },
  ]);
  assert.equal(normStops.length, 3);
  const patDoc = createArtworkDocument("pattern");
  patDoc.name = "MultiStop Curve Test";
  patDoc.background = {
    enabled: true,
    color: "#171a1f",
    opacity: 1,
    finish: "ink",
    roughness: 0.7,
    metalness: 0,
    height: 0,
    fillMode: "linear",
    gradientAngle: 45,
    fillStops: normStops,
  };
  patDoc.layers = [
    createArtworkLayer("motif", {
      name: "MultiStop Curve",
      transform: normCurve.transform,
      shape: {
        motifType: "svg-path",
        svgPath: normCurve.svgPath,
        pathNodes: normCurve.pathNodes,
        closedPath: normCurve.closedPath,
        fillMode: "radial",
        fillStops: normStops,
        strokeMode: "linear",
        strokeStops: normStops,
        strokeWidth: 4,
      },
    }),
  ];
  const patSvg = artworkDocumentToSvgString(patDoc, { width: 256, height: 256 });
  assert(patSvg.includes("<linearGradient") && patSvg.includes("<radialGradient"), "Multi-stop Pattern SVG export must emit linearGradient & radialGradient paint servers");
  assert(patSvg.includes("#ffdd44") && patSvg.includes("45.0%"), "Multi-stop SVG must preserve intermediate stop color & offset");
  const evalTex = evaluateArtworkDocument(patDoc, 128);
  assert.equal(evalTex.albedo.length, 128 * 128 * 4, "Multi-stop Curve Pattern must evaluate cleanly to 128x128 PBR buffers");

  // 20b. Interactive Pattern & Stamp Studio Curve (U), Pen (P), Point Edit (A) + Multi-Stop Gradients UI
  studio.switchWorkspace("pattern");

  const patCircles = [...globalThis.document.querySelectorAll(".vp-tool-circle")];
  assert(patCircles.length >= 8, `Pattern primary dock must expose Curve tool alongside existing tools (got ${patCircles.length})`);
  const btnText = (b) => `${b.title || ""} ${b.textContent || ""}`;
  const patSeBtns = [...globalThis.document.querySelectorAll(".se-tool-btn")];
  const curveCircleBtn = [...patCircles, ...patSeBtns].find((b) => /\bCurve\b/i.test(btnText(b)));
  assert(curveCircleBtn, "Pattern Studio toolbar must provide Curve (U) tool button");
  const penBtn = [...patCircles, ...patSeBtns].find((b) => /\bPen\b/i.test(btnText(b)));
  assert(penBtn, "Pattern Studio toolbar must provide Pen (P) tool button");

  // Place 3 points via Curve tool on SVG canvas and commit with Enter
  const patLayersBefore = studio.getSnapshot().layers;
  curveCircleBtn.click();
  const seSvg = globalThis.document.querySelector(".se-canvas");
  assert(seSvg, "Pattern Studio must mount .se-canvas");
  seSvg.getBoundingClientRect = () => ({ left: 0, top: 0, width: 512, height: 512 });
  for (const [cx, cy] of [[120, 140], [340, 120], [280, 340]]) {
    seSvg.dispatchEvent({ type: "pointerdown", button: 0, clientX: cx, clientY: cy, shiftKey: false, preventDefault() {}, stopPropagation() {} });
    globalThis.window.dispatchEvent({ type: "pointerup", clientX: cx, clientY: cy });
  }
  globalThis.window.dispatchEvent({ type: "keydown", key: "Enter", preventDefault() {}, stopPropagation() {} });
  const patLayers = studio.getSnapshot().layers;
  assert(patLayers > patLayersBefore, `Committing Pattern Curve path must add an authored Curve Path layer (before=${patLayersBefore}, after=${patLayers})`);

  // Verify Stamp Studio also has Curve tool and multi-stop fill controls
  studio.switchWorkspace("stamp");
  const stampCurveBtn = [...globalThis.document.querySelectorAll(".vp-tool-circle"), ...globalThis.document.querySelectorAll(".se-tool-btn")].find((b) => /\bCurve\b/i.test(btnText(b)));
  assert(stampCurveBtn, "Stamp Studio toolbar must provide Curve (U) tool button");
  const linearChip = [...globalThis.document.querySelectorAll(".segmented-track")].flatMap((t) => [...t.children]).find((b) => /Linear Grad/i.test((b.textContent || "").trim()));
  assert(linearChip, "Pattern/Stamp inspector must expose Multi-Stop Gradient Solid / Linear Grad / Radial Grad segmented buttons");
  linearChip.click();
  const addStopBtn = [...globalThis.document.querySelectorAll(".chip-btn")].find((b) => /\+ Add Fill Stop/i.test(b.textContent || ""));
  assert(addStopBtn, "Switching to Linear Grad Fill Source must reveal '+ Add Fill Stop' multi-stop editor button");
  addStopBtn.click();
  assert(globalThis.document.querySelectorAll(".se-gradient-stop-row").length >= 3, "Clicking '+ Add Fill Stop' must append a 3rd editable stop row");

  // 20c. Texture Paint 3D Surface Spline + 3D Object-Space Point Gradient Engine Checks
  const sphereGeo = new THREE.SphereGeometry(1.15, 24, 16);
  const cuesIdx = buildMeshPaintIndex(sphereGeo);
  const cues = buildDefaultMeshCues(sphereGeo, cuesIdx, 128);
  assert(cues.worldPos && cues.sphereRadius > 0.5, "buildDefaultMeshCues must expose worldPos & sphereRadius for 3D point gradients");
  const splinePts = sampleMeshSurfaceSpline3D(
    [
      { pos: [0, 1.15, 0], normal: [0, 1, 0] },
      { pos: [1.15, 0, 0], normal: [1, 0, 0] },
      { pos: [0, -1.15, 0], normal: [0, -1, 0] },
    ],
    false,
    0.5,
    8,
  );
  assert(splinePts.length >= 10, "sampleMeshSurfaceSpline3D must generate smooth dense surface points across 3D anchors");

  const pGrad = normalizePointGradient3D({
    points: [
      { id: "gA", position: [-1, 0, 0], color: "#ff2020", radius: 1.5, weight: 1 },
      { id: "gB", position: [1, 0, 0], color: "#2020ff", radius: 1.5, weight: 1 },
    ],
    background: "#202020",
    falloff: 1.0,
  });
  const midRgb = evaluatePointGradient3DAt(pGrad, [0, 0, 0]);
  assert(midRgb[0] > 0.25 && midRgb[2] > 0.25, "3D Point Gradient midpoint between Red (-X) and Blue (+X) must blend both in linear RGB space");

  const paintDoc = createPaintDocument(128);
  const targetPaintLayer = paintDoc.layers.find((l) => l.kind === "paint") || paintDoc.layers[0];
  const gradOk = applyPointGradient3DToLayer({
    layer: targetPaintLayer,
    cues,
    grad: pGrad,
    brush: { fillChannels: { albedo: true, roughness: true, metalness: true, height: true }, roughness: 0.45, metalness: 0.1 },
    editingMask: false,
  });
  assert(gradOk, "applyPointGradient3DToLayer must succeed cleanly on Texture Paint layer");
  let paintedPx = 0;
  for (let i = 0; i < 128 * 128; i++) if (targetPaintLayer.pixels.albedo[i * 4 + 3] > 200) paintedPx++;
  assert(paintedPx > 128 * 64, "3D Point Gradient must paint dense texels across mesh UV layout");

  // 20d. Texture Paint Workspace Live Interactions (Shift straight line, 3D Curve U, 3D Gradient D)
  studio.switchWorkspace("paint");
  const tpCurveBtn = [...globalThis.document.querySelectorAll(".tp-tool-group .pill-btn")].find((b) => /Curve/i.test(b.textContent || ""));
  const tpGradBtn = [...globalThis.document.querySelectorAll(".tp-tool-group .pill-btn")].find((b) => /Gradient/i.test(b.textContent || ""));
  assert(tpCurveBtn && tpGradBtn, "Texture Paint Band 2 must include 3D Curve and 3D Gradient buttons");

  const uvCanvas = globalThis.document.querySelector(".tp-uv-pane");
  assert(uvCanvas, "Texture Paint must render .tp-uv-pane 2D UV canvas");
  uvCanvas.getBoundingClientRect = () => ({ left: 0, top: 0, width: 512, height: 512 });
  // Shift + click straight line across 2D UV pane
  uvCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 64, clientY: 256, shiftKey: false, preventDefault() {} });
  globalThis.window.dispatchEvent({ type: "pointerup", clientX: 64, clientY: 256 });
  uvCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 448, clientY: 256, shiftKey: true, preventDefault() {} });
  globalThis.window.dispatchEvent({ type: "pointerup", clientX: 448, clientY: 256 });

  // Tool shortcuts u -> curve, d -> gradient
  globalThis.window.dispatchEvent({ type: "keydown", key: "u", preventDefault() {} });
  assert.equal(studio.getSnapshot().tool, "curve", "Shortcut 'u' must switch Texture Paint to 3D Curve tool");
  uvCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 128, clientY: 160, shiftKey: false, preventDefault() {} });
  globalThis.window.dispatchEvent({ type: "pointerup", clientX: 128, clientY: 160 });
  uvCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 256, clientY: 320, shiftKey: false, preventDefault() {} });
  globalThis.window.dispatchEvent({ type: "pointerup", clientX: 256, clientY: 320 });
  uvCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 384, clientY: 160, shiftKey: false, preventDefault() {} });
  globalThis.window.dispatchEvent({ type: "pointerup", clientX: 384, clientY: 160 });
  globalThis.window.dispatchEvent({ type: "keydown", key: "Enter", preventDefault() {} });

  globalThis.window.dispatchEvent({ type: "keydown", key: "d", preventDefault() {} });
  assert.equal(studio.getSnapshot().tool, "gradient", "Shortcut 'd' must switch Texture Paint to 3D Gradient tool");
  const applyGradBtn = [...globalThis.document.querySelectorAll(".action-button")].find((b) => /Apply 3D Gradient/i.test(b.textContent || ""));
  assert(applyGradBtn, "Active Tool = Gradient must display 'Apply 3D Gradient' action in inspector");
  applyGradBtn.click();

  // 20e. Material Workspace Draped Cloth inner supportBall & clothSupport bounds check
  assert.equal(clothSupport.radius, 1.16, "clothSupport radius must match unmodified AlloyStudio sphere radius 1.16");
  assert.deepEqual(clothSupport.center, [0, 1.39, 0], "clothSupport center must match unmodified AlloyStudio [0, 1.39, 0]");
  studio.switchWorkspace("material");
  const matSelect = globalThis.document.querySelector(".mw-select-pill");
  matSelect.value = "cloth";
  matSelect.dispatchEvent({ type: "change" });
  assert.equal(studio.getSnapshot().shape, "cloth");

  sphereGeo.dispose();
});

await suite("21. Multi-Channel Colour/Emissive 3D Space Gradient, Scene-Space Point Gradient (Pattern/Stamp) & Removal of Redundant Bottom Previews", async () => {
  const {
    createDefaultPointGradient2D,
    evaluatePointGradient2DAt,
    rasterizePointGradient2D,
    createArtworkDocument,
    createArtworkLayer,
    artworkDocumentToSvgString,
    evaluateArtworkDocument,
  } = await import("./Engine/Patterns/ArtworkStackEngine.js");
  const {
    EXTRA_COLOR_KEYS,
    ALL_PAINT_CHANNEL_ROWS,
    createPaintDocument,
    createPaintLayer,
    buildMeshPaintIndex,
    buildDefaultMeshCues,
    applyPointGradient3DToLayer,
    compositePaintDocument,
  } = await import("./Engine/TexturePaintEngine.js");
  const studio = globalThis.window.__frontierTextureStudio;

  // 21a. Texture Paint 3D Space Gradient multi-channel colour + emissive engine verification
  assert.equal(EXTRA_COLOR_KEYS.length, 3, "EXTRA_COLOR_KEYS must expose specularColor, sheenColor, attenuationColor");
  assert.equal(ALL_PAINT_CHANNEL_ROWS.length, 22, "ALL_PAINT_CHANNEL_ROWS must include all 19 core PBR + 3 extra colour channels");

  const sphere = new THREE.SphereGeometry(1, 20, 14);
  const idx = buildMeshPaintIndex(sphere);
  const cues = buildDefaultMeshCues(sphere, idx, 128);
  const pDoc = createPaintDocument(128);
  const pLayer = createPaintLayer("paint", { res: 128, name: "MultiColGrad" });
  pDoc.layers.push(pLayer);
  pDoc.selectedLayerId = pLayer.id;

  const gradDef = {
    falloff: 1.0,
    background: "#101820",
    points: [
      { id: "c1", position: [1, 0, 0], color: "#ff3300", radius: 2.0, weight: 1.0 },
      { id: "c2", position: [-1, 0, 0], color: "#0099ff", radius: 2.0, weight: 1.0 },
    ],
  };
  const multiBrush = {
    tool: "gradient",
    opacity: 1,
    gradientChannels: {
      albedo: true,
      roughness: false,
      metalness: false,
      height: false,
      emissive: true,
      specularColor: true,
      sheenColor: true,
      attenuationColor: true,
    },
    extValues: { emissiveIntensity: 2.0, sheen: 0.9, transmission: 0.8 },
  };
  assert.equal(applyPointGradient3DToLayer({ layer: pLayer, cues, grad: gradDef, brush: multiBrush }), true);
  assert.equal(pLayer.channels.emissive, true, "Applying 3D gradient with emissive enabled must auto-enable emissive channel on layer");
  assert.equal(pLayer.channels.emissiveIntensity, true, "Applying 3D gradient with emissive enabled must auto-enable emissiveIntensity on layer");
  assert.equal(pLayer.channels.specularColor, true, "Applying 3D gradient with specularColor must activate specularColor channel");
  assert.equal(pLayer.channels.sheenColor, true, "Applying 3D gradient with sheenColor must activate sheenColor channel");
  assert.equal(pLayer.channels.sheen, true, "Applying 3D gradient with sheenColor must auto-enable sheen scalar");
  assert.equal(pLayer.channels.attenuationColor, true, "Applying 3D gradient with attenuationColor must activate attenuationColor channel");
  assert.equal(pLayer.channels.transmission, true, "Applying 3D gradient with attenuationColor must auto-enable transmission scalar");

  let nonZeroEmissive = 0;
  let nonZeroSpecCol = 0;
  for (let i = 0; i < 128 * 128; i++) {
    const i3 = i * 3;
    if (pLayer.pixels.extValues.emissive[i3] + pLayer.pixels.extValues.emissive[i3 + 2] > 20) nonZeroEmissive++;
    if (pLayer.pixels.extValues.specularColor[i3] + pLayer.pixels.extValues.specularColor[i3 + 2] > 20) nonZeroSpecCol++;
  }
  assert(nonZeroEmissive > 128 * 48, `3D Space Gradient must populate emissive RGB texels (got ${nonZeroEmissive})`);
  assert(nonZeroSpecCol > 128 * 48, `3D Space Gradient must populate specularColor RGB texels (got ${nonZeroSpecCol})`);

  const comp = compositePaintDocument(pDoc, cues);
  let compEmissiveSum = 0;
  for (let i = 0; i < comp.emissive.length; i++) compEmissiveSum += comp.emissive[i];
  assert(compEmissiveSum > 10000, "compositePaintDocument must composite emissive RGB buffer from 3D Space Gradient");

  // 21b. Texture Paint UI channel toggle controls for 3D Space Gradient
  studio.switchWorkspace("paint");
  globalThis.window.dispatchEvent({ type: "keydown", key: "d", preventDefault() {} });
  assert.equal(studio.getSnapshot().tool, "gradient");
  const emChip = [...globalThis.document.querySelectorAll(".chip-btn")].find((b) => (b.textContent || "").trim() === "Emissive");
  assert(emChip, "Texture Paint 3D Gradient inspector must expose 'Emissive' quick channel toggle chip");
  emChip.click();
  assert(emChip.className.includes("active") || [...globalThis.document.querySelectorAll(".chip-btn")].find((b) => (b.textContent || "").trim() === "Emissive").className.includes("active"),
    "Clicking Emissive channel toggle on 3D Space Gradient must activate Emissive");

  // Verify Emissive Glow Power appears cleanly once Emissive is enabled in 3D Gradient Inspector
  const propLabels = [...globalThis.document.querySelectorAll(".property-label")].map((el) => el.textContent || "");
  assert(propLabels.some((t) => t.includes("Emissive Glow Power")), "Enabling Emissive on 3D Space Gradient must surface Emissive Glow Power slider");
  assert(propLabels.some((t) => t.includes("Target Colour & PBR Channels")), "3D Space Gradient inspector must expose full Target Colour & PBR Channels selector");

  // 21c. Pattern & Stamp Engine Scene-Space Point Gradient (follows scene coordinates across transforms)
  const ptGrad = createDefaultPointGradient2D("#ff2244", "#1188ff", 512);
  ptGrad.points[0] = { id: "p0", x: 160, y: 200, color: "#ff2244", radius: 220, weight: 1.0 };
  ptGrad.points[1] = { id: "p1", x: 360, y: 320, color: "#1188ff", radius: 220, weight: 1.0 };
  const cLeft = evaluatePointGradient2DAt(160, 200, ptGrad, { size: 512, seamless: true });
  const cRight = evaluatePointGradient2DAt(360, 320, ptGrad, { size: 512, seamless: true });
  assert(cLeft[0] > cLeft[2] + 60, `Point Gradient at point 0 must skew warm/red (got ${cLeft})`);
  assert(cRight[2] > cRight[0] + 60, `Point Gradient at point 1 must skew cool/blue (got ${cRight})`);

  const patDoc = createArtworkDocument("pattern");
  patDoc.layers = [
    createArtworkLayer("motif", {
      name: "Rotated Scene-Grad Motif",
      transform: { x: 256, y: 256, width: 320, height: 320, rotation: 33 },
      shape: { motifType: "square", fillMode: "point", fillPointGradient: structuredClone(ptGrad) },
    }),
  ];
  const svgSceneA = artworkDocumentToSvgString(patDoc, { width: 512, height: 512 });
  assert(svgSceneA.includes("patternUnits=\"userSpaceOnUse\""), "Scene Point Gradient SVG must emit userSpaceOnUse <pattern>");
  assert(svgSceneA.includes("patternTransform="), "Scene Point Gradient SVG must apply inverse patternTransform so gradient stays anchored to scene coordinates");

  const evalSceneA = evaluateArtworkDocument(patDoc, 128);
  const rgbaBefore = rasterizePointGradient2D(patDoc.layers[0].shape.fillPointGradient, { res: 64, size: 512, seamless: true });
  const cBeforeAt420 = evaluatePointGradient2DAt(420, 420, patDoc.layers[0].shape.fillPointGradient, { size: 512, seamless: true });
  // Move point 0 to (420, 420) and verify field colors & rasterized gradient follow the moved scene point
  patDoc.layers[0].shape.fillPointGradient.points[0] = { id: "p0", x: 420, y: 420, color: "#22ff55", radius: 220, weight: 1.0 };
  const cMoved = evaluatePointGradient2DAt(420, 420, patDoc.layers[0].shape.fillPointGradient, { size: 512, seamless: true });
  assert(cMoved[1] > cMoved[0] + 100 && cMoved[1] > cMoved[2] && cMoved[1] > cBeforeAt420[1] + 40, `Moving point 0 to (420, 420) must follow scene point with green peak (before=${cBeforeAt420}, after=${cMoved})`);
  const rgbaAfter = rasterizePointGradient2D(patDoc.layers[0].shape.fillPointGradient, { res: 64, size: 512, seamless: true });
  assert.notDeepEqual(rgbaBefore, rgbaAfter, "Moving Point Gradient node in scene must immediately update rasterized gradient field");
  const evalSceneB = evaluateArtworkDocument(patDoc, 128);
  assert.equal(evalSceneA.albedo.length, 128 * 128 * 4);
  assert.equal(evalSceneB.albedo.length, 128 * 128 * 4);

  // 21d. Pattern & Stamp Workspace: Point Grad (F) scene clicking + dragging + NO extra bottom previews
  for (const ws of ["pattern", "stamp"]) {
    studio.switchWorkspace(ws);
    assert.equal(globalThis.document.querySelectorAll(".se-bottom-bar").length, 0, `${ws} Studio must remove redundant .se-bottom-bar preview strip`);
    assert.equal(globalThis.document.querySelectorAll(".channel-thumbs").length, 0, `${ws} Studio must not display extra bottom .channel-thumbs previews`);
    assert.equal(globalThis.document.querySelectorAll(".channel-tile").length, 0, `${ws} Studio must not display extra bottom .channel-tile previews`);

    const btnText = (b) => `${b.title || ""} ${b.textContent || ""}`;
    const ptGradBtn = [...globalThis.document.querySelectorAll(".vp-tool-circle"), ...globalThis.document.querySelectorAll(".se-tool-btn")].find((b) => /Point Grad/i.test(btnText(b)));
    assert(ptGradBtn, `${ws} Studio toolbar must provide 'Point Grad (F)' tool`);
    ptGradBtn.click();
    assert.equal(studio.getSnapshot().vectorTool, "gradient", `${ws} Studio must switch to 'gradient' tool on Point Grad click`);

    const seCanvas = globalThis.document.querySelector(".se-canvas");
    seCanvas.getBoundingClientRect = () => ({ left: 0, top: 0, width: 512, height: 512 });
    const svgBeforeClick = seCanvas.innerHTML;
    seCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 140, clientY: 160, shiftKey: false, preventDefault() {} });
    globalThis.window.dispatchEvent({ type: "pointerup", clientX: 140, clientY: 160 });
    seCanvas.dispatchEvent({ type: "pointerdown", button: 0, clientX: 360, clientY: 340, shiftKey: false, preventDefault() {} });
    seCanvas.dispatchEvent({ type: "pointermove", clientX: 390, clientY: 360 });
    globalThis.window.dispatchEvent({ type: "pointerup", clientX: 390, clientY: 360 });

    const svgAfterDrag = seCanvas.innerHTML;
    assert(svgAfterDrag.includes("se-ptgrad-overlay") && svgAfterDrag.includes("data-ptgrad-id"), `${ws} Studio must render interactive scene-space Point Gradient handles in .se-canvas`);
    assert.notEqual(svgBeforeClick, svgAfterDrag, `${ws} Studio Point Gradient placement & dragging must update live scene SVG`);
  }

  sphere.dispose();
});

// ----------------------------------------------------------------- 22. UI & THEME POLISH: HERO SCALE, FULLSCREEN TOP-RIGHT, NEUTRAL PBR, PILL COLOUR & DROPDOWN TABS, BLACK BACKGROUNDS
await suite("22. UI & Theme Polish: Material Fullscreen, Bigger Hero, Neutral PBR Channels, Pill Colour Rows, Pill Dropdown Tabs & Black BG", async () => {
  const studio = globalThis.window.__frontierTextureStudio;
  const fs = await import("node:fs");
  const { PAINT_PBR_CHANNELS, normalizePointGradient3D } = await import("./Engine/TexturePaintEngine.js");
  const { createDefaultPointGradient2D, createArtworkDocument } = await import("./Engine/Patterns/ArtworkStackEngine.js");

  // 22a. Material Stage Top-Left Hero enlarged + Top-Right has strictly 1 Fullscreen button (Spin + route buttons removed)
  studio.switchWorkspace("material");
  const mwStage = globalThis.document.querySelector(".mw-stage");
  const topActions = mwStage.querySelector(".mw-stage-top-actions");
  assert(topActions, "Material stage must render .mw-stage-top-actions");
  const actionBtns = [...topActions.children].filter((c) => c.tagName === "BUTTON");
  assert.equal(actionBtns.length, 1, `Material top-right must have strictly 1 Fullscreen button (found ${actionBtns.length})`);
  const fsBtn = actionBtns[0];
  assert(fsBtn.classList.contains("mw-fullscreen-btn") && /Fullscreen/i.test(fsBtn.textContent || ""), "Material top-right single button must be Fullscreen");
  assert(!/Spin/i.test(topActions.textContent || "") && !/Use in Paint/i.test(topActions.textContent || "") && !/Baking/i.test(topActions.textContent || ""), "Spin and workspace-jump buttons must be removed from Material top-right");

  const mwGrid = globalThis.document.querySelector(".studio-grid");
  assert(!mwGrid.classList.contains("mw-fullscreen"), "Material stage starts in normal inspector view");
  fsBtn.click();
  assert(mwGrid.classList.contains("mw-fullscreen"), "Clicking Fullscreen button must toggle .mw-fullscreen on studio grid");
  assert(/Exit Fullscreen/i.test(fsBtn.textContent || ""), "Active Fullscreen button label must switch to Exit Fullscreen");
  assert.equal(studio.getSnapshot().fullscreen, true, "Snapshot must reflect fullscreen=true");
  fsBtn.click();
  assert(!mwGrid.classList.contains("mw-fullscreen"), "Clicking Fullscreen again must exit fullscreen mode");
  assert.equal(studio.getSnapshot().fullscreen, false, "Snapshot must reflect fullscreen=false");

  // 22b. Colour Rows as full Pill (Circle + Label + Hex) & Dropdowns as Pill Tabs (dropdown | V)
  const colorRow = globalThis.document.querySelector(".color-row");
  assert(colorRow, "Inspector must render .color-row");
  const colorPill = colorRow.querySelector(".color-pill-wrap");
  assert(colorPill && colorPill.querySelector(".color-circle-shell") && colorPill.querySelector(".color-native") && colorPill.querySelector(".color-pill-label") && colorPill.querySelector(".color-hex"),
    "ColorRow must render full pill containing circular swatch, label text, and hex input");

  const pillTriggers = globalThis.document.querySelectorAll(".pill-trigger");
  assert(pillTriggers.length > 0 && pillTriggers.every((t) => t.querySelector(".pill-caret-divider")), "FilterDropdownPill / PBR channel dropdown triggers must render .pill-caret-divider (| V)");
  assert(mwStage.querySelectorAll(".mw-stage-select .pill-caret-divider").length === 2, "Material stage dropdowns must include unified .select-pill-wrap .pill-caret-divider (| V)");

  // Check SelectRow & header pill dropdown dividers in Texture Paint & Baking
  studio.switchWorkspace("paint");
  assert(globalThis.document.querySelectorAll(".select-row .select-pill-wrap .pill-caret-divider").length >= 1, "SelectRow must wrap <select> in .select-pill-wrap with .pill-caret-divider");
  assert(globalThis.document.querySelector(".se-save-btn .pill-caret-divider"), "Save button must include .pill-caret-divider (| V)");
  assert(globalThis.document.querySelectorAll(".pbr-chan-pill").length >= 1, "Texture Paint must render .pbr-chan-pill buttons");

  studio.switchWorkspace("bake");
  assert(globalThis.document.querySelector(".viewport-toolbar .select-pill-wrap .pill-caret-divider"), "Baking study selector must use .select-pill-wrap with .pill-caret-divider");
  assert.equal(studio.getSnapshot().viewMode, "3d", "Baking workspace must default to 3D viewport (not split)");
  const bakePanes = globalThis.document.querySelectorAll(".viewport-split-pane");
  assert.equal(bakePanes.length, 2, "Baking workspace holds 3D and 2D panes");
  assert.equal(bakePanes[0].hidden, false, "Baking workspace 3D viewport must be visible by default");
  assert.equal(bakePanes[1].hidden, true, "Baking workspace 2D map pane must be hidden by default");
  const bakeViewSel = globalThis.document.querySelector(".vp-bake-view-select");
  assert(bakeViewSel, "Baking toolbar must offer view mode selector");
  bakeViewSel.value = "split";
  bakeViewSel.dispatchEvent({ type: "change" });
  assert.equal(bakePanes[0].hidden, false, "Switching Baking view mode to split must show 3D pane");
  assert.equal(bakePanes[1].hidden, false, "Switching Baking view mode to split must show 2D pane");
  bakeViewSel.value = "3d";
  bakeViewSel.dispatchEvent({ type: "change" });
  assert.equal(bakePanes[0].hidden, false, "Switching back to 3d must keep 3D viewport visible");
  assert.equal(bakePanes[1].hidden, true, "Switching back to 3d must hide 2D pane");

  // Baking Bake Map Targets: Alloy Studio icon tile grid (.bk-map-grid / .bk-map-tile / .bk-map-swatch)
  const bakeTiles = globalThis.document.querySelectorAll(".bk-map-grid .bk-map-tile");
  assert.equal(globalThis.document.querySelectorAll(".bake-map-row").length, 0, "Baking targets must not use old text-only .bake-map-row rows");
  assert(bakeTiles.length === 18, `Baking targets must render all 18 visual .bk-map-tile swatches (found ${bakeTiles.length})`);
  const aoTile = [...bakeTiles].find((t) => t.dataset.mapId === "ao");
  const curvTile = [...bakeTiles].find((t) => t.dataset.mapId === "curvature");
  assert(aoTile?.querySelector(".bk-map-swatch") && aoTile?.querySelector(".bk-map-check"), "Each bake map tile must expose visual .bk-map-swatch and .bk-map-check toggle");
  curvTile.click();
  assert.equal(studio.getSnapshot().inspectChannel, "curvature", "Clicking a bake map icon tile must inspect that channel");
  const findCurvCheck = () =>
    [...globalThis.document.querySelectorAll(".bk-map-tile")].find((t) => t.dataset.mapId === "curvature")?.querySelector(".bk-map-check");
  const curvCheck = findCurvCheck();
  const prevPressed = curvCheck.getAttribute("aria-pressed");
  curvCheck.click();
  const nextPressed = findCurvCheck()?.getAttribute("aria-pressed");
  assert.notEqual(prevPressed, nextPressed, "Clicking .bk-map-check must toggle bake channel inclusion");
  findCurvCheck().click();

  // Decluttered Asset Browser: compact header without redundant count badge & single-line non-wrapping family pills
  const drawer = globalThis.document.querySelector(".asset-browser");
  assert(drawer && !drawer.querySelector(".cb-head .cb-count-badge"), "Asset Browser header must not duplicate count badge (.cb-count-badge)");
  const assetState = studio.getAssetBrowser();
  assert(Array.isArray(assetState.expandedCats) && assetState.expandedCats.length === 1, "Asset Browser library tree must start with only active category expanded to avoid vertical clutter");

  // 22c. Neutral PBR Channel Colors (no messy rainbow per-channel colors) & Theme Black Color B / Backgrounds
  assert(PAINT_PBR_CHANNELS.every((ch) => ch.dot === "#9ea2aa"), "All PBR channel specs must use unified neutral theme dot color (#9ea2aa)");
  const pg2d = createDefaultPointGradient2D("#eff4cf", "#0b0b0b", 512);
  assert.equal(pg2d.background, "#0b0b0b", "Default 2D Point Gradient background/colorB must use theme black (#0b0b0b)");
  const pg3d = normalizePointGradient3D({});
  assert.equal(pg3d.background, "#0b0b0b", "Default 3D Point Gradient background must use theme black (#0b0b0b)");
  const patDefaults = createArtworkDocument("pattern");
  assert.equal(patDefaults.background.colorB, "#0b0b0b", "Pattern document background colorB must default to theme black (#0b0b0b)");

  const cssText = fs.readFileSync(new URL("./TextureStudio.css", import.meta.url), "utf8");
  assert(cssText.includes("font-size: 38px;") && cssText.includes(".mw-hero-title"), "TextureStudio.css must enlarge .mw-hero-title to 38px");
  assert(cssText.includes(".studio-grid.mw-fullscreen"), "TextureStudio.css must style .studio-grid.mw-fullscreen");
  assert(cssText.includes("border-radius: 50%;") && cssText.includes("::-webkit-color-swatch"), "TextureStudio.css must render circular color swatches");
  assert(!cssText.includes("#2fe6a7"), "TextureStudio.css must not use bright neon #2fe6a7 accents on PBR channels/dropdowns");
  // Header, toolbar, and viewport stage must be separated by hairline dividers with no extra nested card boxes
  assert(!cssText.includes("margin: 0 12px 10px;"), "TextureStudio.css must not wrap .vp-tools-bar or .se-stage-shell in extra boxed margins");
  assert(!cssText.includes("border-radius: 13px;\n    background: #141517;"), "TextureStudio.css must not put an extra rounded card box around .vp-tools-bar");
  assert(!cssText.includes("border-radius: 14px;\n    border: 1px solid rgba(255, 255, 255, 0.09);"), "TextureStudio.css must not put an extra rounded card box around .se-stage-shell");
  // Inspector headings (.object-heading) across all workspaces must be flat (no card box) and enlarged
  assert(cssText.includes(".object-heading-copy strong {\n    display: block;\n    font-size: 22px;"), "Inspector .object-heading title must be enlarged to 22px");
  assert(cssText.includes(".object-heading {\n    margin: 2px 0 12px;") && cssText.includes("background: transparent;\n    border: 0;\n    border-bottom: 1px solid var(--line);"), "Inspector .object-heading must not render inside a card box");
  assert(cssText.includes(".bk-map-grid") && cssText.includes(".bk-map-tile") && cssText.includes(".bk-map-swatch"), "TextureStudio.css must style Alloy Studio bake map icon swatches");
  const famRuleStart = cssText.indexOf(".cb-family-pills {");
  const famRuleBlock = cssText.slice(famRuleStart, cssText.indexOf("}", famRuleStart) + 1);
  assert(famRuleBlock.includes("flex-wrap: nowrap;"), "Asset Browser .cb-family-pills must stay on a clean single non-wrapping row");
});

await suite("23. UV Studio <-> Texture Studio Bridge (Controlled Integrator Roundtrip across Paint, Bake & Material)", async () => {
  const { CreateControlledIntegratorAsset } = await import("../UVEditor/Engine/ControlledIntegratorLoader.js");
  const {
    SerializeTopologySurfaceForBridge,
    BuildThreeGeometryFromBridgeTransfer,
    SaveUvBridgeTransfer,
    LoadUvBridgeTransfer,
  } = await import("../Shared/UvTextureBridge.js");

  const asset = CreateControlledIntegratorAsset();
  const payload = SerializeTopologySurfaceForBridge(asset.surface, {
    udimTiles: asset.udimTiles,
    importanceGroups: asset.importanceGroups,
  });
  await SaveUvBridgeTransfer(payload, "latest");
  const loaded = await LoadUvBridgeTransfer("latest");
  assert(loaded && loaded.triCount === asset.surface.faces.length, "UV Bridge transfer must preserve exact triangle count");
  assert(loaded.partNames.length === 13 && loaded.partNames.includes("Body Coat"), "UV Bridge transfer must retain vehicle part groups");

  const rawGeo = BuildThreeGeometryFromBridgeTransfer(loaded, THREE);
  const paintGeo = buildPaintTargetGeometry("custom", rawGeo);
  assert(paintGeo.userData.partNames.length === 13, "buildPaintTargetGeometry('custom') must preserve all 13 partNames");
  const paintIdx = buildMeshPaintIndex(paintGeo);
  assert.equal(paintIdx.tris.length, loaded.triCount, "MeshPaintIndex must index all bridged triangles");
  rawGeo.dispose();
  paintGeo.dispose();

  const studio = globalThis.window.__frontierTextureStudio;
  assert.equal(studio.applyImportedMesh(loaded, "paint"), true, "Paint workspace must accept bridged mesh");
  assert.equal(studio.getSnapshot().meshId, "custom", "Paint workspace must switch to custom bridged mesh");
  assert.equal(studio.applyImportedMesh(loaded, "bake"), true, "Baking workspace must accept bridged mesh");
  assert.equal(studio.getSnapshot().studyId, "custom", "Baking workspace must switch to custom bridged study");
  assert.equal(studio.applyImportedMesh(loaded, "material"), true, "Material workspace must accept bridged mesh");
  assert.equal(studio.getSnapshot().shape, "custom", "Material workspace must switch to custom bridged shape");
});

await suite("24. Material Library Full PBR Channels in Outliner & 3D + Baked Material Layer Icons (Materials Only)", async () => {
  const { ALL_PAINT_CHANNEL_ROWS } = await import("./Engine/TexturePaintEngine.js");
  const studio = globalThis.window.__frontierTextureStudio;
  const racingGreen = materials.find((m) => m.id === "racing-green");
  assert(racingGreen, "Racing Green preset must exist in Material Library");

  // 24a. Material Workspace: Racing Green Paint body surface layer exposes ALL 28 PBR surface channels in Authoring + Outliner + Baked Icon
  const { DeclareMaterialAuthoring } = await import("./Engine/Materials/MaterialSpecification.js");
  studio.switchWorkspace("material");
  studio.applyAsset("material", "racing-green");
  const rgAuth = DeclareMaterialAuthoring(racingGreen);
  const rgLayer = rgAuth.Layers?.find((l) => l.Model === "surface");
  assert(rgLayer && rgLayer.Channels.length === 28, `Racing Green surface layer must expose all 28 PBR channels (got ${rgLayer?.Channels?.length})`);
  assert(rgLayer.Channels.includes("colour") && rgLayer.Channels.includes("iridescence") && rgLayer.Channels.includes("sheenColour"), "Surface channels must include extended PBR channels");

  const mwChPills = [...globalThis.document.querySelectorAll(".mw-outliner-ch-pill")];
  assert(mwChPills.length >= 28, `Material outliner must render clickable channel pills for all surface channels (found ${mwChPills.length})`);
  const mwBakedThumbs = [...globalThis.document.querySelectorAll(".mw-mat-baked-thumb")];
  assert(mwBakedThumbs.length === 1, "Material outliner must render baked shader-ball icon strictly on surface material row (not coat/flakes)");
  assert(mwBakedThumbs[0].src.startsWith("data:image/png;base64,"), "Material outliner icon must be baked PNG matching Asset Browser");
  assert(mwBakedThumbs[0].parentNode?.classList?.contains("is-baked-mat"), "Material outliner thumb container must have .is-baked-mat class");

  // 24b. Texture Paint Workspace: Applying Racing Green via Material Library / Bridge enables all 22 PBR channels & baked icon strictly on material layers
  const { createMaterialLayerOverridesFromPreset } = await import("./Engine/TexturePaintEngine.js");
  studio.switchWorkspace("paint");
  const beforeLayers = studio.getSnapshot().layers;
  const applied = studio.applyAsset("material", "racing-green");
  assert.equal(applied, true, "studio.applyAsset('material', 'racing-green') must succeed on Texture Paint");
  const afterLayers = studio.getSnapshot().layers;
  assert.equal(afterLayers, beforeLayers + 1, "Applying Racing Green must append the new material layer");

  const rgPaintOverrides = createMaterialLayerOverridesFromPreset(racingGreen, 256);
  const rgPaintLayer = createPaintLayer("material", rgPaintOverrides);
  const enabledChCount = Object.values(rgPaintLayer.channels || {}).filter(Boolean).length;
  assert.equal(enabledChCount, ALL_PAINT_CHANNEL_ROWS.length, `Material layer must enable all ${ALL_PAINT_CHANNEL_ROWS.length} PBR channels (got ${enabledChCount})`);
  assert(Math.abs((rgPaintLayer.material?.extValues?.clearcoat ?? 0) - 0.94) < 0.01, "Racing Green clearcoat 0.94 must transfer to Paint layer material.extValues");
  assert(Math.abs((rgPaintLayer.material?.extValues?.opacity ?? 1) - 1) < 1e-4, "Opaque Racing Green must keep opacity=1.0 so 3D object renders opaque");

  // Outliner subtitle confirms all 22/22 channels enabled
  const subs = [...globalThis.document.querySelectorAll(".stack-row-sub")].map((el) => el.textContent);
  assert(subs.some((s) => s.includes(`MATERIAL`) && s.includes(`${ALL_PAINT_CHANNEL_ROWS.length}/${ALL_PAINT_CHANNEL_ROWS.length} ch`)),
    `Material layer subtitle must show ${ALL_PAINT_CHANNEL_ROWS.length}/${ALL_PAINT_CHANNEL_ROWS.length} ch (got ${subs.join(" | ")})`);

  // Layer stack thumbnails: baked icon strictly on kind === "material", never on paint/fill/generator/folder!
  const tpMatThumbs = [...globalThis.document.querySelectorAll(".tp-mat-baked-thumb")];
  assert(tpMatThumbs.length >= 1, "Material layer row in Texture Paint stack must use baked shader-ball icon (.tp-mat-baked-thumb)");
  assert(tpMatThumbs[0].src.startsWith("data:image/png;base64,"), "Texture Paint material layer icon must be baked PNG matching Asset Browser");
  assert(tpMatThumbs[0].parentNode?.classList?.contains("is-baked-mat"), "Material layer thumb container must have .is-baked-mat class");
  const allTpThumbContainers = [...globalThis.document.querySelectorAll(".tp-layer-thumbnail")];
  assert(allTpThumbContainers.some((el) => !el.classList.contains("is-baked-mat") && !el.querySelector(".tp-mat-baked-thumb")), "Non-material layers must NOT use baked material icons");

  // Material layer expanded channel pills visible in outliner
  const expPills = [...globalThis.document.querySelectorAll(".tp-exp-pill-btn")];
  assert(expPills.length >= ALL_PAINT_CHANNEL_ROWS.length, `Expanded material layer outliner must expose all ${ALL_PAINT_CHANNEL_ROWS.length} channel pills (got ${expPills.length})`);
});

await suite("25. Texture Paint Procedural Material Shader Parity (Flakes, Depth Absorption, Clearcoat Peel & Presets)", async () => {
  const THREE = await import("three");
  const { attachProceduralMaterialEvaluation, updateProceduralMaterialUniforms } = await import("./Engine/Materials/materials.js");
  const { createMaterialLayerOverridesFromPreset, resolveMaterialLayerSpec, compositePaintDocument } = await import("./Engine/TexturePaintEngine.js");
  const { PAINT_GLSL3_SHADERS } = await import("./Engine/GpuPaintEngine.js");

  const racingGreen = materials.find((m) => m.id === "racing-green");
  assert(racingGreen, "Racing Green preset must exist");

  // 25a. Shared procedural material evaluator attaches TP_PAINT_OVERLAY shader injection cleanly
  const testMat = new THREE.MeshPhysicalMaterial({ color: "#ffffff", clearcoat: 1.0 });
  attachProceduralMaterialEvaluation(testMat, racingGreen, { enabled: true, hasMat: 1, covTex: new THREE.Texture() });
  assert.equal(testMat.userData.paintOverlay, true, "paintOverlay flag must be enabled on paintMat");
  assert.equal(testMat.userData.paintOverlayState.hasMat, 1, "paintOverlayState.hasMat must initialize to 1");
  const mockShader = {
    uniforms: {},
    vertexShader: "#include <begin_vertex>",
    fragmentShader: [
      "#include <color_fragment>",
      "#include <roughnessmap_fragment>",
      "#include <metalnessmap_fragment>",
      "#include <normal_fragment_maps>",
      "#include <clearcoat_normal_fragment_maps>",
      "#include <lights_physical_fragment>",
      "#include <emissivemap_fragment>",
      "#include <lights_fragment_end>",
      "#include <opaque_fragment>",
    ].join("\n"),
  };
  testMat.onBeforeCompile(mockShader);
  assert(mockShader.fragmentShader.includes("TP_PAINT_OVERLAY"), "Shader must define TP_PAINT_OVERLAY");
  assert(mockShader.fragmentShader.includes("uPaintMatCovMap"), "Shader must sample uPaintMatCovMap");
  assert(mockShader.fragmentShader.includes("cellular3("), "Shader must evaluate 3D cellular3 metallic flakes");
  assert(mockShader.fragmentShader.includes("exp(-uDepth*.28)"), "Shader must evaluate multilayer depth absorption");
  assert(mockShader.fragmentShader.includes("uCoatIor"), "Shader must apply independent uCoatIor Fresnel");
  assert(mockShader.uniforms.uFlakes.value > 0.5, "Racing Green uFlakes uniform must match preset");

  // Switching material preset type dynamically updates uniforms and triggers recompile on type change
  const wood = materials.find((m) => m.type === 2) || materials[1];
  updateProceduralMaterialUniforms(testMat, wood, { hasMat: 1, color: wood.color, roughness: wood.roughness, metalness: wood.metalness });
  assert.equal(Math.floor(Number(testMat.userData.params.type)), wood.type, "updateProceduralMaterialUniforms must update material preset type");

  // 25b. GPU Paint shader includes matCoverageRT + 3D cellular3 flakes + decal cutout
  assert(PAINT_GLSL3_SHADERS.COMPOSITE_LAYER_FRAG.includes("uAccMatCov"), "COMPOSITE_LAYER_FRAG must propagate matCoverageRT");
  assert(PAINT_GLSL3_SHADERS.COMPOSITE_LAYER_FRAG.includes("cellular3("), "GpuPaintEngine material fallback must evaluate 3D cellular3 flakes");
  assert(PAINT_GLSL3_SHADERS.DECAL_STAMP_FRAG.includes("uAccMatCov"), "DECAL_STAMP_FRAG must cut out decals from material coverage");

  // 25c. Layer preset overrides + resolveMaterialLayerSpec + CPU flake depth evaluation
  const ov = createMaterialLayerOverridesFromPreset(racingGreen, 32);
  const layer = createPaintLayer("material", ov);
  const spec = resolveMaterialLayerSpec(layer);
  assert.equal(spec.type, 0, "Racing Green resolves to multilayer metallic flake shader (type 0)");
  assert(spec.depth > 0.3 && spec.flakes > 0.5 && spec.coat > 0.8, "Resolved Racing Green spec must preserve depth, flakes, and coat");

  layer.material.color = "#1f6e4a";
  const tintedSpec = resolveMaterialLayerSpec(layer);
  assert.equal(tintedSpec.color.toLowerCase(), "#1f6e4a", "resolveMaterialLayerSpec must apply live base tint override");
  layer.material.color = racingGreen.color;

  const doc = createPaintDocument(32);
  doc.layers = [layer];
  const cues = {
    res: 32,
    covered: new Uint8Array(32 * 32).fill(1),
    partAt: new Int16Array(32 * 32).fill(0),
    worldPos: Float32Array.from({ length: 32 * 32 * 3 }, (_, idx) => ((idx % 97) / 97) * 2 - 1),
    worldNormal: Float32Array.from({ length: 32 * 32 * 3 }, (_, idx) => (idx % 3 === 1 ? 1 : 0)),
    curvature: new Float32Array(32 * 32).fill(0.5),
    ao: new Float32Array(32 * 32).fill(1.0),
  };
  const comp = compositePaintDocument(doc, cues);
  let minR = 255, maxR = 0;
  for (let i = 0; i < 32 * 32; i++) {
    const g = comp.albedo[i * 4 + 1];
    if (g < minR) minR = g;
    if (g > maxR) maxR = g;
  }
  assert(maxR > minR && minR < 100, `CPU Racing Green composite must show deep absorption + flake contrast (min=${minR}, max=${maxR})`);
});

console.log(`\nALL ${results.length} VALIDATION SUITES PASSED CLEANLY`);
