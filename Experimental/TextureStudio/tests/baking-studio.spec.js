import { test, expect } from "@playwright/test";
import { unzipSync, strFromU8 } from "fflate";
async function open(page, mode = "baking") {
  await page.goto(`/?studio=${mode}`);
  if (mode === "baking" || mode === "bake")
    await expect(page.locator(".bk-studio canvas")).toHaveAttribute(
      "data-mesh-ready",
      "true",
    );
  else if (mode === "texture")
    await expect(page.locator(".tp-studio canvas")).toHaveAttribute(
      "data-material-ready",
      "true",
    );
}
async function quick(page) {
  await page
    .getByLabel("Mesh bake resolution", { exact: true })
    .selectOption("64");
  await page.getByLabel("AO samples", { exact: true }).selectOption("4");
}
async function zip(page) {
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Download maps ZIP", exact: true })
    .click();
  const chunks = [];
  for await (const chunk of await (await waiting).createReadStream())
    chunks.push(chunk);
  return unzipSync(Buffer.concat(chunks));
}
async function upload(page, role, text) {
  await page.getByLabel(`${role} mesh file`, { exact: true }).setInputFiles({
    name: `${role}.obj`,
    mimeType: "text/plain",
    buffer: Buffer.from(text),
  });
}
function planeOBJ(name, x, z, uv = true) {
  return `o ${name}\nv ${x} 0 ${z}\nv ${x + 1} 0 ${z}\nv ${x + 1} 1 ${z}\nv ${x} 1 ${z}\n${uv ? "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n" : ""}${uv ? "f 1/1 2/2 3/3\nf 1/1 3/3 4/4" : "f 1 2 3\nf 1 3 4"}\n`;
}
const lowBatch =
  "o A_low\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3\nf 1/1 3/3 4/4\no B_low\nv 2 0 0\nv 3 0 0\nv 3 1 0\nv 2 1 0\nf 5/1 6/2 7/3\nf 5/1 7/3 8/4\n";
const highBatch = lowBatch
  .replaceAll("_low", "_high")
  .replace(/v (\d) (\d) 0/g, "v $1 $2 0.1");

test("shared workspace navigation opens Pattern and Baking instead of empty modal panes", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page, "texture");
  await page.getByRole("button", { name: "Pattern", exact: true }).click();
  await expect(
    page.getByLabel("Pattern design canvas", { exact: true }),
  ).toBeVisible();
  await expect(page.locator(".pe-overlay")).toHaveClass(/studio-pattern/);
  await page.getByRole("button", { name: "Baking", exact: true }).click();
  await expect(page.locator('canvas[data-mesh-ready="true"]')).toBeVisible();
  await page.getByRole("button", { name: "Material", exact: true }).click();
  await expect(page.locator(".studio-material")).toBeVisible();
  const l = await page.locator(".library-panel").boundingBox(),
    i = await page.locator(".inspector-panel").boundingBox(),
    v = await page.locator(".viewport-panel").boundingBox();
  expect(l.x + l.width).toBeLessThanOrEqual(v.x);
  expect(v.x + v.width).toBeLessThanOrEqual(i.x);
  await page
    .getByRole("button", { name: "Pattern studio", exact: true })
    .click();
  await expect(
    page.getByLabel("Pattern design canvas", { exact: true }),
  ).toBeVisible();
  expect(errors).toEqual([]);
});
test("baking a teapot writes real mesh maps, coverage statistics, map metadata and convention-aware PNGs", async ({
  page,
}) => {
  await open(page);
  await quick(page);
  await page
    .getByLabel("Mesh normal convention", { exact: true })
    .selectOption("-Y");
  await page
    .getByRole("button", { name: "Bake mesh maps", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Download maps ZIP", exact: true }),
  ).toBeVisible();
  await expect(page.locator(".bk-result-summary")).toContainText("32 charts");
  await expect(page.locator(".bk-result-summary")).toContainText("0 misses");
  const files = await zip(page),
    manifest = JSON.parse(strFromU8(files["manifest.json"]));
  expect(manifest.schema).toBe("alloy.mesh-bake.v1");
  expect(manifest.sets).toHaveLength(1);
  expect(manifest.settings.normalY).toBe("-Y");
  expect(manifest.sets[0].stats.hits).toBeGreaterThan(3000);
  expect(manifest.graph).toBeUndefined();
  expect(manifest.workflow).toBe("mesh-maps");
  expect(Object.keys(files).filter((k) => k.endsWith(".png"))).toHaveLength(9);
  const png = files[`${manifest.sets[0].folder}/normal.png`];
  expect([...png.slice(1, 4)]).toEqual([80, 78, 71]);
  expect(new DataView(png.buffer, png.byteOffset).getUint32(16)).toBe(64);
  await expect(
    page.getByRole("img", { name: "Tangent normal mesh map", exact: true }),
  ).toBeVisible();
});
test("baking imported low/high OBJ batches matches names and exports every low object as its own set", async ({
  page,
}) => {
  await open(page);
  await upload(page, "Low", lowBatch);
  await expect(
    page.getByLabel("Bake target A_low", { exact: true }),
  ).toBeVisible();
  await expect(
    page.getByLabel("Bake target B_low", { exact: true }),
  ).toBeVisible();
  await upload(page, "High", highBatch);
  await quick(page);
  await page.getByLabel("Match high meshes by name", { exact: true }).check();
  await page
    .getByRole("button", { name: "Bake all selected meshes", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Download maps ZIP", exact: true }),
  ).toBeVisible();
  const files = await zip(page),
    manifest = JSON.parse(strFromU8(files["manifest.json"]));
  expect(manifest.sets.map((s) => s.name)).toEqual(["A_low", "B_low"]);
  expect(manifest.sets.every((s) => s.stats.misses === 0)).toBe(true);
  expect(
    Object.keys(files).filter((k) => k.endsWith("normal.png")),
  ).toHaveLength(4);
});
test("baking invalid UV imports and mismatched high names fail visibly without blanking the workspace", async ({
  page,
}) => {
  await open(page);
  await upload(page, "Low", planeOBJ("NoUV", 0, 0, false));
  await expect(page.getByRole("alert")).toContainText("UV0");
  await expect(
    page.getByLabel("Bake target Teapot", { exact: true }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Dismiss baking error", exact: true })
    .click();
  await upload(page, "Low", planeOBJ("Different_low", 0, 0));
  await quick(page);
  await page.getByLabel("Match high meshes by name", { exact: true }).check();
  await page
    .getByRole("button", { name: "Bake mesh maps", exact: true })
    .click();
  await expect(page.getByRole("alert")).toContainText("No high mesh matches");
  await expect(
    page.getByRole("button", { name: "Bake mesh maps", exact: true }),
  ).toBeEnabled();
  await expect(page.locator(".bk-preview")).toBeVisible();
});
test("baking map grid toggles green, removes nodes, saves v2 recipes and keeps settings history", async ({
  page,
}) => {
  await open(page);
  await expect(
    page.getByRole("button", { name: "Nodes", exact: true }),
  ).toHaveCount(0);
  await expect(
    page.locator(".bk-node-library, .bk-node-editor, .bk-node"),
  ).toHaveCount(0);
  const tile = page.getByRole("button", {
    name: "Bake Bevel normal",
    exact: true,
  });
  await expect(tile).toHaveAttribute("aria-pressed", "false");
  await tile.click();
  await expect(tile).toHaveAttribute("aria-pressed", "true");
  await expect
    .poll(() => tile.evaluate((e) => getComputedStyle(e).borderTopColor))
    .toBe("rgb(99, 198, 142)");
  await page
    .getByRole("spinbutton", { name: "Bevel radius", exact: true })
    .fill("0.15");
  const waiting = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save recipe", exact: true }).click();
  const chunks = [];
  for await (const chunk of await (await waiting).createReadStream())
    chunks.push(chunk);
  const recipe = JSON.parse(Buffer.concat(chunks).toString());
  expect(recipe.schema).toBe("alloy.baking-recipe.v2");
  expect(recipe.graph).toBeUndefined();
  expect(recipe.settings.bevelRadius).toBe(0.15);
  expect(recipe.settings.channels).toContain("bevel-normal");
  expect(recipe.sources.embedded).toBe(false);
  await page
    .getByRole("button", { name: "Undo bake settings", exact: true })
    .click();
  await expect(
    page.getByRole("spinbutton", { name: "Bevel radius", exact: true }),
  ).toHaveValue("0.025");
  await page
    .getByRole("button", { name: "Redo bake settings", exact: true })
    .click();
  await expect(
    page.getByRole("spinbutton", { name: "Bevel radius", exact: true }),
  ).toHaveValue("0.15");
  await page.getByRole("button", { name: "Clear maps", exact: true }).click();
  await expect(page.locator('.bk-map-tile[aria-pressed="true"]')).toHaveCount(
    0,
  );
  await expect(
    page.getByRole("button", { name: "Bake mesh maps", exact: true }),
  ).toBeDisabled();
  await page
    .getByRole("button", { name: "Bake Dust mask", exact: true })
    .click();
  await expect(page.getByLabel("Dust up axis", { exact: true })).toBeVisible();
  await page
    .getByRole("button", { name: "Bake UV island ID", exact: true })
    .click();
  await expect(page.locator('.bk-map-tile[aria-pressed="true"]')).toHaveCount(
    2,
  );
});

test("all 27 tiles produce actual PNGs, normal-space metadata and a UV island palette without nodes", async ({
  page,
}) => {
  await open(page);
  await quick(page);
  await page.getByLabel("Bake demo mesh", { exact: true }).selectOption("Cube");
  await page.getByRole("button", { name: "All maps", exact: true }).click();
  await expect(page.locator('.bk-map-tile[aria-pressed="true"]')).toHaveCount(
    27,
  );
  await page
    .getByRole("button", { name: "Bake mesh maps", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Download maps ZIP", exact: true }),
  ).toBeVisible();
  const files = await zip(page),
    manifest = JSON.parse(strFromU8(files["manifest.json"]));
  expect(Object.keys(files).filter((k) => k.endsWith(".png"))).toHaveLength(27);
  expect(manifest.graph).toBeUndefined();
  expect(manifest.sets[0].chartPalette).toHaveLength(6);
  const channels = manifest.sets[0].channels;
  expect(channels["object-normal"].space).toBe("object");
  expect(channels["world-normal"].space).toBe("world");
  expect(channels["bent-normal"].normalConvention).toBe("OpenGL +Y");
  expect(channels["world-normal"].normalConvention).toBeUndefined();
  for (const map of [
    "uv-island",
    "dust",
    "bevel-normal",
    "bent-world-normal",
  ]) {
    await page
      .getByLabel("Preview mesh map", { exact: true })
      .selectOption(map);
    await expect(page.locator(".bk-map-image img")).toBeVisible();
  }
});

test("baking cancellation stops the worker and workspace switching does not leave a hidden active job", async ({
  page,
}) => {
  await open(page);
  await page
    .getByLabel("Mesh bake resolution", { exact: true })
    .selectOption("512");
  await page.getByLabel("AO samples", { exact: true }).selectOption("64");
  await page
    .getByRole("button", { name: "Bake mesh maps", exact: true })
    .click();
  await page.getByRole("button", { name: "Cancel bake", exact: true }).click();
  await expect(page.getByRole("alert")).toContainText("Bake cancelled");
  await expect(
    page.getByRole("button", { name: "Bake mesh maps", exact: true }),
  ).toBeEnabled();
  await page.getByRole("button", { name: "Pattern", exact: true }).click();
  await expect(
    page.getByLabel("Pattern design canvas", { exact: true }),
  ).toBeVisible();
});
test("baking route aliases, patch mode and phone dock controls remain visible and overflow-free", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page, "bake");
  await page
    .getByRole("button", { name: "Material patch", exact: true })
    .click();
  await expect(
    page.getByRole("heading", { name: "Bake a surface patch", exact: true }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Bake & download ZIP", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Mesh maps", exact: true }).click();
  await page.setViewportSize({ width: 390, height: 844 });
  await page.getByRole("tab", { name: "Inspector", exact: true }).click();
  await page
    .getByLabel("Mesh bake resolution", { exact: true })
    .selectOption("128");
  await expect(
    page.getByLabel("Mesh bake resolution", { exact: true }),
  ).toHaveValue("128");
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth > innerWidth,
    ),
  ).toBe(false);
  await page.getByRole("button", { name: "Texture", exact: true }).click();
  await expect(page.locator(".tp-studio")).toBeVisible();
  expect(errors).toEqual([]);
});
