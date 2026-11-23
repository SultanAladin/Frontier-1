import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { CategoryPill, OpenOrbit } from "./TabReveal.mjs";

// Follow-up 40 / 46: object components (Mass, Rigid Body, Collision) in each showcase object's component stack, the per-workspace
// carousel pages and the editable Vehicle Movement charts.
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/ObjectComponents");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];
const Browser = await chromium.launch({ executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium", args: ["--no-sandbox", "--disable-gpu"] });
const Page = await Browser.newPage({ viewport: { width: 1440, height: 1000 } });
Page.setDefaultTimeout(15000);
const Address = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Key = "Frontier.ProjectZeroHtml.v1";
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll > [data-panel]").first().waitFor();
  await Page.waitForTimeout(300);
};
const Saved = () => Page.evaluate((Name) => JSON.parse(localStorage.getItem(Name)), Key);
// Showcase categories are picked from the Orbit menu (Follow-up 60): Pill(Name).click() opens its ring and picks; Shown() names the open one.
const Pill = (Name) => ({ click: async () => (await CategoryPill(Page, Name)).click() });
const Shown = () => Page.locator(".pi-page").getAttribute("aria-label");
const Metrics = () => Page.locator(".vc-card .metric").allInnerTexts();
async function DragHandle(Plot, Index, Dx, Dy) {
  const Handle = Page.locator(`[data-plot="${Plot}"] .vc-handle`).nth(Index);
  await Handle.scrollIntoViewIfNeeded();
  const Box = await Handle.boundingBox();
  const X = Box.x + Box.width / 2, Y = Box.y + Box.height / 2;
  await Page.mouse.move(X, Y);
  await Page.mouse.down();
  await Page.mouse.move(X + Dx / 2, Y + Dy / 2);
  await Page.mouse.move(X + Dx, Y + Dy);
  await Page.mouse.up();
  await Page.waitForTimeout(250);
}

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());

  // Follow-up 46: object components live in each object's component stack (Components page), not in the outliner.
  await Open("cube");
  const Objects = ["cube", "sphere", "cylinder", "torus", "cone"];
  let Rows = (await Saved())?.Rows || [];
  assert.equal(Rows.filter((Row) => Row.Panel === "object-component" && Objects.includes(Row.Parent)).length, 0);
  assert.equal(Rows.filter((Row) => Row.Panel === "object-component" && ["camera", "cine"].includes(Row.Parent)).length, 2);
  assert.equal(await Page.locator(".outliner-row", { hasText: /^\s*Mass/ }).count(), 0);
  assert.deepEqual(await Page.locator(".pi-components-card .oc-link b").allInnerTexts(), ["Mass", "Rigid Body", "Collision"]);
  await OpenOrbit(Page, "Category");
  assert.deepEqual(await Page.locator(".pi-pill").evaluateAll((Pills) => Pills.map((Each) => Each.getAttribute("aria-label"))), ["Overview", "Components", "Geometry", "Material", "UV", "Fracture", "SDF", "LOD"]);
  assert.equal(await Page.locator('.pi-pill[aria-label="Physics"]').count(), 0);
  await Page.locator('[data-orbit="Category"] .orbit-button').click();
  assert.equal(await Page.locator('[data-orbit="Category"] .orbit-ring').count(), 0);
  // Follow-up 60: every showcase object picks its category from the Orbit menu, which follows the cards, never above them.
  for (const Id of ["cube", "sphere", "cylinder", "torus", "cone"]) {
    await Open(Id);
    assert.equal(await Page.locator(".pi-carousel").getAttribute("data-navigation"), "orbit", Id + " navigation");
    assert.equal(await Page.locator('[data-orbit="Category"]').getAttribute("data-orbit-look"), "categories");
    const Cards = await Page.locator(".pi-page [data-card]").first().boundingBox();
    const Top = await Page.locator(".inspector-scroll > [data-panel] header").first().boundingBox();
    assert.ok(Cards.y < Top.y + Top.height + 140, Id + ": the first card sits right under the header " + JSON.stringify({ Cards, Top }));
  }
  await Open("cube");
  Checks.push("Category navigation is the Orbit menu on every showcase object, after the cards; its ring opens on press and closes again");
  Checks.push("No object component rows in the outliner (cameras keep their Render Engine row); the carousel is Overview, Components, Geometry, Material, UV, Fracture, SDF, LOD");

  // Opening Mass from the Overview turns to the Components page with the Mass entry open; edits are stored on the owning object.
  await Page.locator('.oc-link[data-component="Mass"]').click();
  await Page.locator('.oc-entry-page[data-entry="Mass"]').waitFor();
  assert.equal(await Shown(), "Components");
  assert.match(await Page.locator(".pi-component .metric").first().innerText(), /950\s*kg/);
  await Page.locator(".pi-component select").first().selectOption({ label: "Aluminium · 2700 kg/m³" });
  await Page.waitForTimeout(250);
  assert.match(await Page.locator(".pi-component .metric").first().innerText(), /2,?700\s*kg/);
  let Values = (await Saved()).Values;
  assert.equal(Values.cube["Physics Density"], "Aluminium");
  assert.equal(Values["cube-mass"]?.["Physics Density"], undefined);
  await Page.locator(".inspector-scroll").screenshot({ path: path.join(Proof, "MassComponent.png") });
  await Page.locator(".oc-back").click();
  await Page.locator('.oc-stack-card').waitFor();
  assert.match(await Page.locator('.oc-entry[data-component="Mass"]').innerText(), /2700 kg · Aluminium/);
  Checks.push("Overview → Mass opens the Components page at Mass; Aluminium writes Values.cube, reads 2,700 kg, and the stack line follows");

  // An older address (`torus-collision`) still opens the right entry; a box proxy is chosen by hand.
  await Open("torus-collision");
  await Page.locator('.oc-entry-page[data-entry="Collision"]').waitFor();
  assert.match(await Page.locator(".inspector-scroll h1").first().innerText(), /Torus/i);
  assert.match(await Page.locator('[data-card="Collision shape"]').innerText(), /Needs an SDF bake/);
  await Page.locator('[data-card="Collision shape"] select').selectOption({ label: "Box" });
  await Page.waitForTimeout(200);
  assert.equal((await Saved()).Values.torus["Collision Shape"], "box");
  Checks.push("?inspect=torus-collision selects the torus and opens Collision (starts on SDF, asks for a bake); Box is stored on the torus");

  // Selecting another object and coming back shows the Overview again.
  await Page.locator(".outliner-row", { hasText: "Sphere" }).first().click();
  await Page.waitForTimeout(250);
  await Page.locator(".outliner-row", { hasText: "Torus" }).first().click();
  await Page.waitForTimeout(250);
  // Follow-up 59: the torus uses the Orbit navigation, whose category buttons appear only with the ring open; the open page tells it.
  assert.equal(await Page.locator(".pi-page").getAttribute("data-page"), "overview");
  Checks.push("A component focus belongs to its object: reselecting the torus opens on Overview");

  // Stack editing on the sphere: reorder, switch off, remove, reload, add back.
  await Open("sphere");
  await Pill("Components").click();
  await Page.locator(".oc-stack-card").waitFor();
  const Order = () => Page.locator(".oc-entry").evaluateAll((Entries) => Entries.map((Each) => Each.dataset.component));
  const StackStored = async () => ((await Saved()).Rows.find((Row) => Row.Id === "sphere") || {}).Components;
  assert.deepEqual(await Order(), ["Mass", "Rigid Body", "Collision"]);
  await Page.getByRole("button", { name: "Move Collision up" }).click();
  await Page.waitForTimeout(200);
  assert.deepEqual(await Order(), ["Mass", "Collision", "Rigid Body"]);
  await Page.locator('.oc-entry[data-component="Mass"] .oc-grip').dragTo(Page.locator('.oc-entry[data-component="Rigid Body"]'));
  await Page.waitForTimeout(250);
  const Dragged = await Order();
  assert.deepEqual(Dragged, ["Collision", "Rigid Body", "Mass"]);
  await Page.getByRole("switch", { name: "Switch off Rigid Body" }).click();
  await Page.waitForTimeout(200);
  assert.match(await Page.locator('.oc-entry[data-component="Rigid Body"]').innerText(), /Switched off/);
  await Page.waitForFunction((Name) => JSON.parse(localStorage.getItem(Name)).Rows.find((Row) => Row.Id === "sphere").Components?.[1]?.Enabled === false, Key, { timeout: 5000 }).catch(() => {});
  assert.deepEqual(await StackStored(), [{ Component: "Collision", Enabled: true }, { Component: "Rigid Body", Enabled: false }, { Component: "Mass", Enabled: true }]);
  await Page.locator(".inspector-scroll").screenshot({ path: path.join(Proof, "ComponentStack.png") });
  await Page.getByRole("button", { name: "Remove Rigid Body" }).click();
  await Page.waitForTimeout(250);
  assert.deepEqual(await Order(), ["Collision", "Mass"]);
  await Page.reload();
  await Page.locator(".pi-components-card").waitFor();
  assert.deepEqual(await Page.locator(".pi-components-card .oc-link b").allInnerTexts(), ["Collision", "Mass"]);
  await Pill("Components").click();
  await Page.locator(".oc-stack-card .oc-add button", { hasText: "Rigid Body" }).click();
  await Page.waitForTimeout(250);
  assert.deepEqual(await Order(), ["Collision", "Mass", "Rigid Body"]);
  Checks.push(`Sphere stack: Move up → Mass, Collision, Rigid Body; drag Mass onto Rigid Body → ${Dragged.join(", ")}; Rigid Body switched off is stored as Enabled false; removed it stays removed across reload and is added back at the bottom`);

  // A scene saved at revision 3 (components as outliner rows, Rigid Body deleted, Collision moved first) keeps that stack.
  await Page.evaluate((Name) => {
    const Scene = JSON.parse(localStorage.getItem(Name));
    Scene.Rows = Scene.Rows.map(({ Components, ...Row }) => Row);
    const At = Scene.Rows.findIndex((Row) => Row.Id === "cube");
    const Legacy = (Component, Icon) => ({ Id: "cube-" + Component.toLowerCase(), Name: Component, Panel: "object-component", Icon, Parent: "cube", Description: "", Component });
    Scene.Rows.splice(At + 1, 0, Legacy("Collision", "editor-box-collider"), Legacy("Mass", "editor-mass"));
    Scene.ComponentRevision = 3;
    localStorage.setItem(Name, JSON.stringify(Scene));
  }, Key);
  await Open("cube");
  Rows = (await Saved()).Rows;
  assert.equal((await Saved()).ComponentRevision, 4);
  assert.deepEqual(Rows.find((Row) => Row.Id === "cube").Components, [{ Component: "Collision", Enabled: true }, { Component: "Mass", Enabled: true }]);
  assert.deepEqual(Rows.find((Row) => Row.Id === "sphere").Components, [], "sphere had no rows at revision 3");
  assert.equal(Rows.filter((Row) => Row.Panel === "object-component" && Objects.includes(Row.Parent)).length, 0);
  assert.deepEqual(await Page.locator(".pi-components-card .oc-link b").allInnerTexts(), ["Collision", "Mass"]);
  Checks.push("Revision 3 scene: cube's rows (Collision, Mass; Rigid Body deleted) fold into its stack in that order and leave the outliner");

  // A scene saved before components gets the full stack and the camera rows once.
  await Page.evaluate((Name) => {
    const Scene = JSON.parse(localStorage.getItem(Name));
    Scene.Rows = Scene.Rows.filter((Row) => Row.Panel !== "object-component").map(({ Components, ...Row }) => Row);
    delete Scene.ComponentRevision;
    localStorage.setItem(Name, JSON.stringify(Scene));
  }, Key);
  await Open("cone");
  Rows = (await Saved()).Rows || [];
  assert.equal(Rows.filter((Row) => Row.Panel === "object-component" && ["camera", "cine"].includes(Row.Parent)).length, 2);
  assert.equal(Rows.filter((Row) => Row.Panel === "object-component").length, 2);
  assert.deepEqual(await Page.locator(".pi-components-card .oc-link b").allInnerTexts(), ["Mass", "Rigid Body", "Collision"]);
  Checks.push("Older saved scenes: every object has Mass, Rigid Body, Collision in its stack and the 2 camera rows are added");

  // Follow-up 46: God Rays is the Sun's component (outliner child with its own icon); older scenes move it under the Sun.
  const GodRaysPlace = (List) => {
    const Index = List.findIndex((Row) => Row.Id === "god-rays");
    return { Parent: List[Index]?.Parent, Icon: List[Index]?.Icon, After: List[Index - 1]?.Id };
  };
  assert.deepEqual(GodRaysPlace((await Saved()).Rows), { Parent: "sun", Icon: "god-rays", After: "sun" });
  await Page.evaluate((Name) => {
    const Scene = JSON.parse(localStorage.getItem(Name));
    const Row = Scene.Rows.find((Each) => Each.Id === "god-rays");
    Scene.Rows = Scene.Rows.filter((Each) => Each.Id !== "god-rays");
    Scene.Rows.splice(Scene.Rows.findIndex((Each) => Each.Id === "local-fog") + 1, 0, { ...Row, Parent: "world", Icon: "sun" });
    Scene.Values["god-rays"] = { ...(Scene.Values["god-rays"] || {}), Intensity: 2.5 };
    localStorage.setItem(Name, JSON.stringify(Scene));
  }, Key);
  await Open("god-rays");
  const Relocated = await Saved();
  assert.deepEqual(GodRaysPlace(Relocated.Rows), { Parent: "sun", Icon: "god-rays", After: "sun" });
  assert.equal(Relocated.Values["god-rays"].Intensity, 2.5);
  assert.equal(await Page.locator('.outliner-row.selected img, .outliner-row.selected svg').count() > 0, true);
  await Page.locator(".outliner-rows").first().screenshot({ path: path.join(Proof, "GodRaysUnderSun.png") });
  await Page.screenshot({ path: path.join(Proof, "GodRaysInspector.png") });
  Checks.push("God Rays sits under the Sun with the god-rays icon; a scene with it after Local Fog moves it under the Sun and keeps Intensity 2.5");

  // Each workspace preview has its own page.
  await Open("cube");
  for (const [Page_, Title] of [["UV", "UV preview"], ["Fracture", "Fracture preview"], ["SDF", "SDF preview"], ["LOD", "LOD preview"]]) {
    await Pill(Page_).click();
    await Page.locator(`[data-card="${Title}"]`).waitFor();
  }
  // Follow-up 46: each preview card's slider moves only the card picture; nothing is stored and the viewport is unchanged.
  const Picture = (Selector) => Page.locator(Selector).evaluate((Canvas) => Canvas.toDataURL());
  const Viewport = () => Page.locator(".viewport canvas, canvas.viewport-canvas").first().evaluate((Canvas) => Canvas.width + "x" + Canvas.height).catch(() => "none");
  await Pill("Fracture").click();
  await Page.getByRole("switch", { name: "Enable fracture" }).first().click();
  await Page.waitForTimeout(400);
  assert.equal((await Saved()).Values.cube.Fracture?.Enabled, true);
  const ValuesBefore = JSON.stringify((await Saved())?.Values?.cube || {});
  const ViewportBefore = await Viewport();
  const Moved = [];
  for (const [Page_, Title, Control, Canvas, Target] of [
    ["Fracture", "Fracture preview", "explode", ".ws-preview-fracture canvas", "0.9"],
    ["SDF", "SDF preview", "slice", ".ws-preview-sdf canvas", "0.2"],
    ["LOD", "LOD preview", "level", ".ws-preview-lod-cut canvas", "2"],
  ]) {
    await Pill(Page_).click();
    const Card = Page.locator(`[data-card="${Title}"]`);
    await Card.waitFor();
    const Slider = Card.locator(`[data-preview-control="${Control}"] input[type=range]`);
    await Page.waitForFunction((Selector) => !document.querySelector(Selector)?.disabled, `[data-card="${Title}"] [data-preview-control="${Control}"] input[type=range]`, { timeout: 30000 }).catch(() => {});
    assert.equal(await Slider.isDisabled(), false, `${Page_} slider stays disabled`);
    await Page.waitForTimeout(400);
    const Before = await Picture(Canvas);
    await Slider.fill(Target);
    await Page.waitForTimeout(400);
    const After = await Picture(Canvas);
    assert.notEqual(After, Before, `${Page_} preview did not change`);
    Moved.push(`${Page_}: ${Control} → ${Target} redraws the card`);
    await Card.screenshot({ path: path.join(Proof, `${Page_}PreviewSlider.png`) });
  }
  assert.equal(await Page.locator('.ws-preview-lod-cut').getAttribute("data-level"), "2");
  assert.equal(JSON.stringify((await Saved())?.Values?.cube || {}), ValuesBefore);
  assert.equal(await Viewport(), ViewportBefore);
  Checks.push("Preview sliders (card only, nothing stored): " + Moved.join("; "));
  await Pill("Geometry").click();
  assert.equal(await Page.locator('[data-card="UV preview"]').count(), 0);
  Checks.push("UV, Fracture, SDF and LOD previews each sit on their own carousel page; Geometry no longer holds the UV card");

  // Vehicle Movement charts are editable.
  await Open("vehicle-movement");
  const Before = await Metrics();
  await DragHandle("suspension", 0, 0, -30);
  let Stored = (await Saved()).Values["vehicle-movement"];
  assert(Stored.dampingRatio < 0.6, String(Stored.dampingRatio));
  await DragHandle("torque", 8, 0, -40);
  Stored = (await Saved()).Values["vehicle-movement"];
  assert.equal(Stored.torqueCurve.length, 18);
  assert(Stored.torqueCurve[6][1] > 486, JSON.stringify(Stored.torqueCurve[6]));
  await DragHandle("torque", 0, -30, 0);
  assert((await Saved()).Values["vehicle-movement"].redlineRpm < 7200);
  await DragHandle("aero", 0, 0, -30);
  await DragHandle("aero", 1, 0, 30);
  Stored = (await Saved()).Values["vehicle-movement"];
  assert(Stored.dragArea > 0.74 && Stored.dragArea <= 1.2, String(Stored.dragArea));
  assert(Stored.liftArea < 1.1 && Stored.liftArea >= 0, String(Stored.liftArea));
  const After = await Metrics();
  assert.notEqual(After[2], Before[2]);
  assert.notEqual(After[4], Before[4]);
  await Page.locator('[data-plot="torque"]').screenshot({ path: path.join(Proof, "TorqueCurve.png") });
  Checks.push(`Vehicle charts: peak drag sets ζ ${Stored.dampingRatio}, torque point 7 → ${Stored.torqueCurve[6][1]} N·m, redline ${Stored.redlineRpm}, CdA ${Stored.dragArea}, ClA ${Stored.liftArea}`);
} catch (Error) {
  Errors.push(Error.stack);
} finally {
  fs.writeFileSync(path.join(Proof, "Checks.json"), JSON.stringify({ Checks, Errors }, null, 2));
  await Browser.close();
}
assert.deepEqual(Errors, []);
console.log("Object component checks passed:", Checks.length);
