import fs from "node:fs/promises";
import { zipSync, strToU8 } from "fflate";
import {
  createDemoBakeAssets,
  mergeBakeMeshData,
  disposeBakeParts,
} from "../src/baking/bakeMeshes.js";
import {
  bakeVolumeSdf,
  volumeSlicePixels,
  volumeSdfManifest,
} from "../src/baking/volumeSdf.js";
import { pngRGBA } from "./audit-teapot-bakes.mjs";
const directory = process.argv[2] || "bake-audits/utah-teapot-sdf-64";
await fs.mkdir(directory, { recursive: true });
const assets = createDemoBakeAssets("Teapot"),
  start = Date.now();
let last = 0;
const result = await bakeVolumeSdf(
  mergeBakeMeshData(assets.high),
  { resolution: 64, signMode: "approximate", padding: 0.1 },
  {
    onProgress: (p) => {
      if (Date.now() - last > 8000 || p.done === p.total) {
        console.log(p.phase, p.done, p.total);
        last = Date.now();
      }
    },
  },
);
const manifest = volumeSdfManifest(result, [
    { name: "Utah teapot", triangles: 36672 },
  ]),
  slices = volumeSlicePixels(result),
  files = {};
const floats = new Uint8Array(result.distances.length * 4),
  view = new DataView(floats.buffer);
for (let i = 0; i < result.distances.length; i++)
  view.setFloat32(i * 4, result.distances[i], true);
files["distance.f32"] = floats;
files["sign-confidence.u8"] = result.confidence;
manifest.slicePreviews = { range: slices.range, encoding: slices.encoding };
manifest.audit = {
  durationSeconds: (Date.now() - start) / 1000,
  finite: result.distances.every(Number.isFinite),
  status:
    "APPROXIMATE: source is open; no reliable closed-solid sign is claimed",
};
for (const [key, pixels] of Object.entries(slices.pixels))
  files[key + ".png"] = pngRGBA(pixels, 64, 64);
files["manifest.json"] = strToU8(JSON.stringify(manifest, null, 2));
files["README.txt"] = strToU8(
  "UTAH TEAPOT: APPROXIMATE SIGNED DISTANCE VOLUME\n\n" +
    manifest.audit.status +
    "\n\n" +
    manifest.algorithm +
    "\n\n" +
    manifest.limitations.join("\n\n"),
);
for (const [name, bytes] of Object.entries(files))
  await fs.writeFile(directory + "/" + name, bytes);
await fs.writeFile(
  directory + "/utah-teapot-sdf-64.zip",
  zipSync(files, { level: 0 }),
);
await fs.writeFile(
  directory + "/index.html",
  `<!doctype html><html><meta charset="utf-8"><title>Utah teapot · 3D SDF audit</title><style>body{background:#161815;color:#ddd;font:14px system-ui;margin:30px}.grid{display:grid;grid-template-columns:repeat(3,minmax(180px,1fr));gap:20px}img{width:100%;image-rendering:pixelated}article{background:#242720;border-radius:12px;padding:15px}pre{white-space:pre-wrap}.warning{color:#edc780}</style><h1>Utah teapot · 64³ volume SDF</h1><p class="warning">APPROXIMATE ONLY — ${manifest.topology.boundaryEdges} open boundary edges. Three-ray agreement is not proof of a correct sign.</p><p>${manifest.stats.voxels} voxels · ${manifest.stats.negative} negative samples · ${manifest.stats.ambiguous} disagreements (${(manifest.stats.ambiguousFraction * 100).toFixed(2)}%). ${manifest.audit.durationSeconds.toFixed(1)} seconds. Full distances are unclamped Float32, not these preview PNGs.</p><div class="grid">${Object.keys(
    slices.pixels,
  )
    .map((k) => `<article><h2>${k}</h2><img src="${k}.png"></article>`)
    .join("")}</div><pre>${JSON.stringify(manifest, null, 2)}</pre></html>`,
);
disposeBakeParts(assets.low);
disposeBakeParts(assets.high);
console.log(
  JSON.stringify(
    {
      topology: manifest.topology,
      stats: manifest.stats,
      audit: manifest.audit,
    },
    null,
    2,
  ),
);
