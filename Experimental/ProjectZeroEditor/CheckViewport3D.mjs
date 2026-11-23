import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

// Follow-up 48: the 3D viewport (three.js shapes under the Sun and the Atmosphere's sky, click to select, the marker view behind a
// switch) and the outliner's multi-select with its row menu (Hide, Isolate, Group, Duplicate, Delete with one undo).
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/Viewport3D");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];
const Browser = await chromium.launch({ executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium", args: ["--no-sandbox", "--disable-gpu", "--enable-unsafe-swiftshader"] });
const Page = await Browser.newPage({ viewport: { width: 1440, height: 900 } });
Page.setDefaultTimeout(20000);
const Address = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const Patch = (Id, Values) =>
  Page.evaluate(
    ([Id, Values]) => {
      const Scene = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1") || "{}");
      Scene.Values ||= {};
      Scene.Values[Id] = Object.assign(Scene.Values[Id] || {}, Values);
      localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Scene));
    },
    [Id, Values],
  );
const Canvas = () => Page.locator(".viewport-3d-canvas");
const Ready = async () => {
  await Page.waitForSelector(".viewport-3d-canvas[data-centres]");
  await Page.waitForTimeout(500);
};
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Ready();
};
const Dataset = () => Canvas().evaluate((Each) => ({ ...Each.dataset, centres: JSON.parse(Each.dataset.centres || "{}") }));
// Clicks an object's centre in the viewport.
const ClickObject = async (Id, Modifiers = []) => {
  const { centres } = await Dataset();
  const Box = await Canvas().boundingBox();
  for (const Key of Modifiers) await Page.keyboard.down(Key);
  await Page.mouse.click(Box.x + centres[Id][0] * Box.width, Box.y + centres[Id][1] * Box.height);
  for (const Key of Modifiers) await Page.keyboard.up(Key);
  await Page.waitForTimeout(300);
};
const ChosenRows = () => Page.locator('.outliner-row[aria-selected="true"]').evaluateAll((Rows) => Rows.map((Row) => Row.innerText.split("\n")[0].trim()));
const Row = (Name) => Page.locator(".outliner-row", { hasText: new RegExp("^\\s*" + Name) }).first();
const MenuItems = () => Page.locator(".popup-menu label, .popup-menu > button").evaluateAll((Each) => Each.map((Entry) => Entry.innerText.replace(/\s+/g, " ").trim()));
// The viewport's top centre as the user sees it: a screenshot decoded back in the page.
const SkyColour = async () => {
  const Box = await Canvas().boundingBox();
  const Shot = await Page.screenshot({ clip: { x: Box.x + Box.width / 2 - 4, y: Box.y + 4, width: 8, height: 8 } });
  return Page.evaluate(async (Source) => {
    const Picture = new Image();
    Picture.src = "data:image/png;base64," + Source;
    await Picture.decode();
    const Copy = document.createElement("canvas");
    Copy.width = Picture.width;
    Copy.height = Picture.height;
    Copy.getContext("2d").drawImage(Picture, 0, 0);
    return [...Copy.getContext("2d").getImageData(Picture.width >> 1, Picture.height >> 1, 1, 1).data].slice(0, 3);
  }, Shot.toString("base64"));
};

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());
  await Open("cube");

  // The 3D view is the main view; the five Showcase shapes are out, in that order along the line.
  assert.equal(await Page.locator(".scene-image").getAttribute("data-viewport"), "3d");
  assert.equal(await Page.locator(".checker-pane").count(), 0);
  let Seen = await Dataset();
  assert.equal(Seen.objects, "5");
  assert.deepEqual(Object.keys(Seen.centres), ["cube", "sphere", "cylinder", "torus", "cone"]);
  const Xs = Object.values(Seen.centres).map(([X]) => X);
  assert(Xs.every((X, Index) => !Index || X > Xs[Index - 1]), "left to right: " + Xs);
  assert.equal(Seen.chosen, "cube");
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "Viewport3D-Afternoon.png") });
  Checks.push(`3D view replaces the checkerboard: 5 shapes left to right ${Xs.map((X) => X.toFixed(2)).join(" < ")}; the cube is outlined`);

  // Sky: blue by day, warm at sunset (the sky follows the Sun's time and the Atmosphere).
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas")?.dataset.sky);
  const Noon = await SkyColour();
  assert(Noon[2] > Noon[0], "daytime sky is blue: " + Noon);
  await Patch("sun", { "Local Hours": 17.8 });
  await Open("cube");
  const Dusk = await SkyColour();
  assert(Dusk[0] > Dusk[2], "sunset sky is warm: " + Dusk);
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "Viewport3D-Sunset.png") });
  // Night: the star field is out, over a dark blue (never black) sky; by day it is off.
  assert.equal((await Dataset()).stars, "0.00");
  await Patch("sun", { "Local Hours": 21 });
  await Open("cube");
  assert.equal((await Dataset()).stars, "1.00");
  const Night = await SkyColour();
  assert(Night[2] > Night[0] && Night[2] < 90 && Night[2] > 15, "night sky is dark blue: " + Night);
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "Viewport3D-Night.png") });
  Checks.push(`Star field: off by day, out at 21:00 over a dark blue sky rgb(${Night.join(", ")})`);
  await Patch("sun", { "Local Hours": 15.5 });
  await Open("cube");
  // The sky is the background at infinity: zooming all the way in and out keeps it (the camera rig moves its near and far planes).
  const Zoom = async (Ticks) => {
    const Box = await Canvas().boundingBox();
    await Page.mouse.move(Box.x + Box.width / 2, Box.y + Box.height * 0.08);
    for (let Tick = 0; Tick < Math.abs(Ticks); Tick++) {
      await Page.mouse.wheel(0, Math.sign(Ticks) * 120);
      await Page.waitForTimeout(25);
    }
    await Page.waitForTimeout(1500);
  };
  await Zoom(-24);
  const Near = await SkyColour();
  assert(Near[2] > Near[0] && Near[2] > 120, "sky kept zoomed in: " + Near);
  await Zoom(60);
  const Far = await SkyColour();
  assert(Far[2] > Far[0] && Far[2] > 120, "sky kept zoomed out: " + Far);
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "Viewport3D-ZoomedOut.png") });
  Checks.push(`Zoom: the sky stays at infinity, rgb(${Near.join(", ")}) zoomed in and rgb(${Far.join(", ")}) zoomed out to the 150 m limit`);
  Checks.push(`Sky from SkyShader.js: top of the view rgb(${Noon.join(", ")}) by day, rgb(${Dusk.join(", ")}) at 17:48`);

  // Moving and recolouring an object reaches the viewport.
  await Patch("sphere", { Position: [0, 1.5, 0] });
  await Open("cube");
  const Lifted = (await Dataset()).centres.sphere;
  assert(Lifted[1] < Seen.centres.sphere[1] - 0.04, `sphere rose ${Seen.centres.sphere[1]} → ${Lifted[1]}`);
  await Patch("sphere", { Position: [0, 0, 0] });
  await Open("cube");
  Checks.push(`Transform reaches the view: sphere Position y 1.5 lifts it on screen ${Seen.centres.sphere[1].toFixed(3)} → ${Lifted[1].toFixed(3)}`);

  // Click picks; Ctrl adds; the outliner follows.
  await ClickObject("sphere");
  assert.deepEqual(await ChosenRows(), ["Sphere"]);
  await ClickObject("torus", ["Control"]);
  assert.deepEqual((await ChosenRows()).sort(), ["Sphere", "Torus"]);
  assert.equal((await Dataset()).chosen.split(" ").sort().join(" "), "sphere torus");
  Checks.push("Clicking the sphere selects it in the outliner; Ctrl-clicking the torus adds it (both outlined)");

  // Outliner multi-select: Ctrl toggles, Shift takes a range.
  await Row("Cube").click();
  await Row("Cylinder").click({ modifiers: ["Shift"] });
  assert.deepEqual(await ChosenRows(), ["Cube", "Sphere", "Cylinder"]);
  await Row("Sphere").click({ modifiers: ["Control"] });
  assert.deepEqual(await ChosenRows(), ["Cube", "Cylinder"]);
  await Row("Cone").click({ modifiers: ["Control"] });
  assert.deepEqual(await ChosenRows(), ["Cube", "Cylinder", "Cone"]);
  await Row("Cone").click({ button: "right" });
  const Items = await MenuItems();
  assert.deepEqual(Items, ["3 selected", "Hide", "Isolate", "Focus in viewport", "Group into collection", "Duplicate 3", "Copy names", "Delete 3 Del"]);
  await Page.screenshot({ path: path.join(Proof, "RowMenu-Three.png"), clip: { x: 0, y: 0, width: 760, height: 640 } });
  Checks.push(`Shift-click Cube → Cylinder takes 3 rows; Ctrl-click drops Sphere and adds Cone; the row menu reads: ${Items.join(" · ")}`);

  // Hide all three, then Show them.
  await Page.locator(".popup-menu > button", { hasText: /^Hide$/ }).click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.objects === "2");
  await Row("Cone").click({ button: "right" });
  await Page.locator(".popup-menu > button", { hasText: /^Show$/ }).click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.objects === "5");
  // Isolate the sphere alone.
  await Row("Sphere").click();
  await Row("Sphere").click({ button: "right" });
  await Page.locator(".popup-menu > button", { hasText: "Isolate" }).click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.objects === "1");
  await Page.getByRole("button", { name: /Show all/ }).first().click().catch(() => Page.locator(".stats button").first().click());
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.objects === "5");
  Checks.push("Hide on 3 rows leaves 2 shapes, Show brings back 5; Isolate on the sphere leaves 1; Show all restores 5");

  // Delete three and undo; the Delete key works on the selection too.
  await Row("Cube").click();
  await Row("Cylinder").click({ modifiers: ["Shift"] });
  await Row("Cylinder").click({ button: "right" });
  await Page.locator("[data-row-delete]").click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.objects === "2");
  assert.equal(await Row("Sphere").count(), 0);
  await Page.keyboard.press("Control+z");
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.objects === "5");
  assert.equal(await Row("Sphere").count(), 1);
  await Row("Torus").click();
  await Row("Torus").press("Delete");
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.objects === "4");
  await Row("Cone").click({ button: "right" });
  assert((await MenuItems()).includes("Undo delete Ctrl+Z"));
  await Page.locator(".popup-menu > button", { hasText: "Undo delete" }).click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.objects === "5");
  // Built-in collections and the editor camera can't be deleted.
  await Row("Showcase").click({ button: "right" });
  assert.equal(await Page.locator("[data-row-delete]").isDisabled(), true);
  await Page.keyboard.press("Escape");
  await Page.locator(".menu-dismiss").click().catch(() => {});
  Checks.push("Delete on 3 rows removes them from the outliner and the view, Ctrl+Z restores them; the Delete key removes the torus and Undo delete in the menu restores it; the Showcase can't be deleted");

  // Group, duplicate.
  await Row("Sphere").click();
  await Row("Torus").click({ modifiers: ["Control"] });
  await Row("Torus").click({ button: "right" });
  await Page.locator(".popup-menu > button", { hasText: "Group into collection" }).click();
  await Page.waitForTimeout(300);
  const Grouped = await Page.evaluate(() => {
    const Rows = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Rows;
    const Folder = Rows.find((Row) => Row.Name === "Collection");
    return [Folder?.Parent, Rows.filter((Row) => Row.Parent === Folder?.Id).map((Row) => Row.Id).sort()];
  });
  assert.deepEqual(Grouped, ["showcase", ["sphere", "torus"]]);
  await Row("Cube").click();
  await Row("Cube").click({ button: "right" });
  await Page.locator(".popup-menu > button", { hasText: /^Duplicate/ }).click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.objects === "6");
  Checks.push("Group into collection puts the sphere and torus in a new Collection under the Showcase; Duplicate adds a sixth shape");

  // The marker view is one click away and comes back.
  await Page.locator("[data-viewport-switch]").click();
  await Page.waitForSelector(".checker-pane");
  assert.equal(await Page.locator(".viewport-3d").count(), 0);
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "MarkerView.png") });
  await Page.reload();
  await Page.waitForSelector(".checker-pane");
  await Page.locator("[data-viewport-switch]").click();
  await Ready();
  Checks.push("The 3D switch shows the checkerboard marker view (kept after reload) and switches back");

  assert.deepEqual(Errors, []);
  console.log(Checks.map((Check) => "🟢 " + Check).join("\n"));
  console.log(`${Checks.length} Viewport3D checks passed`);
} finally {
  await Browser.close();
}
