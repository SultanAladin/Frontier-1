import { test, expect } from "@playwright/test";
import { fileURLToPath } from "node:url";

test.describe.configure({ timeout: 60000 });
test.use({ actionTimeout: 10000 });

async function Project(Page)
{
    const Pending = Page.waitForEvent("download");
    await Page.getByRole("button", { name: "Save stamp project", exact: true }).click();
    const Chunks = [];
    for await (const Chunk of await (await Pending).createReadStream()) Chunks.push(Chunk);
    return JSON.parse(Buffer.concat(Chunks).toString());
}

test("Stamp stack uses shared search, counts, filters and keyboard-accessible additive menu", async ({ page }) =>
{
    await page.goto("/?studio=stamp");
    const Stack = page.getByRole("complementary", { name: "Stamp components panel", exact: true });
    const Rows = Stack.getByRole("list", { name: "Stamp components", exact: true }).getByRole("listitem");
    await expect(Rows).toHaveCount(3);
    const Overlap = await Stack.locator(".tp-layer-stats button").evaluateAll(Buttons => Buttons.some(Button =>
    {
        const Caption = Button.querySelector("small").getBoundingClientRect();
        const Count = Button.querySelector("strong").getBoundingClientRect();
        return Caption.left < Count.right && Caption.right > Count.left && Caption.top < Count.bottom && Caption.bottom > Count.top;
    }));
    expect(Overlap).toBe(false);
    await expect(Stack.locator(".stamp-add-tools")).toHaveCount(0);
    await Stack.getByLabel("Find a layer", { exact: true }).fill("Maker ring");
    await expect(Rows).toHaveCount(1);
    await page.getByRole("button", { name: "Hide stamp component Maker ring", exact: true }).click();
    await Stack.getByRole("button", { name: "Hidden layers", exact: true }).click();
    await expect(Rows).toHaveCount(1);
    const Add = Stack.getByRole("button", { name: "Add stamp component", exact: true });
    await Add.click();
    const Menu = page.getByRole("menu", { name: "Add stamp component", exact: true });
    await expect(Menu.getByRole("menuitem")).toHaveCount(10);
    await expect(Menu.getByRole("menuitem", { name: "Text", exact: true })).toBeFocused();
    await page.keyboard.press("End");
    await expect(Menu.getByRole("menuitem", { name: "Star", exact: true })).toBeFocused();
    await page.keyboard.press("Escape");
    await expect(Menu).toHaveCount(0);
    await expect(Add).toBeFocused();
    await Add.click();
    await Menu.getByRole("menuitem", { name: "Rectangle", exact: true }).click();
    await expect(Rows).toHaveCount(4);
    await expect(Stack.getByLabel("Find a layer", { exact: true })).toHaveValue("");
    await expect(Stack.getByRole("button", { name: "Hidden layers", exact: true })).toHaveAttribute("aria-pressed", "false");
    const Document = await Project(page);
    expect(Document.layers).toHaveLength(4);
    expect(Document.layers.find(Layer => Layer.name === "Maker ring").visible).toBe(false);
    await Stack.getByLabel("Find a layer", { exact: true }).fill("no-such-component");
    await expect(Stack.getByText("No matching components.", { exact: false })).toBeVisible();
    await Stack.getByRole("button", { name: "Clear filters", exact: true }).click();
    await expect(Rows).toHaveCount(4);
});

test("Canvas backgrounds persist as view preferences without altering exported decal pixels or history", async ({ page }) =>
{
    const Failures = [];
    page.on("pageerror", Failure => Failures.push(Failure.message));
    await page.goto("/?studio=stamp");
    const Before = await Project(page);
    await page.getByRole("button", { name: "Vector canvas", exact: true }).click();
    await page.getByLabel("Canvas options", { exact: true }).click();
    const Selector = page.getByRole("combobox", { name: "Vector canvas background", exact: true });
    const Backdrop = page.locator("[data-canvas-backdrop]");
    await expect(Selector).toHaveValue("transparent");
    await expect(Backdrop).toHaveAttribute("fill", "url(#se-checker)");
    await Selector.selectOption("ui");
    await expect(Backdrop).toHaveAttribute("fill", "var(--panel, #181818)");
    await Selector.selectOption("fill");
    await page.getByLabel("Vector canvas background colour", { exact: true }).fill("#316c91");
    await expect(Backdrop).toHaveAttribute("fill", "#316c91");
    await expect(page.getByRole("button", { name: "Undo stamp edit", exact: true })).toBeDisabled();
    expect(await Project(page)).toEqual(Before);
    const Alpha = await page.evaluate(async Document =>
    {
        const { composeStampSVG } = await import("/src/stampRaster.js");
        const Result = await composeStampSVG(Document);
        const Image = new window.Image();
        const Url = URL.createObjectURL(new Blob([Result.svg], { type: "image/svg+xml" }));
        Image.src = Url; await Image.decode();
        const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 512;
        const Context = Canvas.getContext("2d"); Context.drawImage(Image, 0, 0);
        URL.revokeObjectURL(Url);
        return Context.getImageData(0, 0, 1, 1).data[3];
    }, await Project(page));
    expect(Alpha).toBe(0);
    await page.getByLabel("Canvas options", { exact: true }).click();
    await page.getByRole("button", { name: "Composition", exact: true }).click();
    await page.getByRole("button", { name: "Vector canvas", exact: true }).click();
    await page.getByLabel("Canvas options", { exact: true }).click();
    await expect(Selector).toHaveValue("fill");
    await expect(Backdrop).toHaveAttribute("fill", "#316c91");
    await page.reload();
    await page.getByRole("button", { name: "Vector canvas", exact: true }).click();
    await expect(Selector).toHaveValue("fill");
    await expect(Backdrop).toHaveAttribute("fill", "#316c91");
    await page.setViewportSize({ width: 1600, height: 1000 });
    await page.screenshot({ path: fileURLToPath(new URL("../../../Exhibits/TextureStudio/Images/StampStackCanvasBackground.png", import.meta.url)), fullPage: true });
    expect(Failures).toEqual([]);
});
