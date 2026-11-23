import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"),
);
const { chromium } = Require("playwright");
const Proof =
  process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/VehicleSockets");
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  args: ["--no-sandbox", "--disable-gpu"],
});
const Page = await Browser.newPage({ viewport: { width: 1365, height: 900 } });
Page.setDefaultTimeout(12000);
const Address =
  process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Checks = [],
  Errors = [],
  FontFailures = [];
Page.on("pageerror", (Error) => Errors.push(Error.stack));
Page.on("requestfailed", (Request) => {
  if (Request.url().includes("fontshare.com")) FontFailures.push(Request.url());
});
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".vcp-details > [data-panel]").waitFor();
  await Page.waitForTimeout(250);
};
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );

const Values = async (Id) => ((await Saved())?.Values || {})[Id] || {};
const Scene = () => Page.locator(".vcp-details .vc-socket-scene");
const Link = () => Page.locator('.vcp-details [data-card="Attached to"] .vc-asset-link');

try {
  // Each socket row: the joint at its pose, then type, range and the asset it is attached to.
  for (const [Id, Kind, Range] of [
    ["vehicle-suspension", "Spring", "Spring & damper"],
    ["vehicle-steering", "Hinge", "Range of motion"],
    ["vehicle-tow", "Dock", "Coupling"],
  ]) {
    await Open(Id);
    const Titles = await Page.locator(".vcp-details .vehicle-inspector [data-card]").evaluateAll((Cards) => Cards.map((Card) => Card.dataset.card));
    assert.deepEqual(Titles, [Kind + " socket", "Socket type", Range, "Attached to"]);
    assert.equal(await Page.locator('.vcp-details .vc-socket-kinds [role="radio"][aria-checked="true"]').innerText(), Kind);
    assert.equal(await Page.locator(".vcp-details .vc-socket-kinds svg").count(), 8);
    // No test drive, trace or vehicle drawing any more.
    assert.equal(await Page.getByRole("button", { name: /test drive|Restart/ }).count(), 0);
    assert.equal(await Page.locator(".vcp-details .vc-trace, .vcp-details [data-card='Attached to'] .side-view, .vcp-details [data-card='Attached to'] .vc-visual").count(), 0);
    assert.equal((await Link().innerText()).split("\n")[0].trim(), "Body");
    Checks.push(`${Id}: pose → Socket type → ${Range} → Attached to (Body link); no animation or vehicle drawing`);
  }

  // Static: the drawing does not change on its own.
  await Open("vehicle-suspension");
  const Before = await Scene().innerHTML();
  await Page.waitForTimeout(800);
  assert.equal(await Scene().innerHTML(), Before);
  await Page.screenshot({ path: path.join(Proof, "SpringPose.png") });
  Checks.push("Socket drawing is static");

  // Dragging the plate sets the preload (pose); the drawing follows.
  await Scene().scrollIntoViewIfNeeded();
  const Box = await Scene().boundingBox();
  await Page.mouse.move(Box.x + Box.width / 2, Box.y + Box.height * 0.42);
  await Page.mouse.down();
  await Page.mouse.move(Box.x + Box.width / 2, Box.y + Box.height * 0.18, { steps: 4 });
  await Page.mouse.up();
  await Page.waitForTimeout(300);
  assert((await Values("vehicle-suspension")).pose > 0.5);
  assert.notEqual(await Scene().innerHTML(), Before);
  assert(await Page.getByText("Spring socket · on Body").count());
  Checks.push("Dragging the spring plate writes the preload pose; outliner summary names the attached asset");

  // Attachment: changing the target relinks; the link selects the asset in the outliner.
  await Page.locator('.vcp-details [data-card="Attached to"] select').first().selectOption({ label: "Chassis Collision" });
  await Page.waitForTimeout(200);
  assert.equal((await Values("vehicle-suspension")).attach, "vehicle-collision");
  assert.equal((await Link().innerText()).split("\n")[0].trim(), "Chassis Collision");
  await Link().scrollIntoViewIfNeeded();
  await Page.screenshot({ path: path.join(Proof, "AttachedTo.png") });
  await Link().click();
  await Page.locator(".vcp-details .vehicle-inspector[data-component='BoxCollision']").waitFor();
  assert.equal(await Page.locator(".vcp-details .inspector-heading h1").innerText(), "Chassis Collision");
  Checks.push("Attach to relinks the socket; clicking the asset name selects that asset");

  // Switching type keeps the numbers and swaps the drawing, range card and figures.
  await Open("vehicle-suspension");
  await Page.locator(".vcp-details .vc-socket-kinds button", { hasText: "Tether" }).click();
  assert.equal((await Values("vehicle-suspension")).kind, "tether");
  await Page.locator('.vcp-details [data-card="Pendulum"]').waitFor();
  assert(await Page.getByText("Swing period").count());
  await Page.locator(".vcp-details .vc-socket-kinds button", { hasText: "Spring" }).click();
  assert((await Values("vehicle-suspension")).pose > 0.5);
  Checks.push("Switching to Tether shows the Pendulum card; switching back keeps the spring numbers");

  // Dock: a click on the drawing releases and reconnects the coupling.
  await Open("vehicle-tow");
  await Scene().click();
  assert.equal((await Values("vehicle-tow")).connected, false);
  assert.match(await Page.locator(".vcp-details .vc-socket-pose .metric").innerText(), /Released/);
  await Scene().click();
  await Page.waitForTimeout(200);
  assert.equal((await Values("vehicle-tow")).connected, true);
  await Page.screenshot({ path: path.join(Proof, "DockPose.png") });
  Checks.push("Dock toggles connected / released on click");
} catch (Error) {
  Errors.push(Error.stack);
} finally {
  fs.writeFileSync(path.join(Proof, "Checks.json"), JSON.stringify({ Checks, Errors, FontFailures }, null, 2));
  await Browser.close();
}
assert.deepEqual(Errors, []);
console.log("Vehicle socket checks passed:", Checks.length);
