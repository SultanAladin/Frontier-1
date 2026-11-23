import { test, expect } from "@playwright/test";
import * as THREE from "three";
import { unzipSync, strFromU8 } from "fflate";
import {
  createDemoBakeAssets,
  mergeBakeMeshData,
  disposeBakeParts,
} from "../src/baking/bakeMeshes.js";
import {
  auditVolumeTopology,
  createVolumeDistanceSampler,
  bakeVolumeSdf,
  volumeSdfManifest,
  volumeSlicePixels,
  validateVolumeSdfSettings,
} from "../src/baking/volumeSdf.js";
function data(name) {
  const assets = createDemoBakeAssets(name),
    mesh = mergeBakeMeshData(assets.high);
  disposeBakeParts(assets.low);
  disposeBakeParts(assets.high);
  return mesh;
}

test("3D SDF uses Euclidean nearest-triangle distance and actual inside/outside parity, not projection height", () => {
  const sampler = createVolumeDistanceSampler(data("Cube"));
  for (const [point, distance] of [
    [[0, 0, 0], -1],
    [[0.75, 0, 0], -0.25],
    [[2, 0, 0], 1],
    [[2, 2, 2], Math.sqrt(3)],
    [[1, 0, 0], 0],
  ]) {
    const value = sampler.sample(new THREE.Vector3(...point));
    expect(value.distance).toBeCloseTo(distance, 8);
    expect(value.confidence).toBe(255);
  }
  sampler.dispose();
});
test("teapot openness is detected after tolerance welding; strict signs reject it, approximate and unsigned are explicit", () => {
  const high = data("Teapot"),
    topology = auditVolumeTopology(high);
  expect(topology.closedEdgeManifold).toBe(false);
  expect(topology.boundaryEdges).toBe(384);
  expect(() => createVolumeDistanceSampler(high)).toThrow(
    /closed.*384 boundary edges/,
  );
  const unsigned = createVolumeDistanceSampler(high, "unsigned"),
    approx = createVolumeDistanceSampler(high, "approximate");
  expect(
    unsigned.sample(new THREE.Vector3(0, 0, 0)).distance,
  ).toBeGreaterThanOrEqual(0);
  expect(unsigned.sample(new THREE.Vector3(0, 0, 0)).confidence).toBe(0);
  expect(
    Number.isFinite(approx.sample(new THREE.Vector3(0, 0, 0)).distance),
  ).toBe(true);
  unsigned.dispose();
  approx.dispose();
});
test("volume stores unclamped float distances at voxel centres with unambiguous x-fastest layout", async () => {
  const result = await bakeVolumeSdf(data("Cube"), {
    resolution: 16,
    signMode: "strict",
    padding: 0.1,
  });
  expect(result.distances).toHaveLength(16 ** 3);
  expect(result.stats.negative).toBeGreaterThan(0);
  expect(result.stats.ambiguous).toBe(0);
  for (const [x, y, z] of [
    [0, 0, 0],
    [8, 8, 8],
    [15, 9, 6],
  ]) {
    const p = [x, y, z].map((v, a) => result.origin[a] + v * result.spacing[a]);
    const q = p.map((v) => Math.abs(v) - 1),
      outside = Math.hypot(...q.map((v) => Math.max(0, v))),
      inside = Math.min(0, Math.max(...q));
    expect(result.distances[x + 16 * (y + 16 * z)]).toBeCloseTo(
      outside + inside,
      6,
    );
  }
  const manifest = volumeSdfManifest(result);
  expect(manifest.storage.clamped).toBe(false);
  expect(manifest.storage.type).toContain("Float32 little-endian");
  expect(manifest.voxelCenterOrigin).toEqual(result.origin);
  expect(manifest.approximate).toBe(false);
  const slices = volumeSlicePixels(result);
  expect(Object.keys(slices.pixels)).toHaveLength(6);
  expect(slices.pixels["slice-z"].length).toBe(16 * 16 * 4);
});
test("SDF resource limits, invalid geometry and cancellation are enforced before heavy allocation", async () => {
  expect(() => validateVolumeSdfSettings({ resolution: 512 })).toThrow(/512³/);
  expect(() => validateVolumeSdfSettings({ padding: NaN })).toThrow(/padding/);
  const controller = new AbortController();
  controller.abort();
  await expect(
    bakeVolumeSdf(
      data("Cube"),
      { resolution: 16 },
      { signal: controller.signal },
    ),
  ).rejects.toThrow(/cancelled/);
  expect(() =>
    auditVolumeTopology({
      positions: new Float32Array([NaN, 0, 0, 1, 0, 0, 0, 1, 0]),
      indices: new Uint32Array([0, 1, 2]),
    }),
  ).toThrow(/non-finite/);
});

test("inline SDF worker exports a real closed cube float volume, slices and manifest", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/?studio=baking");
  await page.getByLabel("Bake demo mesh", { exact: true }).selectOption("Cube");
  await page.getByRole("button", { name: "Volume SDF", exact: true }).click();
  await page
    .getByLabel("SDF voxel resolution", { exact: true })
    .selectOption("16");
  await page.getByRole("button", { name: "Bake 3D SDF", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Download SDF volume ZIP", exact: true }),
  ).toBeVisible();
  const wait = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Download SDF volume ZIP", exact: true })
    .click();
  const stream = await (await wait).createReadStream(),
    chunks = [];
  for await (const chunk of stream) chunks.push(chunk);
  const files = unzipSync(Buffer.concat(chunks)),
    manifest = JSON.parse(strFromU8(files["manifest.json"]));
  expect(manifest.schema).toBe("alloy.volume-sdf.v1");
  expect(manifest.dimensions).toEqual([16, 16, 16]);
  expect(manifest.approximate).toBe(false);
  expect(files["distance.f32"].length).toBe(16 ** 3 * 4);
  expect(files["sign-confidence.u8"].length).toBe(16 ** 3);
  expect(Object.keys(files).filter((k) => k.endsWith(".png"))).toHaveLength(6);
  const bytes = files["distance.f32"],
    view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  expect(view.getFloat32((8 + 16 * (8 + 16 * 8)) * 4, true)).toBeLessThan(0);
  expect(view.getFloat32(0, true)).toBeGreaterThan(0);
  await expect(
    page.getByLabel("Preview mesh map", { exact: true }),
  ).toHaveValue("slice-x");
  await expect(page.locator(".bk-map-image img")).toBeVisible();
  expect(errors).toEqual([]);
});

test("teapot SDF GUI refuses a fake strict sign and labels an explicitly approximate result", async ({
  page,
}) => {
  await page.goto("/?studio=baking");
  await page.getByRole("button", { name: "Volume SDF", exact: true }).click();
  await page
    .getByLabel("SDF voxel resolution", { exact: true })
    .selectOption("16");
  await page.getByRole("button", { name: "Bake 3D SDF", exact: true }).click();
  await expect(page.getByRole("alert")).toContainText("384 boundary edges");
  await page
    .getByLabel("SDF sign handling", { exact: true })
    .selectOption("approximate");
  await page.getByRole("button", { name: "Bake 3D SDF", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Download SDF volume ZIP", exact: true }),
  ).toBeVisible();
  await expect(
    page.getByText("Approximate / non-closed source.", { exact: false }),
  ).toBeVisible();
});
