import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { ResolveRenderSettings, CanonicalRenderSettings, MasterQualityPatch, RendererPatch, MasterTier, PbrShadowPatch, OcclusionOf, RendererLines } from "./RenderSettings.js";

// Per-camera render settings as one Render Engine component: every camera (Editor, Cine, Construct-placed) has a single row whose
// inspector has an Engine tab (renderer, Quality, Output) and one tab per renderer (PBR, SDF, Ray traced, MatCap), each with its own
// shadow algorithm and settings.
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/RenderSettings");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [],
  FontFailures = [];

// Pure data first: clamping, fallbacks, the per-renderer preset fan-out, and the upgrade of the shared Follow-up 37–41 keys.
assert.deepEqual(ResolveRenderSettings({}), CanonicalRenderSettings);
const Wild = ResolveRenderSettings({ RenderEngine: "sdf_shadows", PbrContactRays: 99, PbrPcssTaps: 1, PbrMapRes: 4096, SdfGiIntensity: -3, RenderScale: 60, ExposureGain: "x", RayShadow: "bvh", MatcapShadow: "x" });
assert.equal(Wild.RenderEngine, "gtx_sdf_gi");
assert.equal(Wild.PbrContactRays, 16);
assert.equal(Wild.PbrPcssTaps, 8);
assert.equal(Wild.PbrMapRes, 2048);
assert.equal(Wild.SdfGiIntensity, 0);
assert.equal(Wild.RenderScale, 100);
assert.equal(Wild.ExposureGain, 1.35);
assert.equal(Wild.RayShadow, "bvh_disk");
assert.equal(Wild.MatcapShadow, "drop");
const Ultra = MasterQualityPatch(3);
assert.deepEqual([Ultra.PbrContactRays, Ultra.PbrPcssTaps, Ultra.SdfSteps, Ultra.SdfCones, Ultra.RayShadowRays, Ultra.RayGiSamples, Ultra.RayReflectionBounces], [8, 32, 48, 10, 8, 14, 3]);
assert.equal(PbrShadowPatch(0).PbrMapRes, 1024);
assert.equal(MasterTier({ ...CanonicalRenderSettings, ...PbrShadowPatch(0) }), -1);
assert.equal(RendererPatch("ray", { RayTracer: "pt_final" }).RenderEngine, "pt_final");
const Upgraded = ResolveRenderSettings({ RenderEngine: "pt_realtime", ShadowAlgorithm: "pcf", ShadowMapRes: 1024, SdfPenumbraMode: "analytic_cone", GiAlgorithm: "bvh", GiSamples: 12, ReflectionAlgorithm: "env" });
assert.deepEqual([Upgraded.PbrShadow, Upgraded.PbrMapRes, Upgraded.SdfShadow, Upgraded.RayGi, Upgraded.SdfGi, Upgraded.RayGiSamples, Upgraded.RayReflections, Upgraded.RayTracer], ["pcf", 1024, "analytic_cone", true, false, 12, false, "pt_realtime"]);
Checks.push("Defaults, clamps, unknown-id fallback, per-renderer tier fan-out and the upgrade of the shared Follow-up 41 keys");
// Follow-up 46: each renderer has its own ambient occlusion keys; MatCap has none.
const Occluded = ResolveRenderSettings({ SdfAoMethod: "hbao", SdfAoTaps: 40, RayAoRays: 0, XeGtaoSlices: 12, RayAoMethod: "rtao_bent" });
assert.deepEqual([Occluded.SdfAoMethod, Occluded.SdfAoTaps, Occluded.RayAoRays, Occluded.XeGtaoSlices, Occluded.RayAoMethod], ["sdf_cone", 12, 1, 9, "rtao_bent"]);
assert.deepEqual(["openpbr_direct", "gtx_sdf_gi", "pt_final", "matcap"].map((RenderEngine) => OcclusionOf({ ...CanonicalRenderSettings, RenderEngine })?.Switch ?? null), ["XeGtao", "SdfAo", "RayAo", null]);
assert.deepEqual(RendererLines(CanonicalRenderSettings).at(-1), ["AO", "Cone-traced SDF AO · 4 cones"]);
Checks.push("Ambient occlusion per renderer: XeGTAO (PBR), SDF AO and traced AO keys clamp and fall back; MatCap has none; the summary gains an AO line");

const Browser = await chromium.launch({ executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium", args: ["--no-sandbox", "--disable-gpu"] });
const Page = await Browser.newPage({ viewport: { width: 1365, height: 900 } });
Page.setDefaultTimeout(12000);
const Address = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
Page.on("pageerror", (Error) => Errors.push(Error.stack));
Page.on("requestfailed", (Request) => {
  if (Request.url().includes("fontshare.com")) FontFailures.push(Request.url());
});
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll > [data-panel]").waitFor();
  await Page.waitForTimeout(250);
};
const Saved = () => Page.evaluate(() => JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")));
const Stored = async (Id) => ((await Saved())?.Values?.[Id] || {})["Render Settings"] || {};
const Cards = () => Page.locator(".rc-cards [data-card]").evaluateAll((Each) => Each.map((Card) => Card.dataset.card));
const InUse = () => Page.locator(".rc-use").getAttribute("data-in-use");
const Tab = async (Id) => {
  await Page.locator(`.rc-tabs [data-tab="${Id}"]`).click();
  await Page.locator(`.rc-cards[data-render-tab="${Id}"]`).waitFor();
};

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());

  // Every camera lists one render component with its baked icon; the four retired render-path icons no longer ship.
  await Open("camera");
  const Rows = (await Saved()).Rows;
  for (const Owner of ["camera", "cine"])
    assert.deepEqual(Rows.filter((Row) => Row.Parent === Owner && Row.Panel === "object-component").map((Row) => Row.Name), ["Render Engine"], Owner);
  assert.deepEqual(await Page.locator(".pi-components-card .oc-link b").allInnerTexts(), ["Render Engine"]);
  const Image = Page.locator('.pi-components-card img[data-native-icon="render-settings"]');
  assert.equal(await Image.count(), 1);
  assert((await Image.evaluate((Each) => Each.naturalWidth)) > 0);
  assert.deepEqual(await Page.evaluate(() => ["render-matcap", "render-shadow-maps", "render-sdf", "render-ray-tracing"].filter((Name) => window.NativeAssets?.Icons?.[Name])), []);
  Checks.push("Editor and Cine cameras each carry one Render Engine component with its baked icon; the retired icons are gone");

  // Engine tab: renderer pick, summary lines and the Quality and Output cards; the dot follows the renderer.
  await Open("camera-render-engine");
  assert.deepEqual(await Page.locator(".rc-tabs button").allInnerTexts(), ["Engine", "PBR", "SDF", "Ray traced", "MatCap"]);
  assert.deepEqual(await Cards(), ["Engine", "Quality", "Output"]);
  assert.equal(await Page.locator('.rc-tabs [data-tab="sdf"] i.on').count(), 1);
  await Page.getByRole("radiogroup", { name: "Renderer" }).getByRole("radio", { name: "PBR", exact: true }).click();
  let Settings = await Stored("camera");
  assert.equal(Settings.RenderEngine, "openpbr_direct");
  assert.equal(await Page.locator('.rc-tabs [data-tab="pbr"] i.on').count(), 1);
  assert.match(await Page.locator(".rc-lines").innerText(), /Contact PCSS · 2048²/);
  await Page.locator(".rc-cards").screenshot({ path: path.join(Proof, "Engine.png") });
  Checks.push("Engine tab: picking PBR sets OpenPBR direct, moves the rendering dot and summarises its shadows in one line");

  // Each renderer keeps its own shadow algorithm: PBR PCF at Low, SDF Sobol cap; neither touches the other.
  await Tab("pbr");
  assert.equal(await InUse(), "true");
  assert.deepEqual(await Cards(), ["Shadow algorithm", "Penumbra", "Direct lighting", "Ambient occlusion"]);
  assert.match(await Page.locator('[data-card="Ambient occlusion"]').innerText(), /XeGTAO/);
  await Page.getByRole("switch", { name: "XeGTAO", exact: true }).click();
  assert.equal((await Stored("camera")).XeGtao, false);
  await Page.getByRole("switch", { name: "XeGTAO", exact: true }).click();
  await Page.getByLabel("Shadow quality", { exact: true }).selectOption({ label: "Low" });
  await Page.getByRole("radio", { name: /PCF Bilinear/ }).click();
  Settings = await Stored("camera");
  assert.deepEqual([Settings.PbrShadow, Settings.PbrMapRes, Settings.PbrPcssTaps], ["pcf", 1024, 8]);
  assert.match(await Page.locator('[data-study="kernel"]').textContent(), /1024² map · 9 taps/);
  await Page.locator(".rc-cards").screenshot({ path: path.join(Proof, "Pbr.png") });
  await Tab("sdf");
  assert.equal(await InUse(), "false");
  assert.deepEqual(await Cards(), ["Distance-field shadows", "Cone GI", "SDF ambient occlusion"]);
  await Page.getByRole("radiogroup", { name: "SDF AO method" }).getByRole("radio", { name: /Multi-tap SDF AO/ }).click();
  await Page.locator('[data-card="SDF ambient occlusion"] input[type=range][aria-label="Taps"]').fill("8");
  assert.equal(await Page.locator('[data-card="SDF ambient occlusion"] [data-ao="sdf_multitap"]').count(), 1);
  await Page.locator('[data-card="SDF ambient occlusion"]').screenshot({ path: path.join(Proof, "SdfAmbientOcclusion.png") });
  await Page.locator(".rc-rail button", { hasText: "Hybrid Sobol Cap" }).click();
  Settings = await Stored("camera");
  assert.deepEqual([Settings.SdfShadow, Settings.PbrShadow, Settings.RenderEngine], ["hybrid_sobol_cap", "pcf", "openpbr_direct"]);
  await Page.locator(".rc-cards").screenshot({ path: path.join(Proof, "Sdf.png") });
  assert.deepEqual([Settings.SdfAoMethod, Settings.SdfAoTaps, Settings.XeGtao], ["sdf_multitap", 8, true]);
  Checks.push("PBR stores PCF at Low (1024², 8 taps) and SDF stores the Sobol cap separately; editing SDF leaves PBR rendering");

  // Global tier: Custom after a renderer tier, then Ultra fans out to every renderer.
  await Tab("engine");
  assert.equal(await Page.locator('.rc-segments[aria-label="Global quality tier"] [aria-checked="true"]').count(), 0);
  assert.match(await Page.locator('[data-card="Quality"]').innerText(), /Custom/);
  await Page.getByRole("radio", { name: "Ultra", exact: true }).click();
  Settings = await Stored("camera");
  assert.deepEqual([Settings.PbrContactRays, Settings.SdfCones, Settings.RayGiSamples, Settings.RayReflectionBounces], [8, 10, 14, 3]);
  Checks.push("A renderer's own tier makes the global tier Custom; Ultra then fans out to PBR, SDF and ray tracing");

  // Ray traced: Use, tracer choice remembered across renderer switches, area-light shadows, reflections off.
  await Tab("ray");
  await Page.locator(".rc-use button").click();
  assert.equal((await Stored("camera")).RenderEngine, "highend_bvh4_gi");
  await Page.getByRole("radiogroup", { name: "Tracer" }).getByRole("radio", { name: "PT final", exact: true }).click();
  await Page.getByRole("radio", { name: /WideTree RayQuery · area/ }).click();
  await Page.getByRole("switch", { name: "Trace reflections", exact: true }).click();
  await Page.getByRole("radiogroup", { name: "Ray-traced AO method" }).getByRole("radio", { name: /RTAO \+ bent normal/ }).click();
  await Page.locator('[data-card="Traced ambient occlusion"] input[type=range][aria-label="AO rays"]').fill("6");
  await Page.locator('[data-card="Traced ambient occlusion"]').screenshot({ path: path.join(Proof, "RayAmbientOcclusion.png") });
  assert.deepEqual([(await Stored("camera")).RayAoMethod, (await Stored("camera")).RayAoRays, (await Stored("camera")).SdfAoMethod], ["rtao_bent", 6, "sdf_multitap"]);
  Settings = await Stored("camera");
  assert.deepEqual([Settings.RenderEngine, Settings.RayTracer, Settings.RayShadow, Settings.RayReflections], ["pt_final", "pt_final", "bvh_area", false]);
  await Tab("engine");
  const Renderer = Page.getByRole("radiogroup", { name: "Renderer" });
  await Renderer.getByRole("radio", { name: "SDF", exact: true }).click();
  await Renderer.getByRole("radio", { name: "Ray traced", exact: true }).click();
  assert.equal((await Stored("camera")).RenderEngine, "pt_final");
  Checks.push("Ray traced: PT final, rectangular-area shadows and reflections off are stored; the tracer is remembered across renderers");

  // MatCap on the Cine Camera: its own studio shadow; channel and light edits stay on the Cine Camera.
  await Open("cine-render-engine");
  await Tab("matcap");
  assert.equal(await InUse(), "false");
  await Page.locator(".rc-use button").click();
  assert.equal(await InUse(), "true");
  assert.deepEqual(await Cards(), ["Studio sphere", "Studio shadow", "Studio light"]);
  await Page.getByRole("radio", { name: "Normals", exact: true }).click();
  await Page.getByRole("radio", { name: /Screen contact/ }).click();
  await Page.locator(".rc-matcap").scrollIntoViewIfNeeded();
  const Ball = await Page.locator(".rc-matcap").boundingBox();
  await Page.mouse.click(Ball.x + 4, Ball.y + Ball.height / 2);
  const Cine = await Stored("cine");
  assert.deepEqual([Cine.RenderEngine, Cine.MatcapChannel, Cine.MatcapShadow], ["matcap", "normal", "contact"]);
  assert(Math.abs(Cine.MatcapLight - 180) <= 3, String(Cine.MatcapLight));
  assert.equal((await Stored("camera")).RenderEngine, "pt_final");
  assert.equal(await Page.locator(".outliner-row", { hasText: "MatCap · Ultra · 100 %" }).count(), 0);
  assert.equal(await Page.locator(".outliner-row", { hasText: "MatCap · Medium · 100 %" }).count(), 1);
  await Page.locator(".rc-cards").screenshot({ path: path.join(Proof, "MatCap.png") });
  Checks.push(`MatCap: Use MatCap switches only the Cine Camera; Normals and screen-contact shadow stored, light ${Cine.MatcapLight}°`);

  // A camera placed from Construct gets its own single component; Reset restores the defaults.
  await Page.getByRole("button", { name: "Construct", exact: true }).click();
  await Page.getByRole("dialog", { name: "Construct", exact: true }).waitFor();
  await Page.getByRole("textbox", { name: "Search construct", exact: true }).fill("Main Camera");
  await Page.locator(".construct-matrix").getByRole("button", { name: "Main Camera", exact: true }).click();
  await Page.getByRole("button", { name: "Add to scene", exact: true }).click();
  await Page.waitForTimeout(400);
  const Placed = (await Saved()).Rows.find((Row) => /^camera-/.test(Row.Id) && Row.Panel === "camera");
  assert(Placed, "placed camera");
  assert.deepEqual((await Saved()).Rows.filter((Row) => Row.Parent === Placed.Id).map((Row) => Row.Name), ["Render Engine"]);
  await Open(Placed.Id + "-render-engine");
  await Page.getByRole("radiogroup", { name: "Renderer" }).getByRole("radio", { name: "MatCap", exact: true }).click();
  assert.equal((await Stored(Placed.Id)).RenderEngine, "matcap");
  await Page.locator(".rc-reset button").click();
  assert.equal((await Stored(Placed.Id)).RenderEngine, "gtx_sdf_gi");
  Checks.push("A Construct-placed camera gets its own Render Engine component; Reset restores the source defaults");

  // Scenes saved with the Follow-up 41 rows (ComponentRevision 2) lose the four render-path rows once; old shared keys still read.
  await Page.evaluate(() => {
    const Scene = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1"));
    const Old = [["MatCap", "matcap"], ["Shadow Maps", "shadow-maps"], ["SDF Tracing", "sdf-tracing"], ["Ray Tracing", "ray-tracing"]];
    const At = Scene.Rows.findIndex((Row) => Row.Id === "cine-render-engine");
    Scene.Rows.splice(At + 1, 0, ...Old.map(([Name, Slug]) => ({ Id: "cine-" + Slug, Name, Panel: "object-component", Icon: "render-" + Slug, Parent: "cine", Component: Name })));
    Scene.Values.cine = { ...(Scene.Values.cine || {}), "Render Settings": { RenderEngine: "openpbr_direct", ShadowAlgorithm: "pcf", ShadowMapRes: 1024, SdfPenumbraMode: "analytic_cone" } };
    Scene.ComponentRevision = 2;
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Scene));
  });
  await Open("cine-render-engine");
  const Restored = (await Saved()).Rows;
  assert.deepEqual(Restored.filter((Row) => Row.Parent === "cine" && Row.Panel === "object-component").map((Row) => Row.Name), ["Render Engine"]);
  // Follow-up 46: object components are no longer outliner rows (they live in each object's component stack).
  assert.equal(Restored.filter((Row) => Row.Parent === "cube" && Row.Panel === "object-component").length, 0);
  await Tab("pbr");
  assert.equal(await InUse(), "true");
  assert.equal(await Page.getByRole("radio", { name: /PCF Bilinear/ }).getAttribute("aria-checked"), "true");
  Checks.push("Scenes saved at ComponentRevision 2 drop MatCap, Shadow Maps, SDF Tracing and Ray Tracing rows; their PCF setup reads in the PBR tab");
} catch (Error) {
  Errors.push(Error.stack);
} finally {
  fs.writeFileSync(path.join(Proof, "Checks.json"), JSON.stringify({ Checks, Errors, FontFailures }, null, 2));
  await Browser.close();
}
assert.deepEqual(Errors, []);
console.log("Render settings checks passed:", Checks.length);
