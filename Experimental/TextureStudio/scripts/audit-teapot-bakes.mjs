import fs from "node:fs/promises";
import path from "node:path";
import { deflateSync } from "node:zlib";
import { zipSync, strToU8 } from "fflate";
import {
  createDemoBakeAssets,
  bakeMeshData,
  mergeBakeMeshData,
  disposeBakeParts,
} from "../src/baking/bakeMeshes.js";
import {
  bakeMeshMaps,
  encodeBakeMap,
  dilateBakePixels,
} from "../src/baking/meshBakeCore.js";
import { MESH_MAPS } from "../src/baking/meshMaps.js";

export function pngRGBA(pixels, w, h) {
  const table = Uint32Array.from({ length: 256 }, (_, c) => {
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c >>> 0;
  });
  const chunk = (type, data) => {
    const name = Buffer.from(type),
      payload = Buffer.concat([name, data]);
    let crc = 0xffffffff;
    for (const byte of payload) crc = table[(crc ^ byte) & 255] ^ (crc >>> 8);
    const out = Buffer.alloc(data.length + 12);
    out.writeUInt32BE(data.length);
    payload.copy(out, 4);
    out.writeUInt32BE((crc ^ 0xffffffff) >>> 0, out.length - 4);
    return out;
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(w);
  header.writeUInt32BE(h, 4);
  header[8] = 8;
  header[9] = 6;
  const scan = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++)
    Buffer.from(pixels.buffer, pixels.byteOffset + y * w * 4, w * 4).copy(
      scan,
      y * (w * 4 + 1) + 1,
    );
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", header),
    chunk("IDAT", deflateSync(scan)),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}
export async function auditTeapot(directory = "bake-audits/utah-teapot-512") {
  await fs.mkdir(directory, { recursive: true });
  const assets = createDemoBakeAssets("Teapot");
  const settings = {
    resolution: 512,
    channels: Object.keys(MESH_MAPS),
    samples: 64,
    front: 0.15,
    back: 0.15,
    padding: 4,
    normalY: "+Y",
  };
  const start = Date.now();
  let phase = "",
    last = 0;
  const result = await bakeMeshMaps(
    bakeMeshData(assets.low[0].geometry),
    mergeBakeMeshData(assets.high),
    settings,
    null,
    {
      onProgress: (p) => {
        if (p.phase !== phase || Date.now() - last > 8000) {
          console.log(
            p.phase,
            p.done + "/" + p.total,
            p.hits ?? "",
            p.misses ?? "",
          );
          phase = p.phase;
          last = Date.now();
        }
      },
    },
  );
  const maps = {},
    archive = {},
    res = 512;
  for (const [name, field] of Object.entries(result.maps)) {
    let min = Infinity,
      max = -Infinity,
      nonFinite = 0,
      invalidNormals = 0,
      negativeNormalZ = 0,
      sum = 0,
      zero = 0;
    for (let i = 0; i < res * res; i++)
      if (result.coverage[i]) {
        for (let c = 0; c < 3; c++) {
          const v = field.data[i * 4 + c];
          if (!Number.isFinite(v)) nonFinite++;
          min = Math.min(min, v);
          max = Math.max(max, v);
          sum += v;
        }
        if (
          field.type === "normal" ||
          (MESH_MAPS[name].space && name.includes("normal"))
        ) {
          const xyz = [0, 1, 2].map((c) => field.data[i * 4 + c] * 2 - 1),
            length = Math.hypot(...xyz);
          if (Math.abs(length - 1) > 1e-4) invalidNormals++;
          if (MESH_MAPS[name].space === "tangent" && xyz[2] < -1e-5)
            negativeNormalZ++;
        }
        if (field.data[i * 4] === 0) zero++;
      }
    const bytes = encodeBakeMap(
      field,
      res,
      res,
      { normalY: "+Y", colorSpace: MESH_MAPS[name].colorSpace },
      result.coverage,
    );
    const padded = ["coverage", "alpha"].includes(name)
      ? bytes
      : dilateBakePixels(bytes, res, res, result.coverage, 4);
    const png = pngRGBA(padded, res, res);
    await fs.writeFile(path.join(directory, name + ".png"), png);
    archive[name + ".png"] = new Uint8Array(png);
    maps[name] = {
      label: MESH_MAPS[name].label,
      type: field.type,
      min,
      max,
      mean: sum / (result.stats.covered * 3),
      nonFinite,
      invalidNormals,
      negativeNormalZ,
      zero,
    };
  }
  let bentHemisphereViolations = 0,
    minimumBentHighDot = 1;
  if (result.maps["world-normal"] && result.maps["bent-world-normal"])
    for (let i = 0; i < res * res; i++)
      if (result.coverage[i]) {
        let dot = 0;
        for (let c = 0; c < 3; c++)
          dot +=
            (result.maps["world-normal"].data[i * 4 + c] * 2 - 1) *
            (result.maps["bent-world-normal"].data[i * 4 + c] * 2 - 1);
        minimumBentHighDot = Math.min(minimumBentHighDot, dot);
        if (dot < -1e-5) bentHemisphereViolations++;
      }
  const report = {
    schema: "alloy.teapot-bake-audit.v1",
    resolution: res,
    durationSeconds: (Date.now() - start) / 1000,
    settings: result.settings,
    stats: result.stats,
    bounds: result.bounds,
    maps,
    checks: {
      allFinite: Object.values(maps).every((m) => m.nonFinite === 0),
      allNormalsUnit: Object.values(maps).every((m) => m.invalidNormals === 0),
      fullProjectionCoverage: result.stats.misses === 0,
      frontHemisphereNormals:
        maps.normal.negativeNormalZ === 0 &&
        maps["bevel-normal"].negativeNormalZ === 0,
      bentDirectionInHighHemisphere: bentHemisphereViolations === 0,
    },
    bentDirection: { bentHemisphereViolations, minimumBentHighDot },
    notes: [
      "Utah teapot: 8-segment low, 24-segment high; 32 packed Bezier charts.",
      "RGBA PNGs are 8-bit. IDs and vertex-color outputs may legitimately be constant.",
      "Tangent normal Z < 0 identifies opposite-facing or strongly folded projections; inspect separately.",
      "Thickness is inward opposite-surface distance, not guaranteed wall thickness on an open or intersecting model.",
      "Bent normals are visibility directions, not ordinary bump normals. A negative low-tangent Z can be legitimate when the direction remains in the projected high-normal hemisphere; do not flip/clamp it as a geometric normal.",
    ],
  };
  await fs.writeFile(
    path.join(directory, "audit.json"),
    JSON.stringify(report, null, 2),
  );
  archive["audit.json"] = strToU8(JSON.stringify(report, null, 2));
  const manifest = {
    schema: "alloy.mesh-bake.v1",
    workflow: "mesh-maps",
    engine: "Three.js / CPU MeshBVH",
    settings: result.settings,
    sources: {
      low: [{ name: "Utah teapot", triangles: 4032 }],
      high: [{ name: "Utah teapot", triangles: 36672 }],
    },
    sets: [
      {
        name: "Utah teapot",
        resolution: res,
        stats: result.stats,
        bounds: result.bounds,
        chartPalette: result.chartPalette,
        channels: Object.fromEntries(
          Object.entries(MESH_MAPS).map(([key, meta]) => [
            key,
            {
              ...meta,
              normalConvention:
                meta.space === "tangent" ? "OpenGL +Y" : undefined,
            },
          ]),
        ),
      },
    ],
    encodings: {
      height: "distance = (sample - .5) * 2 * max(front,back)",
      thickness:
        "sample * thicknessRange; distances beyond range saturate white, absent opposite hits stay black",
      id: "24-bit little-endian RGB; source ID 1",
      uvIslands: "see chartPalette",
    },
    limitations: report.notes,
  };
  await fs.writeFile(
    path.join(directory, "manifest.json"),
    JSON.stringify(manifest, null, 2),
  );
  archive["manifest.json"] = strToU8(JSON.stringify(manifest, null, 2));
  const tiles = Object.entries(maps)
    .map(
      ([key, m]) =>
        `<article><img src="${key}.png"><h2>${m.label}</h2><p>${key}<br>range ${m.min.toFixed(4)}–${m.max.toFixed(4)}<br>NaN ${m.nonFinite} · invalid normals ${m.invalidNormals} · backward Z ${m.negativeNormalZ}</p></article>`,
    )
    .join("");
  const html = `<!doctype html><html><meta charset="utf-8"><title>Utah teapot · 512 bake audit</title><style>body{margin:30px;background:#151715;color:#ddd;font:14px system-ui}h1{color:#bfcf99}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:20px}article{padding:15px;border:1px solid #444;border-radius:12px;background:#222}img{width:100%;background:repeating-conic-gradient(#303030 0 25%,#181818 0 50%) 0/20px 20px;image-rendering:auto}h2{font-size:16px}pre{white-space:pre-wrap}a{color:#bfcf99}</style><h1>Utah teapot · all mesh maps at 512</h1><p>CPU BVH projection, ${result.settings.samples} visibility samples. ${result.stats.hits} hits / ${result.stats.misses} misses / ${result.stats.charts} charts. ${report.durationSeconds.toFixed(1)} seconds.</p><pre>${JSON.stringify(report.checks, null, 2)}</pre><p>These are actual exported maps—not illustrative swatches. IDs may appear almost black because their RGB bytes are data. Vertex color is white because the built-in mesh has no vertex colors.</p><div class="grid">${tiles}</div></html>`;
  await fs.writeFile(path.join(directory, "index.html"), html);
  archive["index.html"] = strToU8(html);
  await fs.writeFile(
    path.join(directory, "utah-teapot-512.zip"),
    zipSync(archive, { level: 0 }),
  );
  disposeBakeParts(assets.low);
  disposeBakeParts(assets.high);
  console.log(
    JSON.stringify(
      {
        stats: report.stats,
        checks: report.checks,
        normals: Object.fromEntries(
          Object.entries(maps)
            .filter(([k]) => k.includes("normal"))
            .map(([k, m]) => [
              k,
              { invalid: m.invalidNormals, backwardZ: m.negativeNormalZ },
            ]),
        ),
      },
      null,
      2,
    ),
  );
  return report;
}
if (
  process.argv[1] &&
  path.resolve(process.argv[1]) ===
    path.resolve(new URL(import.meta.url).pathname)
)
  await auditTeapot(process.argv[2]);
