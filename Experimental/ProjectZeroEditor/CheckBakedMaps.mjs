import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { PickTab } from "./TabReveal.mjs";

// Follow-up 48: the BAKING sections show the maps the 3D viewport uses (BakedMaps.js): the Atmosphere's transmittance LUT and sky-view
// map, the Sun's lighting LUT and disc map, the Stars' star map and the Lens Flare's atlas; and the viewport's lens flare (LensFlare.js)
// shows where the sun is on screen, dims behind a shape, and goes with Enabled and with the sun below the horizon.
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/BakedMaps");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];
const Browser = await chromium.launch({ executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium", args: ["--no-sandbox", "--disable-gpu", "--enable-unsafe-swiftshader"] });
const Page = await Browser.newPage({ viewport: { width: 1440, height: 900 } });
Page.setDefaultTimeout(30000);
const Address = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const Store = (Values) => Page.evaluate((Values) => localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify({ Values })), Values);
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.waitForSelector(".viewport-3d-canvas[data-frames]");
  await Page.waitForTimeout(600);
};
const Map = (Name) => Page.locator(`[data-baked-map="${Name}"]`);
// Opens the inspector tab (InspectorTabs.jsx) holding a selector.
const Reveal = async (Selector) => {
  const Tab = await Page.locator(Selector).first().evaluate((Each) => Each.closest("[data-tab-page]")?.dataset.tabPage || null);
  if (Tab) await PickTab(Page, Tab);
};
// A map nobody baked yet shows the bake tab with its planned resolution; Bake bakes it. Returns whether it had to be baked here.
const Baked = async (Name, Size) => {
  await Page.waitForSelector(`[data-baked-map="${Name}"]`, { state: "attached" });
  await Reveal(`[data-baked-map="${Name}"]`);
  await Page.waitForTimeout(200);
  const Unbaked = (await Map(Name).getAttribute("data-baked-state")) === "unbaked";
  if (Unbaked) {
    assert.match(await Map(Name).innerText(), new RegExp("Not baked[\\s\\S]*" + Size.replace("x", " × ")));
    await Map(Name).getByRole("button", { name: "Bake " + Name }).click();
  }
  await Page.waitForSelector(`[data-baked-map="${Name}"][data-baked-size="${Size}"]`);
  await Page.waitForTimeout(150);
  return Unbaked;
};
// Pixels of a map's canvas (x, y in its own pixels).
const Pixels = (Name, Points) => Map(Name).locator("canvas").evaluate((Canvas, Points) => Points.map(([X, Y]) => [...Canvas.getContext("2d").getImageData(X, Y, 1, 1).data].slice(0, 3)), Points);
// How many pixels have a channel above a level.
const BrightCount = (Name, Level) =>
  Map(Name).locator("canvas").evaluate((Canvas, Level) => {
    const Bytes = Canvas.getContext("2d").getImageData(0, 0, Canvas.width, Canvas.height).data;
    let Count = 0;
    for (let At = 0; At < Bytes.length; At += 4) if (Math.max(Bytes[At], Bytes[At + 1], Bytes[At + 2]) > Level) Count++;
    return Count;
  }, Level);
const Shot = async (Card, Name) => {
  await Reveal(`[data-card="${Card}"]`);
  const Section = Page.locator(`[data-card="${Card}"]`);
  await Section.scrollIntoViewIfNeeded();
  await Page.waitForTimeout(300);
  await Section.screenshot({ path: path.join(Proof, Name) });
};
const Dataset = () => Page.locator(".viewport-3d-canvas").evaluate((Each) => ({ ...Each.dataset }));
const Luma = ([R, G, B]) => 0.2126 * R + 0.7152 * G + 0.0722 * B;

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());
  await Store({ sun: { "Local Hours": 15.5 } });

  // Atmosphere: transmittance LUT (clearer up high and looking up) and the sky-view map (blue sky by day).
  await Open("atmosphere");
  await Reveal('[data-baked-map="Transmittance LUT"]');
  const UnbakedShot = path.join(Proof, "BakedMaps-Unbaked.png");
  await Page.locator('[data-card="Atmosphere bake"]').screenshot({ path: UnbakedShot });
  assert(await Baked("Transmittance LUT", "256x64"), "the transmittance LUT waits for Bake");
  // The viewport bakes its sky-view map, so its card is baked already.
  assert(!(await Baked("Sky-view map", "1024x512")), "the sky-view map comes baked from the viewport");
  const Facts = await Map("Transmittance LUT").locator(".baked-map-facts").innerText();
  assert.match(Facts, /Resolution\s+256 × 64/i);
  assert.match(Facts, /Format\s+RGBA8 sRGB/i);
  assert.match(Facts, /Baked\s+.*\d+ ms/i);
  assert.match(Facts, /View elevation -12° → 90°/i);
  assert.match(Facts, /Altitude 60 km → 0 km/i);
  assert.deepEqual(await Map("Sky-view map").locator(".baked-map-x span").allInnerTexts(), ["0°", "90°", "180°", "270°", "360°"]);
  await Map("Transmittance LUT").locator(".baked-map-image").scrollIntoViewIfNeeded();
  const Plot = await Map("Transmittance LUT").locator(".baked-map-image").boundingBox();
  await Page.mouse.move(Plot.x + Plot.width * 0.5, Plot.y + Plot.height * 0.5);
  await Page.mouse.move(Plot.x + Plot.width * 0.96, Plot.y + 5, { steps: 4 });
  await Page.waitForTimeout(150);
  assert.match(await Map("Transmittance LUT").locator("[data-baked-cursor]").innerText(), /^8\d\.\d°, [45]\d\.\d km\s*rgb\(\d+, \d+, \d+\)$/);
  Checks.push("Unbaked maps show the bake tab (Not baked · planned 256 × 64) until Bake; baked maps list resolution, format, bake time and axes (−12° → 90°, 60 km → 0 km) and read the cursor (8x°, 4x–5x km near the top right)");
  const [LowFlat, HighUp, BelowEdge] = await Pixels("Transmittance LUT", [[92, 62], [250, 1], [0, 62]]);
  assert(Luma(HighUp) > Luma(LowFlat) + 40, `clearer up high: ${HighUp} vs ${LowFlat}`);
  assert(LowFlat[0] > LowFlat[2], "low, near the horizon, sunlight is reddened: " + LowFlat);
  assert(Luma(BelowEdge) < 8, "below the planet's edge it is black: " + BelowEdge);
  const [Zenith] = await Pixels("Sky-view map", [[512, 60]]);
  assert(Zenith[2] > Zenith[0], "the sky-view map is blue by day: " + Zenith);
  const Count = +(await Map("Transmittance LUT").getAttribute("data-baked-count"));
  await Map("Transmittance LUT").getByRole("button", { name: "Bake again" }).click();
  await Page.waitForFunction((Count) => +document.querySelector('[data-baked-map="Transmittance LUT"]').dataset.bakedCount > Count, Count);
  await Shot("Atmosphere bake", "BakedMaps-Atmosphere.png");
  Checks.push(`Atmosphere bake: transmittance LUT 256 × 64 (low near the horizon rgb(${LowFlat.join(", ")}), high looking up rgb(${HighUp.join(", ")})) and the viewport's 1024 × 512 sky-view map (zenith rgb(${Zenith.join(", ")})); Bake again bakes again`);

  // Sun: lighting LUT (no beam below the horizon, warm-white beam high up, moonlit fill at night) and the disc map.
  await Open("sun");
  await Baked("Sun lighting LUT", "256x64");
  await Baked("Sun disc map", "192x192");
  const [NightBeam, HighBeam, NightFill, DayFill] = await Pixels("Sun lighting LUT", [[2, 8], [250, 8], [2, 56], [200, 56]]);
  assert(Luma(NightBeam) < 8 && Luma(HighBeam) > 200, `beam: none at −12° ${NightBeam}, bright at 90° ${HighBeam}`);
  assert(NightFill[2] > NightFill[0] && DayFill[2] > DayFill[0], `fill: moonlit ${NightFill}, sky ${DayFill}`);
  assert.match(await Map("Sun lighting LUT").locator(".baked-map-marker").innerText(), /^Now -?\d+\.\d°$/);
  const [Disc] = await Pixels("Sun disc map", [[96, 96]]);
  assert(Luma(Disc) > 230, "the disc is bright at the centre: " + Disc);
  await Shot("Sun lighting bake", "BakedMaps-SunLighting.png");
  await Shot("Sun disk bake", "BakedMaps-SunDisc.png");
  Checks.push(`Sun bakes: lighting LUT (beam rgb(${HighBeam.join(", ")}) high, none below the horizon; fill moonlit rgb(${NightFill.join(", ")}) at night) and the 192 × 192 disc map (centre rgb(${Disc.join(", ")}))`);

  // Stars: the star map follows Limiting magnitude.
  await Open("stars");
  await Baked("Star map", "1024x512");
  const Rich = await BrightCount("Star map", 60);
  await Store({ sun: { "Local Hours": 15.5 }, stars: { "Limiting magnitude": 3 } });
  await Open("stars");
  await Baked("Star map", "1024x512");
  const Sparse = await BrightCount("Star map", 60);
  assert(Rich > 1500 && Sparse < Rich / 2, `stars: ${Rich} lit pixels at 5.5 mag, ${Sparse} at 3 mag`);
  await Store({ sun: { "Local Hours": 15.5 } });
  await Open("stars");
  await Baked("Star map", "1024x512");
  await Shot("Star field bake", "BakedMaps-Stars.png");
  Checks.push(`Star map 1024 × 512: ${Rich} lit pixels at limiting magnitude 5.5, ${Sparse} at 3`);

  // Lens Flare: the atlas has all eight sprites; the composite card draws with it.
  await Open("flare");
  await Baked("Flare atlas", "1024x512");
  const Cells = await Map("Flare atlas").locator("canvas").evaluate((Canvas) =>
    Array.from({ length: 8 }, (_, Cell) => {
      const Bytes = Canvas.getContext("2d").getImageData((Cell % 4) * 256, Math.floor(Cell / 4) * 256, 256, 256).data;
      let Lit = 0;
      for (let At = 0; At < Bytes.length; At += 4) if (Bytes[At] + Bytes[At + 1] + Bytes[At + 2] > 60) Lit++;
      return Lit;
    }),
  );
  assert(Cells.every((Lit) => Lit > 200), "every sprite is drawn: " + Cells);
  assert(+(await Page.locator("[data-flare-preview]").getAttribute("data-sprites")) > 8);
  await Shot("Lens flare image", "BakedMaps-LensFlare.png");
  await Shot("Flare composite", "FlareComposite.png");
  Checks.push(`Flare atlas 1024 × 512: eight sprites lit (${Cells.join(", ")} pixels); the composite card draws from it`);

  // Viewport flare: on with the sun in view, dimmer behind a tall shape, off when disabled or below the horizon.
  const Sun = (Elevation) => ({ "Manual direction": true, "Manual azimuth": 204, "Manual elevation": Elevation });
  await Store({ sun: Sun(2) });
  await Open("cube");
  const Open1 = await Dataset();
  assert(Open1.sunAt !== "off" && +Open1.flare > 0.3 && +Open1.flareSprites > 8, `flare with the sun in view: ${JSON.stringify([Open1.sunAt, Open1.flare])}`);
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "Viewport3D-LensFlare.png") });
  await Store({ sun: Sun(2), cylinder: { Scale: [1, 14, 1] } });
  await Open("cube");
  const Blocked = await Dataset();
  assert(+Blocked.flare < +Open1.flare * 0.6, `a tall cylinder in front of the sun dims it: ${Open1.flare} → ${Blocked.flare}`);
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "Viewport3D-LensFlareBlocked.png") });
  await Store({ sun: Sun(2), flare: { Enabled: false } });
  await Open("cube");
  const Off = await Dataset();
  assert.equal(Off.flare, "0.00");
  assert.equal(Off.flareSprites, "0");
  await Store({ sun: Sun(-5) });
  await Open("cube");
  assert.equal((await Dataset()).flare, "0.00");
  Checks.push(`Viewport lens flare: ${Open1.flare} with the sun at ${Open1.sunAt}, ${Blocked.flare} behind a tall cylinder, 0 when disabled and with the sun below the horizon`);

  assert.deepEqual(Errors, []);
  console.log("Baked map checks passed:", Checks.length);
  for (const Each of Checks) console.log("🟢", Each);
} catch (Error) {
  Errors.push(Error.stack);
  console.error(Error);
  process.exitCode = 1;
  await Page.screenshot({ path: path.join(Proof, "Failure.png") }).catch(() => {});
} finally {
  fs.writeFileSync(path.join(Proof, "Checks.json"), JSON.stringify({ Checks, Errors }, null, 2) + "\n");
  await Browser.close();
}
