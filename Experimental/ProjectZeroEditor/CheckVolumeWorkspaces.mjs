import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { CategoryPill } from "./TabReveal.mjs";

// Follow-up 37: UV and fracture preview cards in the primitive inspector, plus the SDF Bake and LOD Clusters workspaces.
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/VolumeWorkspaces");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [],
  FontFailures = [];
const Browser = await chromium.launch({ executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium", args: ["--no-sandbox", "--disable-gpu"] });
const Page = await Browser.newPage({ viewport: { width: 1440, height: 900 } });
Page.setDefaultTimeout(15000);
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
const Stored = async (Id, Key) => ((await Saved())?.Values?.[Id] || {})[Key];
const Category = async (Name) => (await CategoryPill(Page, Name)).click();
const Card = (Title) => Page.locator(`[data-card="${Title}"]`);
const Metric = (Title) => Card(Title).locator(".metric").first().innerText();
const Launch = (Tool) => Page.locator(`.ws-rail button[aria-label="${Tool}"]`).click();
const Banner = () => Page.locator(".ws-banner").innerText();
const Number_ = (Text) => +Text.replace(/[^\d.]/g, "");

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());

  // UV preview on its own UV page: tile 1001 holds the six cube islands; the button opens the UV workspace.
  await Open("cube");
  await Category("UV");
  await Card("UV preview").scrollIntoViewIfNeeded();
  assert.match(await Card("UV preview").innerText(), /64\.1\s*% of 1001 used/);
  assert.deepEqual(await Page.locator(".ws-udims span.on b").allInnerTexts(), ["1001"]);
  assert.match(await Page.locator(".ws-udims span.on small").innerText(), /6 islands/);
  await Card("UV preview").screenshot({ path: path.join(Proof, "UvPreviewCard.png") });
  await Page.getByRole("button", { name: "Edit in UV workspace", exact: true }).click();
  assert.match(await Banner(), /UV workspace/);
  await Page.keyboard.press("Escape");
  Checks.push("UV preview: 64.1 % of 1001, six islands in UDIM 1001, button opens the UV workspace");

  // Fracture preview: off by default; after enabling in the workspace it reports pieces and the recipe.
  await Category("Fracture");
  assert.equal((await Metric("Fracture preview")).replace(/\s/g, ""), "Off");
  await Page.getByRole("button", { name: "Edit in fracture workspace", exact: true }).click();
  assert.match(await Banner(), /Fracture workspace/);
  await Page.locator('.ws-panel [role=switch][aria-label="Enable fracture"]').click();
  await Page.keyboard.press("Escape");
  await Category("Fracture");
  await Card("Fracture preview").scrollIntoViewIfNeeded();
  const Pieces = Number_(await Metric("Fracture preview"));
  assert(Pieces >= 2, String(Pieces));
  const Text = await Card("Fracture preview").innerText();
  assert.match(Text, /Concrete/);
  assert.match(Text, /2\.50 kJ/);
  assert.match(Text, /Dynamic/);
  await Card("Fracture preview").screenshot({ path: path.join(Proof, "FracturePreviewCard.png") });
  Checks.push(`Fracture preview: Off, then ${Pieces} Concrete pieces at 2.50 kJ after enabling in the workspace`);

  // SDF Bake: sparse volume, conservative Mip 1, probe, bake stamp and staleness.
  await Category("Overview");
  await Launch("SDF Bake");
  assert.match(await Banner(), /SDF Bake workspace/);
  await Page.waitForFunction(() => /of 512 bricks/.test(document.querySelector('[data-card="Distance field"] .metric')?.textContent || ""));
  assert.match(await Card("Distance field").innerText(), /verified/);
  const Slice = await Page.locator(".ws-slice").boundingBox();
  await Page.mouse.click(Slice.x + Slice.width * 0.5, Slice.y + Slice.height * 0.5);
  await Page.waitForTimeout(300);
  assert.match(await Card("Slice").innerText(), /inside the surface/);
  await Page.getByRole("button", { name: "Bake distance field", exact: true }).click();
  const Stamp = (await Stored("cube", "SDF Bake")).Baked;
  assert.equal(typeof Stamp.Signature, "string");
  assert.match(await Card("Bake").first().innerText(), /up to date/);
  await Page.screenshot({ path: path.join(Proof, "SdfBake.png") });
  await Card("Distance field").getByLabel("Resolution", { exact: true }).selectOption({ index: 0 });
  await Page.waitForFunction(() => /of 64 bricks/.test(document.querySelector('[data-card="Distance field"] .metric')?.textContent || ""));
  assert.match(await Page.locator(".ws-bake-status").innerText(), /changed since the last bake/);
  // The SDF page preview reads the same volume and bake stamp.
  await Page.keyboard.press("Escape");
  await Category("SDF");
  await Page.waitForFunction(() => /of 64 bricks/.test(document.querySelector('[data-card="SDF preview"] .metric')?.textContent || ""));
  assert.match(await Card("SDF preview").innerText(), /32³/);
  assert.match(await Card("SDF preview").innerText(), /Changed since the last bake/);
  await Card("SDF preview").screenshot({ path: path.join(Proof, "SdfPreviewCard.png") });
  Checks.push("SDF Bake: 64³ sparse bricks with Mip 1 ≤ Mip 0 verified, centre probe inside, bake stamp, 32³ marks it stale");

  // LOD Clusters on the sphere: DAG levels, forced L0 draws every source triangle, distance coarsens the cut.
  await Page.keyboard.press("Escape");
  await Open("sphere");
  await Launch("LOD Clusters");
  assert.match(await Banner(), /LOD Clusters workspace/);
  await Card("Cluster DAG").waitFor();
  const Levels = Number_(await Metric("Cluster DAG"));
  assert(Levels >= 4, String(Levels));
  assert.equal(await Page.locator(".ws-level-table span").count(), Levels * 4);
  const Source = Number_(await Card("Cluster DAG").locator(".metric + p").innerText().then((Caption) => Caption.match(/([\d,]+) source triangles/)[1]));
  const Near = Number_(await Metric("Error cut"));
  await Card("Error cut").getByLabel("Level", { exact: true }).selectOption({ label: "Force L0" });
  assert.equal(Number_(await Metric("Error cut")), Source);
  await Card("Error cut").getByLabel("Level", { exact: true }).selectOption({ index: 0 });
  await Card("Error cut").getByLabel("View distance value", { exact: true }).fill("200");
  await Page.waitForTimeout(200);
  const Far = Number_(await Metric("Error cut"));
  assert(Far < Near && Near < Source, `${Far} < ${Near} < ${Source}`);
  await Page.getByRole("button", { name: "Bake clusters", exact: true }).click();
  assert.equal((await Stored("sphere", "LOD Clusters")).Baked.Levels, Levels);
  await Card("Error cut").getByLabel("View distance value", { exact: true }).fill("4");
  await Page.waitForTimeout(200);
  await Page.screenshot({ path: path.join(Proof, "LodClusters.png") });
  // The LOD page preview shows the same level count and the bake.
  await Page.evaluate(() => document.activeElement?.blur());
  await Page.keyboard.press("Escape");
  await Category("LOD");
  await Card("LOD preview").waitFor();
  assert.equal(Number_(await Metric("LOD preview")), Levels);
  assert.equal(await Card("LOD preview").locator(".ws-preview-lod svg rect").count(), Levels);
  assert.match(await Card("LOD preview").innerText(), /up to date/);
  await Card("LOD preview").screenshot({ path: path.join(Proof, "LodPreviewCard.png") });
  Checks.push(`LOD Clusters: ${Levels} levels; forced L0 = ${Source} triangles, 4 m cut ${Near}, 200 m cut ${Far}; bake stamp stored`);
} catch (Error) {
  Errors.push(Error.stack);
} finally {
  fs.writeFileSync(path.join(Proof, "Checks.json"), JSON.stringify({ Checks, Errors, FontFailures }, null, 2));
  await Browser.close();
}
assert.deepEqual(Errors, []);
console.log("Volume workspace checks passed:", Checks.length);
