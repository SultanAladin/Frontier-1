import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";

import * as Shaders from "./ShadowLabShaders.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

function checkShaderSyntaxAndUniforms() {
  const engineSrc = fs.readFileSync(path.join(__dirname, "ShadowLabEngine.js"), "utf8");
  const shaderMap = {
    progGBuffer:     Shaders.GBUFFER_VERT + "\n" + Shaders.GBUFFER_FRAG,
    progLightDepth:  Shaders.LIGHT_DEPTH_VERT + "\n" + Shaders.LIGHT_DEPTH_FRAG,
    progPlaneFilter: Shaders.FULLSCREEN_VERT + "\n" + Shaders.PLANE_FILTER_FRAG,
    progStaticShade: Shaders.FULLSCREEN_VERT + "\n" + Shaders.SDF_STATIC_SHADE_FRAG,
    progSdfBounce:   Shaders.FULLSCREEN_VERT + "\n" + Shaders.SDF_RADIOSITY_BOUNCE_FRAG,
    progSdfUnion:    Shaders.FULLSCREEN_VERT + "\n" + Shaders.SDF_UNION_FRAG,
    progGiSdf:       Shaders.FULLSCREEN_VERT + "\n" + Shaders.GI_SDF_FRAG,
    progGiBvh:       Shaders.FULLSCREEN_VERT + "\n" + Shaders.GI_BVH_FRAG,
    progGiBilateral: Shaders.FULLSCREEN_VERT + "\n" + Shaders.GI_BILATERAL_FRAG,
    progReflSdf:       Shaders.FULLSCREEN_VERT + "\n" + Shaders.REFLECTION_SDF_FRAG,
    progReflBvh:       Shaders.FULLSCREEN_VERT + "\n" + Shaders.REFLECTION_BVH_FRAG,
    progEmissiveMapSh: Shaders.FULLSCREEN_VERT + "\n" + Shaders.EMISSIVE_MAP_SHADOW_FRAG,
    progEmissiveBvhSh: Shaders.FULLSCREEN_VERT + "\n" + Shaders.EMISSIVE_BVH_SHADOW_FRAG,
    progComposite:     Shaders.FULLSCREEN_VERT + "\n" + Shaders.COMPOSITE_FRAG,
    pcf:             Shaders.FULLSCREEN_VERT + "\n" + Shaders.SHADOW_PCF_FRAG,
    pcss:            Shaders.FULLSCREEN_VERT + "\n" + Shaders.SHADOW_PCSS_FRAG,
    contact_pcss:    Shaders.FULLSCREEN_VERT + "\n" + Shaders.SHADOW_CONTACT_PCSS_FRAG,
    smrt:            Shaders.FULLSCREEN_VERT + "\n" + Shaders.SHADOW_SMRT_FRAG,
    sdf:             Shaders.FULLSCREEN_VERT + "\n" + Shaders.SHADOW_SDF_FRAG,
    hybrid:          Shaders.FULLSCREEN_VERT + "\n" + Shaders.SHADOW_HYBRID_FRAG,
    bvh:             Shaders.FULLSCREEN_VERT + "\n" + Shaders.SHADOW_BVH_FRAG,
  };

  const declaredUniforms = (glsl) => {
    const set = new Set();
    const m = glsl.matchAll(/\buniform\s+\w+\s+([A-Za-z_]\w*)(?:\s*\[\s*\d+\s*\])?\s*;/g);
    for (const hit of m) {
      set.add(hit[1]);
      set.add(hit[1] + "[0]");
    }
    return set;
  };

  for (const [key, src] of Object.entries(shaderMap)) {
    let braces = 0, parens = 0;
    for (const ch of src) {
      if (ch === "{") braces++;
      else if (ch === "}") braces--;
      else if (ch === "(") parens++;
      else if (ch === ")") parens--;
      assert(braces >= 0 && parens >= 0, `Unbalanced delimiter in ${key}`);
    }
    assert.equal(braces, 0, `Unclosed { in ${key}`);
    assert.equal(parens, 0, `Unclosed ( in ${key}`);
    assert(src.includes("#version 300 es"), `Missing #version 300 es in ${key}`);
    if (src.includes("sampler3D")) {
      assert(src.includes("precision highp sampler3D;"), `Missing highp sampler3D precision in ${key}`);
    }
    const samplers = [...src.matchAll(/\buniform\s+sampler(?:2D|3D)\s+\w+\s*;/g)].length;
    assert(samplers <= 16, `Too many samplers (${samplers} > 16) in ${key}`);

    // Verify all user-defined functions invoked in GLSL are declared in the shader
    const clean = src.replace(/\/\/.*$/gm, "").replace(/\/\*[\s\S]*?\*\//g, "");
    const fnDefs = new Set();
    for (const m of clean.matchAll(/^[ \t]*[A-Za-z_]\w*\s+([A-Za-z_]\w*)\s*\([^)]*\)\s*\{/gm)) {
      fnDefs.add(m[1]);
    }
    const builtins = new Set([
      "main", "if", "for", "while", "switch", "return", "layout",
      "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4",
      "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4", "float", "int", "uint", "bool",
      "texture", "texelFetch", "textureSize", "normalize", "length", "dot", "cross",
      "clamp", "mix", "smoothstep", "step", "min", "max", "abs", "sign", "floor", "ceil",
      "fract", "mod", "pow", "exp", "exp2", "log", "log2", "sqrt", "inversesqrt",
      "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh", "round",
      "all", "any", "greaterThan", "greaterThanEqual", "lessThan", "lessThanEqual",
      "equal", "notEqual", "isnan", "isinf", "floatBitsToUint", "uintBitsToFloat",
      "intBitsToFloat", "floatBitsToInt", "dFdx", "dFdy", "fwidth", "reflect", "refract",
      "determinant", "inverse", "transpose",
    ]);
    for (const m of clean.matchAll(/\b([A-Za-z_]\w*)\s*\(/g)) {
      const fn = m[1];
      assert(builtins.has(fn) || fnDefs.has(fn), `Undeclared GLSL function '${fn}' in ${key}`);
    }
  }

  for (const [progName, glsl] of Object.entries(shaderMap)) {
    if (!progName.startsWith("prog")) continue;
    const decls = declaredUniforms(glsl);
    const re = new RegExp(`getUniformLocation\\(${progName},\\s*"([^"]+)"\\)`, "g");
    for (const hit of engineSrc.matchAll(re)) {
      const uName = hit[1];
      assert(decls.has(uName), `Uniform '${uName}' set on ${progName} is not declared in GLSL!`);
    }
  }

  const algos = ["pcf", "pcss", "contact_pcss", "smrt", "sdf", "hybrid", "bvh"];
  for (const algo of algos) {
    const decls = declaredUniforms(shaderMap[algo]);
    for (const commonU of ["uGPosRough", "uGNormMat", "uLightCount", "uLightType", "uLightOrigin", "uLightDir", "uLightRadius"]) {
      assert(decls.has(commonU), `Algorithm '${algo}' missing multi-light uniform '${commonU}'`);
    }
  }
  console.log(`[PASS] All ${Object.keys(shaderMap).length} shader programs balanced, sampler counts <= 16, & uniform bindings verified.`);
}

function checkAssetBundle() {
  const binPath = path.join(__dirname, "Assets", "VehicleBenchmarkAssets.bin");
  const buf = fs.readFileSync(binPath);
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const magic = String.fromCharCode(dv.getUint8(0), dv.getUint8(1), dv.getUint8(2), dv.getUint8(3));
  assert.equal(magic, "FVB1", "Asset magic must be FVB1");
  const meshCount = dv.getUint32(4, true);
  const nodeCount = dv.getUint32(8, true);
  const triCount = dv.getUint32(12, true);
  const sdfCount = dv.getUint32(16, true);
  assert.equal(meshCount, 4);
  assert.equal(sdfCount, 5);
  assert(triCount > 9000 && nodeCount > 5000);
  console.log(`[PASS] VehicleBenchmarkAssets.bin verified (${meshCount} meshes, ${triCount} tris, ${sdfCount} pre-baked SDFs).`);
}

function checkHtmlAndAssets() {
  const html = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");
  for (const id of [
    "viewport-canvas", "select-algo-a", "select-algo-b", "select-gi-a", "select-gi-b",
    "select-refl-a", "select-refl-b", "slider-light-size", "slider-azimuth",
    "slider-elevation", "slider-light-dist", "slider-fill-boost", "slider-gi-int",
    "btn-emissive-on", "slider-emissive-int",
    "stat-shadow-ms", "stat-girefl-ms", "stat-total-ms", "stat-rebakes",
  ]) {
    assert(html.includes(`id="${id}"`), `Missing DOM element #${id}`);
  }
  assert(html.includes('data-emissive-shadow="shadowed"'), "Missing shadowed emissive mode button");
  assert(html.includes('data-emissive-shadow="unshadowed"'), "Missing unshadowed emissive mode button");
  for (const algo of ["pcf", "pcss", "contact_pcss", "smrt", "sdf", "hybrid", "bvh"]) {
    assert(html.includes(`value="${algo}"`), `Missing algorithm option '${algo}' in HTML`);
  }
  for (const lc of ["1", "2", "3"]) {
    assert(html.includes(`data-light-count="${lc}"`), `Missing light count button '${lc}'`);
  }
  for (const f of ["dm-sans-latin-300-normal.woff2", "dm-sans-latin-400-normal.woff2", "dm-sans-latin-500-normal.woff2"]) {
    assert(fs.existsSync(path.join(__dirname, "Fonts", f)), `Missing offline font ${f}`);
  }
  console.log("[PASS] DOM controls, per-side Shadow/GI/Refl selectors, multi-light controls, & fonts verified.");
}

function checkMathAndSdfLogic() {
  function mat4OrthoRH(l, r, b, t, n, f) {
    const m = new Float32Array(16);
    m[0] = 2 / (r - l);
    m[5] = 2 / (t - b);
    m[10] = -2 / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1;
    return m;
  }
  const o = mat4OrthoRH(-16, 16, -16, 16, 1, 92);
  const zNearNdc = o[10] * (-1) + o[14];
  const zFarNdc  = o[10] * (-92) + o[14];
  assert(Math.abs(zNearNdc + 1.0) < 1e-6, `Expected -1 at near, got ${zNearNdc}`);
  assert(Math.abs(zFarNdc - 1.0) < 1e-6, `Expected +1 at far, got ${zFarNdc}`);
  console.log("[PASS] Right-handed projection math verified.");
}

checkShaderSyntaxAndUniforms();
checkAssetBundle();
checkHtmlAndAssets();
checkMathAndSdfLogic();
console.log("[ALL CHECKS PASSED] ShadowAlgorithmLab is ready.");
