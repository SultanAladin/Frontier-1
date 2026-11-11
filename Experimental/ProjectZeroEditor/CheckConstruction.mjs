import path from "node:path";
import fs from "node:fs";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    path.resolve(Folder, "../FrontierEditor/package.json"),
);
const { chromium } = Require("playwright");
const Proof =
  process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots");
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || undefined,
  args: ["--disable-gpu", "--no-sandbox"],
  headless: true,
});
const Page = await Browser.newPage({
  viewport: { width: 1440, height: 900 },
  deviceScaleFactor: 1,
});
Page.setDefaultTimeout(10000);
const Errors = [];
Page.on("pageerror", (Error) => Errors.push(Error.message));
Page.on("console", (Message) => {
  if (Message.type() === "error") Errors.push(Message.text());
});
const Url = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:5180/";
const Count = () => Page.locator("[data-preview-id]").count();
const Open = async () => {
  await Page.getByRole("button", { name: "Construct", exact: true }).click();
  await Page.getByRole("dialog", { name: "Construct", exact: true }).waitFor();
};
const Choose = async (Name) => {
  await Page.getByRole("textbox", {
    name: "Search construct",
    exact: true,
  }).fill(Name);
  await Page.locator(".construct-matrix")
    .getByRole("button", { name: Name, exact: true })
    .click();
};
const Add = () =>
  Page.getByRole("button", { name: "Add to scene", exact: true }).click();
try {
  await Page.goto(Url, { waitUntil: "networkidle" });
  await Page.evaluate(() => localStorage.clear());
  await Page.reload({ waitUntil: "networkidle" });
  assert.equal(await Count(), 0);
  assert.equal(await Page.locator(".scene-image img").count(), 0);
  await Page.screenshot({ path: path.join(Proof, "Checkerboard.png") });
  await Page.locator(".checker-pane").click({ position: { x: 25, y: 25 } });
  await Page.keyboard.down("Shift");
  for (const Key of ["w", "a", "s", "d"]) await Page.keyboard.press(Key);
  await Page.keyboard.up("Shift");
  for (const Keys of ["a", "Control+Shift+a", "Alt+a", "Control+Alt+a"])
    await Page.keyboard.press(Keys);
  assert.equal(
    await Page.locator(".construct-dialog").count(),
    0,
    "Movement/modifier chords must not open Construct",
  );
  const Consumed = await Page.evaluate(() => {
    const Movement = new KeyboardEvent("keydown", {
      key: "A",
      code: "KeyA",
      shiftKey: true,
      bubbles: true,
      cancelable: true,
    });
    document.body.dispatchEvent(Movement);
    return Movement.defaultPrevented;
  });
  assert.equal(Consumed, false, "Shift+A must remain unconsumed");
  await Page.evaluate(() =>
    document.body.dispatchEvent(
      new KeyboardEvent("keydown", {
        key: "a",
        code: "KeyA",
        ctrlKey: true,
        repeat: true,
        bubbles: true,
        cancelable: true,
      }),
    ),
  );
  assert.equal(
    await Page.locator(".construct-dialog").count(),
    0,
    "Repeated keydown must not reopen Construct",
  );
  await Page.evaluate(() => {
    const Editable = document.createElement("div");
    Editable.id = "ConstructionEditable";
    Editable.contentEditable = "true";
    Editable.textContent = "Editable text";
    document.body.append(Editable);
    Editable.focus();
  });
  await Page.keyboard.press("Control+a");
  assert.equal(
    await Page.locator(".construct-dialog").count(),
    0,
    "Editable content keeps Select All",
  );
  await Page.evaluate(() =>
    document.getElementById("ConstructionEditable").remove(),
  );
  await Page.keyboard.press("Control+a");
  assert.equal(await Page.locator(".construct-dialog").count(), 1);
  const Search = Page.getByRole("textbox", {
    name: "Search construct",
    exact: true,
  });
  await Search.fill("example query");
  await Search.press("Control+a");
  assert.deepEqual(
    await Search.evaluate((Input) => [
      Input.selectionStart,
      Input.selectionEnd,
    ]),
    [0, 13],
  );
  await Search.press("Escape");
  assert.equal(
    await Page.locator(".construct-dialog").count(),
    0,
    "Escape must close from search focus",
  );
  const OutlineSearch = Page.getByRole("textbox", {
    name: "Search outliner",
    exact: true,
  });
  await OutlineSearch.fill("editable");
  await OutlineSearch.press("Control+a");
  assert.deepEqual(
    await OutlineSearch.evaluate((Input) => [
      Input.selectionStart,
      Input.selectionEnd,
    ]),
    [0, 8],
  );
  assert.equal(await Page.locator(".construct-dialog").count(), 0);
  await OutlineSearch.fill("");
  await Open();
  await Page.screenshot({ path: path.join(Proof, "ConstructMenu.png") });
  const Names = await Page.locator(".construct-entity").evaluateAll((Buttons) =>
    Buttons.map((Button) => Button.getAttribute("aria-label")),
  );
  assert.equal(Names.length, 23);
  for (const [Group, Expected] of [
    ["Environment", 8],
    ["Weather", 7],
    ["Cameras", 2],
    ["Geometry", 5],
    ["Lighting", 1],
  ]) {
    await Page.getByRole("navigation", { name: "Construct categories" })
      .getByRole("button", { name: Group, exact: true })
      .click();
    assert.equal(
      await Page.locator(".construct-entity").count(),
      Expected,
      Group,
    );
  }
  await Page.getByRole("navigation", { name: "Construct categories" })
    .getByRole("button", { name: "All", exact: true })
    .click();
  await Search.fill("absent entity");
  assert.equal(await Page.locator(".construct-entity").count(), 0);
  await Page.getByRole("button", { name: "Clear construct search" }).click();
  await Choose("Cube");
  await Page.getByRole("textbox", { name: "Construct entity name" }).fill(
    "Uncommitted",
  );
  await Page.keyboard.press("Escape");
  assert.equal(await Page.locator(".construct-matrix").count(), 1);
  await Page.getByRole("button", { name: "Cancel", exact: true }).click();
  assert.equal(await Count(), 0);
  await Open();
  await Choose("Cube");
  await Page.locator(
    ".construct-properties [data-material-channel=roughness]",
  ).click();
  await Page.locator(".construct-properties")
    .getByLabel("Roughness fill", { exact: true })
    .fill("0.72");
  await Page.screenshot({ path: path.join(Proof, "ConstructProperties.png") });
  await Add();
  assert.equal(await Count(), 1);
  await Page.locator(
    ".inspector-scroll [data-material-channel=roughness]",
  ).click();
  assert.equal(
    await Page.locator(".inspector-scroll")
      .getByLabel("Roughness fill", { exact: true })
      .inputValue(),
    "0.72",
  );
  assert.equal(
    await Page.locator(".preview-placement.selected span").innerText(),
    "Cube 2",
  );
  await Open();
  await Choose("Cube");
  await Add();
  assert.equal(await Count(), 2);
  assert.equal(
    await Page.locator(".preview-placement.selected span").innerText(),
    "Cube 3",
  );
  await Page.getByRole("button", {
    name: "Select Cube 2 in viewport",
    exact: true,
  }).click();
  assert.match(
    await Page.locator(".outliner-row.selected").innerText(),
    /Cube 2/,
  );
  await Page.locator(".outliner-row.selected").press("F2");
  await Page.getByRole("textbox", { name: "Rename object" }).fill(
    "Preview box",
  );
  await Page.getByRole("textbox", { name: "Rename object" }).press("Enter");
  await Page.getByRole("button", {
    name: "Toggle Preview box visibility",
    exact: true,
  }).click();
  assert.equal(await Count(), 1);
  await Page.getByRole("button", {
    name: "Toggle Preview box visibility",
    exact: true,
  }).click();
  assert.equal(await Count(), 2);
  await Page.reload({ waitUntil: "networkidle" });
  assert.equal(await Count(), 2);
  assert.equal(
    await Page.getByRole("button", {
      name: "Select Preview box in viewport",
      exact: true,
    }).count(),
    1,
  );
  // Every catalogue entry must create a visible analytical symbol and select its inspector.
  for (const Name of Names.filter((Name) => Name !== "Cube")) {
    const Before = await Count();
    await Open();
    await Choose(Name);
    await Add();
    assert.equal(await Count(), Before + 1, Name);
    const Selected = await Page.locator(
      ".preview-placement.selected",
    ).getAttribute("data-preview-id");
    assert.ok(Selected);
    assert.equal(
      await Page.locator(".inspector-scroll [data-panel]").count(),
      1,
    );
  }
  assert.equal(await Count(), 24);
  await Page.reload({ waitUntil: "networkidle" });
  assert.equal(await Count(), 24);
  // Folder hiding affects symbols, and the next placement reveals its destination collection.
  await Page.getByRole("textbox", {
    name: "Search outliner",
    exact: true,
  }).fill("World");
  await Page.getByRole("button", {
    name: "Toggle World visibility",
    exact: true,
  }).click();
  assert.ok((await Count()) < 24);
  await Open();
  await Choose("Stars");
  await Add();
  assert.equal(await Count(), 25);
  // Remove and duplicate both update the same outline/preview collection.
  await Page.getByRole("textbox", {
    name: "Search outliner",
    exact: true,
  }).fill("Preview box");
  await Page.locator(".outliner-row")
    .filter({ hasText: "Preview box" })
    .click({ button: "right" });
  await Page.getByRole("menu")
    .getByRole("button", { name: "Duplicate", exact: true })
    .click();
  assert.equal(await Count(), 26);
  await Page.locator(".outliner-row.selected").click({ button: "right" });
  await Page.getByRole("menu")
    .getByRole("button", { name: "Remove from HTML scene", exact: true })
    .click();
  assert.equal(await Count(), 25);
  await Page.getByRole("textbox", {
    name: "Search outliner",
    exact: true,
  }).fill("");
  for (const Width of [1024, 1280, 1920]) {
    await Page.setViewportSize({ width: Width, height: 900 });
    await Open();
    const Extent = await Page.locator(".construct-dialog").boundingBox();
    assert.ok(Extent.x >= 0 && Extent.x + Extent.width <= Width);
    assert.equal(
      await Page.locator(".construct-results").evaluate(
        (Panel) => Panel.scrollWidth > Panel.clientWidth + 1,
      ),
      false,
    );
    await Page.getByRole("button", { name: "Cancel", exact: true }).click();
  }
  // Clean visual proof: a blank checkerboard plus a small constructed scene.
  await Page.setViewportSize({ width: 1440, height: 900 });
  await Page.evaluate(() => localStorage.clear());
  await Page.reload({ waitUntil: "networkidle" });
  for (const Name of [
    "Cube",
    "Sphere",
    "Main Camera",
    "Sun",
    "Clouds",
    "Wind",
  ]) {
    await Open();
    await Choose(Name);
    await Add();
  }
  await Page.getByRole("textbox", {
    name: "Search outliner",
    exact: true,
  }).fill("Cube 2");
  await Page.locator(".row-name").getByText("Cube 2", { exact: true }).click();
  await Page.getByRole("textbox", {
    name: "Search outliner",
    exact: true,
  }).fill("");
  await Page.waitForTimeout(3400);
  await Page.screenshot({ path: path.join(Proof, "ConstructedScene.png") });
  assert.deepEqual(Errors, []);
  console.log(
    JSON.stringify(
      {
        Success: true,
        CatalogueEntries: Names.length,
        Checks: [
          "Ctrl+A opens",
          "Shift+WASD and other modifiers do not open",
          "Shift+A not consumed",
          "Ctrl+A in text fields",
          "Escape from search/properties",
          "six categories",
          "empty search",
          "cancel without creating",
          "property edits carried into scene",
          "unique names",
          "all entries create selectable symbols",
          "rename/visibility/ancestor visibility",
          "duplicate/remove",
          "reload persistence",
          "1024/1280/1440/1920 layouts",
        ],
        Errors,
      },
      null,
      2,
    ),
  );
} catch (Error) {
  console.error(Error.stack, Errors);
  await Page.screenshot({ path: path.join(Proof, "ConstructionFailure.png") });
  process.exitCode = 1;
} finally {
  await Browser.close();
}
