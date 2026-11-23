import { test, expect } from "@playwright/test";
import { patternLayer, patternStarter, validatePattern, patternSVG } from "../src/patternDocument.js";
import { NormalizePatternColourSource } from "../src/PatternColourSources.js";
const Gradient = { category: "linear", angle: 0, stops: [{ color: "#ff0000", offset: 0 }, { color: "#0000ff", offset: 1 }] };
const Raster = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+cOioAAAAASUVORK5CYII=";
function PaintDocument()
{
  return validatePattern({ ...patternStarter("Blank"), tileAxes: "none", repeats: 1, layers: [patternLayer("rect", { paint: true, fillEnabled: true, color: "#eeeeee", stroke: "#222222", width: 400, height: 400, strokeWidth: 8, fillSource: Gradient, strokeSource: { ...Gradient, category: "radial", stops: [{ color: "#00ff00", offset: 0 }, { color: "#00ff00", offset: 1 }] } })] });
}
test("paint sources round-trip while legacy documents remain unchanged", () =>
{
  const Legacy = patternStarter("Diamond weave");
  expect(validatePattern(JSON.parse(JSON.stringify(Legacy)))).toEqual(Legacy);
  expect(Legacy.backgroundSource).toBeUndefined();
  const Document = PaintDocument();
  Document.backgroundSource = { category: "image", image: Raster, fit: "contain" };
  expect(validatePattern(JSON.parse(JSON.stringify(Document)))).toEqual(Document);
  expect(Document.layers[0].fillSource).toEqual(Gradient);
  const Svg = patternSVG(Document);
  expect(Svg).toContain("<linearGradient");
  expect(Svg).toContain("<radialGradient");
  expect(Svg).toContain("gradientUnits=\"userSpaceOnUse\"");
  expect(Svg).toContain(Raster);
  expect(Svg.match(/<linearGradient /g)).toHaveLength(1);
  expect(patternSVG(Document, "preview").match(/<linearGradient /g)).toHaveLength(1);
  for (const Mode of ["finish", "surface"])
  {
    expect(patternSVG(Document, Mode)).not.toContain("Gradient");
    expect(patternSVG(Document, Mode)).not.toContain(Raster);
    expect(patternSVG(Document, Mode)).not.toContain("url(#pattern-paint");
  }
});
test("paint source validation rejects active content and clamps authoring bounds", () =>
{
  for (const Image of ['https://example.com/paint.png', 'data:image/svg+xml;base64,PHN2Zz4=', 'data:image/png;base64,AA\" onload=\"alert(1)', 'data:image/png;base64,' + 'a'.repeat(6000001)])
    expect(NormalizePatternColourSource({ category: "image", image: Image })).toBeUndefined();
  expect(NormalizePatternColourSource({ category: "linear", stops: [{ color: 'url(javascript:alert(1))', offset: 0 }, { color: '#000000', offset: 1 }] })).toBeUndefined();
  expect(NormalizePatternColourSource({ ...Gradient, angle: 10000, stops: [{ color: '#ffffff', offset: 9 }, { color: '#000000', offset: -4 }] })).toMatchObject({ angle: 180, stops: [{ offset: 0 }, { offset: 1 }] });
  const Document = PaintDocument();
  Document.layers[0].fillSource = { category: "image", image: 'javascript:alert(1)' };
  expect(validatePattern(Document).layers[0].fillSource).toBeUndefined();
});
async function OpenPattern(Page)
{
  await Page.goto("/?studio=pattern&pattern=Blank");
  await expect(Page.getByLabel("Pattern design canvas", { exact: true })).toBeVisible();
}
async function AddShape(Page, Name)
{
  await Page.getByRole("button", { name: "Add pattern layer", exact: true }).click();
  await Page.getByRole("group", { name: "Add a pattern motif", exact: true }).getByRole("button", { name: Name, exact: true }).click();
}
async function SavedDocument(Page)
{
  const Download = Page.waitForEvent("download");
  await Page.getByRole("button", { name: "Save document", exact: true }).click();
  const Chunks = [];
  for await (const Chunk of await (await Download).createReadStream()) Chunks.push(Chunk);
  return JSON.parse(Buffer.concat(Chunks).toString());
}
async function RenderSamples(Page, Svg, Locations)
{
  return Page.evaluate(async ({ Svg, Locations }) =>
  {
    const ImageElement = new Image();
    ImageElement.src = `data:image/svg+xml,${encodeURIComponent(Svg)}`;
    await ImageElement.decode();
    const Canvas = document.createElement("canvas");
    Canvas.width = Canvas.height = 512;
    const Context = Canvas.getContext("2d");
    Context.drawImage(ImageElement, 0, 0, 512, 512);
    return Locations.map(([X, Y]) => Array.from(Context.getImageData(X, Y, 1, 1).data));
  }, { Svg, Locations });
}
test("SVG raster actually shades fill, stroke, flat lines and ground sources", async ({ page: Page }) =>
{
  await OpenPattern(Page);
  const Document = PaintDocument();
  const [Left, Right, Stroke] = await RenderSamples(Page, patternSVG(Document), [[125, 256], [385, 256], [56, 256]]);
  expect(Left[0]).toBeGreaterThan(Left[2]);
  expect(Right[2]).toBeGreaterThan(Right[0]);
  expect(Stroke.slice(0, 3)).toEqual([0, 255, 0]);
  Document.layers = [patternLayer("path", { paint: true, path: 'M0 50L100 50', fillEnabled: false, width: 400, height: 1, strokeWidth: 10, strokeSource: Gradient })];
  const [LineLeft, LineRight] = await RenderSamples(Page, patternSVG(Document), [[125, 256], [385, 256]]);
  expect(LineLeft[0]).toBeGreaterThan(LineLeft[2]);
  expect(LineRight[2]).toBeGreaterThan(LineRight[0]);
  const Image = await Page.evaluate(() =>
  {
    const Canvas = document.createElement('canvas'); Canvas.width = 64; Canvas.height = 32;
    const Context = Canvas.getContext('2d'); Context.fillStyle = '#ff0000'; Context.fillRect(0, 0, 32, 32); Context.fillStyle = '#0000ff'; Context.fillRect(32, 0, 32, 32);
    return Canvas.toDataURL();
  });
  Document.layers = [];
  Document.backgroundSource = { category: 'image', image: Image, fit: 'cover' };
  const [GroundLeft, GroundRight] = await RenderSamples(Page, patternSVG(Document), [[125, 256], [385, 256]]);
  expect(GroundLeft.slice(0, 3)).toEqual([255, 0, 0]); expect(GroundRight.slice(0, 3)).toEqual([0, 0, 255]);
  Document.layers = [patternLayer('rect', { paint: true, fillEnabled: true, width: 400, height: 200, fillSource: Document.backgroundSource })];
  delete Document.backgroundSource;
  const [ImageLeft, ImageRight] = await RenderSamples(Page, patternSVG(Document), [[125, 256], [385, 256]]);
  expect(ImageLeft.slice(0, 3)).toEqual([255, 0, 0]); expect(ImageRight.slice(0, 3)).toEqual([0, 0, 255]);
  Document.layers = [patternLayer("path", { paint: true, path: 'M0 50L100 50', fillEnabled: false, width: 400, height: 1, strokeWidth: 10, strokeSource: { category: 'image', image: Image, fit: 'cover' } })];
  const [StrokeImageLeft, StrokeImageRight] = await RenderSamples(Page, patternSVG(Document), [[125, 256], [385, 256]]);
  expect(StrokeImageLeft.slice(0, 3)).toEqual([255, 0, 0]); expect(StrokeImageRight.slice(0, 3)).toEqual([0, 0, 255]);
  Document.layers = []; Document.tileAxes = "xy"; Document.backgroundSource = Gradient;
  Document.repeat = 'mirror';
  const MirroredGround = await RenderSamples(Page, patternSVG(Document), [[64, 64], [448, 64]]);
  // Mirrored raster sample centres can straddle a one-level interpolation rounding boundary.
  for (let Channel = 0; Channel < 3; Channel++) expect(Math.abs(MirroredGround[0][Channel] - MirroredGround[1][Channel])).toBeLessThanOrEqual(1);
  Document.repeat = 'half-drop';
  const DroppedGround = await RenderSamples(Page, patternSVG(Document), [[64, 256], [320, 256]]);
  for (let Channel = 0; Channel < 3; Channel++) expect(Math.abs(DroppedGround[0][Channel] - DroppedGround[1][Channel])).toBeLessThanOrEqual(1);
});
test("Pattern add/filter operations retain identity, locks, existing layers and undo", async ({ page: Page }) =>
{
  await OpenPattern(Page);
  await AddShape(Page, 'Rectangle'); await Page.getByLabel('Layer name', { exact: true }).fill('Foundation');
  await AddShape(Page, 'Rosette'); await Page.getByLabel('Layer name', { exact: true }).fill('Accent');
  const List = Page.getByRole('list', { name: 'Pattern layers', exact: true });
  await expect(List.getByRole('listitem')).toHaveCount(2);
  await Page.getByLabel('Layer visibility 2: Accent').uncheck();
  await Page.getByLabel('Find a layer', { exact: true }).fill('FOUNDATION');
  await expect(List.getByRole('listitem')).toHaveCount(1);
  await List.getByRole('button', { name: 'Foundation', exact: true }).click();
  await Page.getByLabel('Layer name', { exact: true }).fill('Foundation edited');
  await Page.getByLabel('Layer lock 1: Foundation edited').check();
  await Page.getByRole('button', { name: 'Hidden layers', exact: true }).click();
  await expect(List.getByRole('listitem')).toHaveCount(0);
  await Page.getByRole('button', { name: 'Clear layer search', exact: true }).click();
  await expect(List.getByRole('listitem')).toHaveCount(1);
  await expect(List).toContainText('Accent');
  await Page.getByRole('button', { name: 'Locked layers', exact: true }).click();
  await expect(List).toContainText('Foundation edited');
  await expect(Page.getByLabel('Layer name', { exact: true })).toBeDisabled();
  await AddShape(Page, 'Ring');
  await expect(List.getByRole('listitem')).toHaveCount(3);
  const Document = await SavedDocument(Page);
  expect(Document.layers.map(Layer => Layer.name)).toEqual(['Foundation edited', 'Accent', 'Ring']);
  expect(Document.layers[0].locked).toBe(true); expect(Document.layers[1].visible).toBe(false); expect(Document.layers[2].fillEnabled).toBe(false);
  await Page.getByTitle('Undo', { exact: true }).click();
  await expect(List.getByRole('listitem')).toHaveCount(2);
});
test("inspector edits render sources, round-trip saved JSON, and undo/redo", async ({ page: Page }) =>
{
  const Failures = []; Page.on('pageerror', Failure => Failures.push(Failure.message));
  await OpenPattern(Page); await AddShape(Page, 'Rectangle');
  await Page.getByLabel('Motif fill source', { exact: true }).selectOption('linear');
  await Page.getByLabel('Motif fill stop 1 colour').fill('#ff0000');
  await Page.getByLabel('Motif fill stop 2 colour').fill('#0000ff');
  await Page.getByLabel('Motif stroke source', { exact: true }).selectOption('radial');
  await Page.getByLabel('Stroke width', { exact: true }).fill('12');
  const Document = await SavedDocument(Page);
  expect(Document.layers[0].fillSource.category).toBe('linear'); expect(Document.layers[0].strokeSource.category).toBe('radial');
  const Canvas = Page.getByLabel('Pattern design canvas', { exact: true });
  await expect(Canvas.locator('linearGradient')).not.toHaveCount(0);
  await expect(Canvas.locator('[data-layer="0"] [fill^="url(#pattern-paint-"]').first()).toBeAttached();
  await Page.getByTitle('Undo', { exact: true }).click();
  await Page.getByTitle('Redo', { exact: true }).click();
  expect((await SavedDocument(Page)).layers[0].strokeWidth).toBe(12);
  await Page.getByLabel('Open pattern JSON').setInputFiles({ name: 'paint.pattern.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(Document)) });
  expect((await SavedDocument(Page)).layers[0].fillSource).toEqual(Document.layers[0].fillSource);
  await Page.getByLabel('Motif fill source', { exact: true }).selectOption('solid');
  expect((await SavedDocument(Page)).layers[0].fillSource).toBeUndefined();
  expect(Failures).toEqual([]);
});
test("image upload is embedded, rejects invalid content and paints ground in the canvas", async ({ page: Page }) =>
{
  await OpenPattern(Page); await AddShape(Page, 'Rectangle');
  await Page.getByLabel('Motif fill image file', { exact: true }).setInputFiles({ name: 'bad.png', mimeType: 'image/png', buffer: Buffer.from('not an image') });
  await expect(Page.getByRole('alert')).toContainText(/decode|image/i);
  const Image = await Page.evaluate(() =>
  {
    const Canvas = document.createElement('canvas'); Canvas.width = 32; Canvas.height = 16;
    const Context = Canvas.getContext('2d'); Context.fillStyle = '#28aa77'; Context.fillRect(0, 0, 32, 16); return Canvas.toDataURL().split(',')[1];
  });
  const File = { name: 'paint.png', mimeType: 'image/png', buffer: Buffer.from(Image, 'base64') };
  await Page.getByLabel('Motif fill image file', { exact: true }).setInputFiles(File);
  await expect(Page.getByLabel('Motif fill source', { exact: true })).toHaveValue('image');
  await Page.getByLabel('Motif fill image fit').selectOption('contain');
  await Page.getByLabel('Motif stroke image file', { exact: true }).setInputFiles(File);
  await expect(Page.getByLabel('Motif stroke source', { exact: true })).toHaveValue('image');
  await Page.getByLabel('Ground image file', { exact: true }).setInputFiles(File);
  await expect(Page.getByLabel('Ground source', { exact: true })).toHaveValue('image');
  const Document = await SavedDocument(Page);
  expect(Document.layers[0].fillSource).toMatchObject({ category: 'image', fit: 'contain' });
  expect(Document.layers[0].fillSource.image).toBe('data:image/png;base64,' + Image);
  expect(Document.layers[0].strokeSource.category).toBe('image');
  expect(Document.backgroundSource.image).toMatch(/^data:image\/png;base64,/);
  expect(Document.layers[0].fillSource.image).not.toContain('blob:');
  await expect(Page.getByLabel('Pattern design canvas', { exact: true }).locator('defs image')).not.toHaveCount(0);
  await expect.poll(() => Page.evaluate(() => JSON.parse(localStorage.getItem('alloy-pattern-draft-v2') || '{}').doc?.backgroundSource?.category)).toBe('image');
  await Page.reload();
  const Restore = Page.getByRole('button', { name: 'Restore draft', exact: true });
  await expect(Restore).toBeVisible(); await Restore.click();
  await Page.getByRole('list', { name: 'Pattern layers', exact: true }).getByRole('listitem').first().getByRole('button').first().click();
  await expect(Page.getByLabel('Motif fill source', { exact: true })).toHaveValue('image');
});

test("drawing defaults and multi-selection apply real sources while locked motifs stay unchanged", async ({ page: Page }) =>
{
  await OpenPattern(Page);
  await Page.getByLabel('Drawing fill source', { exact: true }).selectOption('linear');
  await AddShape(Page, 'Rectangle');
  await AddShape(Page, 'Ellipse');
  const List = Page.getByRole('list', { name: 'Pattern layers', exact: true });
  expect((await SavedDocument(Page)).layers.every(Layer => Layer.fillSource?.category === 'linear')).toBe(true);
  await List.getByRole('listitem').last().getByRole('button').first().click({ modifiers: ['Control'] });
  await expect(Page.getByLabel('Drawing fill source', { exact: true })).toBeVisible();
  await Page.getByLabel('Drawing fill source', { exact: true }).selectOption('radial');
  expect((await SavedDocument(Page)).layers.every(Layer => Layer.fillSource?.category === 'radial')).toBe(true);
  await Page.getByLabel('Layer lock 1: rect').check();
  await Page.getByLabel('Drawing stroke source', { exact: true }).selectOption('linear');
  const Document = await SavedDocument(Page);
  expect(Document.layers[0].strokeSource).toBeUndefined();
  expect(Document.layers[1].strokeSource.category).toBe('linear');
  await Page.getByRole('button', { name: 'Add pattern layer', exact: true }).click();
  const Choices = Page.getByRole('group', { name: 'Add a pattern motif', exact: true });
  await expect(Choices).toBeVisible();
  const Bounds = await Choices.boundingBox(); expect(Bounds.x).toBeGreaterThanOrEqual(12); expect(Bounds.y + Bounds.height).toBeLessThanOrEqual(888);
  await Page.keyboard.press('Escape');
  await expect(Choices).toHaveCount(0);
  await expect(Page.getByRole('dialog', { name: 'Pattern studio', exact: true })).toBeVisible();
});
