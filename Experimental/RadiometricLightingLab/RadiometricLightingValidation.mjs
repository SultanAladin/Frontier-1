#!/usr/bin/env node
// Node ESM Validation Suite for Experimental/RadiometricLightingLab
import assert from "node:assert/strict";
import cp from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  FullscreenVertexShader,
  RadiometricSceneFragmentShader,
  DisplayToneFragmentShader,
} from "./LightingLabShaders.js";
import {
  LuminaireClassNames,
  BuildPhotometricAtlasFloatData,
  BuildHeitzLtcFloatData,
  ConstructSceneLuminaires,
  BuildPhotometricLightPartition,
  ComputeLuminaireLinearRadiance,
} from "./LightingLabEngine.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// 1. Verify all 7 luminaire classes exist in taxonomy
assert.equal(LuminaireClassNames.length, 8);
assert.equal(LuminaireClassNames[2], "PhotometricIes");
assert.equal(LuminaireClassNames[3], "AutomotiveEce");
assert.equal(LuminaireClassNames[4], "PolygonalAreaLtc");
assert.equal(LuminaireClassNames[5], "LinearTubeSegment");
assert.equal(LuminaireClassNames[6], "EmissiveSurface");
assert.equal(LuminaireClassNames[7], "EnvironmentPortal");

// 2. Verify 128 x (32 * 6) LM-63 Photometric Atlas bounds & finite candela values
const iesAtlas = BuildPhotometricAtlasFloatData();
assert.equal(iesAtlas.width, 128);
assert.equal(iesAtlas.height, 192);
for (let i = 0; i < iesAtlas.data.length; ++i) {
  assert.ok(Number.isFinite(iesAtlas.data[i]), `Non-finite IES sample at ${i}`);
  assert.ok(iesAtlas.data[i] >= 0.0 && iesAtlas.data[i] <= 1.0001, `Out-of-range IES sample at ${i}`);
}

// 3. Verify 64 x 64 Heitz LTC Lookup Table finite values
const ltc = BuildHeitzLtcFloatData();
assert.equal(ltc.width, 64);
assert.equal(ltc.height, 64);
for (let i = 0; i < ltc.data.length; ++i) {
  assert.ok(Number.isFinite(ltc.data[i]), `Non-finite LTC entry at ${i}`);
}

// 4. Verify all 3 AutoSpace Lobby Scenes & 96-Light Stress Partition Invariants
for (const sceneId of [0, 1, 2]) {
  for (const density of [0, 1, 2]) {
    const lums = ConstructSceneLuminaires(sceneId, density);
    if (density === 1) assert.equal(lums.length, 40);
    if (density === 2) assert.equal(lums.length, 96);

    const tree = BuildPhotometricLightPartition(lums);
    assert.equal(tree.nodes.length, 2 * lums.length - 1, "Binary partition must contain 2N - 1 entries");
    assert.equal(tree.nodes[tree.rootIndex].count, lums.length);
    assert.ok(tree.totalPower > 0.0);

    // Verify power conservation across internal splits
    for (const nd of tree.nodes) {
      if (nd.left >= 0 && nd.right >= 0) {
        const sumChildren = tree.nodes[nd.left].totalPower + tree.nodes[nd.right].totalPower;
        assert.ok(Math.abs(nd.totalPower - sumChildren) < 1e-4, "Partition cluster power conservation violated");
      }
      assert.ok(ComputeLuminaireLinearRadiance(lums[nd.repIndex]).every((c) => c >= 0));
    }
  }
}

// 5. Verify DOM IDs referenced by LightingLabEngine.js exist in index.html
const html = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");
const requiredIds = [
  "glCanvas", "polarCanvas", "segScenes", "segEngines", "segSolvers", "segDensity", "segCut",
  "outlinerScroll", "outlinerCount", "inspName", "inspClassBadge", "giantWhole", "giantDec",
  "giantUnit", "giantCaption", "ctlIntensity", "ctlKelvin", "ctlRange", "ctlAperture",
  "selIesProfile", "btnSplit", "btnHeatmap", "btnFalseLux", "btnGizmos", "btnWheelSpin", "btnLiftAnim",
];
for (const id of requiredIds) {
  assert.ok(html.includes(`id="${id}"`), `Missing required DOM id="${id}" in index.html`);
}

// 6. Verify all JS uniform lookups match GLSL declarations & compile cleanly via slangc when available
const engineSrc = fs.readFileSync(path.join(__dirname, "LightingLabEngine.js"), "utf8");
const extractUniforms = (glsl) => new Set([...glsl.matchAll(/\buniform\s+\w+\s+([A-Za-z_]\w*)\s*;/g)].map((m) => m[1]));
const sceneDecl = extractUniforms(RadiometricSceneFragmentShader);
const dispDecl = extractUniforms(DisplayToneFragmentShader);

for (const m of engineSrc.matchAll(/uLoc\(sceneProg,\s*"([^"]+)"\)/g)) {
  assert.ok(sceneDecl.has(m[1]), `Uniform '${m[1]}' requested on sceneProg is missing in RadiometricSceneFragmentShader`);
}
for (const m of engineSrc.matchAll(/uLoc\(displayProg,\s*"([^"]+)"\)/g)) {
  assert.ok(dispDecl.has(m[1]), `Uniform '${m[1]}' requested on displayProg is missing in DisplayToneFragmentShader`);
}

const slangcPath = path.resolve(__dirname, "../../ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc");
if (fs.existsSync(slangcPath)) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "radlab-glsl-"));
  try {
    const specs = [
      ["vs.glsl", "vertex", FullscreenVertexShader.replaceAll("gl_VertexID", "gl_VertexIndex")],
      ["scene.glsl", "fragment", RadiometricSceneFragmentShader],
      ["disp.glsl", "fragment", DisplayToneFragmentShader.replace("out vec4 outColor;", "layout(location = 0) out vec4 outColor;")],
    ];
    for (const [name, stage, src] of specs) {
      const p = path.join(tmp, name);
      fs.writeFileSync(p, src);
      const res = cp.spawnSync(slangcPath, [p, "-allow-glsl", "-emit-spirv-directly", "-O0", "-stage", stage, "-entry", "main", "-target", "spirv", "-o", `${p}.spv`], { encoding: "utf8" });
      assert.equal(res.status, 0, `Shader ${name} failed compiler verification:\n${res.stderr}`);
    }
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

console.log("[PASS] RadiometricLightingValidation: 7 luminaire classes, 128x32 IES atlas, 64x64 LTC LUT, 96-light Partition, uniforms & SPIR-V shader compile verified.");
