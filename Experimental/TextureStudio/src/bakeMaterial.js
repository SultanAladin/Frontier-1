import * as THREE from "three";
import { zipSync, strToU8 } from "fflate";
import { createMaterial, normalizeMaterial } from "./materials.js";
import { SURFACE_BAKE_MAPS } from "./surfaceBakeMaps.js";

export const bakeChannels = [
  "base-color",
  "roughness",
  "metalness",
  "normal",
  "height",
  "emission",
];
const yieldUI = () => new Promise((resolve) => setTimeout(resolve, 24));
const check = (signal) => {
  if (signal?.aborted) throw new DOMException("Bake cancelled", "AbortError");
};

// A flat XY patch of the same shader fields, NOT a shaded viewport screenshot or
// an unwrap of the current mesh. The procedural source never samples these maps.
export async function bakeMaterial(
  input,
  {
    resolution = 1024,
    widthMM = 100,
    onProgress = () => {},
    signal,
    channels = bakeChannels,
  } = {},
) {
  if (![256, 512, 1024, 2048].includes(resolution))
    throw new Error("Choose a supported bake resolution.");
  if (!Number.isFinite(widthMM) || widthMM < 1 || widthMM > 1000)
    throw new Error("Patch width must be between 1 and 1000 mm.");
  if (!Array.isArray(channels))
    throw new Error("Surface map selection must be a list.");
  const selected = [...new Set(channels)].filter((key) =>
    bakeChannels.includes(key),
  );
  if (!selected.length) throw new Error("Enable at least one surface map.");
  check(signal);
  const params = normalizeMaterial(input),
    span = widthMM / 100;
  const baseHeightRange = [11, 30].includes(params.type)
    ? 0.012
    : params.type === 21
      ? Math.max(0.12, params.ribDepth * 2.2)
      : params.type === 22
        ? 0.05
        : params.type === 18
          ? 0.08
          : 0.03;
  const heightRange = Math.max(baseHeightRange, params.pattern ? 0.05 : 0);
  let renderer, geometry, material;
  const files = {};
  try {
    onProgress({
      done: 0,
      total: selected.length,
      label: "Compiling channel shader",
    });
    await yieldUI();
    check(signal);
    renderer = new THREE.WebGLRenderer({
      antialias: false,
      alpha: true,
      preserveDrawingBuffer: true,
    });
    renderer.setSize(resolution, resolution);
    renderer.setPixelRatio(1);
    renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
    renderer.toneMapping = THREE.NoToneMapping;
    let shaderError = null;
    renderer.debug.onShaderError = (gl, program, vertex, fragment) => {
      shaderError = new Error(
        "Bake shader failed: " + gl.getShaderInfoLog(fragment),
      );
    };
    const scene = new THREE.Scene();
    scene.add(new THREE.AmbientLight(0xffffff, 1));
    const camera = new THREE.OrthographicCamera(
      -span / 2,
      span / 2,
      span / 2,
      -span / 2,
      0.1,
      100,
    );
    camera.position.set(span / 2, span / 2, 10);
    camera.lookAt(span / 2, span / 2, 0);
    geometry = new THREE.PlaneGeometry(span, span);
    geometry.translate(span / 2, span / 2, 0);
    material = createMaterial({
      ...params,
      clothMapping: false,
      bakeMode: 1,
      bakeHeightRange: heightRange,
    });
    // View-dependent lobes remain in the recipe, not in unlit surface maps.
    material.transmission = 0;
    material.iridescence = 0;
    material.anisotropy = 0;
    material.clearcoat = 0;
    material.sheen = 0;
    scene.add(new THREE.Mesh(geometry, material));
    await material.userData.ready;
    await renderer.compileAsync(scene, camera);
    check(signal);
    for (const [index, name] of selected.entries()) {
      onProgress({
        done: index,
        total: selected.length,
        label: "Baking " + name,
      });
      await yieldUI();
      check(signal);
      if (index === 0) renderer.render(scene, camera);
      if (shaderError) throw shaderError;
      if (!material.userData.shader)
        throw new Error("Channel shader did not initialize.");
      material.userData.shader.uniforms.uBakeMode.value =
        bakeChannels.indexOf(name) + 1;
      renderer.render(scene, camera);
      if (shaderError) throw shaderError;
      const blob = await new Promise((resolve, reject) =>
        renderer.domElement.toBlob(
          (b) => (b ? resolve(b) : reject(new Error("PNG encoding failed."))),
          "image/png",
        ),
      );
      check(signal);
      files[name + ".png"] = new Uint8Array(await blob.arrayBuffer());
      onProgress({
        done: index + 1,
        total: selected.length,
        label: name + " complete",
      });
    }
    const manifest = {
      schema: "alloy.surface-bake.v1",
      material: params,
      resolution,
      domain: {
        projection:
          params.type >= 31 && params.type <= 36
            ? "full normalized UV0 surface (U: 0–1, V: 0–1)"
            : "flat XY patch",
        uvConvention:
          params.type >= 31 && params.type <= 36
            ? "Leaf/blade/petal: U across width, V base to tip, midrib U=.5. Fruit/cactus/stem: U around body, V along length. No silhouette alpha; not an atlas."
            : null,
        alpha:
          "Opaque coverage for mesh-surface materials; legacy types 27/28 retain cutout coverage. Pattern image alpha blends finishes; it does not cut out the mesh",
        origin: [0, 0, 0],
        widthMM,
        sceneUnitsPerMM: 0.01,
        repeatable: false,
      },
      channels: {
        "base-color.png": { colorSpace: "sRGB" },
        "roughness.png": { colorSpace: "linear" },
        "metalness.png": { colorSpace: "linear" },
        "normal.png": {
          colorSpace: "linear",
          convention: "OpenGL tangent-space, +Y",
        },
        "height.png": {
          colorSpace: "linear",
          encoding:
            "8-bit; heightSceneUnits = (sample - 0.5) * rangeSceneUnits",
          rangeSceneUnits: heightRange,
          rangeMM: heightRange * 100,
        },
        "emission.png": {
          colorSpace: "sRGB",
          intensityMultiplier: Math.max(1, params.emissionStrength),
        },
      },
      limitations: [
        params.type >= 31 && params.type <= 36
          ? "This is one full UV0 organ surface, not an atlas or an unwrap of your mesh. U crosses the leaf/blade/petal, V runs base to tip; midrib U=.5. For fruit/cactus/stem, U wraps the body and V runs along its length. Match your mesh UVs. All six channels are opaque. Width controls the physical scale of normal/height relief, not the number of leaves."
          : "This is a planar material patch, not an asset UV bake. Seamless tiling is not guaranteed.",
        "Clearcoat, transmission, sheen, anisotropy, skin/wax scattering and iridescence remain recipe/shader properties. Static maps cannot reproduce all view-dependent appearance.",
        "Height is 8-bit and clamped to the documented range. It is not geometry or a precision displacement bake.",
        "Use linear/non-color sampling for roughness, metalness, normals and height. Do not apply sRGB decoding to data channels.",
        "No ambient occlusion or studio lighting is baked into base color.",
        ...(params.type === 37 || params.type === 38
          ? [
              "Sand grains are procedural surface/normal relief, not particle geometry. Nominal grain sizes assume 0.01 scene units per mm at repetition 1. Per-grain dielectric IOR/specular and the editable grain beds remain shader/preset properties; the six static channels do not encode their spatial specular response.",
            ]
          : []),
      ],
    };
    manifest.channels = Object.fromEntries(
      selected.map((name) => [
        name + ".png",
        { ...SURFACE_BAKE_MAPS[name], ...manifest.channels[name + ".png"] },
      ]),
    );
    files["material.json"] = strToU8(JSON.stringify(manifest, null, 2));
    files["README.txt"] = strToU8(
      "ALLOY PROCEDURAL SURFACE BAKE\n\n" +
        manifest.limitations.join("\n\n") +
        "\n\nSee material.json for scale, channels, emission multiplier and all original procedural settings.\n",
    );
    check(signal);
    return {
      bytes: zipSync(files, { level: 0 }),
      manifest,
      maps: Object.fromEntries(
        selected.map((key) => [key, files[key + ".png"]]),
      ),
    };
  } finally {
    material?.dispose();
    geometry?.dispose();
    renderer?.dispose();
    renderer?.forceContextLoss();
  }
}
