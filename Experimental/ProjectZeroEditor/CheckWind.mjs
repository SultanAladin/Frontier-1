import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { ResolveWind, EvaluateWind } from "./WindSpecification.js";
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
  headless: true,
  args: ["--no-sandbox", "--disable-gpu"],
});
const Page = await Browser.newPage({
  viewport: { width: 1440, height: 960 },
  deviceScaleFactor: 1,
});
Page.setDefaultTimeout(10000);
const Errors = [],
  Checks = [];
Page.on("pageerror", (Error) => Errors.push(Error.message));
const FontFailures = [];
Page.on("console", (Message) => {
  if (Message.type() !== "error") return;
  // The reference font is an external dependency; report its availability separately from application errors.
  if (Message.location().url?.startsWith("https://cdn.fontshare.com/"))
    FontFailures.push({ Url: Message.location().url, Error: Message.text() });
  else Errors.push(Message.text());
});
const Search = Page.getByRole("textbox", {
  name: "Search outliner",
  exact: true,
});
async function Select(Name) {
  await Search.fill(Name);
  await Page.locator(".row-name").getByText(Name, { exact: true }).click();
  await Search.fill("");
  await Search.blur();
  await Page.locator(".inspector-scroll").evaluate(
    (Node) => (Node.scrollTop = 0),
  );
}
const Dialog = Page.getByRole("dialog", { name: "WindEditor", exact: true });
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
try {
  const Field = ResolveWind({
    WindField: {
      Components: [
        { Id: "a", Type: "Directional", Strength: 7, Bearing: 90 },
        { Id: "b", Type: "Directional", Strength: 5, Bearing: 90 },
      ],
    },
  });
  assert.ok(Math.abs(EvaluateWind(Field, 0, 0)[0] - 12) < 1e-8);
  assert.deepEqual(EvaluateWind({ ...Field, Components: [] }, 0, 0), [0, 0]);
  assert.ok(
    Math.abs(
      EvaluateWind(
        {
          ...Field,
          Components: Field.Components.map((Item) => ({
            ...Item,
            Enabled: false,
          })),
        },
        0,
        0,
      )[0],
    ) < 1e-8,
  );
  for (const Type of ["Tornado", "Radial", "Gust"]) {
    const Local = ResolveWind({
      WindField: {
        Components: [
          { Type, Strength: 20, Radius: 200, Frequency: 0.5, Bearing: 90 },
        ],
      },
    });
    assert.ok(EvaluateWind(Local, 0, 0).every(Number.isFinite));
    assert.deepEqual(EvaluateWind(Local, 250, 0), [0, 0]);
    assert.ok(
      EvaluateWind(Local, 60, 20).some((Value) => Math.abs(Value) > 0.1),
    );
  }
  const Gust = ResolveWind({
    WindField: {
      Components: [{ Type: "Gust", Strength: 10, Bearing: 90, Frequency: 0.5 }],
    },
  });
  assert.notDeepEqual(
    EvaluateWind(Gust, 0, 0, 0),
    EvaluateWind(Gust, 0, 0, 0.5),
  );
  Checks.push(
    "Finite local field evaluation, radial falloff, time-dependent gusts, exact linear superposition and empty/disabled fields",
  );
  await Page.goto(process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:5180/", {
    waitUntil: "networkidle",
  });
  await Page.evaluate(() => document.fonts.ready);
  await Select("Cube");
  const Cap = Page.locator(".entity-capabilities");
  assert.deepEqual(
    await Cap.locator(".quick-tile").evaluateAll((Nodes) =>
      Nodes.map((Node) => Node.getAttribute("aria-label")),
    ),
    ["Visible", "Dynamic", "Cast shadows", "GI", "Physics", "Locked"],
  );
  for (const Label of ["Dynamic", "Cast shadows", "GI", "Physics", "Locked"]) {
    const Button = Cap.getByRole("button", { name: Label, exact: true });
    const Before = await Button.getAttribute("aria-pressed");
    await Button.click();
    assert.notEqual(await Button.getAttribute("aria-pressed"), Before);
    await Button.press("Space");
    assert.equal(await Button.getAttribute("aria-pressed"), Before);
  }
  assert.equal(
    await Cap.locator(".quick-icon-seat")
      .first()
      .evaluate((Node) => getComputedStyle(Node).borderRadius),
    "50%",
  );
  await Cap.getByRole("button", { name: "Physics", exact: true }).click();
  assert.equal((await Saved()).Values.cube.DYNAMIC, true);
  await Cap.getByRole("button", { name: "Dynamic", exact: true }).click();
  assert.equal((await Saved()).Values.cube.PHYSICS, false);
  await Cap.getByRole("button", { name: "Visible", exact: true }).click();
  assert.equal((await Saved()).Hidden.cube, true);
  await Cap.getByRole("button", { name: "Visible", exact: true }).click();
  await Page.screenshot({ path: Proof + "/EntityQuickTiles.png" });
  await Select("Post Process");
  assert.equal(await Cap.locator(".quick-tile").count(), 1);
  assert.equal(
    await Cap.locator(".quick-tile").getAttribute("aria-label"),
    "Enabled",
  );
  assert.equal(await Page.locator(".instance-tags").count(), 0);
  await Select("Area Light");
  assert.equal(await Cap.locator(".quick-tile").count(), 3);
  Checks.push(
    "Entity-specific circular toggles, keyboard activation, visibility persistence and removal of irrelevant Instance tags",
  );
  await Select("Precipitation");
  assert.equal(
    await Page.locator(
      '.outliner-row.selected [data-action-icon="rain"]',
    ).count(),
    1,
  );
  await Select("Wind");
  const Mini = Page.locator(".wind-inspector canvas");
  await Page.waitForFunction(
    () => +document.querySelector(".wind-inspector canvas").dataset.frame > 2,
  );
  const Before = await Mini.evaluate((Node) => Node.toDataURL());
  await Page.waitForTimeout(350);
  assert.notEqual(await Mini.evaluate((Node) => Node.toDataURL()), Before);
  await Page.screenshot({ path: Proof + "/WindInspector.png" });
  await Page.getByRole("button", {
    name: "Expand WindEditor",
    exact: true,
  }).click();
  await Dialog.waitFor();
  await Dialog.getByRole("button", { name: "+ Tornado", exact: true }).click();
  // Signed numbers and minimum-bounded dimensions must remain typeable.
  const XInput = Dialog.getByLabel("Component X (m)", { exact: true });
  await XInput.fill("");
  await XInput.pressSequentially("-250");
  await XInput.press("Tab");
  assert.equal(await XInput.inputValue(), "-250");
  const WidthInput = Dialog.getByLabel("Field Width", { exact: true });
  await WidthInput.fill("");
  await WidthInput.pressSequentially("1200");
  await WidthInput.press("Enter");
  assert.equal(await WidthInput.inputValue(), "1200");
  await WidthInput.fill("1000");
  await WidthInput.press("Tab");

  await Dialog.getByLabel("Component Strength (m/s)", { exact: true }).fill(
    "36",
  );
  await Page.keyboard.press("Tab");
  await Dialog.getByLabel("Component Radius (m)", { exact: true }).fill("340");
  await Page.keyboard.press("Tab");
  const Map = Dialog.locator(".wind-placement"),
    Bounds = await Map.boundingBox();
  const Handle = Dialog.getByRole("button", {
    name: "Tornado 1 position",
    exact: true,
  }).locator("circle");
  const HandleBox = await Handle.boundingBox();
  await Page.mouse.move(
    HandleBox.x + HandleBox.width / 2,
    HandleBox.y + HandleBox.height / 2,
  );
  await Page.mouse.down();
  await Page.mouse.move(
    Bounds.x + Bounds.width * 0.36,
    Bounds.y + Bounds.height * 0.62,
    { steps: 10 },
  );
  await Page.mouse.up();
  assert.ok(
    Math.abs(
      +(await Dialog.getByLabel("Component X (m)", {
        exact: true,
      }).inputValue()) + 140,
    ) < 5,
  );
  assert.ok(
    Math.abs(
      +(await Dialog.getByLabel("Component Z (m)", {
        exact: true,
      }).inputValue()) - 120,
    ) < 5,
  );
  await Dialog.getByRole("button", {
    name: "Tornado 1 position",
    exact: true,
  }).focus();
  await Page.keyboard.press("ArrowRight");
  await Page.waitForFunction(
    () =>
      Math.abs(
        +document.querySelector('[aria-label="Component X (m)"]').value + 130,
      ) < 5,
  );
  assert.ok(
    Math.abs(
      +(await Dialog.getByLabel("Component X (m)", {
        exact: true,
      }).inputValue()) + 130,
    ) < 5,
  );
  const Touch = await Page.context().newCDPSession(Page);
  const TouchBox = await Handle.boundingBox(),
    TX = TouchBox.x + TouchBox.width / 2,
    TY = TouchBox.y + TouchBox.height / 2;
  await Touch.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: [{ x: TX, y: TY }],
  });
  for (let Step = 1; Step <= 8; Step++)
    await Touch.send("Input.dispatchTouchEvent", {
      type: "touchMove",
      touchPoints: [{ x: TX + Step * 5, y: TY - Step * 3 }],
    });
  await Touch.send("Input.dispatchTouchEvent", {
    type: "touchEnd",
    touchPoints: [],
  });
  await Page.waitForFunction(
    () => +document.querySelector('[aria-label="Component X (m)"]').value > -90,
  );
  await XInput.fill("-130");
  await XInput.press("Tab");
  await Dialog.getByLabel("Component Z (m)", { exact: true }).fill("120");
  await Page.keyboard.press("Tab");
  await Dialog.getByRole("button", { name: "+ Gust", exact: true }).click();
  await Dialog.getByLabel("Component Strength (m/s)", { exact: true }).fill(
    "15",
  );
  await Page.keyboard.press("Tab");
  const Canvas = Dialog.locator("canvas");
  await Dialog.getByRole("button", {
    name: "Pause preview",
    exact: true,
  }).click();
  await Page.waitForTimeout(100);
  const Paused = await Canvas.getAttribute("data-time");
  await Page.waitForTimeout(250);
  assert.equal(await Canvas.getAttribute("data-time"), Paused);
  const Combined = await Canvas.evaluate((Node) => Node.toDataURL());
  await Dialog.getByRole("button", {
    name: "Enable Tornado 1",
    exact: true,
  }).click();
  await Page.waitForTimeout(100);
  assert.notEqual(await Canvas.evaluate((Node) => Node.toDataURL()), Combined);
  await Dialog.getByRole("button", {
    name: "Enable Tornado 1",
    exact: true,
  }).click();
  await Dialog.getByRole("button", {
    name: "Select Tornado 1",
    exact: true,
  }).click();
  await Page.screenshot({ path: Proof + "/WindEditor.png" });
  await Dialog.getByRole("button", {
    name: "Resume preview",
    exact: true,
  }).click();
  if (process.env.FRONTIER_CAPTURE_WIND) {
    fs.mkdirSync(Proof + "/Frames", { recursive: true });
    for (let Index = 0; Index < 16; Index++) {
      await Page.waitForTimeout(150);
      await Dialog.locator(".wind-view-pair").screenshot({
        path: Proof + `/Frames/${String(Index).padStart(2, "0")}.png`,
      });
    }
  }
  await Dialog.getByRole("button", { name: "+ Radial", exact: true }).click();
  assert.equal(await Dialog.locator(".wind-parts>div").count(), 5);
  await Dialog.getByRole("button", {
    name: "Remove wind component",
    exact: true,
  }).click();
  assert.equal(await Dialog.locator(".wind-parts>div").count(), 4);
  Checks.push(
    "Animated selected wind card, expanding editor, component CRUD, mouse/touch and keyboard placement, pause/resume and live combined-field response",
  );
  await Dialog.getByRole("button", {
    name: "+ Wind field",
    exact: true,
  }).click();
  await Dialog.getByLabel("Wind field name", { exact: true }).fill(
    "Valley wind",
  );
  const SecondId = await Dialog.getByLabel("Edited wind field", {
    exact: true,
  }).inputValue();
  assert.notEqual(SecondId, "wind");
  assert.equal(await Dialog.locator(".wind-parts>div").count(), 2);
  await Dialog.getByRole("button", { name: "+ Tornado", exact: true }).click();
  await Dialog.getByRole("button", {
    name: "Close WindEditor",
    exact: true,
  }).click();
  await Select("Clouds");
  await Page.getByLabel("Cloud wind field", { exact: true }).selectOption(
    SecondId,
  );
  assert.equal((await Saved()).Values.clouds.WindFieldId, SecondId);
  await Page.locator('[data-card="Wind binding"]').scrollIntoViewIfNeeded();
  await Page.getByRole("button", {
    name: "Edit wind field ↗",
    exact: true,
  }).click();
  assert.equal(
    await Dialog.getByLabel("Edited wind field", { exact: true }).inputValue(),
    SecondId,
  );
  await Dialog.getByLabel("Component Strength (m/s)", { exact: true }).fill(
    "9.5",
  );
  await Page.keyboard.press("Tab");
  assert.equal(
    (await Saved()).Values[SecondId].WindField.Components[0].Strength,
    9.5,
  );
  assert.equal(
    (await Saved()).Values.clouds.WindField,
    undefined,
    "Editing a bound field must not write components to the cloud",
  );
  await Dialog.getByRole("button", {
    name: "Close WindEditor",
    exact: true,
  }).click();
  await Page.getByRole("button", { name: "Follow Wind", exact: true }).click();
  assert.equal(
    await Page.locator(".wind-binding canvas").getAttribute("data-active"),
    "false",
  );
  await Page.getByRole("button", { name: "Follow Wind", exact: true }).click();
  await Page.locator('[data-card="Wind binding"]').scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/CloudWindBinding.png" });
  await Select("Local Cloud");
  await Page.getByLabel("Cloud wind field", { exact: true }).selectOption(
    "wind",
  );
  assert.equal((await Saved()).Values["local-cloud"].WindFieldId, "wind");
  await Page.reload({ waitUntil: "networkidle" });
  await Select("Clouds");
  assert.equal(
    await Page.getByLabel("Cloud wind field", { exact: true }).inputValue(),
    SecondId,
  );
  await Select("Wind");
  await Page.getByRole("button", {
    name: "Expand WindEditor",
    exact: true,
  }).click();
  assert.equal(await Dialog.locator(".wind-parts>div").count(), 4);
  for (const Width of [1024, 1280, 1366, 1920]) {
    await Page.setViewportSize({ width: Width, height: 900 });
    await Page.waitForTimeout(120);
    assert.ok(
      await Dialog.evaluate((Node) => Node.scrollWidth <= Node.clientWidth + 1),
    );
    assert.ok(
      await Dialog.locator(".wind-editor-main").evaluate(
        (Node) => Node.scrollWidth <= Node.clientWidth + 1,
      ),
    );
  }
  await Page.keyboard.press("Escape");
  assert.equal(await Dialog.count(), 0);
  await Page.waitForFunction(
    () =>
      document.activeElement?.getAttribute("aria-label") ===
      "Expand WindEditor",
  );
  Checks.push(
    "Multiple independent wind fields, global/local cloud binding, reload persistence, Escape and 1024/1280/1366/1920 layouts",
  );
  await Select("Valley wind");
  await Page.locator(".outliner-row.selected").click({ button: "right" });
  await Page.getByRole("menu")
    .getByRole("button", { name: "Remove from HTML scene", exact: true })
    .click();
  await Select("Clouds");
  assert.match(
    await Page.getByLabel("Cloud wind field", { exact: true })
      .locator("option:checked")
      .innerText(),
    /Missing field/,
  );
  await Page.getByLabel("Cloud wind field", { exact: true }).selectOption("");
  assert.equal(await Page.locator(".wind-binding canvas").count(), 0);
  Checks.push(
    "Cloud-bound editor ownership, Follow Wind gating, missing-field handling and explicit still-air selection",
  );
  assert.deepEqual(Errors, []);
} catch (Error) {
  Errors.push(Error.stack);
  await Page.screenshot({ path: Proof + "/WindFailure.png" }).catch(() => {});
  process.exitCode = 1;
} finally {
  fs.writeFileSync(
    Proof + "/Wind.json",
    JSON.stringify({ Checks, Errors, FontFailures }, null, 2),
  );
  console.log(JSON.stringify({ Checks, Errors, FontFailures }, null, 2));
  await Browser.close();
}
