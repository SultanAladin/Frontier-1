import { test, expect } from "@playwright/test";

for (const Width of [1440, 780, 390])
{
    test(`Pattern Layers owns an independent bounded panel at ${Width}px`, async ({ page: Page }) =>
    {
        await Page.setViewportSize({ width: Width, height: 900 });
        const Errors = [];
        Page.on("pageerror", (Error) => Errors.push(Error.message));
        await Page.goto("/?studio=pattern&pattern=blank");
        const Layers = Page.getByRole("complementary", { name: "Pattern layers", exact: true });
        await expect(Layers).toBeVisible();
        expect(await Layers.evaluate((Panel) => Panel.parentElement.classList.contains("pe-body"))).toBe(true);
        expect(await Page.locator(".pe-library .se-layer-panel").count()).toBe(0);
        await Page.getByRole("button", { name: "triangle", exact: true }).click();
        await expect(Layers.locator(".se-layer-row")).toHaveCount(1);
        const Visibility = Layers.getByRole("checkbox", { name: /^Layer visibility/ });
        await expect(Visibility).toBeChecked();
        await Visibility.uncheck();
        await expect(Visibility).not.toBeChecked();
        await Page.getByRole("button", { name: "Undo", exact: true }).click();
        await expect(Visibility).toBeChecked();
        const Lock = Layers.getByRole("checkbox", { name: /^Layer lock/ });
        await Lock.check();
        await expect(Lock).toBeChecked();
        await Page.getByRole("button", { name: "Undo", exact: true }).click();
        await expect(Lock).not.toBeChecked();
        expect(await Page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
        const Bounds = await Layers.boundingBox();
        expect(Bounds.width).toBeGreaterThan(100);
        expect(Bounds.x + Bounds.width).toBeLessThanOrEqual(Width + 1);
        expect(Errors).toEqual([]);
    });
}

test("Texture Painting uses the same layer-panel boundary without losing its controls", async ({ page: Page }) =>
{
    await Page.goto("/?studio=texture");
    const Layers = Page.getByRole("complementary", { name: "Layer stack", exact: true });
    await expect(Layers).toHaveClass(/layer-stack-panel/);
    await expect(Layers.getByRole("button", { name: "Add layer folder", exact: true })).toBeVisible();
    await expect(Layers.getByRole("textbox", { name: "Find a layer", exact: true })).toBeVisible();
    await Layers.getByRole("button", { name: "Add layer folder", exact: true }).click();
    await expect(Layers.locator('[data-layer-id]')).not.toHaveCount(0);
});
