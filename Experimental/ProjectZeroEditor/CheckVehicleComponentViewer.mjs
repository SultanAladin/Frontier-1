// Follow-up 62: the Controlled Vehicle's Component Viewer panel (VehicleComponentViewer.jsx). Its inspector opens the panel, which has its
// own Components outliner, the vehicle view and its own Details; a click on a component (view or outliner) opens that component's native
// inspector in the panel while the editor stays on the vehicle; edits change the view; Add / Delete; Simulate; Esc closes; choosing a
// component elsewhere opens the panel on it.
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/VehicleComponentViewer");
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  args: ["--no-sandbox", "--disable-gpu", "--enable-unsafe-swiftshader"],
});
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
Page.setDefaultTimeout(20000);
const Address = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Checks = [],
  Errors = [],
  FontFailures = [];
Page.on("pageerror", (Error) => Errors.push(Error.stack));
Page.on("requestfailed", (Request) => {
  if (Request.url().includes("fontshare.com")) FontFailures.push(Request.url());
});
const Canvas = () => Page.locator(".vcv-canvas");
const Readings = () => Page.evaluate(() => ({ ...document.querySelector(".vcv-canvas")?.dataset }));
const Details = () => Page.locator(".vcp-details .inspector-heading h1").innerText();
const MainHeading = () => Page.locator(".inspector-scroll:not(.vcp-details) .inspector-heading h1").innerText();
const Saved = async (Id) => ((await Page.evaluate(() => JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1"))))?.Values || {})[Id] || {};
const Tag = () => Page.locator(".vcv-tag span").innerText();
const PanelOn = (Id) => Page.waitForFunction((Id) => document.querySelector(".vcp")?.dataset.vcpSelected === Id && document.querySelector(".vcv-canvas")?.dataset.selectedComponent === Id, Id);
// Clicks where a component's pick target lands on screen.
const ClickComponent = async (Id) => {
  const Point = await Page.evaluate((Id) => {
    const Surface = document.querySelector(".vcv-canvas"), Rect = Surface.getBoundingClientRect(), At = JSON.parse(Surface.dataset.picks)[Id];
    return At && [Rect.left + At[0] * Rect.width, Rect.top + At[1] * Rect.height];
  }, Id);
  assert(Point, "no pick target for " + Id);
  await Page.mouse.click(Point[0], Point[1]);
  await PanelOn(Id);
};

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());
  await Page.goto(Address + "?inspect=vehicle");
  await Page.locator(".inspector-scroll > [data-panel]").waitFor();

  // The vehicle's inspector opens the panel: outliner, view and Details; the ControlVehicle with its 8 native components drawn.
  assert.equal(await Page.locator(".vcp").count(), 0);
  await Page.locator("[data-open-component-viewer]").click();
  await Page.waitForFunction(() => document.querySelector(".vcv-canvas")?.dataset.picks);
  await Page.waitForTimeout(1200);
  assert.deepEqual((await Readings()).components.split(" ").sort(), ["vehicle-body", "vehicle-brake", "vehicle-collision", "vehicle-headlights", "vehicle-movement", "vehicle-steering", "vehicle-suspension", "vehicle-tow"]);
  assert.equal(await Page.locator(".vcp-row").count(), 9);
  assert.equal(await Page.locator(".vcp-outliner-heading h1").innerText(), "Components");
  assert.equal(await Page.locator("[data-vcp-count]").innerText(), "9");
  assert.match(await Page.locator(".vcp-row.root").innerText(), /Origin\s*ROOT/);
  assert.match(await Page.locator(".vcp-divider").first().innerText(), /ACTOR COMPONENTS\s*1/);
  assert.equal(await Details(), "Controlled Vehicle");
  assert.equal(await MainHeading(), "Controlled Vehicle");
  // Each component shows its own icon (sockets by type).
  const Icons = await Page.evaluate(() => Object.fromEntries([...document.querySelectorAll(".vcp-row")].map((Row) => [Row.dataset.vcvRow, Object.entries(window.NativeAssets.Icons).find(([, Source]) => Source === Row.querySelector("img")?.getAttribute("src"))?.[0]])));
  assert.deepEqual(Icons, {
    vehicle: "vehicle-component-origin",
    "vehicle-body": "vehicle-component-body",
    "vehicle-suspension": "vehicle-component-spring",
    "vehicle-steering": "vehicle-component-hinge",
    "vehicle-tow": "vehicle-component-dock",
    "vehicle-collision": "vehicle-component-box-collision",
    "vehicle-headlights": "vehicle-component-spot-light",
    "vehicle-brake": "vehicle-component-point-light",
    "vehicle-movement": "vehicle-component-movement",
  });
  await Page.screenshot({ path: path.join(Proof, "Panel.png") });
  Checks.push("The vehicle inspector opens the Component Viewer panel: Components outliner (Origin root, actor divider, 9 icons), view, Details");

  // A click on each component in the view opens its native inspector in the panel's Details; the editor stays on the vehicle.
  for (const [Id, Name, Class] of [
    ["vehicle-headlights", "Headlights", "SpotLight"],
    ["vehicle-steering", "Steering", "Socket"],
    ["vehicle-suspension", "Front Suspension", "Socket"],
    ["vehicle-tow", "Tow Hitch", "Socket"],
    ["vehicle-brake", "Brake Lights", "PointLight"],
    ["vehicle-movement", "Vehicle Movement", "VehicleMovement"],
  ]) {
    await ClickComponent(Id);
    await Page.locator(`.vcp-details .vehicle-inspector[data-component='${Class}']`).waitFor();
    assert.equal(await Details(), Name);
    assert.equal(await Page.locator(`.vcp-row[data-vcv-row='${Id}']`).getAttribute("aria-selected"), "true");
    assert.equal(await MainHeading(), "Controlled Vehicle");
  }
  await Page.screenshot({ path: path.join(Proof, "Movement.png") });
  Checks.push("Clicking each component in the view opens its inspector in the panel's Details; the editor's inspector stays on the vehicle");

  // The outliner selects too.
  await Page.locator(".vcp-row[data-vcv-row='vehicle-body']").click();
  await Page.locator(".vcp-details .vehicle-inspector[data-component='StaticMesh']").waitFor();
  await PanelOn("vehicle-body");
  Checks.push("The panel's outliner selects the Body and opens the Static Mesh inspector in Details");

  // Editing in Details changes the view: the tow hitch releases (dock), the collision turns overlap-only.
  await ClickComponent("vehicle-tow");
  assert.match(await Tag(), /connected/);
  await Page.locator(".vcp-details .vc-socket-scene").click();
  assert.equal((await Saved("vehicle-tow")).connected, false);
  await Page.waitForFunction(() => /released|seeking/i.test(document.querySelector(".vcv-tag span")?.textContent || ""));
  await Page.screenshot({ path: path.join(Proof, "TowReleased.png") });
  await Page.locator(".vcp-row[data-vcv-row='vehicle-collision']").click();
  await Page.locator(".vcp-details .vehicle-inspector[data-component='BoxCollision']").waitFor();
  assert.match(await Tag(), /blocks all/);
  await Page.locator(".vcp-details").getByRole("switch", { name: "Generate overlap events" }).click();
  await Page.waitForFunction(() => /overlap only/.test(document.querySelector(".vcv-tag span")?.textContent || ""));
  Checks.push("Edits in the panel's Details reach the view: releasing the tow dock, overlap-only collision");

  // The eye hides a component in the view.
  await Page.locator(".vcp-row[data-vcv-row='vehicle-headlights'] .vcp-eye").click();
  assert(!(await Readings()).components.includes("vehicle-headlights"));
  await Page.locator(".vcp-row[data-vcv-row='vehicle-headlights'] .vcp-eye").click();
  assert((await Readings()).components.includes("vehicle-headlights"));
  Checks.push("The eye hides and shows a component");

  // Add puts a native component on the vehicle (drawn, listed, opened); Delete removes it.
  await Page.locator(".vcp-add").click();
  await Page.locator(".vcp-menu-item", { hasText: "Point Light" }).click();
  await PanelOn("vehicle-point-light-1");
  assert.equal(await Details(), "Point Light 1");
  assert((await Readings()).components.includes("vehicle-point-light-1"));
  assert.equal(await Page.locator("[data-vcp-count]").innerText(), "10");
  await Page.screenshot({ path: path.join(Proof, "Added.png") });
  await Canvas().focus();
  await Page.keyboard.press("Delete");
  await PanelOn("vehicle");
  assert.equal(await Page.locator(".vcp-row[data-vcv-row='vehicle-point-light-1']").count(), 0);
  Checks.push("Add → Point Light adds a drawn, listed component and opens it; Delete removes it");

  // Simulate rolls the wheels and works the springs.
  const Still = await Readings();
  await Page.locator(".vcp-simulate").click();
  await Page.waitForTimeout(2500);
  const Moving = await Readings();
  assert.notEqual(Moving.spin, Still.spin);
  await Page.screenshot({ path: path.join(Proof, "Simulate.png") });
  await Page.locator(".vcp-simulate").click();
  Checks.push(`Simulate rolls the wheels (spin ${Still.spin} → ${Moving.spin}) and runs the sockets`);

  // Esc closes the panel.
  await Canvas().focus();
  await Page.keyboard.press("Escape");
  await Page.locator(".vcp").waitFor({ state: "detached" });
  Checks.push("Esc closes the panel");

  // Choosing a component elsewhere (here ?inspect=) selects the vehicle and opens the panel on that component.
  await Page.goto(Address + "?inspect=vehicle-steering");
  await Page.locator(".vcp").waitFor();
  await PanelOn("vehicle-steering");
  assert.equal(await Details(), "Steering");
  assert.equal(await MainHeading(), "Controlled Vehicle");
  Checks.push("Choosing a component outside the panel opens the panel on it; the editor selects the vehicle");
} catch (Error) {
  Errors.push(Error.stack);
} finally {
  fs.writeFileSync(path.join(Proof, "Checks.json"), JSON.stringify({ Checks, Errors, FontFailures }, null, 2));
  await Browser.close();
}
assert.deepEqual(Errors, []);
console.log("Vehicle component viewer checks passed:", Checks.length);
