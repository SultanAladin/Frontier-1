import { test, expect } from "@playwright/test";
import * as THREE from "three";
import { MESH_MAPS, uvIslandColor } from "../src/baking/meshMaps.js";
import {
  bakeMeshData,
  createDemoBakeAssets,
  disposeBakeParts,
  mergeBakeMeshData,
} from "../src/baking/bakeMeshes.js";
import {
  bakeMeshMaps,
  encodeBakeMap,
  validateMeshBakeSettings,
} from "../src/baking/meshBakeCore.js";
import {
  BAKE_RECIPE_SCHEMA,
  BAKE_PREFS_KEY,
  LEGACY_BAKE_BACKUP_KEY,
  restoreBakeRecipe,
  defaultBakeRecipe,
  normalizeBakeSelection,
  parseBakeRecipe,
} from "../src/baking/bakeRecipe.js";
function plane(z = 0) {
  const geometry = new THREE.PlaneGeometry(1, 1);
  geometry.translate(0, 0, z);
  const data = bakeMeshData(geometry);
  geometry.dispose();
  return data;
}
const settings = (channels, extra = {}) => ({
  resolution: 64,
  channels,
  front: 0.3,
  back: 0.3,
  samples: 8,
  ...extra,
});

test("all 27 selected mesh maps are evaluated directly without a graph or fabricated fields", async () => {
  expect(Object.keys(MESH_MAPS)).toHaveLength(27);
  expect(new Set(Object.values(MESH_MAPS).map((m) => m.label)).size).toBe(27);
  const result = await bakeMeshMaps(
    plane(),
    plane(0.1),
    settings(Object.keys(MESH_MAPS), { dustAxis: "+Z" }),
  );
  expect(result.graph).toBeNull();
  expect(result.stats).toMatchObject({ hits: 4096, misses: 0, charts: 1 });
  expect(Object.keys(result.maps)).toEqual(Object.keys(MESH_MAPS));
  for (const [key, map] of Object.entries(result.maps)) {
    expect(map.type, key).toBe(MESH_MAPS[key].type);
    expect(map.data.length, key).toBe(4096 * 4);
    expect(Array.from(map.data).every(Number.isFinite), key).toBe(true);
  }
  expect(result.maps.normal.data.slice(0, 4)).toEqual(
    new Float32Array([0.5, 0.5, 1, 1]),
  );
  expect(result.maps["vector-displacement"].data[2]).toBeCloseTo(2 / 3, 5);
  expect(result.maps["base-color"].data[0]).toBe(1);
  expect(result.maps.dust.data[0]).toBeGreaterThan(0.5);
  expect(result.maps.dirt.data[0]).toBe(0);
  expect(result.maps["edge-wear"].data[0]).toBe(0);
});

test("mesh recipe v2 preserves disabled selections, zero effects and settings; legacy graphs are not secretly applied", () => {
  expect(defaultBakeRecipe().schema).toBe(BAKE_RECIPE_SCHEMA);
  const empty = normalizeBakeSelection({
    channels: [],
    bevelRadius: 0,
    bevelStrength: 0,
    maskStrength: 0,
  });
  expect(empty.channels).toEqual([]);
  expect(empty).toMatchObject({
    bevelRadius: 0,
    bevelStrength: 0,
    maskStrength: 0,
  });
  expect(() => validateMeshBakeSettings(empty)).toThrow(/at least one/);
  expect(() => validateMeshBakeSettings({ channels: "normal" })).toThrow(
    /list/,
  );
  const legacy = parseBakeRecipe({
    schema: "alloy.baking-recipe.v1",
    settings: settings(["normal"]),
    graph: { arbitrary: "not evaluated" },
  });
  expect(legacy).toMatchObject({ schema: BAKE_RECIPE_SCHEMA, legacy: true });
  expect(legacy.graph).toBeUndefined();
  expect(() =>
    parseBakeRecipe({
      schema: BAKE_RECIPE_SCHEMA,
      settings: { channels: ["invented"] },
    }),
  ).toThrow(/unknown/);
});

test("UV island maps encode 32 distinct charts and a reproducible palette, not object IDs", async () => {
  const assets = createDemoBakeAssets();
  try {
    const result = await bakeMeshMaps(
      bakeMeshData(assets.low[0].geometry),
      bakeMeshData(assets.high[0].geometry),
      settings(["uv-island", "uv", "wireframe", "id"]),
    );
    expect(result.chartPalette).toHaveLength(32);
    expect(new Set(result.chartPalette.map((c) => c.rgb.join(","))).size).toBe(
      32,
    );
    const colors = new Set();
    for (let i = 0; i < result.coverage.length; i++)
      if (result.coverage[i]) {
        const expected = uvIslandColor(result.charts[i]);
        for (let c = 0; c < 3; c++)
          expect(result.maps["uv-island"].data[i * 4 + c]).toBeCloseTo(
            expected[c],
            6,
          );
        colors.add(expected.join(","));
        expect(result.maps.uv.data[i * 4]).toBeCloseTo(
          ((i % 64) + 0.5) / 64,
          6,
        );
        expect(result.maps.uv.data[i * 4 + 1]).toBeCloseTo(
          1 - (Math.floor(i / 64) + 0.5) / 64,
          6,
        );
      }
    expect(colors.size).toBe(32);
    expect(
      result.maps.wireframe.data.some((v, index) => index % 4 === 0 && v > 0.9),
    ).toBe(true);
    // At 64px the dense teapot triangles can all lie within one pixel of an
    // edge; test the interior/edge contrast on a roomy single UV quad instead.
    const quad = await bakeMeshMaps(plane(), plane(), settings(["wireframe"]));
    expect(
      quad.maps.wireframe.data.some((v, index) => index % 4 === 0 && v === 0),
    ).toBe(true);
    expect(
      quad.maps.wireframe.data.some((v, index) => index % 4 === 0 && v > 0.9),
    ).toBe(true);
  } finally {
    disposeBakeParts(assets.low);
    disposeBakeParts(assets.high);
  }
});

test("alpha / hit masks differ from low UV coverage and derived masks do not invent data on misses", async () => {
  const result = await bakeMeshMaps(
    plane(),
    plane(2),
    settings([
      "alpha",
      "coverage",
      "dust",
      "dirt",
      "bevel-mask",
      "vector-displacement",
    ]),
  );
  expect(result.stats.misses).toBe(4096);
  expect(result.maps.coverage.data[0]).toBe(1);
  for (const key of ["alpha", "dust", "dirt", "bevel-mask"])
    expect(result.maps[key].data[0], key).toBe(0);
  expect(
    Array.from(result.maps["vector-displacement"].data.slice(0, 3)),
  ).toEqual([0.5, 0.5, 0.5]);
});

test("bevel normals and masks soften cube edges without a node or any topology changes; zero radius bypasses", async () => {
  const assets = createDemoBakeAssets("Cube");
  try {
    const low = bakeMeshData(assets.low[0].geometry),
      high = bakeMeshData(assets.high[0].geometry),
      original = Array.from(low.positions);
    const result = await bakeMeshMaps(
      low,
      high,
      settings(["normal", "bevel-normal", "bevel-mask"], { bevelRadius: 0.15 }),
    );
    let changed = 0;
    for (let i = 0; i < result.coverage.length; i++)
      if (result.coverage[i] && result.maps["bevel-mask"].data[i * 4] > 0.01)
        changed++;
    expect(changed).toBeGreaterThan(100);
    expect(Array.from(low.positions)).toEqual(original);
    const bypass = await bakeMeshMaps(
      low,
      high,
      settings(["normal", "bevel-normal", "bevel-mask"], { bevelRadius: 0 }),
    );
    expect(bypass.maps["bevel-normal"].data).toEqual(bypass.maps.normal.data);
    expect(
      bypass.maps["bevel-mask"].data.every((v, i) => i % 4 === 3 || v === 0),
    ).toBe(true);
  } finally {
    disposeBakeParts(assets.low);
    disposeBakeParts(assets.high);
  }
});

test("world / object / tangent normals use genuinely distinct frames including imported nonuniform transforms", async () => {
  const geometry = new THREE.PlaneGeometry(1, 1),
    transform = new THREE.Matrix4().compose(
      new THREE.Vector3(1, 2, 3),
      new THREE.Quaternion().setFromEuler(new THREE.Euler(Math.PI / 2, 0, 0)),
      new THREE.Vector3(2, 0.7, 1.5),
    );
  geometry.applyMatrix4(transform);
  geometry.userData.bakeObjectToWorld = transform.toArray();
  try {
    const data = bakeMeshData(geometry),
      result = await bakeMeshMaps(
        data,
        data,
        settings(["normal", "world-normal", "object-normal"]),
      );
    expect(result.stats.misses).toBe(0);
    expect(Array.from(result.maps.normal.data.slice(0, 3))).toEqual([
      0.5, 0.5, 1,
    ]);
    expect(result.maps["world-normal"].data[1]).toBeCloseTo(0, 5);
    expect(result.maps["world-normal"].data[2]).toBeCloseTo(0.5, 5);
    expect(result.maps["object-normal"].data[1]).toBeCloseTo(0.5, 5);
    expect(result.maps["object-normal"].data[2]).toBeCloseTo(1, 5);
    const convention = { normalY: "-Y", colorSpace: "linear" };
    const a = encodeBakeMap(
        result.maps["world-normal"],
        64,
        64,
        convention,
        result.coverage,
      ),
      b = encodeBakeMap(
        result.maps["world-normal"],
        64,
        64,
        { ...convention, normalY: "+Y" },
        result.coverage,
      );
    expect(a).toEqual(b); // OpenGL/DirectX only flips tangent normal fields.
  } finally {
    geometry.dispose();
  }
});

test("bent normals turn away from an occluding wall; secondary AO uses its separate distance", async () => {
  const floor = new THREE.PlaneGeometry(1, 1),
    wall = new THREE.PlaneGeometry(2, 1);
  wall.rotateY(Math.PI / 2);
  wall.translate(0.3, 0, 0.4);
  try {
    const high = mergeBakeMeshData([{ geometry: floor }, { geometry: wall }]),
      result = await bakeMeshMaps(
        bakeMeshData(floor),
        high,
        settings(
          [
            "normal",
            "bent-normal",
            "bent-world-normal",
            "bent-object-normal",
            "occlusion",
            "occlusion-secondary",
          ],
          { samples: 64, aoDistance: 0.8, secondaryAoDistance: 0.1 },
        ),
      );
    const i = (32 * 64 + 32) * 4;
    expect(result.maps.occlusion.data[i]).toBeLessThan(0.95);
    expect(result.maps["occlusion-secondary"].data[i]).toBe(1);
    expect(result.maps["bent-world-normal"].data[i]).toBeLessThan(0.47);
    expect(result.maps.normal.data[i]).toBe(0.5);
    for (const key of [
      "bent-normal",
      "bent-world-normal",
      "bent-object-normal",
    ]) {
      const v = Array.from(result.maps[key].data.slice(i, i + 3)).map(
        (c) => (c - 0.5) * 2,
      );
      expect(Math.hypot(...v)).toBeCloseTo(1, 5);
    }
  } finally {
    floor.dispose();
    wall.dispose();
  }
});

test("dust changes with up axis and curvature splits into genuine convexity and concavity", async () => {
  const upward = await bakeMeshMaps(
      plane(),
      plane(),
      settings(["dust"], { dustAxis: "+Z" }),
    ),
    downward = await bakeMeshMaps(
      plane(),
      plane(),
      settings(["dust"], { dustAxis: "-Z" }),
    );
  expect(upward.maps.dust.data[0]).toBeGreaterThan(0.5);
  expect(downward.maps.dust.data[0]).toBe(0);
  const assets = createDemoBakeAssets("Sphere");
  try {
    const result = await bakeMeshMaps(
      bakeMeshData(assets.low[0].geometry),
      bakeMeshData(assets.high[0].geometry),
      settings(["curvature", "convexity", "cavity"], { curvatureRadius: 0.15 }),
    );
    let convex = 0;
    for (let i = 0; i < result.coverage.length; i++)
      if (result.coverage[i]) {
        const sign = (result.maps.curvature.data[i * 4] - 0.5) * 2;
        expect(result.maps.convexity.data[i * 4]).toBeCloseTo(
          Math.max(0, Math.min(1, sign * result.settings.maskStrength)),
          5,
        );
        expect(result.maps.cavity.data[i * 4]).toBeCloseTo(
          Math.max(0, Math.min(1, -sign * result.settings.maskStrength)),
          5,
        );
        if (result.maps.convexity.data[i * 4] > 0.01) convex++;
      }
    expect(convex).toBeGreaterThan(100);
  } finally {
    disposeBakeParts(assets.low);
    disposeBakeParts(assets.high);
  }
});

test("large all-map jobs fail early with a working-field budget instead of allocating unbounded buffers", async () => {
  await expect(
    bakeMeshMaps(
      plane(),
      plane(),
      settings(Object.keys(MESH_MAPS), { resolution: 1024 }),
    ),
  ).rejects.toThrow(/320 MB/);
});

test("legacy baking drafts are archived verbatim before settings-only migration, and quota failures keep the original", () => {
  const raw = JSON.stringify({
      schema: "alloy.baking-recipe.v1",
      settings: settings(["normal"]),
      graph: { nodes: [{ id: "custom-user-node" }] },
    }),
    data = new Map([[BAKE_PREFS_KEY, raw]]);
  const storage = {
    getItem: (key) => data.get(key) ?? null,
    setItem: (key, value) => data.set(key, value),
  };
  expect(restoreBakeRecipe(storage).legacy).toBe(true);
  expect(data.get(LEGACY_BAKE_BACKUP_KEY)).toBe(raw);
  expect(data.get(BAKE_PREFS_KEY)).toBe(raw);
  const quota = {
    getItem: (key) => (key === BAKE_PREFS_KEY ? raw : null),
    setItem: () => {
      throw new Error("quota");
    },
  };
  expect(restoreBakeRecipe(quota).legacyBackupFailed).toBe(true);
});
