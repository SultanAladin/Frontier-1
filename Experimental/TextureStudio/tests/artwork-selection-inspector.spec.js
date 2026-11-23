import { test, expect } from "@playwright/test";
import { fileURLToPath } from "node:url";
test.describe.configure({ timeout: 60000 });
const Evidence = Name => fileURLToPath(new URL(`../../../Exhibits/TextureStudio/Images/${Name}.png`, import.meta.url));
async function Save(Page, Workspace)
{
    const Pending = Page.waitForEvent("download");
    await Page.getByRole("button", { name: Workspace === "pattern" ? "Save document" : "Save stamp project", exact: true }).click();
    const Chunks = []; for await (const Chunk of await (await Pending).createReadStream()) Chunks.push(Chunk);
    return JSON.parse(Buffer.concat(Chunks).toString());
}
for (const Workspace of ["pattern", "stamp"])
{
    test(`${Workspace}: inspector alignment, multi-selection paint, switches and selection mask`, async ({ page }) =>
    {
        const Errors = []; page.on("pageerror", Failure => Errors.push(Failure.message));
        await page.goto(`/?studio=${Workspace}${Workspace === "pattern" ? "&pattern=Blank" : ""}`);
        if (Workspace === "pattern")
        {
            const Document = await page.evaluate(async () =>
            {
                const { patternLayer, validatePattern } = await import("/src/patternDocument.js");
                return validatePattern({ schema: "alloy.pattern.v1", layers: [patternLayer("rect", { name: "Target", paint: true, width: 300, height: 300 }), patternLayer("ellipse", { name: "Stencil", paint: true, width: 220, height: 220 })] });
            });
            await page.getByLabel("Open pattern JSON").setInputFiles({ name: "selection.pattern.json", mimeType: "application/json", buffer: Buffer.from(JSON.stringify(Document)) });
        }
        else
        {
            await page.getByRole("button", { name: "Vector canvas", exact: true }).click();
            for (const Shape of ["Rectangle", "Ellipse"])
            {
                await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
                await page.getByRole("menuitem", { name: Shape, exact: true }).click();
            }
        }
        const Inspector = page.locator(Workspace === "pattern" ? ".pe-inspector" : ".stamp-right");
        const Names = Workspace === "pattern" ? ["Target", "Stencil"] : ["Rectangle", "Ellipse"];
        const Select = Name => Workspace === "pattern" ? page.getByRole("list", { name: "Pattern layers", exact: true }).getByRole("button", { name: Name, exact: true }) : page.getByRole("button", { name: `Select stamp component ${Name}`, exact: true });
        await Select(Names[0]).click();
        await expect(Inspector.getByRole("button", { name: "Align left", exact: true })).toBeVisible();
        await expect(page.locator(".pe-workspace .se-selection-context,.stamp-centre .se-selection-context")).toHaveCount(0);
        await expect(Inspector.getByRole("group", { name: "Boolean operations", exact: true })).toHaveCount(0);
        const SingleBefore = await Save(page, Workspace);
        await Inspector.getByRole("button", { name: "Align left", exact: true }).click();
        const Aligned = await Save(page, Workspace);
        expect(Aligned.layers.find(Layer => Layer.name === Names[0]).x).not.toBe(SingleBefore.layers.find(Layer => Layer.name === Names[0]).x);
        if (Workspace === "pattern") await page.getByTitle("Undo", { exact: true }).click();
        else await page.getByRole("button", { name: "Undo stamp edit", exact: true }).click();
        await Select(Names[0]).click();
        await Select(Names[1]).click({ modifiers: ["Control"] });
        await expect(Inspector.getByRole("button", { name: "Union", exact: true })).toBeEnabled();
        await expect(Inspector.getByRole("switch", { name: Workspace === "pattern" ? "Drawing fill enabled" : "Stamp vector fill enabled", exact: true })).toBeVisible();
        await page.getByLabel(Workspace === "pattern" ? "Drawing fill color" : "Stamp vector fill colour", { exact: true }).fill("#ff0000");
        await page.getByLabel("Selection opacity", { exact: true }).fill("0.6");
        const Painted = await Save(page, Workspace);
        const Pair = Painted.layers.filter(Layer => Names.includes(Layer.name));
        expect(Pair.map(Layer => Layer.opacity)).toEqual([.6, .6]);
        expect(Pair.every(Layer => (Workspace === "pattern" ? Layer.color : Layer.vector.color) === "#ff0000")).toBe(true);
        await (Workspace === "stamp" ? Inspector.locator(".stamp-inspector-scroll") : Inspector).evaluate(Surface => Surface.scrollTop = 0);
        await page.mouse.move(1, 1); await page.screenshot({ path: Evidence(`${Workspace === "pattern" ? "Pattern" : "Stamp"}SelectionInspector`), fullPage: true });
        await Inspector.getByRole("button", { name: "Intersection", exact: true }).click();
        expect((await Save(page, Workspace)).layers).toHaveLength(Painted.layers.length - 1);
        if (Workspace === "pattern") await page.getByTitle("Undo", { exact: true }).click();
        else await page.getByRole("button", { name: "Undo stamp edit", exact: true }).click();
        await Select(Names[0]).click(); await Select(Names[1]).click({ modifiers: ["Control"] });
        await Inspector.getByRole("button", { name: `${Names[1]} masks ${Names[0]}`, exact: true }).click();
        await expect.poll(async () => (await Save(page, Workspace)).layers.find(Layer => Layer.name === Names[0]).mask?.category).toBe("image");
        const Masked = await Save(page, Workspace);
        expect(Masked.layers.find(Layer => Layer.name === Names[1]).visible).toBe(false);
        expect(Masked.layers.length).toBe(Painted.layers.length);
        const Alpha = await page.evaluate(async ImageSource =>
        {
            const Image = new window.Image(); Image.src = ImageSource; await Image.decode();
            const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 512;
            const Context = Canvas.getContext("2d"); Context.drawImage(Image, 0, 0);
            return [Context.getImageData(256, 256, 1, 1).data[3], Context.getImageData(0, 0, 1, 1).data[3]];
        }, Masked.layers.find(Layer => Layer.name === Names[0]).mask.image);
        expect(Alpha[0]).toBeGreaterThan(100); expect(Alpha[1]).toBe(0);
        if (Workspace === "pattern") await page.getByTitle("Undo", { exact: true }).click();
        else await page.getByRole("button", { name: "Undo stamp edit", exact: true }).click();
        const Restored = await Save(page, Workspace);
        expect(Restored.layers.find(Layer => Layer.name === Names[0]).mask).toBeUndefined();
        expect(Restored.layers.find(Layer => Layer.name === Names[1]).visible).toBe(true);
        expect(Errors).toEqual([]);
    });
}

test("Stamp composition aligns native text without replacing its typography", async ({ page }) =>
{
    await page.goto("/?studio=stamp");
    const Before = await Save(page, "stamp");
    await page.locator(".stamp-right").getByRole("button", { name: "Align left", exact: true }).click();
    const After = await Save(page, "stamp");
    expect(After.layers[0].x).not.toBe(Before.layers[0].x);
    expect(After.layers[0].runs).toEqual(Before.layers[0].runs);
    expect(After.layers[0].kind).toBe(Before.layers[0].kind);
    await page.getByRole("button", { name: "Undo stamp edit", exact: true }).click();
    expect(await Save(page, "stamp")).toEqual(Before);
});

test("Selection masks respect rotation, keep original sources and reject protected targets", async ({ page }) =>
{
    await page.goto("/?studio=pattern&pattern=Blank");
    const Result = await page.evaluate(async () =>
    {
        const { ArtworkSelectionMask } = await import("/src/ArtworkSelectionMask.js");
        const { patternLayer, validatePattern } = await import("/src/patternDocument.js");
        const Document = validatePattern({ schema: "alloy.pattern.v1", layers: [patternLayer("rect", { paint: true, width: 300, height: 300, rotation: 65 }), patternLayer("rect", { paint: true, width: 100, height: 40, rotation: 20, opacity: .5 })] });
        const Encoded = JSON.stringify(Document), Masked = await ArtworkSelectionMask(Document, 0, 1);
        const Image = new window.Image(); Image.src = Masked.layers[0].mask.image; await Image.decode();
        const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 512;
        const Context = Canvas.getContext("2d"); Context.drawImage(Image, 0, 0);
        let Rejected = 0;
        for (const Protected of [{ ...Document, layers: [{ ...Document.layers[0], locked: true }, Document.layers[1]] }, Masked])
        {
            try { await ArtworkSelectionMask(Protected, 0, 1); } catch { ++Rejected; }
        }
        return { OriginalUntouched: JSON.stringify(Document) === Encoded, Alpha: [Context.getImageData(256, 256, 1, 1).data[3], Context.getImageData(0, 0, 1, 1).data[3]], Rejected };
    });
    expect(Result.OriginalUntouched).toBe(true); expect(Result.Rejected).toBe(2);
    expect(Result.Alpha[0]).toBeGreaterThanOrEqual(127); expect(Result.Alpha[0]).toBeLessThanOrEqual(128); expect(Result.Alpha[1]).toBe(0);
});
