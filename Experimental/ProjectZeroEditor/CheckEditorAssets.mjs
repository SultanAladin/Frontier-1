import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { OpenOrbit, PickTab } from "./TabReveal.mjs";

// Follow-up 48: editor assets (EditorAssets.js) — scene cameras stand in the 3D viewport as camera models on tripods with their frustum,
// the outliner shows each camera's model as its icon, and the camera inspector picks the body (A · Film, B · Digital, C · Mirrorless).
// And the lettered inspector tabs (InspectorTabs.jsx): every tabbed inspector has its own look, one page open at a time, ← → move,
// and the open tab is remembered while the editor is open. Follow-up 60: the pages are picked from an Orbit menu, and three vehicles
// (the Controlled Vehicle, an Off-road Vehicle and a Pickup Truck) park in the level, sit in a Vehicles folder and have an inspector.
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/EditorAssets");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];
const Browser = await chromium.launch({ executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium", args: ["--no-sandbox", "--disable-gpu", "--enable-unsafe-swiftshader"] });
const Page = await Browser.newPage({ viewport: { width: 1440, height: 900 } });
Page.setDefaultTimeout(30000);
const Address = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.waitForSelector(".viewport-3d-canvas[data-frames]");
  await Page.waitForTimeout(600);
};
const Canvas = () => Page.locator(".viewport-3d-canvas");
const RowIcon = (Id) => Page.locator(`[data-outliner-row="${Id}"] .native-icon`).first();
// Opaque pixels of an icon image.
const Opaque = (Id) =>
  RowIcon(Id).evaluate(async (Picture) => {
    await Picture.decode();
    const Board = document.createElement("canvas");
    Board.width = Picture.naturalWidth;
    Board.height = Picture.naturalHeight;
    const Brush = Board.getContext("2d");
    Brush.drawImage(Picture, 0, 0);
    const Bytes = Brush.getImageData(0, 0, Board.width, Board.height).data;
    let Count = 0;
    for (let At = 3; At < Bytes.length; At += 4) if (Bytes[At] > 128) Count++;
    return [Count, Board.width * Board.height];
  });

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());

  // Outliner icons: each camera row shows its body's model; the icons are pictures of the model, not blank squares.
  await Open("cine");
  assert.equal(await RowIcon("camera").getAttribute("data-native-icon"), "asset-camera-mirrorless");
  assert.equal(await RowIcon("cine").getAttribute("data-native-icon"), "asset-camera-film");
  const [Covered, Total] = await Opaque("cine");
  assert(Covered > Total * 0.2 && Covered < Total * 0.9, `the film camera icon is a picture: ${Covered} of ${Total} pixels`);
  await Page.locator('[data-outliner-row="camera"]').screenshot({ path: path.join(Proof, "Outliner-EditorCamera.png") });
  await Page.locator('[data-outliner-row="cine"]').screenshot({ path: path.join(Proof, "Outliner-CineCamera.png") });
  Checks.push(`Outliner: Editor Camera shows the mirrorless model, Cine Camera the film camera (${Covered} of ${Total} icon pixels drawn)`);

  // Viewport: the scene camera stands as its model (the editor camera is the view itself and is not drawn).
  assert.equal(await Canvas().getAttribute("data-cameras"), "cine:film");
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "Viewport3D-CineCamera.png") });

  // Camera inspector, Body page: picking B · Digital changes the viewport model and the outliner icon.
  await PickTab(Page, "camera:body");
  assert.equal(await Page.locator('[data-camera-body-shown]').getAttribute("data-camera-body-shown"), "film");
  await Page.locator('[data-camera-body="digital"]').click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.cameras === "cine:digital");
  assert.equal(await RowIcon("cine").getAttribute("data-native-icon"), "asset-camera-digital");
  assert.equal(await Page.locator('[data-camera-body="digital"]').getAttribute("aria-checked"), "true");
  await Page.locator('[data-card="Camera body"]').screenshot({ path: path.join(Proof, "CameraBody.png") });
  await Page.waitForTimeout(400);
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "Viewport3D-DigitalCamera.png") });
  await Page.locator('[data-camera-body="film"]').click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.cameras === "cine:film");
  Checks.push("Camera body: B · Digital swaps the viewport model (cine:digital) and the outliner icon (asset-camera-digital); A · Film swaps back");

  // Follow-up 59: Tripod off draws the body alone at head height (a camera that moves); on brings the tripod back. Stored per camera.
  const Tripod = Page.getByRole("switch", { name: "Tripod" });
  assert.equal(await Tripod.getAttribute("aria-checked"), "true");
  await Tripod.click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.cameras === "cine:film:free");
  assert.equal(await Tripod.getAttribute("aria-checked"), "false");
  assert.equal(JSON.parse(await Page.evaluate(() => localStorage.getItem("Frontier.ProjectZeroHtml.v1"))).Values.cine.Tripod, false);
  await Page.waitForTimeout(400);
  await Page.locator(".scene-image").screenshot({ path: path.join(Proof, "Viewport3D-CameraNoTripod.png") });
  await Page.locator('[data-card="Camera body"]').screenshot({ path: path.join(Proof, "CameraBodyNoTripod.png") });
  await Tripod.click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.cameras === "cine:film");
  Checks.push("Tripod switch: off draws the film camera alone at head height (cine:film:free, Tripod false stored); on brings the tripod back");

  // Hiding the camera takes it out of the viewport; showing it brings it back.
  await Page.getByRole("button", { name: "Toggle Cine Camera visibility" }).click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.cameras === "");
  await Page.getByRole("button", { name: "Toggle Cine Camera visibility" }).click();
  await Page.waitForFunction(() => document.querySelector(".viewport-3d-canvas").dataset.cameras === "cine:film");
  Checks.push("Hiding Cine Camera takes its model out of the viewport; showing it brings it back");

  // Lettered pages (Follow-up 60): picked from the Orbit menu, pinned after the cards, never above them; each inspector's ring has its
  // own look; one page at a time; ← → on the button; remembered while the editor stays open.
  const Looks = {};
  for (const Id of ["sun", "atmosphere", "stars", "moon", "flare", "cine"]) {
    await Open(Id);
    const Tabs = Page.locator("[data-tabs]").first();
    Looks[Id] = await Tabs.getAttribute("data-tabs-look");
    const Owner = await Tabs.getAttribute("data-tabs");
    assert.equal(await Page.locator("[data-tab-for]").count(), 0, Id + ": the ring starts shut");
    const Header = await Page.locator(".inspector-scroll > [data-panel] > header").boundingBox();
    const First = await Tabs.locator(".it-page:not([hidden]) [data-card]").first().boundingBox();
    assert(First.y < Header.y + Header.height + 60, Id + ": the first card sits right under the header");
    await OpenOrbit(Page, Owner);
    assert.equal(await Page.locator(`[data-orbit="${Owner}"]`).getAttribute("data-orbit-look"), Looks[Id]);
    const Letters = await Tabs.locator("[data-tab-for]").count();
    assert(Letters >= 3, Id + " has lettered pages");
    assert.equal(await Tabs.locator(".it-page:not([hidden])").count(), 1, Id + " shows one page");
    await Page.locator(`[data-orbit="${Owner}"] .orbit-ring`).screenshot({ path: path.join(Proof, `Orbit-${Owner}.png`) });
    const Second = await Tabs.locator("[data-tab-for]").nth(1).getAttribute("data-tab-for");
    await Tabs.locator("[data-tab-for]").nth(1).click();
    assert(await Page.locator(`[data-tab-page="${Second}"]`).isVisible());
    assert.equal(await Page.locator(`[data-orbit="${Owner}"] .orbit-ring`).count(), 0, Id + ": picking a page closes the ring");
    assert.equal(await Tabs.locator(".it-page:not([hidden])").count(), 1);
    await Page.locator(`[data-orbit="${Owner}"] .orbit-button`).focus();
    await Page.keyboard.press("ArrowRight");
    const Third = await Tabs.locator(".it-page").nth(2).getAttribute("data-tab-page");
    assert(await Page.locator(`[data-tab-page="${Third}"]`).isVisible(), Id + " → opens the next page");
  }
  assert.equal(new Set(Object.values(Looks)).size, 6, "every tabbed inspector looks different: " + JSON.stringify(Looks));
  // Remembered: open Stars' third page, select another row and come back.
  await Open("stars");
  await PickTab(Page, "stars:renderer");
  await Page.locator('[data-outliner-row="sun"]').click();
  await Page.locator('[data-tabs="sun"]').waitFor();
  await Page.locator('[data-outliner-row="stars"]').click();
  await Page.locator('[data-tabs="stars"]').waitFor();
  assert(await Page.locator('[data-tab-page="stars:renderer"]').isVisible(), "Stars reopens on Renderer");
  Checks.push("Orbit pages: " + Object.entries(Looks).map(([Id, Look]) => `${Id} ${Look}`).join(", ") + " — cards first, the ring starts shut, picking closes it, one page open, ← → move, the open page is remembered");

  // Vehicles (Follow-up 60): the Controlled Vehicle, the Off-road Vehicle and the Pickup Truck park in the level, sit in a Vehicles folder
  // in the outliner with their models as icons, and open the vehicle inspector (Orbit pages Model · Size · Drive).
  await Open("vehicle");
  assert.equal(await Canvas().getAttribute("data-vehicles"), "vehicle:controlled offroad-vehicle:offroad pickup-truck:pickup");
  const Order = await Page.locator("[data-outliner-row]").evaluateAll((Rows) => Rows.map((Row) => Row.dataset.outlinerRow));
  const Folder = Order.indexOf("vehicles");
  assert(Folder >= 0, "the Vehicles folder is in the outliner");
  assert.deepEqual(
    ["vehicle", "offroad-vehicle", "pickup-truck"].map((Id) => Order.indexOf(Id) > Folder),
    [true, true, true],
    "the three vehicles follow their folder",
  );
  const Depth = (Id) => Page.locator(`[data-outliner-row="${Id}"]`).evaluate((Row) => Number(getComputedStyle(Row).getPropertyValue("--depth")));
  assert.equal(await Depth("vehicle"), (await Depth("vehicles")) + 1, "the vehicles nest in the folder");
  assert.equal(await Depth("vehicle-movement"), (await Depth("vehicle")) + 1, "the Controlled Vehicle's components nest under it");
  for (const [Id, Model] of [["vehicle", "controlled"], ["offroad-vehicle", "offroad"], ["pickup-truck", "pickup"]]) {
    assert.equal(await RowIcon(Id).getAttribute("data-native-icon"), "asset-vehicle-" + Model);
    const [Drawn, Area] = await Opaque(Id);
    assert(Drawn > Area * 0.15 && Drawn < Area * 0.9, `${Id}'s icon is a picture: ${Drawn} of ${Area} pixels`);
  }
  await Page.locator('[data-outliner-row="vehicles"]').scrollIntoViewIfNeeded();
  await Page.locator(".outliner-rows").screenshot({ path: path.join(Proof, "Outliner-Vehicles.png") });
  Checks.push("Vehicles: three parked in the viewport (data-vehicles), a Vehicles folder holding them in the outliner with asset-vehicle-* model icons, the Controlled Vehicle's components under it");

  // The inspector: the model picture with paints, then Size and Drive from the garage Orbit.
  assert.equal(await Page.locator('[data-orbit="vehicle"]').getAttribute("data-orbit-look"), "garage");
  assert.equal(await Page.locator("[data-vehicle-shown]").getAttribute("data-vehicle-shown"), "controlled");
  await Page.locator('[data-vehicle-paint="#b8262e"]').click();
  await Page.waitForFunction(() => document.querySelector("[data-vehicle-shown]")?.dataset.vehiclePaintShown === "#b8262e");
  assert.match(await Page.locator('[data-outliner-row="vehicle"]').innerText(), /Rally red/);
  await Page.locator(".inspector-scroll").screenshot({ path: path.join(Proof, "Vehicle-Model.png") });
  await PickTab(Page, "vehicle:size");
  await Page.locator(".inspector-scroll").screenshot({ path: path.join(Proof, "Vehicle-Size.png") });
  await PickTab(Page, "vehicle:drive");
  assert((await Page.locator("[data-vehicle-part]").count()) >= 6, "Drive lists the Controlled Vehicle's components");
  // Follow-up 62: a Drive link opens the Component Viewer panel on that component; the editor stays on the vehicle.
  await Page.locator('[data-vehicle-part="vehicle-movement"]').click();
  await Page.locator('.vcp[data-vcp-selected="vehicle-movement"]').waitFor();
  await Page.locator('[data-outliner-row="vehicle"][aria-selected="true"]').waitFor();
  await Page.keyboard.press("Escape");
  await Page.locator(".vcp").waitFor({ state: "detached" });
  for (const Id of ["offroad-vehicle", "pickup-truck"]) {
    await Open(Id);
    assert.equal(await Page.locator("[data-vehicle-shown]").getAttribute("data-vehicle-shown"), Id === "pickup-truck" ? "pickup" : "offroad");
    assert.equal(await Page.locator("[data-vehicle-part]").count(), 0);
    await Page.locator(".inspector-scroll").screenshot({ path: path.join(Proof, `Vehicle-${Id}.png`) });
  }
  await Canvas().screenshot({ path: path.join(Proof, "Viewport-Vehicles.png") });
  Checks.push("Vehicle inspector: garage Orbit; a paint repaints the picture and the outliner summary (Rally red); Drive lists and selects the components");

  // Scenes saved before Follow-up 60 (a Vehicle group, no folder, no parked vehicles) gain the folder and the two parked vehicles.
  await Page.evaluate(() => {
    const Saved = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1"));
    Saved.Rows = Saved.Rows.filter((Row) => !["vehicles", "offroad-vehicle", "pickup-truck"].includes(Row.Id)).map((Row) =>
      Row.Id === "vehicle" ? { Id: "vehicle", Name: "Vehicle", Panel: "group", Parent: null } : Row,
    );
    Saved.VehicleRevision = 1;
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Open("vehicle");
  assert.equal(await Canvas().getAttribute("data-vehicles"), "vehicle:controlled offroad-vehicle:offroad pickup-truck:pickup");
  assert.match(await Page.locator('[data-outliner-row="vehicle"]').innerText(), /Controlled Vehicle/);
  assert.equal(await Page.locator('[data-outliner-row="vehicles"]').count(), 1);
  Checks.push("Saved scenes from before: the Vehicle group becomes the Controlled Vehicle inside a new Vehicles folder, joined by the parked vehicles");

  assert.deepEqual(Errors, []);
  console.log("Editor asset checks passed:", Checks.length);
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
