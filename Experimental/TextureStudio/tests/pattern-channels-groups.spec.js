/*============================================================================================================================================
                                                    PATTERN-CHANNELS-GROUPS.SPEC.JS
============================================================================================================================================*/
// 🧩 Channel validation, nested organization, linked edits and actual authoring/raster evidence.

import { test, expect } from "@playwright/test";
import { fileURLToPath } from "node:url";
import { validatePattern, patternLayer, patternSVG } from "../src/patternDocument.js";
import { CreatePatternEnclosure, StorePatternGroup, InsertPatternGroup, PropagatePatternProperties } from "../src/PatternOrganization.js";
import { DerivePatternNormalPixels } from "../src/PatternSurfaceChannels.js";
test.describe.configure({ timeout: 60000 });
test.use({ actionTimeout: 30000 });
const Source = () => validatePattern({ schema: "alloy.pattern.v1", backgroundOpacity: 0, tileAxes: "none", layers: [patternLayer("rect", { name: "Thread", x: 210, relief: .5 }), patternLayer("ellipse", { name: "Loop", x: 300, relief: .2 })] });
test("Nested groups/folders preserve members, reject cycles and survive normalization", () =>
{
    let Document = validatePattern(CreatePatternEnclosure(Source(), [0, 1], "Needlework"));
    const Group = Document.Enclosures[0].Token;
    Document = validatePattern(CreatePatternEnclosure(Document, [], "Folder", "folder"));
    const Folder = Document.Enclosures[1].Token;
    Document.Enclosures[0].EnclosureToken = Folder;
    expect(validatePattern(Document).Enclosures[0].EnclosureToken).toBe(Folder);
    Document.Enclosures[1].EnclosureToken = Group;
    expect(() => validatePattern(Document)).toThrow(/cycle/);
});
test("Stored groups preserve nested geometry and linked edits keep placement, locks and independent copies", () =>
{
    let Document = validatePattern(CreatePatternEnclosure(Source(), [0, 1], "Threads"));
    Document = validatePattern(StorePatternGroup(Document, [0, 1], "Motif"));
    const Token = Document.StoredGroups[0].Token;
    Document = validatePattern(InsertPatternGroup(Document, Token, true));
    Document = validatePattern(InsertPatternGroup(Document, Token, false));
    Document.EditLinkedInstances = true; Document.layers[2].x += 50; Document.layers[3].locked = true;
    const Next = structuredClone(Document); Next.layers[0].color = "#123456"; Next.layers[0].width *= .5; Next.layers[1].relief = .8;
    const Edited = validatePattern(PropagatePatternProperties(Document, Next));
    expect(Edited.layers[2].color).toBe("#123456"); expect(Edited.layers[2].x).toBe(Document.layers[2].x);
    expect(Edited.layers[2].width).toBe(Document.layers[2].width * .5);
    expect(Edited.layers[3].relief).toBe(Document.layers[3].relief); expect(Edited.layers[4].color).toBe(Document.layers[4].color);
    expect(Edited.StoredGroups[0].Layers[0].color).toBe("#123456");
    expect(validatePattern(Edited)).toEqual(Edited);
    const Unchanged = PropagatePatternProperties(Edited, { ...Edited, EditLinkedInstances: false });
    expect(Unchanged.StoredGroups).toEqual(Edited.StoredGroups);
    const LocalScale = structuredClone(Edited); LocalScale.layers[0].width *= 2;
    const Toggled = PropagatePatternProperties(LocalScale, { ...LocalScale, EditLinkedInstances: false });
    expect(Toggled.StoredGroups).toEqual(LocalScale.StoredGroups);
});
test("Group-wide channel edits preserve unrelated channels and locked artwork", () =>
{
    let Document = validatePattern(CreatePatternEnclosure(Source(), [0, 1], "Threads")); Document.EditGroupProperties = true;
    Document.layers[1].SurfaceChannels = { Normal: { Enabled: true, Strength: .4 } };
    const Next = structuredClone(Document); Next.layers[0].SurfaceChannels = { Height: { Enabled: false, Strength: 1 } };
    const Edited = PropagatePatternProperties(Document, Next);
    expect(Edited.layers[1].SurfaceChannels.Normal.Strength).toBe(.4); expect(Edited.layers[1].SurfaceChannels.Height.Enabled).toBe(false);
    Document.layers[1].locked = true; expect(PropagatePatternProperties(Document, Next).layers[1].SurfaceChannels.Height).toBeUndefined();
});
test("Surface maps encode real height, explicit disabled contributions and non-flat normal pixels", () =>
{
    const Document = Source();
    expect(patternSVG(Document, "height")).toContain("rgb(178,178,178)");
    Document.layers[0].SurfaceChannels = { Height: { Enabled: false, Strength: 1 }, Roughness: { Enabled: false } };
    expect(patternSVG(Document, "params")).toContain("128)");
    expect(patternSVG(Document, "height")).not.toContain("rgb(178,178,178)");
    expect(() => validatePattern({ ...Document, layers: [{ ...Document.layers[0], SurfaceChannels: { Normal: { Source: "https://invalid/map.png" } } }] })).toThrow(/Normal maps/);
    const Height = new Uint8ClampedArray(4 * 16); for (let Index = 0; Index < 16; ++Index) { Height[Index * 4] = Index % 4 < 2 ? 128 : 220; Height[Index * 4 + 3] = 255; }
    const Normals = DerivePatternNormalPixels(Height, 4, 4, 2); expect(new Set(Array.from(Normals).filter((_, Index) => Index % 4 === 0)).size).toBeGreaterThan(1);
});
async function Save(Page)
{
    const Pending = Page.waitForEvent("download"); await Page.getByRole("button", { name: "Save document", exact: true }).click();
    const Chunks = []; for await (const Chunk of await (await Pending).createReadStream()) Chunks.push(Chunk); return JSON.parse(Buffer.concat(Chunks).toString());
}
test("Pattern multi-edit channels, nested folders, stored linked groups, save/reopen and undo", async ({ page }) =>
{
    const Errors = []; page.on("pageerror", Failure => Errors.push(Failure.message));
    await page.goto("/?studio=pattern&pattern=Blank");
    await page.getByLabel("Open pattern JSON").setInputFiles({ name: "channels.pattern.json", mimeType: "application/json", buffer: Buffer.from(JSON.stringify(Source())) });
    const Organization = page.getByRole("region", { name: "Pattern groups and folders" });
    await page.locator('.pattern-organization summary').first().click();
    await page.getByLabel("Group or folder name").fill("Stitched motif");
    await page.getByRole("button", { name: "+ Group", exact: true }).click();
    await page.getByRole("button", { name: /◇ Stitched motif/ }).click();
    await page.getByLabel("Group-wide property editing").check();
    const Channels = page.locator('.pattern-surface-channels'); await Channels.locator('summary').click();
    await page.getByLabel("Channel Height amount", { exact: true }).fill("0.72");
    await page.getByRole("button", { name: "Show all artwork", exact: true }).click();
    await page.getByLabel("Group or folder name").fill("Textiles");
    await page.getByRole("button", { name: "+ Folder", exact: true }).click();
    let Document = await Save(page); const Folder = Document.Enclosures.find(Entry => Entry.Caption === "Textiles").Token;
    await page.getByRole("button", { name: /◇ Stitched motif/ }).click();
    await page.getByLabel("Group destination").selectOption(Folder);
    await page.getByRole("button", { name: "Nest container here", exact: true }).click();
    await page.locator('.pattern-organization summary').nth(1).click();
    await page.getByRole("button", { name: "Store selected group", exact: true }).click();
    await page.getByRole("button", { name: "Insert linked", exact: true }).click();
    await page.getByLabel("Linked instance editing").check();
    Document = await Save(page); expect(Document.StoredGroups).toHaveLength(1); expect(Document.layers.some(Layer => Layer.relief === .72)).toBe(true);
    expect(Document.Enclosures.some(Entry => Entry.EnclosureToken === Folder)).toBe(true);
    await page.getByLabel("Channel Height amount", { exact: true }).fill("0.45");
    const Changed = await Save(page); expect(Changed.layers.filter(Layer => Layer.LinkedDefinition).every(Layer => Layer.relief === .45)).toBe(true);
    await page.getByTitle("Undo", { exact: true }).click(); expect(await Save(page)).toEqual(Document);
    await page.getByLabel("Inspect pattern channel").selectOption("Normal"); await expect(page.getByAltText("Normal channel map", { exact: true })).toBeVisible();
    await page.getByRole("button", { name: "Hide pattern library", exact: true }).click();
    await page.getByAltText("Normal channel map", { exact: true }).scrollIntoViewIfNeeded();
    await page.mouse.move(1, 1);
    await page.screenshot({ path: fileURLToPath(new URL('../../../Exhibits/TextureStudio/Images/PatternSurfaceChannelsAndGroups.png', import.meta.url)), fullPage: true });
    await page.getByLabel("Open pattern JSON").setInputFiles({ name: "roundtrip.pattern.json", mimeType: "application/json", buffer: Buffer.from(JSON.stringify(Document)) });
    expect(await Save(page)).toEqual(Document); expect(Errors).toEqual([]);
});

test("Supplied normal maps, independent channel coverage and height-derived weave normals affect the material renderer", async ({ page }) =>
{
    test.setTimeout(180000);
    const Errors = []; page.on("pageerror", Failure => Errors.push(Failure.message)); page.on("console", Message => { if (Message.type() === "error") Errors.push(Message.text()); });
    await page.goto("/?studio=pattern&pattern=Blank");
    const Result = await page.evaluate(async () =>
    {
        const { validatePattern, patternLayer } = await import('/src/patternDocument.js');
        const { RasterPatternChannel } = await import('/src/PatternSurfaceChannelControls.jsx');
        const { applyWeave } = await import('/src/patternWeaves.js');
        const { CreatePatternEnclosure, StorePatternGroup } = await import('/src/PatternOrganization.js');
        const { attachSurfaceSources } = await import('/src/patternRuntime.js');
        const Module = await (await fetch('/src/patternRuntime.js')).text();
        const THREE = await import(Module.match(/from\s*"([^"]+three[^"\n]+)"/)[1]);
        const NormalMap = document.createElement('canvas'); NormalMap.width = NormalMap.height = 8;
        const Context = NormalMap.getContext('2d'); Context.fillStyle = 'rgb(204,128,230)'; Context.fillRect(0, 0, 8, 8);
        const Document = validatePattern({ schema: 'alloy.pattern.v1', tileAxes: 'none', backgroundOpacity: 0, layers: [patternLayer('rect', { width: 240, height: 240, roughness: .8, relief: .6 }), patternLayer('ellipse', { width: 140, height: 140, roughness: .2, SurfaceChannels: { Roughness: { Enabled: false }, Height: { Enabled: false }, Normal: { Enabled: true, Strength: 1, Source: NormalMap.toDataURL() }, Occlusion: { Enabled: true, Strength: .3 } } })] });
        const Material = new THREE.MeshPhysicalMaterial(); const Attached = attachSurfaceSources(Material, { pattern: Document }); await Material.userData.ready;
        const Packed = Attached.uniforms.uPatternParams.value.image, Mask = Attached.uniforms.uPatternChannelMask.value.image;
        const Offset = (Math.floor(Packed.height / 2) * Packed.width + Math.floor(Packed.width / 2)) * 4;
        const Centre = Array.from(Packed.data.slice(Offset, Offset + 4)), Coverage = Array.from(Mask.data.slice(Offset, Offset + 4)); Material.dispose();
        const Direct = await RasterPatternChannel(Document, 'Normal'); const Pixel = Array.from(Direct.getContext('2d').getImageData(256, 256, 1, 1).data);
        let Weave = validatePattern(applyWeave({ schema: 'alloy.pattern.v1', layers: [], backgroundOpacity: 0 }, { enabled: true, threads: 8 }));
        Weave = validatePattern(CreatePatternEnclosure(Weave, Weave.layers.map((_, Index) => Index), 'Plain weave yarn'));
        Weave = validatePattern(StorePatternGroup(Weave, Weave.layers.map((_, Index) => Index), 'Plain weave'));
        const Canvas = await RasterPatternChannel(Weave, 'Normal'), Pixels = Canvas.getContext('2d').getImageData(0, 0, Canvas.width, Canvas.height).data;
        let NonFlat = 0; for (let Offset = 0; Offset < Pixels.length; Offset += 4) if (Math.abs(Pixels[Offset] - 128) > 3 || Math.abs(Pixels[Offset + 1] - 128) > 3) ++NonFlat;
        return { Document, Weave, Centre, Coverage, Pixel, NonFlat };
    });
    expect(Result.Centre[0]).toBeCloseTo(.8 * 255, -1); expect(Result.Centre[2]).toBeGreaterThan(128); expect(Result.Centre[3]).toBeCloseTo(.3 * 255, -1);
    expect(Result.Coverage).toEqual([255, 255, 255, 255]); expect(Result.Pixel[0]).toBeGreaterThan(180); expect(Result.NonFlat).toBeGreaterThan(100);
    await page.getByLabel("Open pattern JSON").setInputFiles({ name: "weave.pattern.json", mimeType: "application/json", buffer: Buffer.from(JSON.stringify(Result.Weave)) });
    await page.getByLabel('Pattern base material', { exact: true }).selectOption('natural-cotton');
    await page.getByRole('button', { name: '3D material', exact: true }).click();
    await expect(page.locator('.pe-material-preview canvas')).toBeVisible();
    await expect(page.getByLabel('Material preview status', { exact: true })).toContainText('Live material · ready', { timeout: 90000 });
    await page.getByRole('button', { name: 'Inspect detail', exact: true }).click({ timeout: 45000 });
    await page.getByRole('button', { name: 'Hide pattern library', exact: true }).click();
    await page.locator('.pattern-organization summary').first().click();
    await page.getByRole('button', { name: /◇ Plain weave yarn/ }).click();
    await page.locator('.pattern-surface-channels summary').click();
    await page.waitForTimeout(1200);
    await page.screenshot({ path: fileURLToPath(new URL('../../../Exhibits/TextureStudio/Images/PatternWeaveChannelMaterial.png', import.meta.url)), fullPage: true });
    expect(Errors).toEqual([]);
});

test("Active group receives new artwork; linked feature dimensions and normal uploads are undoable", async ({ page }) =>
{
    let Document = validatePattern(CreatePatternEnclosure(Source(), [0, 1], "Threads"));
    Document = validatePattern(StorePatternGroup(Document, [0, 1], "Stored threads"));
    Document = validatePattern(InsertPatternGroup(Document, Document.StoredGroups[0].Token, true)); Document.EditLinkedInstances = true;
    await page.goto('/?studio=pattern&pattern=Blank');
    await page.getByLabel('Open pattern JSON').setInputFiles({ name: 'linked.pattern.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(Document)) });
    await page.locator('.pattern-organization summary').first().click();
    await page.getByRole('button', { name: /◇ Threads/ }).first().click();
    await page.getByRole('button', { name: 'Add pattern layer', exact: true }).click();
    await page.getByRole('group', { name: 'Add a pattern motif', exact: true }).getByRole('button', { name: 'Rectangle', exact: true }).click();
    let Saved = await Save(page); expect(Saved.layers.at(-1).EnclosureToken).toBe(Document.Enclosures[0].Token);
    await page.getByRole('button', { name: 'Show all artwork', exact: true }).click();
    await page.getByRole('list', { name: 'Pattern layers', exact: true }).getByRole('button', { name: Saved.layers[0].name, exact: true }).click({ modifiers: ['Alt'] });
    const Before = await Save(page);
    await page.getByLabel('Width', { exact: true }).fill('120');
    Saved = await Save(page); expect(Saved.layers[0].width).toBe(120); expect(Saved.layers[2].width).toBe(120); expect(Saved.layers[2].x).toBe(Before.layers[2].x);
    await page.getByTitle('Undo', { exact: true }).click(); expect(await Save(page)).toEqual(Before);
    await page.locator('.pattern-surface-channels summary').click();
    const Encoded = await page.evaluate(() => { const Canvas = document.createElement('canvas'); Canvas.width = Canvas.height = 8; const Context = Canvas.getContext('2d'); Context.fillStyle = 'rgb(190,128,240)'; Context.fillRect(0, 0, 8, 8); return Canvas.toDataURL().split(',')[1]; });
    await page.getByLabel('Supply pattern normal map', { exact: true }).setInputFiles({ name: 'normal.png', mimeType: 'image/png', buffer: Buffer.from(Encoded, 'base64') });
    await expect(page.locator('.pattern-surface-channels')).toContainText('Supplied tangent-space normal map');
    Saved = await Save(page); expect(Saved.layers[0].SurfaceChannels.Normal.Source).toBe(Saved.layers[2].SurfaceChannels.Normal.Source);
    await page.getByTitle('Undo', { exact: true }).click(); expect(await Save(page)).toEqual(Before);
});

test("Normal-map UV extent, rotation, flips and strength work on both legacy and native paint shapes", async ({ page }) =>
{
    await page.goto('/?studio=pattern&pattern=Blank');
    const Samples = await page.evaluate(async () =>
    {
        const { RasterPatternChannel } = await import('/src/PatternSurfaceChannelControls.jsx');
        const { validatePattern, patternLayer } = await import('/src/patternDocument.js');
        const Canvas = document.createElement('canvas'); Canvas.width = Canvas.height = 16;
        const Context = Canvas.getContext('2d'); Context.fillStyle = 'rgb(204,128,230)'; Context.fillRect(0, 0, 8, 16); Context.fillStyle = 'rgb(128,204,230)'; Context.fillRect(8, 0, 8, 16);
        const Source = Canvas.toDataURL();
        const Render = async (Paint, Rotation = 0, Flip = false, Strength = 1) =>
        {
            const Document = validatePattern({ schema: 'alloy.pattern.v1', tileAxes: 'none', backgroundOpacity: 0, layers: [patternLayer('rect', { paint: Paint, width: 160, height: 80, rotation: Rotation, flipX: Flip, SurfaceChannels: { Height: { Enabled: false }, Normal: { Enabled: true, Strength, Source } } })] });
            const Map = await RasterPatternChannel(Document, 'Normal'), Read = (X, Y) => Array.from(Map.getContext('2d').getImageData(X, Y, 1, 1).data);
            return [Read(210, 256), Read(300, 256), Read(256, 210), Read(256, 300)];
        };
        return { Legacy: await Render(false), Paint: await Render(true), Rotated: await Render(true, 90), Flipped: await Render(true, 0, true), Strong: await Render(true, 0, false, 4) };
    });
    for (const Caption of ['Legacy', 'Paint'])
    {
        expect(Samples[Caption][0][0]).toBeGreaterThan(180); expect(Samples[Caption][1][1]).toBeGreaterThan(180);
    }
    expect(Samples.Rotated[2][1]).toBeLessThan(80); expect(Samples.Flipped[1][0]).toBeLessThan(80);
    expect(Samples.Strong[0][0]).toBeGreaterThan(Samples.Paint[0][0]); expect(Samples.Strong[0][2]).toBeLessThan(Samples.Paint[0][2]);
});

test("Actual PBR framebuffer lighting respects OpenGL positive-green normal direction", async ({ page }) =>
{
    test.setTimeout(90000);
    await page.goto('/?studio=pattern&pattern=Blank');
    const Result = await page.evaluate(async () =>
    {
        const Raw = await (await fetch('/src/patternRuntime.js')).text(), THREE = await import(Raw.match(/from\s*"([^"\n]+three[^"\n]+)"/)[1]);
        const { createMaterial } = await import('/src/materials.js');
        const { validatePattern, patternLayer } = await import('/src/patternDocument.js');
        const Renderer = new THREE.WebGLRenderer({ antialias: false }); Renderer.setSize(32, 32);
        const Target = new THREE.WebGLRenderTarget(32, 32), Geometry = new THREE.PlaneGeometry(2, 2), Camera = new THREE.OrthographicCamera(-1, 1, 1, -1, .1, 10); Camera.position.z = 3;
        const Scene = new THREE.Scene(), Light = new THREE.DirectionalLight(0xffffff, 2); Light.position.set(0, 3, 3); Scene.add(Light);
        const Errors = []; Renderer.debug.onShaderError = (Context, Program, Vertex, Fragment) => Errors.push(Context.getShaderInfoLog(Fragment));
        async function Sample(Green)
        {
            const Canvas = document.createElement('canvas'); Canvas.width = Canvas.height = 8; const Context = Canvas.getContext('2d'); Context.fillStyle = `rgb(128,${Green},230)`; Context.fillRect(0, 0, 8, 8);
            const Document = validatePattern({ schema: 'alloy.pattern.v1', repeats: 1, tileAxes: 'none', backgroundOpacity: 0, layers: [patternLayer('rect', { color: '#ffffff', width: 512, height: 512, relief: 0, SurfaceChannels: { Height: { Enabled: false }, Normal: { Enabled: true, Strength: 1, Source: Canvas.toDataURL() } } })] });
            const Material = createMaterial({ type: 7, color: '#ffffff', roughness: .8, metalness: 0, pattern: Document }); await Material.userData.ready;
            const Artwork = new THREE.Mesh(Geometry, Material); Scene.add(Artwork); await Renderer.compileAsync(Scene, Camera); Renderer.setRenderTarget(Target); Renderer.render(Scene, Camera);
            const Pixel = new Uint8Array(4); Renderer.readRenderTargetPixels(Target, 16, 16, 1, 1, Pixel); Scene.remove(Artwork); Material.dispose(); return Array.from(Pixel);
        }
        const Up = await Sample(204), Down = await Sample(51); Renderer.setRenderTarget(null); Geometry.dispose(); Target.dispose(); Renderer.dispose(); return { Up, Down, Errors };
    });
    expect(Result.Errors).toEqual([]); expect(Result.Up[0]).toBeGreaterThan(Result.Down[0] + 5);
});
