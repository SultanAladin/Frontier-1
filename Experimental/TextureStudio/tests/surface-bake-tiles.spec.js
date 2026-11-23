import { test, expect } from "@playwright/test";
import { unzipSync, strFromU8 } from "fflate";
import { materials } from "../src/materials.js";

test("corrected corrugated zinc preset retains old recipe links without removing saved material data", async ({
  page,
}) => {
  expect(
    materials.some(
      (p) => p.id === "corrugated-zinc" && p.name === "Corrugated Zinc",
    ),
  ).toBe(true);
  expect(materials.some((p) => p.id === "corrugated-tin")).toBe(false);
  await page.goto("/?material=corrugated-tin");
  await expect(page.locator("h1")).toHaveText("Corrugated Zinc");
  await expect(
    page.getByText("Corrugated zinc sheet", { exact: true }),
  ).toBeVisible();
});

test("surface bake tiles select real data maps, stay green and show actual PNG previews after baking", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/?studio=baking&material=sand");
  await page
    .getByRole("button", { name: "Material patch", exact: true })
    .click();
  await expect(page.getByLabel("Bake resolution", { exact: true })).toHaveValue(
    "512",
  );
  await expect(page.locator(".surface-bake-grid .bk-map-tile")).toHaveCount(6);
  await page.getByRole("button", { name: "Masks / data", exact: true }).click();
  await expect(
    page.locator('.surface-bake-grid [aria-pressed="true"]'),
  ).toHaveCount(3);
  const rough = page.getByRole("button", {
    name: "Bake surface Roughness",
    exact: true,
  });
  await rough.hover();
  await expect
    .poll(() => rough.evaluate((e) => getComputedStyle(e).borderColor))
    .toBe("rgb(99, 198, 142)");
  await page.getByLabel("Bake resolution", { exact: true }).selectOption("256");
  await page.getByLabel("Bake patch width", { exact: true }).fill("16");
  const wait = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Bake & download ZIP", exact: true })
    .click();
  const stream = await (await wait).createReadStream(),
    chunks = [];
  for await (const chunk of stream) chunks.push(chunk);
  const files = unzipSync(Buffer.concat(chunks)),
    manifest = JSON.parse(strFromU8(files["material.json"]));
  expect(
    Object.keys(files)
      .filter((k) => k.endsWith(".png"))
      .sort(),
  ).toEqual(["height.png", "metalness.png", "roughness.png"]);
  expect(Object.keys(manifest.channels).sort()).toEqual([
    "height.png",
    "metalness.png",
    "roughness.png",
  ]);
  // Verify subset mode uses each original shader channel index: metalness must
  // remain black, not accidentally become the roughness/base-color pass.
  const max = await page.evaluate(async (bytes) => {
    const b = await createImageBitmap(
        new Blob([new Uint8Array(bytes)], { type: "image/png" }),
      ),
      c = document.createElement("canvas");
    c.width = b.width;
    c.height = b.height;
    const ctx = c.getContext("2d");
    ctx.drawImage(b, 0, 0);
    b.close();
    let max = 0;
    const d = ctx.getImageData(0, 0, c.width, c.height).data;
    for (let i = 0; i < d.length; i += 4) max = Math.max(max, d[i]);
    return max;
  }, Array.from(files["metalness.png"]));
  expect(max).toBe(0);
  await expect(page.locator(".surface-bake-grid img")).toHaveCount(3);
  await expect(page.locator(".bk-map-image img")).toBeVisible();
  await page
    .getByRole("button", { name: "Clear surface maps", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Bake & download ZIP", exact: true }),
  ).toBeDisabled();
  expect(errors).toEqual([]);
});
