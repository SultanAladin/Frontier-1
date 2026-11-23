#!/usr/bin/env node
// Phase 0 mechanical extraction: every VehicleHmiPrototype file, shader entry point,
// GPU pipeline/pass site and UI label becomes one row in a checked contract table.
// Usage: node ExtractContract.mjs <demoRoot> <outDir>
// The demo source is not vendored in this repo (only its HTML is); extract it from the
// upstream object first, e.g.:
//   git archive 6afb6f00b5f25c45f7cda92c7da65b64845768f3 Experimental/VehicleHmiPrototype | tar -x -C <scratch>
// Exit code is non-zero when coverage breaks (unassigned shader family, unreadable file).
// Tables are builder/config evidence, mechanically derived — the runtime encoder order
// and final DOM are Phase 3 capture work (see README.md beside the outputs).

import { readdirSync, readFileSync, statSync, writeFileSync, mkdirSync } from "node:fs";
import { join, relative, basename } from "node:path";

const [demoRoot, outDir] = process.argv.slice(2);
if (!demoRoot || !outDir) {
  console.error("usage: node ExtractContract.mjs <demoRoot> <outDir>");
  process.exit(2);
}
mkdirSync(outDir, { recursive: true });

// -- Shader families: the 12 Slang porting families of the plan. Explicit per-file table
//    (auditable); any Shader/*.wgsl missing here fails the extraction loudly.
const SHADER_FAMILY = new Map(Object.entries({
  AtmosphereMedium: "Atmosphere", AtmosphereMultiple: "Atmosphere",
  AtmosphereScattering: "Atmosphere", AtmosphereTransmittance: "Atmosphere",
  CelestialSky: "Atmosphere", SkyProjection: "Atmosphere",
  NightStars: "Atmosphere", NativeTwilight: "Atmosphere",
  CloudField: "Clouds", CloudPresent: "Clouds", CloudRecords: "Clouds",
  CloudShadowProject: "Clouds", CloudShadowRecords: "Clouds",
  CloudShadowSample: "Clouds", CloudTrace: "Clouds",
  CardCapture: "Cards", CardDirect: "Cards", CardFinal: "Cards",
  CardIrradiance: "Cards", CardProjection: "Cards", CardRadiosity: "Cards",
  CardSampling: "Cards",
  DistanceRecords: "DistanceField", DistanceTransfer: "DistanceField",
  DistanceTraversal: "DistanceField", DistanceUnion: "DistanceField",
  FragmentDistance: "DistanceField",
  DirectRadiance: "Illumination", ReservoirRecords: "Illumination",
  ReservoirSpatial: "Illumination", ReservoirTemporal: "Illumination",
  FireIllumination: "Illumination", ClusteredLights: "Illumination",
  DiscrepancySequence: "Illumination", RayTraversal: "Illumination",
  ReflectionTraversal: "Illumination",
  VarianceTemporal: "Denoise", VarianceUpsample: "Denoise", VarianceWavelet: "Denoise",
  GeometryClusterCull: "Geometry", VirtualizedGeometryCull: "Geometry",
  HiZDownsample: "Geometry", HiZPyramid: "Geometry",
  ClusterDebugFallback: "Geometry", VisibilityRaster: "Geometry",
  SurfaceRaster: "Geometry", ForwardRaster: "Geometry", ShadowRaster: "Geometry",
  PlumeAdvection: "Effects", PlumeMarch: "Effects", PlumePressure: "Effects",
  PlumeRecords: "Effects", PlumeVorticity: "Effects", SparkRaster: "Effects",
  EffectRaster: "Effects", EffectRecords: "Effects", LightningRaster: "Effects",
  PrecipitationCompute: "Weather", PrecipitationRaster: "Weather",
  PrecipitationRecords: "Weather", PrecipitationSurface: "Weather",
  SnowfallRaster: "Weather", FogMedium: "Weather", SurfaceDepositRaster: "Weather",
  CabinOverlay: "CabinHmi", CabinVolume: "CabinHmi", VehicleVisual: "CabinHmi",
  DisplayAntialias: "Display", DisplayExposure: "Display",
  DisplayPresent: "Display", DisplayRecombination: "Display",
  FlareImage: "Display", FlareVisibility: "Display", LensFlare: "Display",
  InspectionMarch: "Display",
  SceneRecords: "SharedRecords", FrameDescriptor: "SharedRecords",
  PackingCodec: "SharedRecords", MaterialResolve: "SharedRecords",
  SurfaceReflectance: "SharedRecords",
}));

const BINARY_EXT = new Set([".png", ".jpg", ".jpeg", ".webp", ".gif", ".ico", ".ttf",
  ".otf", ".woff", ".woff2", ".bin", ".facet", ".blend", ".blend1", ".zip"]);

function walk(dir, acc = []) {
  for (const name of readdirSync(dir).sort()) {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) walk(path, acc);
    else acc.push(path);
  }
  return acc;
}

function kindOf(rel) {
  const top = rel.split("/")[0];
  if (top === "Shader" && rel.endsWith(".wgsl")) return "Shader";
  if (top === "Source" && rel.endsWith(".js")) return "Source";
  if (top === "Style" && rel.endsWith(".css")) return "Style";
  if (top === "Tools" && rel.endsWith(".mjs")) return "Tools";
  if (rel.endsWith(".html")) return "Html";
  return "Other";
}

const clean = (s) => String(s).replace(/[\t\r\n]+/g, " ").replace(/\s+/g, " ").trim();
const count = (text, re) => (text.match(re) || []).length;

// UI label sources, in the forms this codebase actually uses them (audited 2026-10-22):
// text assignments, config-object label/title/caption fields, title/placeholder
// properties, aria-labels, canvas fillText, and template literals.
function extractJsLabels(text) {
  const found = [];
  const push = (raw) => {
    const label = clean(String(raw).replace(/\$\{[^}]*\}/g, "·"));
    if (label.length >= 2 && label.length <= 100 && !found.includes(label)) found.push(label);
  };
  for (const m of text.matchAll(/(?:textContent|innerText)\s*=\s*(?:"([^"\n]{1,100})"|'([^'\n]{1,100})'|`([^`\n]{1,120})`)/g)) push(m[1] || m[2] || m[3]);
  for (const m of text.matchAll(/\b(?:label|title|caption|heading)\s*:\s*(?:"([^"\n]{1,100})"|'([^'\n]{1,100})')/g)) push(m[1] || m[2]);
  for (const m of text.matchAll(/\.(?:title|placeholder)\s*=\s*(?:"([^"\n]{1,100})"|'([^'\n]{1,100})')/g)) push(m[1] || m[2]);
  for (const m of text.matchAll(/setAttribute\(\s*["']aria-label["']\s*,\s*(?:"([^"\n]{1,100})"|'([^'\n]{1,100})')/g)) push(m[1] || m[2]);
  for (const m of text.matchAll(/fillText\(\s*(?:"([^"\n]{1,100})"|'([^'\n]{1,100})'|`([^`\n]{1,120})`)/g)) push(m[1] || m[2] || m[3]);
  return found;
}

function extractHtmlLabels(text) {
  const found = [];
  const push = (raw) => {
    const label = clean(String(raw).replace(/&[a-z]+;/g, " "));
    if (label.length >= 2 && label.length <= 120 && /[A-Za-z]/.test(label) && !found.includes(label)) found.push(label);
  };
  const body = text.replace(/<script[\s\S]*?<\/script>/gi, " ").replace(/<style[\s\S]*?<\/style>/gi, " ");
  for (const m of body.matchAll(/>([^<]{2,200})</g)) push(m[1]);
  for (const m of body.matchAll(/(?:title|placeholder|aria-label)\s*=\s*"([^"]{1,120})"/g)) push(m[1]);
  return found;
}

const files = walk(demoRoot);
const inventory = ["Path\tBytes\tLines\tKind\tFamily"];
const shaderRows = [];
const pipelines = ["Path\tComputePipelines\tRenderPipelines\tShaderModules\tComputePasses\tRenderPasses\tDispatches\tDraws\tBindGroups\tSubmits\tReferencedShaders"];
const ui = ["Path\tElements\tTextAssignments\tListeners\tLabels"];
const labels = ["Path\tLabel\tPortedTo\tApprovedRemoval"];

const shaderIncludes = new Map();   // wgsl basename -> [included basenames]
const jsReferencedShaders = new Set();
let failures = 0;
const kindTotals = new Map();

for (const abs of files) {
  const rel = relative(demoRoot, abs).split("\\").join("/");
  const ext = rel.slice(rel.lastIndexOf(".")).toLowerCase();
  const bytes = statSync(abs).size;
  const kind = kindOf(rel);
  kindTotals.set(kind, (kindTotals.get(kind) || 0) + 1);

  let text = "";
  if (!BINARY_EXT.has(ext)) {
    try { text = readFileSync(abs, "utf8"); } catch { failures++; console.error("unreadable: " + rel); }
  }
  const lines = text ? text.split("\n").length : 0;

  let family = "";
  if (kind === "Shader") {
    const stem = basename(rel, ".wgsl");
    family = SHADER_FAMILY.get(stem) || "";
    if (!family) { failures++; console.error("unassigned shader family: " + rel); }

    const entries = [...text.matchAll(/@(compute|vertex|fragment)\s*(?:@workgroup_size\(([^)]*)\))?\s*fn\s+(\w+)/g)]
      .map((m) => `${m[3]}:${m[1]}`).join(",");
    const workgroups = [...text.matchAll(/@workgroup_size\(([^)]*)\)/g)].map((m) => clean(m[1])).join(";");
    const bindings = count(text, /@binding\(\d+\)/g);
    const groups = [...new Set([...text.matchAll(/@group\((\d+)\)/g)].map((m) => m[1]))].join(",");
    const includes = [...text.matchAll(/\/\/!\s*include\s*"([^"]+)"/g)].map((m) => m[1]);
    shaderIncludes.set(basename(rel), includes.map((i) => basename(i)));
    shaderRows.push([rel, family, entries, workgroups, bindings, groups, includes.join(",")]);
  }
  inventory.push([rel, bytes, lines, kind, family].join("\t"));

  if (kind === "Source") {
    const shaderRefs = [...new Set([...text.matchAll(/["'`]([^"'`\n]*\.wgsl)["'`]/g)].map((m) => basename(m[1])))].sort();
    for (const ref of shaderRefs) jsReferencedShaders.add(ref);
    pipelines.push([rel,
      count(text, /createComputePipeline/g), count(text, /createRenderPipeline/g),
      count(text, /createShaderModule/g), count(text, /beginComputePass/g),
      count(text, /beginRenderPass/g), count(text, /dispatchWorkgroups/g),
      count(text, /\.draw(?:Indexed)?\s*\(/g), count(text, /setBindGroup/g),
      count(text, /\.submit\s*\(/g), shaderRefs.join(",")].join("\t"));

    const elements = {};
    for (const m of text.matchAll(/createElement\(\s*["'`](\w+)["'`]/g)) elements[m[1]] = (elements[m[1]] || 0) + 1;
    const elementTally = Object.entries(elements).sort().map(([t, n]) => `${t}:${n}`).join(";");
    const listeners = {};
    for (const m of text.matchAll(/addEventListener\(\s*["'`](\w+)["'`]/g)) listeners[m[1]] = (listeners[m[1]] || 0) + 1;
    const listenerTally = Object.entries(listeners).sort().map(([t, n]) => `${t}:${n}`).join(";");
    const fileLabels = extractJsLabels(text);
    for (const label of fileLabels) labels.push([rel, label, "", ""].join("\t"));
    ui.push([rel, elementTally, count(text, /textContent\s*=|innerText\s*=/g), listenerTally, fileLabels.length].join("\t"));
  }

  if (kind === "Html") {
    const fileLabels = extractHtmlLabels(text);
    for (const label of fileLabels) labels.push([rel, label, "", ""].join("\t"));
    ui.push([rel, "", 0, "", fileLabels.length].join("\t"));
  }
}

// Shader reachability: roots are JS-referenced modules; closure follows //! include.
// A shader outside the closure is either include-only via a missed reference form
// or dead in the demo — either way the port sees it named, never silently dropped.
const reachable = new Set(jsReferencedShaders);
const queue = [...reachable];
while (queue.length) {
  const current = queue.pop();
  for (const included of shaderIncludes.get(current) || []) {
    if (!reachable.has(included)) { reachable.add(included); queue.push(included); }
  }
}
const shaders = ["Path\tFamily\tEntrypoints\tWorkgroupSizes\tBindings\tGroups\tIncludes\tReachability"];
for (const row of shaderRows) {
  const name = basename(row[0]);
  const reach = jsReferencedShaders.has(name) ? "Root" : reachable.has(name) ? "Included" : "Unreachable";
  shaders.push([...row, reach].join("\t"));
}

writeFileSync(join(outDir, "FileInventory.tsv"), inventory.join("\n") + "\n");
writeFileSync(join(outDir, "ShaderContract.tsv"), shaders.join("\n") + "\n");
writeFileSync(join(outDir, "PipelineSites.tsv"), pipelines.join("\n") + "\n");
writeFileSync(join(outDir, "UiControlInventory.tsv"), ui.join("\n") + "\n");
writeFileSync(join(outDir, "UiLabels.tsv"), labels.join("\n") + "\n");

const unreachable = shaderRows.filter((r) => !reachable.has(basename(r[0]))).length;
console.log(`files: ${files.length}  (${[...kindTotals].map(([k, n]) => `${k} ${n}`).join(", ")})`);
console.log(`shaders: ${shaderRows.length} (unreachable ${unreachable})  ui labels: ${labels.length - 1}  failures: ${failures}`);
process.exit(failures ? 1 : 0);
