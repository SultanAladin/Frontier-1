import { test, expect } from "@playwright/test";
import { materials, normalizeMaterial, createMaterial } from "../src/materials.js";
import { getRecipe } from "../src/materialProfiles.js";
import { DeclareMaterialAuthoring, ChannelAddress, DeclareSharedParameter, ReadMaterialParameter, WritePbrChannel, WriteMaterialParameter, LinkMaterialParameter, UnlinkMaterialParameter, ExposeMaterialVariable, ProjectDesignerVariable, NormalizeMaterialAuthoring, ResolveAuthoringParameters, DeclareCodedField, DesignerControlQuantity, ProjectDesignerControl, PbrChannels } from "../src/MaterialSpecification.js";
import { EnumerateMaterialFields, RasterPbrFields, SamplePbrField } from "../src/FieldProjection.js";
const Paint = () => { const Parameters = structuredClone(materials.find(Entry => Entry.id === "racing-green")); Parameters.Authoring = DeclareMaterialAuthoring(Parameters); return Parameters; };
test("Racing Green declares physical layers and independent field inputs", () => {
    const Parameters = Paint(), Document = Parameters.Authoring;
    expect(Document.Layers.map(Layer => Layer.Name)).toEqual(["Clearcoat", "Metallic flakes", "Paint body"]);
    expect(Document.Layers.every(Layer => Layer.Channels.every(Channel => Object.hasOwn(PbrChannels, Channel)))).toBe(true);
    expect(Document.Generators[0].Inputs).toContain("flakeSize");
    expect(ChannelAddress(Document.Layers[1], "coverage")).not.toBe("flakes");
    expect(Document.Variables).toHaveLength(0);
    const Resolved = ResolveAuthoringParameters(normalizeMaterial(Parameters), Parameters);
    expect(Resolved.color).toBe(Parameters.color); expect(Resolved.FlakeWeight).toBe(1);
});
test("Properties clamp the same value without shrinking its physical domain", () => {
    let Parameters = Paint(), Body = Parameters.Authoring.Layers.at(-1), Address = ChannelAddress(Body, "roughness");
    Parameters = DeclareSharedParameter(Parameters, Address, PbrChannels.roughness, "Paint finish");
    Parameters = ExposeMaterialVariable(Parameters, Parameters.Authoring.Variables[0].Identity, { Name: "Designer finish", Minimum: .2, Maximum: .4 });
    Parameters = ProjectDesignerVariable(Parameters, Parameters.Authoring.Variables[0], .9);
    expect(Parameters.roughness).toBe(.4);
    Parameters = WritePbrChannel(Parameters, Body, "roughness", .91);
    expect(ReadMaterialParameter(Parameters, Address)).toBe(.91);
    expect(normalizeMaterial(Parameters).roughness).toBe(.91);
    Parameters.Authoring.Layers[1].Enabled = false;
    expect(normalizeMaterial(Parameters).flakes).toBe(Parameters.flakes);
    expect(ResolveAuthoringParameters(normalizeMaterial(Parameters), Parameters).roughness).toBe(.91);
    expect(Parameters.Authoring.Variables[0]).not.toHaveProperty("Quantity");
    expect(Parameters.Authoring.Variables[0]).not.toHaveProperty("Value");
});
test("sharing follows identity; detaching copies only the current input", () => {
    let Parameters = DeclareSharedParameter(Paint(), "roughness", PbrChannels.roughness, "Finish");
    const Identity = Parameters.Authoring.Variables[0].Identity;
    Parameters = LinkMaterialParameter(Parameters, "metalness", Identity);
    Parameters = WriteMaterialParameter(Parameters, "metalness", .38);
    expect(Parameters.roughness).toBe(.38); expect(ReadMaterialParameter(Parameters, "metalness")).toBe(.38);
    Parameters.Authoring.Variables[0].Name = "Shared finish";
    expect(Parameters.Authoring.Links.metalness).toBe(Identity);
    Parameters = UnlinkMaterialParameter(Parameters, "metalness");
    Parameters = WriteMaterialParameter(Parameters, "roughness", .6);
    expect(ReadMaterialParameter(Parameters, "metalness")).toBe(.38);
    expect(() => LinkMaterialParameter(Parameters, "roughness", Identity)).toThrow(/Cyclic/);
    Parameters = DeclareSharedParameter(Parameters, "color", PbrChannels.colour, "Tint");
    expect(() => LinkMaterialParameter(Parameters, "metalness", Parameters.Authoring.Variables[1].Identity)).toThrow(/domain/);
});
test("a single coded field drives Roughness and Metallic without copied settings", async () => {
    let Parameters = DeclareCodedField(Paint(), "noise", "Shared fractal", "Surface breakup");
    const Identity = Parameters.Authoring.Generators.at(-1).Identity;
    Parameters.metalness = Parameters.roughness;
    Parameters.Authoring.Sources.roughness = { Mode: "field", Generator: Identity };
    Parameters.Authoring.Sources.metalness = { Mode: "field", Generator: Identity };
    const Before = await RasterPbrFields(Parameters, 8), Rows = EnumerateMaterialFields(Parameters), Roughness = Rows.findIndex(Row => Row.Channel === "roughness"), Metallic = Rows.findIndex(Row => Row.Channel === "metallic");
    const Offset = Slot => (Math.floor(Slot / Math.min(8, Rows.length)) * 8 * Before.Width + (Slot % Math.min(8, Rows.length)) * 8) * 4;
    expect(Array.from(Before.Pixels.slice(Offset(Roughness), Offset(Roughness) + 4))).toEqual(Array.from(Before.Pixels.slice(Offset(Metallic), Offset(Metallic) + 4)));
    Parameters = WriteMaterialParameter(Parameters, `field:${Identity}:seed`, 201);
    const After = await RasterPbrFields(Parameters, 8);
    expect(Array.from(After.Pixels)).not.toEqual(Array.from(Before.Pixels));
    expect(After.Pixels.every(Number.isFinite)).toBe(true);
    Parameters.roughness = 0;
    const Attenuated = await RasterPbrFields(Parameters, 8);
    expect(Attenuated.Pixels[Offset(Roughness)]).toBe(0);
    const Material = createMaterial(Parameters); await Material.userData.ready; Material.dispose();
});
test("designer macros resolve actual targets rather than stale tuning", () => {
    let Parameters = Paint(); const Control = getRecipe(Parameters).controls.find(Entry => !Entry.direct && Entry.targets.some(Target => Target.key === "coatRoughness"));
    Parameters.tuning = { [Control.id]: .1 };
    Parameters = ProjectDesignerControl(Parameters, Control, .75);
    expect(DesignerControlQuantity(Parameters, Control)).toBeCloseTo(.75);
});
test("scratch, golf and solar retain distinct construction inputs", () => {
    const Documents = [29, 18, 17].map(Type => DeclareMaterialAuthoring(materials.find(Entry => Entry.type === Type)));
    expect(Documents[0].Generators[0].Inputs.some(Address => Address.startsWith("scratch"))).toBe(true);
    expect(Documents[1].Generators[0].Inputs.some(Address => Address.startsWith("dimple"))).toBe(true);
    expect(Documents[2].Generators[0].Inputs.some(Address => /cell|bus|finger/i.test(Address))).toBe(true);
});
test("native flake channels can take a solid value instead of their generated output", async () => {
    let Parameters = Paint();
    const Flakes = Parameters.Authoring.Layers.find(Layer => Layer.Model === "flakes"), Address = ChannelAddress(Flakes, "roughness");
    Parameters.Authoring.Sources[Address] = { Mode: "solid" };
    Parameters = WritePbrChannel(Parameters, Flakes, "roughness", .72);
    expect(() => NormalizeMaterialAuthoring(Parameters.Authoring)).not.toThrow();
    expect(ResolveAuthoringParameters(normalizeMaterial(Parameters), Parameters).FlakeOutputs.roughness).toBe(false);
    const Raster = await RasterPbrFields(Parameters, 8), Rows = Raster.Rows, Slot = Rows.findIndex(Row => Row.Model === "flakes" && Row.Channel === "roughness");
    const Columns = Math.min(8, Rows.length), Offset = (Math.floor(Slot / Columns) * 8 * Raster.Width + (Slot % Columns) * 8) * 4;
    expect(Raster.Pixels[Offset]).toBeCloseTo(.72);
});

test("direct field inputs read full texture/generator output rather than a hidden previous value", () => {
    const Scalar = { Source: { Mode: "texture", Direct: true }, Specification: PbrChannels.roughness, Domain: PbrChannels.roughness, Constant: .1 };
    const Raster = { Width: 1, Height: 1, Pixels: new Uint8Array([255, 255, 255, 255]) };
    expect(SamplePbrField(Scalar, 0, 0, Raster)[0]).toBeCloseTo(1);
    const Colour = { ...Scalar, Specification: PbrChannels.colour, Constant: "#000000" };
    expect(SamplePbrField(Colour, 0, 0, Raster)).toEqual([1, 1, 1, 1]);
});

test("invalid source kinds and references are rejected", () => {
    const Parameters = Paint(); Parameters.Authoring.Sources.roughness = { Mode: "script" };
    expect(() => NormalizeMaterialAuthoring(Parameters.Authoring)).toThrow(/source/);
    delete Parameters.Authoring.Sources.roughness; Parameters.Authoring.Links.roughness = "missing";
    expect(() => NormalizeMaterialAuthoring(Parameters.Authoring)).toThrow(/Missing/);
});

test("right inspector separates views, exposes shared variables and recovers edits", async ({ page }) => {
    const Failures = [];
    page.on("pageerror", Error => Failures.push(Error.message));
    page.on("console", Message => { if (Message.type() === "error") Failures.push(Message.text()); });
    await page.goto("/");
    const Inspector = page.locator(".inspector-panel");
    await expect(Inspector.getByRole("heading", { name: "PBR layer stack" })).toBeVisible();
    await expect(Inspector.getByLabel("Paint body PBR layer", { exact: true })).toBeVisible();
    await expect(Inspector.getByText("Native substrate", { exact: true })).toHaveCount(0);
    await Inspector.getByRole("button", { name: "Share Paint body Roughness", exact: true }).click();
    await Inspector.getByLabel("Expose Paint body Roughness to Properties", { exact: true }).check();
    await Inspector.getByLabel("Exposed property label", { exact: true }).fill("Designer finish");
    await Inspector.locator(".inspector-tabs").getByRole("button", { name: "Properties", exact: true }).click();
    await expect(Inspector.getByText("Designer finish", { exact: true }).first()).toBeVisible();
    await expect(Inspector.locator(".inspector-tabs").getByRole("button", { name: "Generators", exact: true })).toHaveCount(0);
    await Inspector.locator(".inspector-tabs").getByRole("button", { name: /^Layers/ }).click();
    const Flakes = Inspector.getByLabel("Metallic flakes PBR layer", { exact: true });
    await expect(Flakes.getByLabel("Metallic flakes Colour card", { exact: true }).getByLabel("Flake distribution generator sheet")).toBeVisible();
    const Body = Inspector.getByLabel("Paint body PBR layer", { exact: true });
    const Roughness = Body.getByLabel("Paint body Roughness card", { exact: true });
    await Roughness.getByLabel("Paint body Roughness source", { exact: true }).selectOption("field");
    await Roughness.getByLabel("Add coded generator", { exact: true }).selectOption("noise");
    await expect(Roughness.getByLabel("Fractal breakup generator sheet")).toBeVisible();
    await expect(Roughness.getByLabel("Roughness map maximum value", { exact: true })).toHaveCount(0);
    await expect.poll(async () => page.evaluate(() => JSON.parse(localStorage.getItem("alloy-authoring-v2:racing-green")).Authoring.Sources.roughness?.Mode)).toBe("field");
    await page.reload();
    await expect(Inspector.getByLabel("Paint body Roughness card", { exact: true }).getByLabel("Fractal breakup generator sheet")).toBeVisible();
    expect(Failures.filter(Message => !Message.includes("favicon"))).toEqual([]);
});

test("each layer field selects value, texture or an inline generator with only its matching controls", async ({ page }) => {
    await page.goto("/");
    const Inspector = page.locator(".inspector-panel"), Body = Inspector.getByLabel("Paint body PBR layer", { exact: true });
    const Roughness = Body.getByLabel("Paint body Roughness card", { exact: true });
    await Roughness.getByLabel("Roughness value", { exact: true }).fill("0.65");
    await Roughness.getByLabel("Paint body Roughness source", { exact: true }).selectOption("texture");
    await expect(Roughness.getByLabel("Roughness value", { exact: true })).toHaveCount(0);
    await expect(Roughness.getByLabel("Roughness map maximum value", { exact: true })).toHaveCount(0);
    await expect(Roughness.locator(".mw-card-output")).toHaveCount(0);
    const Preview = Roughness.getByLabel("Square texture preview", { exact: true });
    await expect(Preview).toBeVisible();
    const Bounds = await Preview.boundingBox();
    expect(Math.abs(Bounds.width - Bounds.height)).toBeLessThan(1);
    await expect(Roughness.getByText("Load from file", { exact: true })).toBeVisible();
    const PNG = await page.evaluate(() => { const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 2; const Context = Canvas.getContext("2d"); Context.fillStyle = "#808080"; Context.fillRect(0, 0, 2, 2); return Canvas.toDataURL().split(",")[1]; });
    await Roughness.getByLabel("Paint body Roughness texture", { exact: true }).setInputFiles({ name: "roughness.png", mimeType: "image/png", buffer: Buffer.from(PNG, "base64") });
    await expect(Roughness.getByAltText("Embedded channel source")).toBeVisible();
    await Roughness.getByLabel("Paint body Roughness source", { exact: true }).selectOption("field");
    await Roughness.getByLabel("Add coded generator", { exact: true }).selectOption("noise");
    const Metallic = Body.getByLabel("Paint body Metallic card", { exact: true });
    await Metallic.getByLabel("Paint body Metallic source", { exact: true }).selectOption("field");
    await Metallic.getByLabel("Paint body Metallic generator", { exact: true }).selectOption({ label: "Fractal breakup · Surface fields" });
    await Metallic.getByLabel("Seed value", { exact: true }).fill("84");
    await expect(Roughness.getByLabel("Seed value", { exact: true })).toHaveValue("84");
    const Colour = Body.getByLabel("Paint body Colour card", { exact: true });
    await Colour.getByLabel("Paint body Colour source", { exact: true }).selectOption("ramp");
    await Colour.getByRole("button", { name: "+ Ramp stop", exact: true }).click();
    await expect(Colour.locator(".mw-ramp-stops input[type=color]")).toHaveCount(3);
    await Colour.getByLabel("Paint body Colour source", { exact: true }).selectOption("value");
    await expect(Colour.getByLabel("Colour", { exact: true })).toBeVisible();
    await expect(Colour.getByLabel("Gradient start", { exact: true })).toHaveCount(0);
    await Roughness.getByLabel("Paint body Roughness source", { exact: true }).selectOption("value");
    await expect(Roughness.getByLabel("Roughness value", { exact: true })).toHaveValue("0.65");
    await expect(Roughness.locator(".mw-card-output, .mw-texture-preview")).toHaveCount(0);
    await expect(Roughness.getByLabel("Fractal breakup generator sheet")).toHaveCount(0);
    await expect(Inspector.locator(".inspector-tabs").getByRole("button", { name: "Generators", exact: true })).toHaveCount(0);
});

test("input selectors and sliders are visible immediately, and folding never hides the selector", async ({ page }) => {
    await page.goto("/");
    const Inspector = page.locator(".inspector-panel"), Coat = Inspector.getByLabel("Clearcoat PBR layer", { exact: true });
    const Roughness = Coat.getByLabel("Clearcoat Roughness card", { exact: true });
    await expect(Roughness.getByLabel("Clearcoat Roughness source", { exact: true })).toBeVisible();
    await expect(Roughness.getByLabel("Roughness value", { exact: true })).toBeVisible();
    await Roughness.getByLabel("Roughness value", { exact: true }).fill("0.37");
    await Roughness.getByRole("button", { name: "Roughness", exact: true }).click();
    await expect(Roughness.getByLabel("Clearcoat Roughness source", { exact: true })).toBeVisible();
    await Roughness.getByRole("button", { name: "Roughness", exact: true }).click();
    await expect(Roughness.getByLabel("Roughness value", { exact: true })).toHaveValue("0.37");
    await Roughness.getByRole("button", { name: "Roughness", exact: true }).click();
    await Roughness.getByLabel("Clearcoat Roughness source", { exact: true }).selectOption("texture");
    await expect(Roughness.getByLabel("Clearcoat Roughness texture", { exact: true })).toBeVisible();
});

test("scoped PBR fields compile, alter rendered pixels and survive self-contained shader export", async ({ page }) => {
    await page.goto("/");
    const Result = await page.evaluate(async () => {
        const THREE = await import("/node_modules/three/build/three.module.js");
        const { materials, createMaterial } = await import("/src/materials.js");
        const { DeclareMaterialAuthoring, ChannelAddress, PbrChannels, DeclareCodedField } = await import("/src/MaterialSpecification.js");
        const { exportMaterialShader, exportMaterialJSON } = await import("/src/materialExport.js");
        const Renderer = new THREE.WebGLRenderer({ antialias: false }); Renderer.setSize(16, 16);
        const Failures = []; Renderer.debug.onShaderError = (GL, Program, Vertex, Fragment) => Failures.push(GL.getShaderInfoLog(Vertex) + GL.getShaderInfoLog(Fragment));
        const Scene = new THREE.Scene(), Camera = new THREE.PerspectiveCamera(40, 1, .1, 10); Camera.position.z = 3;
        Scene.add(new THREE.AmbientLight("white", 2)); const Lamp = new THREE.DirectionalLight("white", 3); Lamp.position.set(1, 2, 3); Scene.add(Lamp);
        const Geometry = new THREE.SphereGeometry(1, 16, 12), Target = new THREE.WebGLRenderTarget(16, 16);
        const Render = async Material => { await Material.userData.ready; const Mesh = new THREE.Mesh(Geometry, Material); Scene.add(Mesh); Renderer.setRenderTarget(Target); Renderer.render(Scene, Camera); const Pixels = new Uint8Array(16 * 16 * 4); Renderer.readRenderTargetPixels(Target, 0, 0, 16, 16, Pixels); Scene.remove(Mesh); Material.dispose(); return Array.from(Pixels); };
        let Parameters = structuredClone(materials.find(Entry => Entry.id === "racing-green")); Parameters.Authoring = DeclareMaterialAuthoring(Parameters);
        const Original = await Render(createMaterial(Parameters));
        Parameters = DeclareCodedField(Parameters, "noise", "Shared field", "Surface fields");
        const Field = Parameters.Authoring.Generators.at(-1).Identity, Body = Parameters.Authoring.Layers.at(-1), Coat = Parameters.Authoring.Layers[0];
        Body.Channels = Object.keys(PbrChannels); Coat.Channels.push("normal");
        for (const Layer of [Body, Coat]) for (const Channel of Layer.Channels) Parameters.Authoring.Sources[ChannelAddress(Layer, Channel)] = { Mode: "field", Generator: Field };
        const Modified = await Render(createMaterial(Parameters));
        const Source = exportMaterialShader(Parameters), URL = URLConstructor(Source);
        function URLConstructor(Source) { return window.URL.createObjectURL(new Blob([Source.replace("from 'three'", `from '${location.origin}/node_modules/three/build/three.module.js'`)], { type: "text/javascript" })); }
        let Exported;
        try { const Module = await import(/* @vite-ignore */ URL); Exported = await Render(Module.default); }
        finally { window.URL.revokeObjectURL(URL); }
        const Recovery = JSON.parse(exportMaterialJSON(Parameters)).material;
        Target.dispose(); Geometry.dispose(); Renderer.dispose();
        return { Failures, Changed: Original.some((Quantity, Index) => Quantity !== Modified[Index]), Parity: Modified.every((Quantity, Index) => Quantity === Exported[Index]), Recovered: Recovery.Authoring.Sources.roughness.Generator === Recovery.Authoring.Sources.metalness.Generator };
    });
    expect(Result.Failures).toEqual([]); expect(Result.Changed).toBe(true); expect(Result.Parity).toBe(true); expect(Result.Recovered).toBe(true);
});


test("layer sliders use the original inspector styling and retain range and numeric editing", async ({ page }) => {
    await page.goto("/");
    const Card = page.getByLabel("Clearcoat Roughness card", { exact: true });
    const Range = Card.getByRole("slider", { name: "Roughness", exact: true });
    await expect(Range).toBeVisible();
    const Styles = await Range.evaluate(Input => {
        const Original = document.createElement("div"); Original.className = "inspector-scroll";
        Original.style.cssText = "position:fixed;left:-10000px;width:300px";
        Original.append(Input.closest(".slider-field").cloneNode(true));
        document.querySelector(".studio-material").append(Original);
        const Read = (Element, Pseudo) => {
            const Style = getComputedStyle(Element, Pseudo);
            return Object.fromEntries(["height", "padding", "borderWidth", "borderRadius", "backgroundColor", "backgroundImage", "fontSize", "color"].map(Key => [Key, Style[Key]]));
        };
        const Actual = { Track: Read(Input), Thumb: Read(Input, "::-webkit-slider-thumb"), Value: Read(Input.closest(".slider-field").querySelector(".slider-value input")) };
        const Reference = { Track: Read(Original.querySelector(".range-input")), Thumb: Read(Original.querySelector(".range-input"), "::-webkit-slider-thumb"), Value: Read(Original.querySelector(".slider-value input")) };
        Original.remove(); return { Actual, Reference };
    });
    expect(Styles.Actual).toEqual(Styles.Reference);
    await Range.focus(); await Range.press("ArrowRight");
    await expect(Card.getByLabel("Roughness value", { exact: true })).toHaveValue("0.1");
    await Card.getByLabel("Roughness value", { exact: true }).fill("0.42");
    await expect(Range).toHaveValue("0.42");
});
