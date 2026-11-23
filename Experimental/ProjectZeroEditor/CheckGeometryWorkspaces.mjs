import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

// Follow-up 46: algorithms ported from the Geometry Compiler into new workspaces (Collision Fit, Icon Capture), the Nanite-style
// additions to LOD Clusters (surface parts kept apart, back-facing clusters culled) and the Component Viewer opened from the editor.
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/GeometryWorkspaces");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];
const Browser = await chromium.launch({ executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium", args: ["--no-sandbox", "--disable-gpu"] });
const Page = await Browser.newPage({ viewport: { width: 1440, height: 900 } });
Page.setDefaultTimeout(20000);
const Address = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll > [data-panel]").waitFor();
  await Page.waitForTimeout(250);
};
const Saved = () => Page.evaluate(() => JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")));
const Stored = async (Id, Key) => ((await Saved())?.Values?.[Id] || {})[Key];
const StoredBecomes = (Id, Key, Test) =>
  Page.waitForFunction(
    ([Id, Key, Source]) => {
      const Values = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1") || "{}")?.Values?.[Id] || {};
      return new Function("Stored", "return " + Source)(Values[Key]);
    },
    [Id, Key, Test],
  );
const Card = (Title) => Page.locator(`[data-card="${Title}"]`);
const Metric = (Title) => Card(Title).locator(".metric").first().innerText();
const Launch = (Tool) => Page.locator(`.ws-rail button[aria-label="${Tool}"]`).click();
const Banner = () => Page.locator(".ws-banner").innerText();
const Number_ = (Text) => +Text.replace(/[^\d.]/g, "");
const Switch = (Label) => Page.locator(`.ws-panel [role=switch][aria-label="${Label}"]`);
const Click = (Locator) => Locator.evaluate((Each) => Each.click());

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());

  // F53: the six tools sit in a vertical rail on the viewport's left edge, not at the top of the inspector.
  await Open("cube");
  const Tools = await Page.locator(".ws-rail button").evaluateAll((Each) => Each.map((Button) => Button.getAttribute("aria-label")));
  assert.deepEqual(Tools, ["UV", "Fracture", "SDF Bake", "LOD Clusters", "Collision Fit", "Icon Capture"]);
  assert.equal(await Page.locator(".inspector-scroll .ws-launcher, .inspector-scroll .ws-tabs").count(), 0);
  const Rail = await Page.locator(".ws-rail").boundingBox(), View = await Page.locator(".scene-image").boundingBox();
  assert(Rail.x - View.x < 20 && Rail.height > Rail.width * 4, JSON.stringify({ Rail, View }));
  Checks.push(`Rail on the viewport's left edge (${Math.round(Rail.x - View.x)} px in, ${Math.round(Rail.width)}×${Math.round(Rail.height)}): UV, Fracture, SDF Bake, LOD Clusters, Collision Fit, Icon Capture; nothing at the inspector top`);

  // Collision Fit on the cube: closed, 12 triangles, box is tightest; Use writes the Collision component.
  await Launch("Collision Fit");
  assert.match(await Banner(), /Collision Fit workspace/);
  await Card("Surface check").waitFor();
  assert.match(await Metric("Surface check"), /Closed/);
  const Check = await Card("Surface check").innerText();
  assert.match(Check, /12 triangles · 8 vertices · 1 part/);
  assert.match(Check, /Open edges\s*0/);
  assert.match(Check, /Non-manifold edges\s*0/);
  assert.match(Check, /Surface area\s*6\.000 m²/);
  assert.match(Check, /Enclosed volume\s*1\.000 m³/);
  const Bars = await Page.locator(".ws-fit-bar").evaluateAll((Each) => Each.map((Bar) => [Bar.dataset.fit, Bar.querySelector(".ws-fit-figure").textContent, Bar.textContent.includes("tightest")]));
  assert.deepEqual(Bars.map(([Type]) => Type), ["box", "sphere", "capsule", "convex", "triangles", "custom"]);
  // 1 m³ over the 1.02 m margin box: 94.2 %. The tightest marker sits on the box.
  assert.equal(Bars[0][1], "94.2 %");
  assert.deepEqual(Bars.map(([, , Tightest]) => Tightest), [true, false, false, false, false, false]);
  const BoxFill = Bars[0][1], SphereFill = Bars[1][1];
  assert.equal(await Card("Fitted shapes").locator(".metric").first().innerText().then((Text) => /Box/.test(Text)), true);
  await Page.screenshot({ path: path.join(Proof, "CollisionFit.png") });
  await Click(Page.locator('.ws-fit-use[data-use="sphere"]'));
  await StoredBecomes("cube", "Collision Shape", 'Stored === "sphere"');
  assert.match(await Metric("Fitted shapes"), /Sphere/);
  assert.match(await Page.locator('.ws-fit-bar[data-fit="sphere"]').innerText(), /in use/i);
  await Click(Page.getByRole("button", { name: "Back to Automatic", exact: true }));
  await StoredBecomes("cube", "Collision Shape", 'Stored === "auto"');
  Checks.push(`Collision Fit cube: closed, 12 triangles, 6 m², 1 m³; box ${BoxFill} (tightest), sphere ${SphereFill}; Use sphere stored, back to auto`);

  // The cylinder's cap rims are separate rings; the weld makes the check closed.
  await Page.keyboard.press("Escape");
  await Open("cylinder");
  await Launch("Collision Fit");
  await Card("Surface check").waitFor();
  assert.match(await Metric("Surface check"), /Closed/);
  assert.match(await Card("Surface check").innerText(), /Open edges\s*0/);
  Checks.push("Collision Fit cylinder: welded cap rims read as a closed surface");

  // Icon Capture on the cube: transparent PNG, coverage, thumbnail stored at 128 px.
  await Page.keyboard.press("Escape");
  await Open("cube");
  await Launch("Icon Capture");
  assert.match(await Banner(), /Icon Capture workspace/);
  await Card("Capture box").waitFor();
  assert.match(await Metric("Capture box"), /256/);
  assert.match(await Card("Capture box").innerText(), /40° yaw · 22° pitch · 1\.00× framing/);
  await Click(Page.locator('[data-capture="shoot"]'));
  await Page.locator(".ws-capture-plate img").waitFor();
  const Coverage = Number_(await Metric("Icon"));
  assert(Coverage > 20 && Coverage < 80, String(Coverage));
  const Png = await Page.locator(".ws-capture-plate img").evaluate(async (Image) => {
    await Image.decode();
    const Canvas = document.createElement("canvas");
    Canvas.width = Image.naturalWidth;
    Canvas.height = Image.naturalHeight;
    const Brush = Canvas.getContext("2d");
    Brush.drawImage(Image, 0, 0);
    const Corner = Brush.getImageData(0, 0, 1, 1).data[3], Middle = Brush.getImageData(Canvas.width / 2, Canvas.height / 2, 1, 1).data[3];
    // A row across the top face crosses the split between its two triangles; every pixel must be opaque (no seam).
    let Seams = 0;
    for (let X = 90; X < 170; X++) if (Brush.getImageData(X, 60, 1, 1).data[3] < 255) Seams++;
    return { Width: Image.naturalWidth, Height: Image.naturalHeight, Corner, Middle, Seams, Url: Image.src.slice(0, 22) };
  });
  assert.deepEqual(Png, { Width: 256, Height: 256, Corner: 0, Middle: 255, Seams: 0, Url: "data:image/png;base64," });
  await Click(Page.getByRole("button", { name: "Save as thumbnail", exact: true }));
  await StoredBecomes("cube", "Icon Capture", "Stored && Stored.Side === 128");
  const Thumbnail = await Stored("cube", "Icon Capture");
  assert.equal(Thumbnail.Size, 256);
  assert.equal(Thumbnail.Look, "Clay");
  assert.match(Thumbnail.Url, /^data:image\/png;base64,/);
  await Page.screenshot({ path: path.join(Proof, "IconCapture.png") });
  // 512 px normals look.
  await Card("Capture box").getByLabel("Output size", { exact: true }).selectOption({ index: 2 });
  await Card("Capture box").getByLabel("Look", { exact: true }).selectOption({ label: "Normals" });
  await Click(Page.locator('[data-capture="shoot"]'));
  await Page.waitForFunction(() => /512 px · Normals/.test(document.querySelector('[data-card="Icon"]')?.textContent || ""));
  const Big = await Page.locator(".ws-capture-plate img").evaluate(async (Image) => (await Image.decode(), Image.naturalWidth));
  assert.equal(Big, 512);
  Checks.push(`Icon Capture cube: 256 px transparent PNG (corner α 0, centre α 255, no seam across the top face), ${Coverage} % covered, 128 px thumbnail stored; 512 px normals`);

  // LOD Clusters on the cube: six surface parts; keeping them apart gives single-part clusters; back-face culling drops clusters.
  await Page.keyboard.press("Escape");
  await Launch("LOD Clusters");
  await Card("Cluster DAG").waitFor();
  assert.match(await Card("Cluster DAG").innerText(), /6 surface parts; clusters may span them/);
  const Before = Number_(await Metric("Error cut"));
  await Click(Switch("Keep surface parts apart"));
  await StoredBecomes("cube", "LOD Clusters", "Stored && Stored.KeepParts === true");
  await Page.waitForFunction(() => /no cluster mixes two of them/.test(document.querySelector('[data-card="Cluster DAG"]')?.textContent || ""));
  await Card("Error cut").getByLabel("Colour by", { exact: true }).selectOption({ label: "Part" });
  await Card("Error cut").getByLabel("Level", { exact: true }).selectOption({ label: "Force L0" });
  const AllL0 = Number_(await Metric("Error cut"));
  await Click(Switch("Cull back-facing clusters"));
  await Page.waitForFunction(() => /culled facing away/.test(document.querySelector('[data-card="Error cut"] .metric')?.parentElement?.textContent || ""));
  const Culled = Number_(await Metric("Error cut"));
  const Caption = await Card("Error cut").innerText();
  const Dropped = +Caption.match(/(\d+) culled facing away/)[1];
  assert(Culled < AllL0 && Dropped > 0, `${Culled} < ${AllL0}, ${Dropped} culled`);
  await Page.screenshot({ path: path.join(Proof, "LodParts.png") });
  Checks.push(`LOD Clusters cube: 6 parts kept apart; forced L0 ${AllL0} triangles, back-face culling drops ${Dropped} clusters to ${Culled} (auto cut before ${Before})`);

  // Component Viewer opens over the editor and closes with Esc.
  await Page.keyboard.press("Escape");
  await Click(Page.locator('button[aria-label="Component Viewer"]'));
  await Page.locator('.cv-host[role="dialog"]').waitFor();
  const Viewer = Page.frameLocator(".cv-host-view");
  await Page.waitForFunction(() => document.querySelector(".cv-host-view")?.contentDocument?.title === "Component Viewer · Frontier");
  // The viewer compiles its ControlVehicle sample on boot; the boot screen is removed once that is done (or after a failure).
  await Page.waitForFunction(() => {
    const Inner = document.querySelector(".cv-host-view")?.contentDocument;
    return Inner && !Inner.getElementById("Boot");
  }, null, { timeout: 180000 });
  const Failed = await Viewer.locator("body").innerText().then((Text) => /Could not load the ControlVehicle sample/.test(Text));
  assert.equal(Failed, false);
  await Page.waitForTimeout(1500);
  await Page.screenshot({ path: path.join(Proof, "ComponentViewer.png") });
  await Page.keyboard.press("Escape");
  await Page.locator(".cv-host").waitFor({ state: "detached" });
  Checks.push("Component Viewer: opens over the editor, compiles its ControlVehicle sample (boot screen cleared), Esc closes it");
} catch (Error) {
  Errors.push(Error.stack);
} finally {
  fs.writeFileSync(path.join(Proof, "Checks.json"), JSON.stringify({ Checks, Errors }, null, 2));
  await Browser.close();
}
console.log(JSON.stringify({ Checks, Errors }, null, 2));
assert.deepEqual(Errors, []);
console.log("Geometry workspace checks passed:", Checks.length);
