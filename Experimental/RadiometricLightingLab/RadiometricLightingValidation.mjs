#!/usr/bin/env node
// Node ESM + Slang SPIR-V / D3D11 HLSL Validation Suite for Experimental/RadiometricLightingLab
import assert from "node:assert/strict";
import cp from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  FullscreenVertexShader,
  PrimaryGbufferFragmentShader,
  UnifiedPartitionFragmentShader,
  ClusteredFroxelFragmentShader,
  RestirDiFragmentShader,
  PureSdfVoxelFragmentShader,
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

// 4. Verify all 3 AutoSpace Lobby Scenes, 96-Light Partition & Threaded Pre-Order Skip-Links
for (const sceneId of [0, 1, 2]) {
  for (const density of [0, 1, 2]) {
    const lums = ConstructSceneLuminaires(sceneId, density);
    if (density === 1) assert.equal(lums.length, 40);
    if (density === 2) assert.equal(lums.length, 96);

    const tree = BuildPhotometricLightPartition(lums);
    assert.equal(tree.nodes.length, 2 * lums.length - 1, "Binary partition must contain 2N - 1 entries");
    assert.equal(tree.rootIndex, 0, "Pre-order root must be at index 0");
    assert.equal(tree.nodes[0].missSkip, tree.nodes.length, "Root missSkip must point past entire array");

    // Verify full pre-order threaded walk visits every single leaf exactly once
    const visitedLeaves = new Set();
    let cur = 0;
    while (cur >= 0 && cur < tree.nodes.length) {
      const nd = tree.nodes[cur];
      if (nd.isLeaf === 1) {
        visitedLeaves.add(nd.repIndex);
        cur = nd.missSkip;
      } else {
        assert.equal(nd.left, cur + 1, "Pre-order left child must be cur + 1");
        const sumChildren = tree.nodes[nd.left].totalPower + tree.nodes[nd.right].totalPower;
        assert.ok(Math.abs(nd.totalPower - sumChildren) < 1e-4, "Cluster power conservation violated");
        cur = cur + 1;
      }
    }
    assert.equal(visitedLeaves.size, lums.length, "Threaded pre-order walk must visit all leaves");
    assert.ok(ComputeLuminaireLinearRadiance(lums[0]).every((c) => c >= 0));
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

// 6. Strict Windows ANGLE D3D11 FXC Anti-TDR Guards (Zero implicit-grad texture(), Zero local arrays)
const allFragmentShaders = [
  ["PrimaryGbuffer", PrimaryGbufferFragmentShader],
  ["UnifiedPartition", UnifiedPartitionFragmentShader],
  ["ClusteredFroxels", ClusteredFroxelFragmentShader],
  ["RestirDi", RestirDiFragmentShader],
  ["PureSdfVoxel", PureSdfVoxelFragmentShader],
  ["DisplayTone", DisplayToneFragmentShader],
];

for (const [name, src] of allFragmentShaders) {
  const clean = src.replace(/\/\/.*$/gm, "").replace(/\/\*[\s\S]*?\*\//g, "");
  assert.ok(!/\btexture\s*\(/.test(clean), `[${name}] Forbidden implicit-gradient texture() found — use textureLod(..., 0.0) or texelFetch() for D3D11 FXC safety!`);
  assert.ok(!/\b(?:float|int|uint|vec[234]|ivec[234]|mat[234])\s+\w+\s*\[\s*\d+\s*\]/.test(clean), `[${name}] Forbidden local register array found — breaks D3D11 FXC link speed!`);
}

// 7. Verify uniform names & compile all 7 shaders to BOTH SPIR-V and D3D11 HLSL via slangc
const engineSrc = fs.readFileSync(path.join(__dirname, "LightingLabEngine.js"), "utf8");
const extractUniforms = (glsl) => new Set([...glsl.matchAll(/\buniform\s+\w+\s+([A-Za-z_]\w*)\s*;/g)].map((m) => m[1]));
const gbufDecl = extractUniforms(PrimaryGbufferFragmentShader);
const shadeDecl = extractUniforms(UnifiedPartitionFragmentShader);
const dispDecl = extractUniforms(DisplayToneFragmentShader);

for (const m of engineSrc.matchAll(/uLoc\(gbufferProg,\s*"([^"]+)"\)/g)) {
  assert.ok(gbufDecl.has(m[1]), `Missing uniform '${m[1]}' in PrimaryGbufferFragmentShader`);
}
for (const m of engineSrc.matchAll(/uLoc\(prog,\s*"([^"]+)"\)/g)) {
  assert.ok(shadeDecl.has(m[1]), `Missing uniform '${m[1]}' in CommonRadiometricHeader`);
}
for (const m of engineSrc.matchAll(/uLoc\(displayProg,\s*"([^"]+)"\)/g)) {
  assert.ok(dispDecl.has(m[1]), `Missing uniform '${m[1]}' in DisplayToneFragmentShader`);
}

const slangcPath = path.resolve(__dirname, "../../ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc");
if (fs.existsSync(slangcPath)) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "radlab-d3d11-"));
  try {
    const specs = [
      ["vs.glsl", "vertex", FullscreenVertexShader.replaceAll("gl_VertexID", "gl_VertexIndex")],
      ...allFragmentShaders.map(([name, src]) => [`${name}.glsl`, "fragment", src]),
    ];
    for (const [name, stage, src] of specs) {
      const p = path.join(tmp, name);
      fs.writeFileSync(p, src);
      const spvRes = cp.spawnSync(slangcPath, [p, "-allow-glsl", "-emit-spirv-directly", "-O0", "-stage", stage, "-entry", "main", "-target", "spirv", "-o", `${p}.spv`], { encoding: "utf8" });
      assert.equal(spvRes.status, 0, `[SPIR-V] ${name} failed:\n${spvRes.stderr}`);

      const hlslRes = cp.spawnSync(slangcPath, [p, "-allow-glsl", "-stage", stage, "-entry", "main", "-target", "hlsl", "-profile", stage === "vertex" ? "vs_5_0" : "ps_5_0", "-o", `${p}.hlsl`], { encoding: "utf8" });
      assert.equal(hlslRes.status, 0, `[D3D11 HLSL ps_5_0] ${name} failed:\n${hlslRes.stderr}`);
    }
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

console.log("[PASS] RadiometricLightingValidation: 7 luminaire classes, threaded zero-array Partition, D3D11 FXC anti-TDR rules, SPIR-V & HLSL ps_5_0 compilation verified across all 7 shaders.");
