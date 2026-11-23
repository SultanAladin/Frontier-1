import { test, expect } from "@playwright/test";
import { SampleGenerator } from "../src/FieldSpecification.js";
import { materials } from "../src/materials.js";
import { RasterPbrFields } from "../src/FieldProjection.js";
import { WriteMaterialParameter, NormalizeMaterialAuthoring } from "../src/MaterialSpecification.js";

test("camouflage is deterministic, periodic, multi-colour and seed-dependent", () => {
    const Settings = { scale: 8, seed: 12 }, Samples = [];
    for (let Vertical = 0; Vertical < 32; Vertical++) for (let Horizontal = 0; Horizontal < 32; Horizontal++) {
        const Quantity = SampleGenerator("camouflage", Settings, Horizontal / 32 - .5, Vertical / 32 - .5);
        expect(Number.isFinite(Quantity)).toBe(true); Samples.push(Quantity);
    }
    expect(new Set(Samples).size).toBe(4);
    expect(Samples).toEqual(Samples.map((Quantity, Index) => SampleGenerator("camouflage", Settings, (Index % 32) / 32 - .5, Math.floor(Index / 32) / 32 - .5)));
    expect(Samples.some((Quantity, Index) => Quantity !== SampleGenerator("camouflage", { ...Settings, seed: 84 }, (Index % 32) / 32 - .5, Math.floor(Index / 32) / 32 - .5))).toBe(true);
    for (let Index = 0; Index < 31; Index++) {
        expect(SampleGenerator("camouflage", Settings, -.5, Index / 31 - .5)).toBe(SampleGenerator("camouflage", Settings, .5, Index / 31 - .5));
        expect(SampleGenerator("camouflage", Settings, Index / 31 - .5, -.5)).toBe(SampleGenerator("camouflage", Settings, Index / 31 - .5, .5));
    }
});

test("fabric and polymer use editable procedural camouflage, not image assets", async () => {
    for (const Identity of ["woodland-camouflage-fabric", "camouflage-ballistic-polymer"]) {
        const Parameters = materials.find(Entry => Entry.id === Identity), Generator = Parameters.Authoring.Generators.find(Entry => Entry.Procedure === "camouflage");
        expect(Generator).toBeTruthy();
        expect(Parameters.Authoring.Sources.color).toEqual({ Mode: "field", Direct: true, Generator: Generator.Identity });
        expect(() => NormalizeMaterialAuthoring(Parameters.Authoring)).not.toThrow();
        const Raster = await RasterPbrFields(Parameters, 16);
        expect(Raster.Pixels.every(Number.isFinite)).toBe(true);
        const Modified = WriteMaterialParameter(Parameters, `field:${Generator.Identity}:colour1`, "#ff0000");
        expect(Array.from((await RasterPbrFields(Modified, 16)).Pixels)).not.toEqual(Array.from(Raster.Pixels));
        expect(JSON.stringify(Parameters.Authoring.Sources)).not.toContain("data:image");
    }
});

test("camouflage presets expose their colour generator inside the field", async ({ page }) => {
    const Failures = [];
    page.on("pageerror", Error => Failures.push(Error.message));
    page.on("console", Message => { if (Message.type() === "error") Failures.push(Message.text()); });
    await page.goto("/?material=woodland-camouflage-fabric");
    const Inspector = page.locator(".inspector-panel"), Colour = Inspector.getByLabel("Surface body Colour card", { exact: true });
    await expect(Colour.getByLabel("Woodland camouflage generator sheet")).toBeVisible();
    await expect(Colour.getByLabel("Sand colour", { exact: true })).toBeVisible();
    await expect(Colour.getByLabel("Scale value", { exact: true })).toBeVisible();
    await Colour.getByLabel("Seed value", { exact: true }).fill("84");
    await expect(Colour.getByLabel("Seed value", { exact: true })).toHaveValue("84");
    await expect(page.locator("canvas[data-material-ready]")).toHaveAttribute("data-material-ready", "true");
    await page.getByRole("button", { name: "Apply Camouflage Ballistic Polymer", exact: true }).click();
    await expect(Inspector.getByRole("heading", { name: "Camouflage Ballistic Polymer", exact: true })).toBeVisible();
    await expect(Colour.getByLabel("Woodland camouflage generator sheet")).toBeVisible();
    await expect(page.locator("canvas[data-material-ready]")).toHaveAttribute("data-material-ready", "true");
    expect(Failures).toEqual([]);
});

test("smooth camouflage has continuous edges rather than quantized colour steps", () => {
    const Values = [];
    for (let Vertical = 0; Vertical < 40; Vertical++) for (let Horizontal = 0; Horizontal < 40; Horizontal++) Values.push(SampleGenerator("camouflage-smooth", { scale: 8, seed: 12, softness: .03 }, Horizontal / 40 - .5, Vertical / 40 - .5));
    expect(Values.every(Number.isFinite)).toBe(true);
    expect(new Set(Values.map(Quantity => Quantity.toFixed(4))).size).toBeGreaterThan(20);
    for (let Index = 0; Index < 20; Index++) expect(SampleGenerator("camouflage-smooth", { scale: 8, seed: 12 }, -.5, Index / 20 - .5)).toBeCloseTo(SampleGenerator("camouflage-smooth", { scale: 8, seed: 12 }, .5, Index / 20 - .5));
    expect(materials.find(Entry => Entry.id === "woodland-camouflage-fabric-smooth")).toBeTruthy();
    expect(materials.find(Entry => Entry.id === "camouflage-ballistic-polymer-smooth")).toBeTruthy();
});

test("palette array adds/removes editable colours and smooth style exposes softness", async ({ page }) => {
    const Errors = [];
    page.on("pageerror", Error => Errors.push(Error.message));
    page.on("console", Message => { if (Message.type() === "error") Errors.push(Message.text()); });
    await page.goto("/?material=woodland-camouflage-fabric-smooth");
    const Colour = page.getByLabel("Surface body Colour card", { exact: true }), Palette = Colour.locator(".mw-camouflage-palette");
    await expect(Palette.locator("input[type=color]")).toHaveCount(4);
    await Palette.getByRole("button", { name: "+ Add colour", exact: true }).click();
    await expect(Palette.locator("input[type=color]")).toHaveCount(5);
    await Palette.getByLabel("Colour 5", { exact: true }).fill("#e0c878");
    await expect(Palette.getByLabel("Colour 5", { exact: true })).toHaveValue("#e0c878");
    await Palette.getByRole("button", { name: "Remove last colour", exact: true }).click();
    await expect(Palette.locator("input[type=color]")).toHaveCount(4);
    await expect(Colour.getByLabel("Softness value", { exact: true })).toBeVisible();
    await expect(Colour.getByLabel("Pixel Size value", { exact: true })).toHaveCount(0);
    await expect(page.locator("canvas[data-material-ready]")).toHaveAttribute("data-material-ready", "true");
    await Colour.getByLabel("Camouflage edge style", { exact: true }).selectOption("camouflage");
    await expect(Colour.getByLabel("Pixel Size value", { exact: true })).toBeVisible();
    await expect(Colour.getByLabel("Softness value", { exact: true })).toHaveCount(0);
    await expect(page.locator("canvas[data-material-ready]")).toHaveAttribute("data-material-ready", "true");
    expect(Errors).toEqual([]);
});
