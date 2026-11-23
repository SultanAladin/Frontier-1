import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { convexHull, inflateHull } from "./ConvexHull.js";
import { colliderFromBytes } from "./ColliderImport.js";
import { shatter, normalize, signature, recipeDiff } from "../FractureEditor/FractureModel.js";
import { pieceColliders } from "./PieceCollision.js";

// Follow-up 47: more collision shapes (convex hull, exact triangles, custom imported collider), and in the Fracture workspace several
// fracture points with their own energy plus a collision type for the pieces.
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/Colliders");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];

// ---------------------------------------------------------------------------------------------------------------- pure algorithms
// Hull of a unit cube plus interior points is the cube; growing it by 0.01 moves every face out by 0.01.
const Corners = [];
for (const X of [-0.5, 0.5]) for (const Y of [-0.5, 0.5]) for (const Z of [-0.5, 0.5]) Corners.push(X, Y, Z);
for (let Index = 0; Index < 300; Index++) Corners.push(Math.sin(Index * 12.9898) * 0.49, Math.sin(Index * 78.233) * 0.49, Math.sin(Index * 37.719) * 0.49);
const CubeHull = convexHull(new Float32Array(Corners));
assert.equal(CubeHull.Points, 8);
assert.equal(CubeHull.Faces, 12);
assert(Math.abs(CubeHull.Volume - 1) < 1e-6 && Math.abs(CubeHull.Area - 6) < 1e-6);
assert(Math.abs(inflateHull(CubeHull, 0.01).Volume - 1.02 ** 3) < 1e-6);
// Points on a sphere: every point is on the hull; V − E + F = 2.
const Sphere = [];
for (let Index = 0; Index < 600; Index++) {
  const U = 1 - (2 * (Index + 0.5)) / 600, A = Index * 2.399963, Ring = Math.sqrt(1 - U * U);
  Sphere.push(Ring * Math.cos(A), U, Ring * Math.sin(A));
}
const SphereHull = convexHull(new Float32Array(Sphere));
assert.equal(SphereHull.Points, 600);
assert.equal(SphereHull.Points - (SphereHull.Faces * 3) / 2 + SphereHull.Faces, 2);
assert(SphereHull.Volume > 4.1 && SphereHull.Volume < (4 / 3) * Math.PI);
Checks.push(`Convex hull: cube 8 points / 12 faces / 1 m³ / 6 m², grown 0.01 → ${(1.02 ** 3).toFixed(4)} m³; 600 sphere points all on the hull, Euler 2, ${SphereHull.Volume.toFixed(3)} m³`);

// Custom collider: an octahedron OBJ (±1 m) reads as 8 triangles, closed, 4/3 m³; a big file is refused.
const Octahedron = "v 1 0 0\nv -1 0 0\nv 0 1 0\nv 0 -1 0\nv 0 0 1\nv 0 0 -1\nf 1 3 5\nf 3 2 5\nf 2 4 5\nf 4 1 5\nf 3 1 6\nf 2 3 6\nf 4 2 6\nf 1 4 6\n";
const OctaPath = path.join(os.tmpdir(), "Octahedron.obj");
fs.writeFileSync(OctaPath, Octahedron);
const Imported = colliderFromBytes("Octahedron.obj", new TextEncoder().encode(Octahedron).buffer);
assert.equal(Imported.Triangles, 8);
assert.equal(Imported.Vertices, 6);
assert.equal(Imported.Closed, true);
assert(Math.abs(Imported.Volume - 4 / 3) < 1e-6 && Math.abs(Imported.HullVolume - 4 / 3) < 1e-6);
let Big = "";
for (let Index = 0; Index < 4100; Index++) Big += `v ${Index} 0 0\nv ${Index} 1 0\nv ${Index} 0 1\nf ${Index * 3 + 1} ${Index * 3 + 2} ${Index * 3 + 3}\n`;
assert.throws(() => colliderFromBytes("Big.obj", new TextEncoder().encode(Big).buffer), /limited to 4,000/);
assert.throws(() => colliderFromBytes("Car.fbx", new ArrayBuffer(8)), /FBX has no reader/);
Checks.push("Custom collider import: octahedron OBJ → 8 triangles, 6 vertices, closed, 1.333 m³; 4,100 triangles refused; FBX refused");

// Fracture points: one point is the original format (same pieces, same bake signature); two points pull pieces to both.
const Owner = { Id: "cube", Name: "Cube", Primitive: "cube", Scale: [1, 1, 1] };
const Single = { Enabled: true, Material: "glass", Energy: 4000, X: 0.3, Y: 0.2, Z: 0.5, Seed: 7 };
const Folded = normalize({ ...Single, Points: [{ X: 0.3, Y: 0.2, Z: 0.5, Energy: 4000 }] });
assert.equal(Folded.Points, undefined);
assert.equal(signature(Owner, Folded), signature(Owner, Single));
const Twin = { Enabled: true, Material: "glass", Seed: 7, Points: [{ X: -0.5, Y: 0.3, Z: 0.5, Energy: 3000 }, { X: 0.5, Y: -0.3, Z: 0.5, Energy: 9000 }] };
const TwinResult = shatter(Owner, Twin);
assert.equal(normalize(Twin).Energy, 12000);
assert.equal(TwinResult.impacts.length, 2);
const Near = [0, 1].map((Index) => TwinResult.cells.filter((Cell) => Cell.nearest === Index).length);
assert(Near[0] > 4 && Near[1] > 4, String(Near));
assert(Math.abs(TwinResult.cells.reduce((Sum, Cell) => Sum + Cell.volume, 0) - 1) < 1e-6);
const Changed = recipeDiff(Owner, { ...Twin, Baked: { Signature: signature(Owner, Single) } }).map((Each) => Each.key);
assert(Changed.includes("Points"));
// Piece colliders: Voronoi pieces are convex, so the hull fills them 100 %; boxes are looser.
const Hulls = pieceColliders(TwinResult.cells, "convex"), Boxes = pieceColliders(TwinResult.cells, "box"), Exact = pieceColliders(TwinResult.cells, "triangles");
assert(Hulls.Fill > 0.999, String(Hulls.Fill));
assert(Boxes.Fill < 0.9 && Boxes.Fill > 0.2, String(Boxes.Fill));
assert.equal(Exact.Fill, 1);
Checks.push(`Fracture points: one point keeps the old signature; two points (3 + 9 kJ) → ${TwinResult.cells.length} pieces, ${Near.join(" / ")} nearest each, volume 1 m³; piece hulls fill ${(Hulls.Fill * 100).toFixed(1)} %, boxes ${(Boxes.Fill * 100).toFixed(1)} %`);

// ------------------------------------------------------------------------------------------------------------------------- browser
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
const StoredBecomes = (Id, Key, Test) =>
  Page.waitForFunction(
    ([Id, Key, Source]) => {
      const Values = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1") || "{}")?.Values?.[Id] || {};
      return new Function("Stored", "return " + Source)(Values[Key]);
    },
    [Id, Key, Test],
  );
const Card = (Title) => Page.locator(`[data-card="${Title}"]`);
const Launch = (Tool) => Page.locator(`.ws-rail button[aria-label="${Tool}"]`).click();
const Click = (Locator) => Locator.evaluate((Each) => Each.click());

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());

  // Collision Fit on the cube: six shapes. The grown hull ties with the box (both 1.02 m), so the cheaper box keeps "tightest";
  // triangles are the surface itself (100 %, static only); Custom waits for an import.
  await Open("cube");
  await Launch("Collision Fit");
  await Card("Fitted shapes").waitFor();
  const Bars = () => Page.locator(".ws-fit-bar").evaluateAll((Each) => Each.map((Bar) => [Bar.dataset.fit, Bar.querySelector(".ws-fit-figure").textContent, Bar.disabled, Bar.textContent.includes("tightest")]));
  let Shown = await Bars();
  assert.deepEqual(Shown.map(([Type, Fill, Off, Best]) => [Type, Fill, Off, Best]), [
    ["box", "94.2 %", false, true],
    ["sphere", Shown[1][1], false, false],
    ["capsule", Shown[2][1], false, false],
    ["convex", "94.2 %", false, false],
    ["triangles", "100.0 %", false, false],
    ["custom", Shown[5][1], true, false],
  ]);
  assert.match(await Page.locator('.ws-fit-bar[data-fit="triangles"]').innerText(), /static only/i);
  await Click(Page.locator('.ws-fit-bar[data-fit="convex"]'));
  await Page.waitForTimeout(150);
  await Page.screenshot({ path: path.join(Proof, "CollisionConvexHull.png") });
  Checks.push("Collision Fit cube: Box 94.2 % (tightest), Convex hull 94.2 % (ties, box kept), Triangles 100 % (static only), Custom off until an import");

  // Import the octahedron in the workspace: Custom lights up with its own fill; Use stores it on the Collision component.
  await Page.locator('.ws-panel input[type="file"]').setInputFiles(OctaPath);
  await StoredBecomes("cube", "Collision Custom", "Stored && Stored.Triangles === 8");
  await Page.locator(".ws-panel [role=status]").filter({ hasText: "Imported Octahedron.obj" }).waitFor();
  Shown = await Bars();
  // The cube encloses 1 m³; the octahedron 4/3 m³ → 75.0 %.
  assert.deepEqual(Shown[5].slice(0, 3), ["custom", "75.0 %", false]);
  await Click(Page.locator('.ws-fit-use[data-use="custom"]'));
  await StoredBecomes("cube", "Collision Shape", 'Stored === "custom"');
  await Page.screenshot({ path: path.join(Proof, "CollisionCustom.png") });
  Checks.push("Imported Octahedron.obj in Collision Fit: stored (8 triangles), Custom 75.0 % of the cube, Use → Collision Shape custom");

  // The Collision component page shows the import and its volume; Convex hull reports its points and faces; triangles on a
  // dynamic body warns that it falls back to the hull.
  await Page.keyboard.press("Escape");
  await Open("cube-collision");
  await Card("Collision shape").waitFor();
  const Page_ = await Card("Collision shape").innerText();
  assert.match(Page_, /Octahedron\.obj/);
  assert.match(Page_, /OBJ · 8 triangles · closed · 1\.333 m³/);
  await Card("Collision shape").locator("select").first().selectOption({ label: "Convex hull" });
  await StoredBecomes("cube", "Collision Shape", 'Stored === "convex"');
  assert.match(await Card("Collision shape").locator("[data-hull]").innerText(), /8 points, 12 faces/);
  assert.match(await Card("Collision shape").innerText(), /1\.061 m³ proxy/);
  await Page.evaluate(() => {
    const Scene = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1"));
    Object.assign(Scene.Values.cube, { PHYSICS: true, DYNAMIC: true, "Physics Body": "dynamic", "Collision Shape": "triangles" });
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Scene));
  });
  await Open("cube-collision");
  await Card("Collision shape").locator("[data-static-only]").waitFor();
  await Page.screenshot({ path: path.join(Proof, "CollisionComponentTriangles.png") });
  Checks.push("Collision component: shows Octahedron.obj (OBJ · 8 triangles · closed · 1.333 m³); Convex hull 8 points / 12 faces, 1.061 m³ proxy; Triangles on a dynamic body warns");

  // Fracture workspace: enable, click the cube to add a second point, drag it, give it its own energy, choose box colliders.
  await Open("cube");
  await Launch("Fracture");
  await Page.locator('.ws-panel [role=switch][aria-label="Enable fracture"]').click();
  await StoredBecomes("cube", "Fracture", "Stored && Stored.Enabled === true");
  await Card("Fracture points").waitFor();
  // Let the view repaint with the pieces before picking on it.
  await Page.waitForTimeout(600);
  const Canvas = Page.locator(".ws-view-fracture canvas");
  const Box = await Canvas.boundingBox();
  await Page.mouse.click(Box.x + Box.width * 0.42, Box.y + Box.height * 0.42);
  await StoredBecomes("cube", "Fracture", "Stored && Stored.Points && Stored.Points.length === 2");
  assert.equal(await Page.locator(".ws-points li").count(), 2);
  assert.equal(await Page.locator('.ws-points li[aria-current="true"]').getAttribute("data-point"), "1");
  const Before = (await Saved()).Values.cube.Fracture.Points[1];
  // Drag the new marker from where it is drawn (pieces are exploded, so it isn't exactly where the click landed).
  await Page.waitForFunction(() => JSON.parse(document.querySelector(".ws-view-fracture canvas").dataset.markers || "[]").length === 2);
  const [Mx, My] = JSON.parse(await Canvas.getAttribute("data-markers"))[1];
  await Page.mouse.move(Box.x + Mx, Box.y + My);
  await Page.mouse.down();
  await Page.mouse.move(Box.x + Mx + 30, Box.y + My + 40, { steps: 6 });
  await Page.mouse.up();
  await Page.waitForFunction(([X, Y, Z]) => {
    const P = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values.cube.Fracture.Points[1];
    return P.X !== X || P.Y !== Y || P.Z !== Z;
  }, [Before.X, Before.Y, Before.Z]);
  const After = (await Saved()).Values.cube.Fracture;
  assert.equal(After.Points.length, 2);
  await Click(Card("Fracture points").getByRole("button", { name: "Blast", exact: true }));
  await StoredBecomes("cube", "Fracture", "Stored && Stored.Points && Stored.Points[1].Energy !== Stored.Points[0].Energy");
  const Energies = (await Saved()).Values.cube.Fracture.Points.map((Each) => Each.Energy);
  assert.equal((await Saved()).Values.cube.Fracture.Energy, Energies[0] + Energies[1]);
  const Pieces = +(await Card("Shatter").locator(".metric").first().innerText()).replace(/\D/g, "");
  await Click(Page.locator('[data-piece-collision="box"]'));
  await StoredBecomes("cube", "Fracture", 'Stored && Stored.PieceCollision === "box"');
  const BoxFill = +(await Card("Piece collision").locator(".metric").first().innerText()).replace(/[^\d]/g, "");
  await Click(Page.locator('[data-piece-collision="convex"]'));
  await StoredBecomes("cube", "Fracture", 'Stored && Stored.PieceCollision === "convex"');
  assert.match(await Card("Piece collision").locator(".metric").first().innerText(), /100\s*%/);
  assert(BoxFill < 100 && BoxFill > 20, String(BoxFill));
  await Card("Piece collision").getByRole("switch", { name: "Show colliders" }).click();
  await Page.waitForTimeout(250);
  await Page.screenshot({ path: path.join(Proof, "FracturePoints.png") });
  // Keep only one point: back to the original single-point format.
  await Click(Card("Fracture points").getByRole("button", { name: /^Keep only point/ }));
  await StoredBecomes("cube", "Fracture", "Stored && !Stored.Points");
  Checks.push(`Fracture workspace: click adds point 2, dragging moves it, Blast gives it ${Energies[1]} J (total ${Energies[0] + Energies[1]} J, ${Pieces} pieces); piece colliders box ${BoxFill} % vs convex hull 100 %; Keep only point → single-point format`);

  // The workspace rail switches tools inside a workspace and the lit tool leaves it.
  await Launch("Collision Fit");
  await Card("Fitted shapes").waitFor();
  assert.equal(await Page.locator('.ws-rail button[aria-pressed="true"]').getAttribute("aria-label"), "Collision Fit");
  await Launch("Collision Fit");
  await Page.locator(".ws-view").waitFor({ state: "detached" });
  assert.equal(await Page.locator('.ws-rail button[aria-pressed="true"]').count(), 0);
  Checks.push("Rail: switching Fracture → Collision Fit lights the new tool; clicking the lit tool leaves the workspace");

  assert.deepEqual(Errors, []);
  console.log(Checks.map((Check) => "🟢 " + Check).join("\n"));
  console.log(`${Checks.length} Colliders checks passed`);
} finally {
  await Browser.close();
}
