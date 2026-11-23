import { test, expect } from "@playwright/test";

test("Stamp shared vector booleans retain corner pixels, native artwork, undo and export", async ({ page }) =>
{
    const Failures = [];
    page.on("pageerror", Failure => Failures.push(Failure.message));
    await page.goto("/?studio=stamp");
    await expect(page.getByAltText("Composed stamp preview", { exact: true })).toBeVisible();
    const Original = await page.evaluate(async () =>
    {
        const { createStampDocument, normalizeStampDocument } = await import("/src/stampDocument.js");
        const { patternLayer } = await import("/src/patternDocument.js");
        const { StampVectorCanvasDocument, StampDocumentFromVectorCanvas } = await import("/src/StampVectorProjection.js");
        const Document = normalizeStampDocument({ ...createStampDocument(), layers: [{ id: "VectorArtwork", kind: "vector", name: "Ellipse", vector: patternLayer("ellipse") }, ...createStampDocument().layers] });
        const Projection = StampVectorCanvasDocument(Document);
        const Restored = StampDocumentFromVectorCanvas(Projection, Document, [Projection.layers.length - 1]);
        return { NativeRetained: JSON.stringify(Restored.Document.layers.slice(1)) === JSON.stringify(Document.layers.slice(1)), TargetsRetained: JSON.stringify([Restored.Document.channels, Restored.Document.channelValues]) === JSON.stringify([Document.channels, Document.channelValues]), Identities: Restored.SelectedIDs };
    });
    expect(Original).toEqual({ NativeRetained: true, TargetsRetained: true, Identities: ["VectorArtwork"] });
    await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
    await page.getByRole("menuitem", { name: "Rectangle", exact: true }).click();
    await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
    await page.getByRole("menuitem", { name: "Ellipse", exact: true }).click();
    await page.getByRole("button", { name: "Select stamp component Rectangle", exact: true }).click({ modifiers: ["Control"] });
    await expect(page.getByRole("group", { name: "Boolean operations", exact: true })).toBeVisible();
    await page.screenshot({ path: "../../Exhibits/TextureStudio/Images/StampVectorSelection.png", fullPage: true });
    await page.getByRole("button", { name: "Difference", exact: true }).click();
    await expect(page.getByRole("button", { name: "Select stamp component Difference", exact: true })).toBeVisible();
    const Download = page.waitForEvent("download");
    await page.getByRole("button", { name: "Save stamp project", exact: true }).click();
    const Chunks = [];
    for await (const Chunk of await (await Download).createReadStream()) Chunks.push(Chunk);
    const Document = JSON.parse(Buffer.concat(Chunks).toString());
    expect(Document.layers.find(Layer => Layer.name === "Difference").kind).toBe("vector");
    const Pixels = await page.evaluate(async Document =>
    {
        const { composeStampSVG } = await import("/src/stampRaster.js");
        const Vector = Document.layers.find(Layer => Layer.name === "Difference");
        const Svg = await composeStampSVG({ ...Document, layers: [Vector] });
        const Image = new window.Image();
        Image.src = URL.createObjectURL(new Blob([Svg.svg], { type: "image/svg+xml" }));
        await Image.decode();
        const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 512;
        const Context = Canvas.getContext("2d"); Context.drawImage(Image, 0, 0, 512, 512);
        URL.revokeObjectURL(Image.src);
        return { Centre: Context.getImageData(256, 256, 1, 1).data[3], Corner: Context.getImageData(Math.round(Vector.x - Vector.width * .45), Math.round(Vector.y - Vector.height * .45), 1, 1).data[3] };
    }, Document);
    expect(Pixels.Centre).toBe(0);
    expect(Pixels.Corner).toBeGreaterThan(200);
    await page.screenshot({ path: "../../Exhibits/TextureStudio/Images/StampVectorBoolean.png", fullPage: true });
    await page.getByRole("button", { name: "Undo stamp edit", exact: true }).click();
    await expect(page.getByRole("button", { name: "Select stamp component Rectangle", exact: true })).toBeVisible();
    await page.getByRole("button", { name: "Redo stamp edit", exact: true }).click();
    await expect(page.getByRole("button", { name: "Select stamp component Difference", exact: true })).toBeVisible();
    await page.getByRole("button", { name: "Composition", exact: true }).click();
    await expect(page.getByAltText("Composed stamp preview", { exact: true })).toBeVisible();
    await page.getByRole("button", { name: "Use in Texture", exact: true }).click();
    await expect(page.getByRole("region", { name: "Texture studio", exact: true })).toBeVisible();
    const Preset = await page.evaluate(async () => (await import("/src/stampDocument.js")).readStampPresets()[0]);
    expect(Preset.project.layers.some(Layer => Layer.kind === "vector" && Layer.name === "Difference")).toBe(true);
    expect(Preset.project.channels).toEqual(Document.channels);
    expect(Preset.project.channelValues).toEqual(Document.channelValues);
    expect(Preset.raster.dataUrl).toMatch(/^data:image\/png;base64,/);
    expect(Failures).toEqual([]);
});

test("Stamp vector drawing, pen finalization, shared gradients and editable preset round trip", async ({ page }) =>
{
    const Failures = [];
    page.on("pageerror", Failure => Failures.push(Failure.message));
    await page.goto("/?studio=stamp");
    await page.getByRole("button", { name: "Vector canvas", exact: true }).click();
    async function Coordinate(X, Y)
    {
        return page.locator('[data-coordinate-space="tile"]').evaluate((Canvas, Coordinate) =>
        {
            const Point = new DOMPoint(Coordinate.X, Coordinate.Y).matrixTransform(Canvas.getScreenCTM());
            return { x: Point.x, y: Point.y };
        }, { X, Y });
    }
    await page.getByRole("button", { name: "Rectangle tool (R)", exact: true }).click();
    const Start = await Coordinate(50, 50), End = await Coordinate(180, 180);
    await page.mouse.move(Start.x, Start.y); await page.mouse.down();
    await page.mouse.move(End.x, End.y); await page.mouse.up();
    await page.getByLabel("Stamp fill source", { exact: true }).selectOption("linear");
    await page.getByLabel("Stamp fill gradient angle", { exact: true }).fill("45");
    await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
    for (const [X, Y] of [[320, 50], [430, 100], [340, 200]])
    {
        const Point = await Coordinate(X, Y); await page.mouse.click(Point.x, Point.y);
    }
    await page.keyboard.press("Enter");
    const Pending = page.waitForEvent("download");
    await page.getByRole("button", { name: "Save stamp project", exact: true }).click();
    const Chunks = [];
    for await (const Chunk of await (await Pending).createReadStream()) Chunks.push(Chunk);
    const Document = JSON.parse(Buffer.concat(Chunks).toString());
    const Vectors = Document.layers.filter(Layer => Layer.kind === "vector");
    expect(Vectors).toHaveLength(2);
    expect(Vectors.some(Layer => Layer.vector.kind === "rect" && Layer.vector.fillSource?.category === "linear")).toBe(true);
    expect(Vectors.some(Layer => Layer.vector.kind === "path" && Layer.vector.nodes.length === 3)).toBe(true);
    await page.locator(".stamp-inspector-scroll").getByRole("button", { name: "Save stamp preset", exact: true }).click();
    await expect(page.getByRole("button", { name: "Open stamp preset Alloy maker mark", exact: true })).toBeVisible();
    await page.getByLabel("Document actions", { exact: true }).click();
    await page.getByRole("button", { name: "New stamp project", exact: true }).click();
    await page.getByRole("button", { name: "Open stamp preset Alloy maker mark", exact: true }).click();
    await expect(page.getByRole("list", { name: "Stamp components", exact: true }).getByRole("listitem")).toHaveCount(Document.layers.length);
    expect(Failures).toEqual([]);
});
