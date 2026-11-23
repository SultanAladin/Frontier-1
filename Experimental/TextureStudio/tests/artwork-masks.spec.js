import { fileURLToPath } from "node:url";
import { test, expect } from "@playwright/test";
import { NormalizeArtworkMask } from "../src/ArtworkMask.js";
import { patternLayer, validatePattern, patternSVG } from "../src/patternDocument.js";

test.describe.configure({ timeout: 60000 });
test("Mask descriptors remain editable, bounded and reject linked images", () =>
{
    expect(() => NormalizeArtworkMask({ category: "image", image: "https://example.com/a.png" })).toThrow();
    const Mask = NormalizeArtworkMask({ category: "ellipse", width: 100, invert: true, opacity: 0 });
    const Document = validatePattern({ schema: "alloy.pattern.v1", layers: [patternLayer("rect", { mask: Mask })] });
    expect(Document.layers[0].mask).toEqual(Mask);
    expect(JSON.parse(JSON.stringify(Document)).layers[0].mask.opacity).toBe(0);
    expect(patternSVG(Document)).toContain('mask="url(#artwork-mask-');
});

test("Actual Pattern and Stamp mask pixels, invert, zero, native text preservation and icon-only Boolean controls", async ({ page }) =>
{
    const Errors = [];
    page.on("pageerror", Failure => Errors.push(Failure.message));
    await page.goto("/?studio=stamp");
    await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
    await page.getByRole("menuitem", { name: "Rectangle", exact: true }).click();
    await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
    await page.getByRole("menuitem", { name: "Ellipse", exact: true }).click();
    await page.getByRole("button", { name: "Select stamp component Rectangle", exact: true }).click({ modifiers: ["Control"] });
    const Boolean = page.getByRole("button", { name: "Difference", exact: true });
    await expect(Boolean.locator("svg")).toHaveCount(1);
    expect(await Boolean.textContent()).toBe("");
    await page.getByRole("button", { name: "Select stamp component Rectangle", exact: true }).click();
    await page.getByRole("button", { name: "Add mask", exact: true }).click();
    await page.getByRole("button", { name: "Invert artwork mask", exact: true }).click();
    const Results = await page.evaluate(async () =>
    {
        const { patternLayer, patternSVG, validatePattern } = await import("/src/patternDocument.js");
        const { createStampDocument, createStampLayer } = await import("/src/stampDocument.js");
        const { StampVectorCanvasDocument, StampDocumentFromVectorCanvas } = await import("/src/StampVectorProjection.js");
        const { composeStampSVG } = await import("/src/stampRaster.js");
        const Mask = { category: "ellipse", enabled: true, width: 100, height: 100, x: 50, y: 50, opacity: 1, mode: "alpha", invert: false };
        const Geometry = patternLayer("rect", { x: 256, y: 256, width: 300, height: 300, paint: true, fillEnabled: true, color: "#ff0000", mask: Mask });
        async function Pixels(Svg)
        {
            const Image = new window.Image(); const Url = URL.createObjectURL(new Blob([Svg], { type: "image/svg+xml" })); Image.src = Url; await Image.decode();
            const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 512; const Context = Canvas.getContext("2d"); Context.drawImage(Image, 0, 0, 512, 512); URL.revokeObjectURL(Url);
            return [Context.getImageData(256, 256, 1, 1).data[3], Context.getImageData(115, 115, 1, 1).data[3]];
        }
        const Document = { schema: "alloy.pattern.v1", tileAxes: "none", backgroundOpacity: 0, layers: [Geometry] };
        const Stamp = { ...createStampDocument(), layers: [createStampLayer("vector", { vector: Geometry, mask: Mask, x: 256, y: 256, width: 300, height: 300 })] };
        const Alpha = await Pixels(patternSVG(Document));
        const StampAlpha = await Pixels((await composeStampSVG(Stamp)).svg);
        const Inverted = await Pixels(patternSVG({ ...Document, layers: [{ ...Geometry, mask: { ...Mask, invert: true } }] }));
        const Zero = await Pixels(patternSVG({ ...Document, layers: [{ ...Geometry, mask: { ...Mask, opacity: 0 } }] }));
        const Projection = StampVectorCanvasDocument(Stamp);
        const Restored = StampDocumentFromVectorCanvas(Projection, Stamp).Document;
        const Native = createStampDocument(); Native.layers[0].mask = Mask;
        const NativeRestored = StampDocumentFromVectorCanvas(StampVectorCanvasDocument(Native), Native).Document;
        return { Alpha, StampAlpha, Inverted, Zero, MaskPreserved: JSON.stringify(Restored.layers[0].mask) === JSON.stringify(validatePattern(Document).layers[0].mask), TextPreserved: JSON.stringify(NativeRestored.layers[0].runs) === JSON.stringify(Native.layers[0].runs) };
    });
    expect(Results).toEqual({ Alpha: [255, 0], StampAlpha: [255, 0], Inverted: [0, 255], Zero: [0, 0], MaskPreserved: true, TextPreserved: true });
    await page.screenshot({ path: fileURLToPath(new URL("../../../Exhibits/TextureStudio/Images/StampMaskIcons.png", import.meta.url)), fullPage: true });
    expect(Errors).toEqual([]);
});

test("Pattern mask inspector participates in history, save/reopen and preserves Boolean icon names", async ({ page }) =>
{
    await page.goto("/?studio=pattern&pattern=Blank");
    await page.getByRole("button", { name: "Add pattern layer", exact: true }).click();
    await page.getByRole("group", { name: "Add a pattern motif", exact: true }).getByRole("button", { name: "Rectangle", exact: true }).click();
    await page.getByRole("button", { name: "Add mask", exact: true }).click();
    await page.getByRole("button", { name: "Invert artwork mask", exact: true }).click();
    await page.getByLabel("Artwork mask opacity", { exact: true }).fill("0");
    const Save = async () =>
    {
        const Download = page.waitForEvent("download"); await page.getByRole("button", { name: "Save document", exact: true }).click();
        const Chunks = []; for await (const Chunk of await (await Download).createReadStream()) Chunks.push(Chunk);
        return JSON.parse(Buffer.concat(Chunks).toString());
    };
    const Saved = await Save(); expect(Saved.layers[0].mask.opacity).toBe(0); expect(Saved.layers[0].mask.invert).toBe(true);
    await page.getByTitle("Undo", { exact: true }).click(); expect((await Save()).layers[0].mask.opacity).toBe(1);
    await page.getByTitle("Redo", { exact: true }).click(); expect((await Save()).layers[0].mask.opacity).toBe(0);
    await page.getByLabel("Open pattern JSON").setInputFiles({ name: "masked.pattern.json", mimeType: "application/json", buffer: Buffer.from(JSON.stringify(Saved)) });
    expect((await Save()).layers[0].mask).toEqual(Saved.layers[0].mask);
    await page.getByLabel("Artwork mask opacity", { exact: true }).fill("1");
    await expect(page.getByRole("button", { name: "Select mask of rect", exact: true })).toHaveAttribute("aria-pressed", "true");
    await page.screenshot({ path: fileURLToPath(new URL("../../../Exhibits/TextureStudio/Images/PatternMaskIcons.png", import.meta.url)), fullPage: true });
});

test("Embedded image alpha and luminance masks produce fractional exported coverage", async ({ page }) =>
{
    await page.goto("/?studio=stamp");
    const Samples = await page.evaluate(async () =>
    {
        const { patternLayer, patternSVG } = await import("/src/patternDocument.js");
        const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 2;
        const Context = Canvas.getContext("2d"); Context.fillStyle = "rgb(128,128,128)"; Context.fillRect(0, 0, 2, 2);
        const ImageMask = { category: "image", enabled: true, width: 100, height: 100, x: 50, y: 50, opacity: 1, mode: "luminance", invert: false, image: Canvas.toDataURL() };
        async function Alpha(Mask)
        {
            const Svg = patternSVG({ schema: "alloy.pattern.v1", tileAxes: "none", backgroundOpacity: 0, layers: [patternLayer("rect", { paint: true, fillEnabled: true, color: "#ff0000", width: 300, height: 300, mask: Mask })] });
            const Image = new window.Image(); const Url = URL.createObjectURL(new Blob([Svg], { type: "image/svg+xml" })); Image.src = Url; await Image.decode();
            const Raster = document.createElement("canvas"); Raster.width = Raster.height = 512; const Surface = Raster.getContext("2d"); Surface.drawImage(Image, 0, 0); URL.revokeObjectURL(Url);
            return Surface.getImageData(256, 256, 1, 1).data[3];
        }
        const Luminance = await Alpha(ImageMask), Inverse = await Alpha({ ...ImageMask, invert: true });
        Context.clearRect(0, 0, 2, 2); Context.fillStyle = "rgba(255,255,255,.5)"; Context.fillRect(0, 0, 2, 2);
        return [Luminance, Inverse, await Alpha({ ...ImageMask, mode: "alpha", image: Canvas.toDataURL() })];
    });
    for (const Sample of Samples) expect(Sample).toBeGreaterThanOrEqual(127);
    for (const Sample of Samples) expect(Sample).toBeLessThanOrEqual(128);
});
