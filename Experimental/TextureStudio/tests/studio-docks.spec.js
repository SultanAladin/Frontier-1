import { test, expect } from "@playwright/test";
const docks = {
  material: [".library-panel", ".viewport-panel", ".inspector-panel"],
  pattern: [".pe-library", ".pe-workspace", ".pe-inspector"],
  texture: [".tp-layers-dock", ".tp-viewport-dock", ".tp-inspector-dock"],
  stamp: [".stamp-left", ".stamp-centre", ".stamp-right"],
  baking: [".bk-assets", ".bk-preview", ".bk-inspector"],
};
for (const width of [1440, 1024, 780]) {
  test(`all five studios keep libraries / layers / baking left and inspectors right at ${width}px`, async ({
    page,
  }) => {
    await page.setViewportSize({ width, height: 900 });
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    page.on("console", (m) => {
      if (/Invalid hook call|Multiple instances of Three/.test(m.text()))
        errors.push(m.text());
    });
    await page.goto("/?studio=texture");
    await expect(page.locator(".tp-studio canvas")).toHaveAttribute(
      "data-material-ready",
      "true",
    );
    for (const workspace of [
      "texture",
      "pattern",
      "stamp",
      "baking",
      "material",
    ]) {
      if (workspace !== "texture")
        await page
          .getByRole("navigation", { name: "Studio workspace" })
          .getByRole("button", {
            name: new RegExp(`^${workspace}(?: studio)?$`, "i"),
          })
          .click();
      await expect(page.locator(".studio-workspace-error")).toHaveCount(0);
      const [left, center, right] = docks[workspace].map((s) =>
        page.locator(s),
      );
      for (const p of [left, center, right]) await expect(p).toBeVisible();
      const [l, c, r] = await Promise.all([
        left.boundingBox(),
        center.boundingBox(),
        right.boundingBox(),
      ]);
      expect(l.x + l.width).toBeLessThanOrEqual(c.x + 0.5);
      expect(c.x + c.width).toBeLessThanOrEqual(r.x + 0.5);
      expect(l.y).toBeCloseTo(r.y, 0);
      expect(r.x + r.width).toBeLessThanOrEqual(width + 0.5);
      expect(c.width).toBeGreaterThan(150);
      expect(
        await page.evaluate(
          () => document.documentElement.scrollWidth > innerWidth,
        ),
      ).toBe(false);
    }
    expect(errors).toEqual([]);
  });
}

test("phone docks retain accessible tabs and the map picker without overflowing", async ({
  page,
}) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto("/?studio=baking");
  await expect(page.locator(".bk-studio canvas")).toHaveAttribute(
    "data-mesh-ready",
    "true",
  );
  await page.getByRole("tab", { name: "Meshes / maps", exact: true }).click();
  await page
    .getByRole("button", { name: "Bake Dust mask", exact: true })
    .click();
  await page.getByRole("tab", { name: "Inspector", exact: true }).click();
  await expect(page.getByLabel("Dust up axis", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Texture", exact: true }).click();
  await page.getByRole("tab", { name: "Inspector", exact: true }).click();
  await expect(
    page.getByRole("textbox", { name: "Layer name", exact: true }),
  ).toBeVisible();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth > innerWidth,
    ),
  ).toBe(false);
});
