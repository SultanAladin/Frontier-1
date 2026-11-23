/*============================================================================================================================================
                                                        ARTWORK-ARRAYS.SPEC.JS
============================================================================================================================================*/
// 🧩 Array placement/envelopes, collision-free captions and original Stamp artwork preservation.

import { test, expect } from "@playwright/test";
import { fileURLToPath } from "node:url";
import { GenerateArtworkArray } from "../src/ArtworkArrays.js";
import { NormalizeArtworkNames } from "../src/ArtworkNames.js";
import { patternLayer, validatePattern } from "../src/patternDocument.js";
const Specification = { Category: "linear", Count: 4, Columns: 3, Rows: 2, StepX: 40, StepY: 0, Radius: 80, RadiusStep: 40, Rings: 2, CentreX: 256, CentreY: 256, StartAngle: 0, Sweep: 360, RotateCopies: false, ScaleProfile: "uniform", StartScale: 100, EndScale: 50, Seed: 7 };
const Source = () => validatePattern({ schema: "alloy.pattern.v1", layers: [patternLayer("rect", { name: "Motif", paint: true, width: 100, height: 50 })] });
test("Linear/grid/circular/radial placement and scale envelopes are bounded and deterministic", () =>
{
    const Document = Source(), Before = JSON.stringify(Document);
    const Linear = GenerateArtworkArray(Document, [0], { ...Specification, ScaleProfile: "linear" });
    expect(Linear.Document.layers.map(Layer => Layer.x)).toEqual([196, 236, 276, 316]);
    expect(Linear.Document.layers[0].width).toBe(100); expect(Linear.Document.layers.at(-1).width).toBe(50);
    const Grid = GenerateArtworkArray(Document, [0], { ...Specification, Category: "grid", StepY: 60 });
    expect(Grid.Document.layers).toHaveLength(6); expect(new Set(Grid.Document.layers.map(Layer => Layer.y)).size).toBe(2);
    const Ring = GenerateArtworkArray(Document, [0], { ...Specification, Category: "circular", RotateCopies: true, ScaleProfile: "circular" }).Document.layers;
    expect(Ring.map(Layer => Layer.width)).toEqual([100, 75, 50, 75]);
    expect(Ring[0].x).toBeCloseTo(336); expect(Ring[1].y).toBeCloseTo(336); expect(Ring[1].rotation).toBe(90);
    const Radial = GenerateArtworkArray(Document, [0], { ...Specification, Category: "radial", ScaleProfile: "radial" }).Document.layers;
    expect(Radial).toHaveLength(8); expect(Radial[0].width).toBeCloseTo(100 - 50 * 80 / 120); expect(Radial[4].width).toBe(50);
    const Noise = Seed => GenerateArtworkArray(Document, [0], { ...Specification, ScaleProfile: "noise", Seed }).Document.layers.map(Layer => Layer.width);
    expect(Noise(7)).toEqual(Noise(7)); expect(Noise(7)).not.toEqual(Noise(8));
    expect(JSON.stringify(Document)).toBe(Before);
    expect(() => GenerateArtworkArray(Document, [0], { ...Specification, Count: 25 }, 24)).toThrow();
    expect(() => GenerateArtworkArray(Document, [0], { ...Specification, StepX: NaN })).toThrow();
    expect(() => GenerateArtworkArray({ ...Document, layers: [{ ...Document.layers[0], locked: true }] }, [0], Specification)).toThrow();
});
test("Outliner captions avoid case-insensitive collisions and remain idempotent", () =>
{
    const Layers = [{ id: "a", name: "diamond" }, { id: "b", name: "Diamond" }, { id: "c", name: "diamond 01" }, { id: "d", name: "custom" }];
    const Named = NormalizeArtworkNames(Layers);
    expect(new Set(Named.map(Layer => Layer.name.toLowerCase())).size).toBe(4);
    expect(NormalizeArtworkNames(Named)).toEqual(Named);
    expect(Named.map(Layer => Layer.id)).toEqual(Layers.map(Layer => Layer.id));
    expect(Named[3].name).toBe("custom"); expect(Layers[0].name).toBe("diamond");
    const Document = validatePattern({ schema: "alloy.pattern.v1", layers: [patternLayer("rect"), patternLayer("rect")] });
    expect(Document.layers.map(Layer => Layer.name)).toEqual(["rect 01", "rect 02"]);
});
async function Save(Page, Workspace)
{
    const Pending = Page.waitForEvent("download");
    await Page.getByRole("button", { name: Workspace === "pattern" ? "Save document" : "Save stamp project", exact: true }).click();
    const Chunks = []; for await (const Chunk of await (await Pending).createReadStream()) Chunks.push(Chunk);
    return JSON.parse(Buffer.concat(Chunks).toString());
}
for (const Workspace of ["pattern", "stamp"])
{
    test(`${Workspace}: Inspector arrays create editable copies with unique names and one undo`, async ({ page }) =>
    {
        const Errors = []; page.on("pageerror", Failure => Errors.push(Failure.message));
        await page.goto(`/?studio=${Workspace}${Workspace === "pattern" ? "&pattern=Blank" : ""}`);
        if (Workspace === "pattern")
        {
            await page.getByRole("button", { name: "Add pattern layer", exact: true }).click();
            await page.getByRole("group", { name: "Add a pattern motif", exact: true }).getByRole("button", { name: "Rectangle", exact: true }).click();
            await page.getByLabel("Width", { exact: true }).fill("80");
            await page.getByLabel("Height", { exact: true }).fill("60");
        }
        const Before = await Save(page, Workspace);
        const Panel = page.locator(".artwork-array-controls"); await Panel.locator("summary").click();
        await Panel.getByLabel("Array layout", { exact: true }).selectOption("circular");
        await Panel.getByLabel("Array copies per ring", { exact: true }).fill("3");
        await Panel.getByLabel("Array radius", { exact: true }).fill("100");
        await Panel.getByLabel("Array scale profile", { exact: true }).selectOption("linear");
        await Panel.getByLabel("Array start scale %", { exact: true }).fill("70");
        await Panel.getByLabel("Array end scale %", { exact: true }).fill("40");
        await Panel.getByRole("button", { name: "Generate array", exact: true }).click();
        const After = await Save(page, Workspace);
        expect(After.layers).toHaveLength(Before.layers.length + 2);
        expect(new Set(After.layers.map(Layer => Layer.name.toLowerCase())).size).toBe(After.layers.length);
        if (Workspace === "stamp")
        {
            const Original = Before.layers[0], Copies = After.layers.filter(Layer => Layer.name.startsWith(Original.name));
            expect(Copies).toHaveLength(3); expect(Copies.every(Layer => Layer.kind === Original.kind)).toBe(true);
            expect(Copies.every(Layer => JSON.stringify(Layer.runs) === JSON.stringify(Original.runs))).toBe(true);
            expect(After.channels).toEqual(Before.channels); expect(After.channelValues).toEqual(Before.channelValues);
        }
        await page.locator(Workspace === "pattern" ? ".pe-inspector" : ".stamp-inspector-scroll").evaluate(Surface => Surface.scrollTop = 0);
        await page.mouse.move(1, 1); await page.screenshot({ path: fileURLToPath(new URL(`../../../Exhibits/TextureStudio/Images/${Workspace === "pattern" ? "Pattern" : "Stamp"}ArtworkArrays.png`, import.meta.url)), fullPage: true });
        if (Workspace === "pattern") await page.getByTitle("Undo", { exact: true }).click();
        else await page.getByRole("button", { name: "Undo stamp edit", exact: true }).click();
        expect(await Save(page, Workspace)).toEqual(Before); expect(Errors).toEqual([]);
    });
}

test("Grouped array copies preserve relative offsets, masks and original native SVG/text types", async ({ page }) =>
{
    const Grouped = validatePattern({ schema: "alloy.pattern.v1", layers: [patternLayer("rect", { name: "Left", x: 246, width: 20, height: 10, group: "pair", paint: true, mask: { category: "paint", base: .5 } }), patternLayer("rect", { name: "Right", x: 266, width: 20, height: 10, group: "pair", paint: true })] });
    const Copies = GenerateArtworkArray(Grouped, [0, 1], { ...Specification, Count: 2, StartScale: 200, StepX: 100 }).Document.layers;
    expect(Copies[1].x - Copies[0].x).toBe(40); expect(Copies[1].group).toBe(Copies[0].group);
    expect(Copies[2].group).not.toBe(Copies[0].group); expect(Copies[0].mask).toEqual(Grouped.layers[0].mask);
    await page.goto("/?studio=stamp");
    const Result = await page.evaluate(async Specification =>
    {
        const { createStampDocument } = await import("/src/stampDocument.js");
        const { StampVectorCanvasDocument, StampDocumentFromVectorCanvas } = await import("/src/StampVectorProjection.js");
        const { GenerateArtworkArray } = await import("/src/ArtworkArrays.js");
        const Original = createStampDocument(), Projection = StampVectorCanvasDocument(Original);
        const Generated = GenerateArtworkArray(Projection, Projection.layers.map((_, Index) => Index), { ...Specification, Count: 2, StepX: 60 }, 24);
        const Converted = StampDocumentFromVectorCanvas(Generated.Document, Original, Generated.Selection).Document;
        return { Original, Converted };
    }, Specification);
    expect(Result.Converted.layers).toHaveLength(Result.Original.layers.length * 2);
    for (const Original of Result.Original.layers)
    {
        const Matches = Result.Converted.layers.filter(Layer => Layer.name.startsWith(Original.name));
        expect(Matches).toHaveLength(2);
        expect(Matches.every(Layer => Layer.kind === Original.kind)).toBe(true);
        if (Original.runs) expect(Matches.every(Layer => JSON.stringify(Layer.runs) === JSON.stringify(Original.runs))).toBe(true);
        if (Original.svg) expect(Matches.every(Layer => Layer.svg === Original.svg)).toBe(true);
    }
});
