import { test, expect } from "@playwright/test";

for (const Workspace of ["material", "pattern"])
{
    test(`${Workspace} renders the lagged camera and confines the canvas to its viewport`, async ({ page: Page }) =>
    {
        const Errors = [];
        Page.on("pageerror", (Error) => Errors.push(Error.message));
        await Page.goto(`/?studio=${Workspace}`);
        if (Workspace === "pattern") await Page.getByRole("button", { name: "3D material", exact: true }).click();
        const Canvas = Page.locator(Workspace === "pattern" ? ".pe-material-preview canvas" : ".viewport-panel canvas");
        await expect(Canvas).toHaveAttribute("data-material-ready", "true");
        await expect(Canvas).toHaveAttribute("data-camera-lag", "0.12");
        const Before = await Canvas.getAttribute("data-camera-pose");
        const Bounds = await Canvas.boundingBox();
        await Page.mouse.move(Bounds.x + Bounds.width * 0.5, Bounds.y + Bounds.height * 0.5);
        await Page.mouse.down({ button: "middle" });
        await Page.mouse.move(Bounds.x + Bounds.width * 0.6, Bounds.y + Bounds.height * 0.55, { steps: 6 });
        await Page.mouse.up({ button: "middle" });
        await expect.poll(() => Canvas.getAttribute("data-camera-pose")).not.toBe(Before);
        const Target = await Canvas.getAttribute("data-camera-target");
        await Page.keyboard.down("Shift");
        await Page.mouse.down({ button: "middle" });
        await Page.mouse.move(Bounds.x + Bounds.width * 0.65, Bounds.y + Bounds.height * 0.6, { steps: 4 });
        await Page.mouse.up({ button: "middle" });
        await Page.keyboard.up("Shift");
        await expect.poll(() => Canvas.getAttribute("data-camera-target")).not.toBe(Target);
        const Zoom = Number(await Canvas.getAttribute("data-rendered-zoom"));
        await Page.mouse.wheel(0, -80);
        await expect.poll(async () => Number(await Canvas.getAttribute("data-rendered-zoom"))).toBeGreaterThan(Zoom);
        await Canvas.focus();
        await Page.keyboard.press("NumpadDecimal");
        await expect(Canvas).toHaveAttribute("data-zoom", "100");
        expect(await Canvas.evaluate((Surface) =>
        {
            const CanvasBounds = Surface.getBoundingClientRect();
            const HostBounds = Surface.parentElement.getBoundingClientRect();
            return Math.abs(CanvasBounds.width - HostBounds.width) < 1 && Math.abs(CanvasBounds.height - HostBounds.height) < 1;
        })).toBe(true);
        expect(Errors).toEqual([]);
    });
}
