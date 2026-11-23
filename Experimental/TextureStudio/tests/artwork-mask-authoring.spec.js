import { test, expect } from "@playwright/test";
import { fileURLToPath } from "node:url";
import { NormalizeArtworkMask } from "../src/ArtworkMask.js";
const Capture = Name => fileURLToPath(new URL(`../../../Exhibits/TextureStudio/Images/${Name}.png`, import.meta.url));
test.describe.configure({ timeout: 60000 });
async function Save(Page, Workspace)
{
    const Download = Page.waitForEvent("download");
    await Page.getByRole("button", { name: Workspace === "pattern" ? "Save document" : "Save stamp project", exact: true }).click();
    const Chunks = []; for await (const Chunk of await (await Download).createReadStream()) Chunks.push(Chunk);
    return JSON.parse(Buffer.concat(Chunks).toString());
}
async function Drag(Page, Start = [.25, .5], End = [.75, .5])
{
    const Bounds = await Page.getByRole("application", { name: "Paint selected layer mask", exact: true }).boundingBox();
    await Page.mouse.move(Bounds.x + Bounds.width * Start[0], Bounds.y + Bounds.height * Start[1]);
    await Page.mouse.down(); await Page.mouse.move(Bounds.x + Bounds.width * End[0], Bounds.y + Bounds.height * End[1], { steps: 8 }); await Page.mouse.up();
}
async function AddPattern(Page)
{
    await Page.goto("/?studio=pattern&pattern=Blank");
    await Page.getByRole("button", { name: "Add pattern layer", exact: true }).click();
    await Page.getByRole("group", { name: "Add a pattern motif", exact: true }).getByRole("button", { name: "Rectangle", exact: true }).click();
}
test("Retained mask paint validates bounds and rejects corrupt geometry", () =>
{
    expect(() => NormalizeArtworkMask({ category: "paint", paint: [{ category: "stroke", points: [[NaN, 0], [1, 1]] }] })).toThrow();
    expect(() => NormalizeArtworkMask({ category: "paint", paint: Array(257).fill({}) })).toThrow();
    const Mask = NormalizeArtworkMask({ category: "paint", base: 1, paint: [{ category: "rect", points: [[-.5, -.2], [1.5, 2]], ink: 0 }] });
    expect(Mask.paint[0].points).toEqual([[0, 0], [1, 1]]);
    expect(NormalizeArtworkMask(JSON.parse(JSON.stringify(Mask)))).toEqual(Mask);
});
test("Painted and filled mask geometry changes actual exported pixels without changing artwork", async ({ page }) =>
{
    await page.goto("/?studio=pattern&pattern=Blank");
    const Pixels = await page.evaluate(async () =>
    {
        const { patternLayer, patternSVG } = await import("/src/patternDocument.js");
        const Geometry = patternLayer("rect", { paint: true, fillEnabled: true, width: 400, height: 400, x: 256, y: 256 });
        async function Samples(Mask)
        {
            const Svg = patternSVG({ schema: "alloy.pattern.v1", tileAxes: "none", backgroundOpacity: 0, layers: [{ ...Geometry, mask: Mask }] });
            const Image = new window.Image(); Image.src = `data:image/svg+xml,${encodeURIComponent(Svg)}`; await Image.decode();
            const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 512; const Surface = Canvas.getContext("2d"); Surface.drawImage(Image, 0, 0);
            return [[256, 256], [100, 100]].map(([X, Y]) => Surface.getImageData(X, Y, 1, 1).data[3]);
        }
        const Stroke = { category: "stroke", points: [[.2, .5], [.8, .5]], width: .1, ink: 0 };
        const Rectangle = { category: "rect", points: [[.25, .25], [.75, .75]], ink: 1 };
        return { Painted: await Samples({ category: "paint", base: 1, paint: [Stroke] }), Filled: await Samples({ category: "paint", base: 0, paint: [Rectangle] }), Inverted: await Samples({ category: "paint", base: 0, invert: true, paint: [Rectangle] }) };
    });
    expect(Pixels).toEqual({ Painted: [0, 255], Filled: [255, 0], Inverted: [0, 255] });
});
for (const Workspace of ["pattern", "stamp"])
{
    test(`${Workspace}: owned mask row, painting, fills, history and compact toolbar`, async ({ page }) =>
    {
        const Errors = []; page.on("pageerror", Failure => Errors.push(Failure.message));
        if (Workspace === "pattern") await AddPattern(page);
        else
        {
            await page.goto("/?studio=stamp");
            await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
            await page.getByRole("menuitem", { name: "Rectangle", exact: true }).click();
        }
        const Before = await Save(page, Workspace);
        const OwnerName = Workspace === "pattern" ? Before.layers.at(-1).name : "Rectangle";
        const Toolbar = page.getByRole("toolbar", { name: "Drawing tools", exact: true });
        const ToolbarBounds = await Toolbar.boundingBox(); expect(ToolbarBounds.height).toBeLessThan(105);
        await expect(page.getByRole("group", { name: "Boolean operations", exact: true })).toHaveCount(0);
        await page.getByRole("button", { name: "Add mask", exact: true }).click();
        await expect(page.getByRole("button", { name: `Select mask of ${OwnerName}`, exact: true })).toHaveAttribute("aria-pressed", "true");
        await expect(page.getByRole("application", { name: "Paint selected layer mask", exact: true })).toBeVisible();
        await Drag(page);
        const Painted = await Save(page, Workspace), Index = Painted.layers.findIndex(Layer => Layer.name === OwnerName);
        expect(Painted.layers[Index].mask.paint).toHaveLength(1);
        expect(Painted.layers[Index].mask.paint[0].ink).toBe(0);
        expect(Painted.layers[Index].kind).toBe(Before.layers[Index].kind);
        if (Workspace === "stamp") expect(Painted.layers.filter(Layer => Layer.name !== OwnerName)).toEqual(Before.layers.filter(Layer => Layer.name !== OwnerName));
        await page.getByRole("button", { name: "Undo mask edit", exact: true }).click();
        expect((await Save(page, Workspace)).layers[Index].mask.paint).toBeUndefined();
        await page.getByRole("button", { name: "Redo mask edit", exact: true }).click();
        expect((await Save(page, Workspace)).layers[Index].mask.paint).toHaveLength(1);
        await page.getByRole("button", { name: "Hide all", exact: true }).click();
        await page.getByRole("button", { name: "White — reveal", exact: true }).click();
        await page.getByRole("button", { name: "Ellipse mask", exact: true }).click();
        await Drag(page, [.2, .2], [.8, .8]);
        const Filled = await Save(page, Workspace);
        expect(Filled.layers[Index].mask.base).toBe(0); expect(Filled.layers[Index].mask.paint[0].category).toBe("ellipse");
        await page.screenshot({ path: Capture(`${Workspace === "pattern" ? "Pattern" : "Stamp"}MaskPainting`), fullPage: true });
        await page.getByRole("button", { name: "Done", exact: true }).click();
        await expect(page.getByRole("toolbar", { name: "Drawing tools", exact: true })).toBeVisible();
        await page.getByRole("button", { name: `Select mask of ${OwnerName}`, exact: true }).click();
        await expect(page.getByRole("application", { name: "Paint selected layer mask", exact: true })).toBeVisible();
        await page.getByRole("button", { name: "Remove mask", exact: true }).click();
        await expect(page.getByRole("button", { name: `Select mask of ${OwnerName}`, exact: true })).toHaveCount(0);
        expect((await Save(page, Workspace)).layers[Index].mask).toBeUndefined();
        expect(Errors).toEqual([]);
    });
}
test("Compact tool disclosures preserve additional shapes, snapping and arrange actions", async ({ page }) =>
{
    await AddPattern(page);
    await page.getByLabel("More drawing tools", { exact: true }).click();
    await page.getByRole("button", { name: "Star tool (S)", exact: true }).click();
    await expect(page.getByLabel("More drawing tools", { exact: true }).locator("..")).not.toHaveAttribute("open");
    await page.getByLabel("Canvas options", { exact: true }).click();
    await page.getByLabel("Grid snapping", { exact: true }).selectOption("16");
    await expect(page.getByLabel("Grid snapping", { exact: true })).toHaveValue("16");
    await page.getByLabel("Canvas options", { exact: true }).click();
    await expect(page.getByRole("button", { name: "Align left", exact: true })).toBeVisible();
    await page.screenshot({ path: Capture("PatternCompactTools"), fullPage: true });
});

test("Mask cancel, keyboard save, reopen and locked ownership remain safe", async ({ page }) =>
{
    await AddPattern(page);
    await page.getByRole("button", { name: "Add mask", exact: true }).click();
    const Surface = page.getByRole("application", { name: "Paint selected layer mask", exact: true });
    const Bounds = await Surface.boundingBox();
    await page.mouse.move(Bounds.x + 20, Bounds.y + 20); await page.mouse.down();
    await page.mouse.move(Bounds.x + 60, Bounds.y + 60); await page.keyboard.press("Escape"); await page.mouse.up();
    expect((await Save(page, "pattern")).layers[0].mask.paint).toBeUndefined();
    await Drag(page);
    const Saved = await Save(page, "pattern");
    await Surface.focus(); const KeyboardDownload = page.waitForEvent("download"); await page.keyboard.press("Control+s"); await KeyboardDownload;
    await page.getByLabel("Open pattern JSON").setInputFiles({ name: "painted.pattern.json", mimeType: "application/json", buffer: Buffer.from(JSON.stringify(Saved)) });
    expect((await Save(page, "pattern")).layers[0].mask).toEqual(Saved.layers[0].mask);
    await page.getByLabel("Layer lock 1: rect", { exact: true }).check();
    await expect(page.getByRole("button", { name: "Hide all", exact: true })).toBeDisabled();
    await Drag(page);
    expect((await Save(page, "pattern")).layers[0].mask).toEqual(Saved.layers[0].mask);
});
for (const Workspace of ["pattern", "stamp"])
{
    test(`${Workspace}: embedded-width layout keeps tools, mask and Done inside the viewport`, async ({ page }) =>
    {
        await page.setViewportSize({ width: 900, height: 680 });
        if (Workspace === "pattern") await AddPattern(page);
        else
        {
            await page.goto("/?studio=stamp"); await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
            await page.getByRole("menuitem", { name: "Rectangle", exact: true }).click();
        }
        await page.getByRole("button", { name: "Add mask", exact: true }).click();
        const Editor = await page.getByRole("application", { name: "Paint selected layer mask", exact: true }).boundingBox();
        const Done = await page.getByRole("button", { name: "Done", exact: true }).boundingBox();
        expect(Editor.width).toBeGreaterThan(100); expect(Editor.y + Editor.height).toBeLessThanOrEqual(680);
        expect(Done.y).toBeGreaterThan(0); expect(Done.y + Done.height).toBeLessThan(680);
        await page.screenshot({ path: Capture(`${Workspace === "pattern" ? "Pattern" : "Stamp"}MaskCompactViewport`), fullPage: true });
    });
}

test("Image alpha and luminance remain fractional beneath painted coverage", async ({ page }) =>
{
    await page.goto("/?studio=pattern&pattern=Blank");
    const Samples = await page.evaluate(async () =>
    {
        const { NormalizeArtworkMask, ApplyArtworkMask } = await import("/src/ArtworkMask.js");
        const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 1;
        const Context = Canvas.getContext("2d"); const Pixel = Context.createImageData(1, 1); Pixel.data.set([128, 128, 128, 128]); Context.putImageData(Pixel, 0, 0);
        const Source = Canvas.toDataURL(); Canvas.width = Canvas.height = 64;
        const Result = {};
        for (const Mode of ["alpha", "luminance"])
        {
            const Mask = NormalizeArtworkMask({ category: "image", image: Source, mode: Mode, paint: [{ category: "rect", points: [[.25, .25], [.75, .75]], ink: 0, opacity: .5 }] });
            const SVG = `<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">${ApplyArtworkMask({ id: Mode, mask: Mask }, '<rect width="64" height="64" fill="white"/>')}</svg>`;
            const Image = new window.Image(); Image.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(SVG)}`; await Image.decode();
            Context.clearRect(0, 0, 64, 64); Context.drawImage(Image, 0, 0);
            Result[Mode] = [Context.getImageData(4, 4, 1, 1).data[3], Context.getImageData(32, 32, 1, 1).data[3]];
        }
        return Result;
    });
    expect(Samples.alpha[0]).toBeCloseTo(128, -1); expect(Samples.alpha[1]).toBeCloseTo(64, -1);
    expect(Samples.luminance[0]).toBeCloseTo(64, -1); expect(Samples.luminance[1]).toBeCloseTo(32, -1);
});

test("Reveal-all masks preserve strokes outside the fill bounds", async ({ page }) =>
{
    await page.goto("/?studio=pattern&pattern=Blank");
    const Samples = await page.evaluate(async () =>
    {
        const { patternLayer, patternSVG } = await import("/src/patternDocument.js");
        const Shape = patternLayer("rect", { x: 256, y: 256, width: 200, height: 200, fillEnabled: true, strokeWidth: 30 });
        const Result = [];
        for (const Mask of [undefined, { category: "paint", base: 1 }])
        {
            const SVG = patternSVG({ schema: "alloy.pattern.v1", tileAxes: "none", backgroundOpacity: 0, layers: [{ ...Shape, mask: Mask }] });
            const Image = new window.Image(); Image.src = `data:image/svg+xml,${encodeURIComponent(SVG)}`; await Image.decode();
            const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 512; const Surface = Canvas.getContext("2d"); Surface.drawImage(Image, 0, 0);
            Result.push([[147, 256], [256, 147], [365, 256], [256, 365]].map(([X, Y]) => Surface.getImageData(X, Y, 1, 1).data[3]));
        }
        return Result;
    });
    expect(Samples[0]).toEqual([255, 255, 255, 255]); expect(Samples[1]).toEqual(Samples[0]);
});

test("Flat stroked paths keep a nonzero mask domain", async ({ page }) =>
{
    await page.goto("/?studio=pattern&pattern=Blank");
    const Samples = await page.evaluate(async () =>
    {
        const { patternLayer, patternSVG } = await import("/src/patternDocument.js");
        const Shape = patternLayer("path", { x: 256, y: 256, width: 200, height: 1, paint: true, closed: false, fillEnabled: false, strokeWidth: 30, nodes: [{ x: 0, y: 50 }, { x: 100, y: 50 }] });
        const Result = [];
        for (const Mask of [undefined, { category: "paint", base: 1 }])
        {
            const SVG = patternSVG({ schema: "alloy.pattern.v1", tileAxes: "none", backgroundOpacity: 0, layers: [{ ...Shape, mask: Mask }] });
            const Image = new window.Image(); Image.src = `data:image/svg+xml,${encodeURIComponent(SVG)}`; await Image.decode();
            const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 512; const Surface = Canvas.getContext("2d"); Surface.drawImage(Image, 0, 0);
            Result.push([[256, 256], [256, 268]].map(([X, Y]) => Surface.getImageData(X, Y, 1, 1).data[3]));
        }
        return Result;
    });
    expect(Samples[0][0]).toBe(255); expect(Samples[0][1]).toBeGreaterThan(0); expect(Samples[1]).toEqual(Samples[0]);
});
