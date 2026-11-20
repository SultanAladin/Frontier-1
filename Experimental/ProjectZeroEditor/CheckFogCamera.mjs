import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import {
  FogShapes,
  ResolveFogShape,
  FogGeometry,
  ValidFogPolygon,
} from "./FogShape.js";
import { EnsureEditorCamera, IsEditorCamera } from "./ScenePolicy.js";
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    new URL("../FrontierEditor/package.json", import.meta.url),
);
const { chromium } = Require("playwright");
const Proof =
  process.env.FRONTIER_PROOF_FOLDER ||
  path.resolve("Frontier/Experimental/ProjectZeroEditor/Screenshots");
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || undefined,
  args: ["--no-sandbox", "--disable-gpu"],
  headless: true,
});
const Page = await Browser.newPage({
  viewport: { width: 1440, height: 1100 },
  deviceScaleFactor: 1,
});
Page.setDefaultTimeout(10000);
const Errors = [],
  Checks = [];
Page.on("pageerror", (Error) => Errors.push(Error.message));
Page.on("console", (Message) => {
  if (Message.type() === "error") Errors.push(Message.text());
});
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
async function Select(Name) {
  const Search = Page.getByLabel("Search outliner", { exact: true });
  await Search.fill(Name);
  await Page.locator(".row-name").getByText(Name, { exact: true }).click();
  await Search.fill("");
  await Search.blur();
}
async function SetNumber(Name, Value) {
  const Input = Page.getByLabel(Name, { exact: true });
  await Input.fill(String(Value));
  await Input.press("Tab");
}
async function Import(Scene) {
  const Name = "Policy import " + Date.now();
  await Page.locator(
    'input[type=file][accept="application/json"]',
  ).setInputFiles({
    name: "Scene.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify({ ...Scene, Name })),
  });
  await Page.waitForFunction(
    (Name) =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Name ===
      Name,
    Name,
  );
}

try {
  assert.equal(ResolveFogShape({ "Half Size": [2, 3, 4] }).Width, 4);
  assert.deepEqual(
    FogGeometry(ResolveFogShape({ "Half Size": [2, 3, 4] })).Size,
    [4, 6, 8],
  );
  for (const Type of FogShapes) {
    const Mesh = FogGeometry(
      ResolveFogShape({ FogShape: { Type, Radius: 25, Height: 60 } }),
    );
    assert.ok(Mesh.Vertices.every((Point) => Point.every(Number.isFinite)));
    assert.ok(Mesh.Size.every((Size) => Size > 0));
  }
  assert.deepEqual(
    FogGeometry(ResolveFogShape({ FogShape: { Type: "Sphere", Radius: 25 } }))
      .Size,
    [50, 50, 50],
  );
  assert.deepEqual(
    FogGeometry(
      ResolveFogShape({
        FogShape: { Type: "Cylinder", Radius: 25, Height: 60 },
      }),
    ).Size,
    [50, 50, 60],
  );
  assert.ok(
    ValidFogPolygon([
      [0, 0],
      [5, 0],
      [2, 2],
      [5, 5],
      [0, 5],
    ]),
  );
  assert.equal(
    ValidFogPolygon([
      [0, 0],
      [5, 5],
      [0, 5],
      [5, 0],
    ]),
    false,
  );
  for (const Rows of [
    [],
    [
      {
        Id: "camera",
        Name: "Wrong",
        Panel: "geometry",
        Parent: "cube",
        Preview: true,
      },
    ],
    [{ Id: "camera" }, { Id: "camera" }],
  ]) {
    const Result = EnsureEditorCamera(Rows);
    assert.equal(Result.filter(IsEditorCamera).length, 1);
    assert.equal(Result.find(IsEditorCamera).Name, "Editor Camera");
    assert.equal(Result.find(IsEditorCamera).Preview, undefined);
    assert.equal(Result.find((Row) => Row.Panel === "camera").Id, "camera");
  }
  Checks.push(
    "All shape meshes and analytical bounds, legacy-box conversion, custom-polygon validation and permanent-camera normalization",
  );
  await Page.goto(process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:5180/", {
    waitUntil: "networkidle",
  });
  await Page.evaluate(() => document.fonts.ready);
  await Select("Local Fog");
  const Card = Page.locator('[data-card="Fog volume shape"]');
  await Card.scrollIntoViewIfNeeded();
  assert.equal(await Card.locator("select").inputValue(), "Box");
  assert.equal(
    await Page.getByLabel("Half Size X", { exact: true }).count(),
    0,
  );
  for (const Type of FogShapes) {
    await Page.getByLabel("Fog volume shape", { exact: true }).selectOption(
      Type,
    );
    assert.equal(await Card.locator("svg").getAttribute("data-shape"), Type);
    assert.equal((await Saved()).Values["local-fog"].FogShape.Type, Type);
  }
  await Page.getByLabel("Fog volume shape", { exact: true }).selectOption(
    "Sphere",
  );
  await SetNumber("Fog Radius (m)", 25);
  await SetNumber("Centre X", 10);
  assert.equal(
    +(await Card.locator('[data-bound="Min-X"]').getAttribute("data-value")),
    -15,
  );
  assert.equal(
    +(await Card.locator('[data-bound="Size-Z"]').getAttribute("data-value")),
    50,
  );
  await Card.scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/FogSphere.png" });
  await Page.getByLabel("Fog volume shape", { exact: true }).selectOption(
    "Prism",
  );
  await SetNumber("Fog Sides", 3);
  await SetNumber("Fog Radius (m)", 40);
  await SetNumber("Fog Height (m)", 90);
  assert.equal(await Card.locator("svg line").count(), 9);
  await Card.scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/FogPrism.png" });
  await Page.getByLabel("Fog volume shape", { exact: true }).selectOption(
    "Custom",
  );
  await SetNumber("Fog Height (m)", 10);
  await SetNumber("Centre Y", 20);
  await SetNumber("Centre Z", 30);
  await Page.getByLabel("Custom fog footprint", { exact: true }).fill(
    "-2, -1\n3, -1\n3, 4\n-2, 4",
  );
  await Page.getByRole("button", {
    name: "Apply footprint",
    exact: true,
  }).click();
  assert.equal(
    +(await Card.locator('[data-bound="Min-X"]').getAttribute("data-value")),
    8,
  );
  assert.equal(
    +(await Card.locator('[data-bound="Max-Z"]').getAttribute("data-value")),
    35,
  );
  const Good = (await Saved()).Values["local-fog"].FogShape;
  await Page.getByLabel("Custom fog footprint", { exact: true }).fill(
    "0,0\n5,5\n0,5\n5,0",
  );
  await Page.getByRole("button", {
    name: "Apply footprint",
    exact: true,
  }).click();
  await Page.getByRole("alert").waitFor();
  assert.deepEqual((await Saved()).Values["local-fog"].FogShape, Good);
  await Page.reload({ waitUntil: "networkidle" });
  await Select("Local Fog");
  assert.equal(
    await Page.getByLabel("Fog volume shape", { exact: true }).inputValue(),
    "Custom",
  );
  assert.match(
    await Page.getByLabel("Custom fog footprint", { exact: true }).inputValue(),
    /-2, -1/,
  );
  for (const Width of [1024, 1280, 1366, 1920]) {
    await Page.setViewportSize({ width: Width, height: 1100 });
    await Card.scrollIntoViewIfNeeded();
    assert.ok(
      await Card.evaluate((Node) => Node.scrollWidth <= Node.clientWidth + 1),
    );
  }
  await Page.setViewportSize({ width: 1440, height: 1100 });
  await Page.getByLabel("Custom fog footprint", { exact: true }).fill(
    "-100,-70\n30,-100\n100,-10\n50,100\n-80,60",
  );
  await SetNumber("Fog Height (m)", 180);
  await Page.getByRole("button", {
    name: "Apply footprint",
    exact: true,
  }).click();
  await Card.scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/FogCustom.png" });
  Checks.push(
    "Shape dropdown, shape-specific controls, dimensions/centre-driven world bounds, custom edit/rejection, reload and four responsive widths",
  );
  await Select("Editor Camera");
  await Page.locator(".inspector-scroll").evaluate(
    (Node) => (Node.scrollTop = 0),
  );
  assert.ok(
    await Page.getByLabel("Toggle Editor Camera visibility", {
      exact: true,
    }).isDisabled(),
  );
  const CameraRow = Page.locator(".outliner-row.selected");
  await CameraRow.focus();
  await Page.keyboard.press("F2");
  assert.equal(
    await Page.getByLabel("Rename object", { exact: true }).count(),
    0,
  );
  await CameraRow.click({ button: "right" });
  const Menu = Page.getByRole("menu");
  assert.ok(
    await Menu.getByRole("button", {
      name: "Remove from HTML scene",
      exact: true,
    }).isDisabled(),
  );
  assert.ok(
    await Menu.getByRole("button", {
      name: "Rename F2",
      exact: true,
    }).isDisabled(),
  );
  await Page.screenshot({ path: Proof + "/EditorCamera.png" });
  await Menu.getByRole("button", { name: "Duplicate", exact: true }).click();
  let State = await Saved();
  const Copy = State.Rows.find((Row) => Row.Id === State.Selected);
  assert.equal(Copy.Name, "Main Camera");
  assert.equal(Copy.Panel, "camera");
  assert.notEqual(Copy.Id, "camera");
  await Page.locator(".outliner-row.selected").click({ button: "right" });
  assert.equal(
    await Menu.getByRole("button", {
      name: "Remove from HTML scene",
      exact: true,
    }).isDisabled(),
    false,
  );
  await Menu.getByRole("button", {
    name: "Remove from HTML scene",
    exact: true,
  }).click();
  assert.equal((await Saved()).Rows.filter(IsEditorCamera).length, 1);
  await Page.getByRole("button", { name: "Construct", exact: true }).click();
  await Page.getByLabel("Search construct", { exact: true }).fill(
    "Main Camera",
  );
  await Page.locator(".construct-matrix")
    .getByRole("button", { name: "Main Camera", exact: true })
    .click();
  await Page.getByRole("button", { name: "Add to scene", exact: true }).click();
  assert.equal((await Saved()).Rows.filter(IsEditorCamera).length, 1);
  const Scene = await Saved();
  await Import({
    Format: "Frontier HTML UI study",
    Rows: Scene.Rows.filter((Row) => !["camera", "cameras"].includes(Row.Id)),
    Values: Scene.Values,
    Hidden: { camera: true },
  });
  assert.equal((await Saved()).Rows.filter(IsEditorCamera).length, 1);
  assert.equal((await Saved()).Hidden.camera, false);
  assert.deepEqual(
    (await Saved()).Values["local-fog"].FogShape,
    Scene.Values["local-fog"].FogShape,
  );
  await Import({
    Format: "Frontier HTML UI study",
    Rows: [],
    Values: { camera: { "Focal Length": 35 } },
    Hidden: { camera: true },
  });
  await Select("Editor Camera");
  assert.equal(
    await Page.getByLabel("Focal Length", { exact: true }).first().inputValue(),
    "35",
  );
  assert.equal((await Saved()).Rows.filter(IsEditorCamera).length, 1);
  await Page.evaluate(() => {
    const State = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    State.Rows = [];
    State.Hidden.camera = true;
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(State));
  });
  await Page.reload({ waitUntil: "networkidle" });
  assert.equal((await Saved()).Rows.filter(IsEditorCamera).length, 1);
  assert.equal((await Saved()).Hidden.camera, false);
  Checks.push(
    "Editor Camera rename/hide/delete guards, normal duplicate/Construct scene cameras, protected identity on empty/missing-camera imports and reload; fog shape scene round-trip",
  );
  assert.deepEqual(Errors, []);
} catch (Error) {
  Errors.push(Error.stack);
  await Page.screenshot({ path: Proof + "/FogCameraFailure.png" }).catch(
    () => {},
  );
  process.exitCode = 1;
} finally {
  fs.writeFileSync(
    Proof + "/FogCamera.json",
    JSON.stringify({ Checks, Errors }, null, 2),
  );
  console.log(JSON.stringify({ Checks, Errors }, null, 2));
  await Browser.close();
}
