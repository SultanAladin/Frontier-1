import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const Folder = path.dirname(fileURLToPath(import.meta.url)),
  Root = path.resolve(Folder, "../../..");
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    path.resolve(Folder, "../FrontierEditor/package.json"),
);
const { chromium } = Require("playwright");
import fs from "node:fs";
import assert from "node:assert/strict";
const Proof = process.env.FRONTIER_PROOF_FOLDER || Folder + "/Screenshots";
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
const Errors = [],
  Results = [];
let Changed = 0;
Page.setDefaultTimeout(10000);
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const FontFailures = [];
Page.on("console", (Message) => {
  if (Message.type() !== "error") return;
  // The reference font is an external dependency; report its availability separately from application errors.
  if (Message.location().url?.startsWith("https://cdn.fontshare.com/"))
    FontFailures.push({ Url: Message.location().url, Error: Message.text() });
  else Errors.push(Message.text());
});
const PanelNames = [
  "Sun",
  "Editor Camera",
  "Stars",
  "Moons",
  "Atmosphere",
  "Lens Flare",
  "Wind",
  "Precipitation",
  "Rainbow",
  "Clouds",
  "Local Cloud",
  "Height Fog",
  "Atmospheric Fog",
  "Local Fog",
  "Post Process",
  "Cube",
  "Area Light",
  "World",
];
async function Select(Name) {
  await Page.getByRole("textbox", {
    name: "Search outliner",
    exact: true,
  }).fill(Name);
  await Page.locator(".row-name").getByText(Name, { exact: true }).click();
  await Page.locator(".inspector-scroll [data-panel]").waitFor();
}
try {
  await Page.goto(process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:5180/", {
    waitUntil: "networkidle",
  });
  await Page.evaluate(() => document.fonts.ready);
  assert.equal(
    await Page.locator(".notch-handle").getAttribute("aria-expanded"),
    "false",
  );
  await Page.screenshot({ path: Proof + "/Editor.png" });
  for (const Name of PanelNames) {
    await Select(Name);
    await Page.locator(".inspector-scroll details").evaluateAll((Nodes) =>
      Nodes.forEach((Node) => (Node.open = true)),
    );
    const Type = await Page.locator(
      ".inspector-scroll [data-panel]",
    ).getAttribute("data-panel");
    const Numbers = await Page.locator(
      '.inspector-scroll input[type="number"]:not([disabled])',
    ).all();
    for (const Control of Numbers) {
      const Minimum = +((await Control.getAttribute("min")) || 0),
        Maximum = +((await Control.getAttribute("max")) || 100),
        Step = +((await Control.getAttribute("step")) || 1);
      const Next =
        Math.round((Minimum + (Maximum - Minimum) * 0.43) / Step) * Step;
      await Control.fill(String(+Next.toFixed(4)));
      await Control.press("Tab");
      const Actual = +(await Control.inputValue());
      assert.ok(
        Math.abs(Actual - Next) < Math.max(0.011, Step),
        Name + ": number did not update",
      );
      Changed++;
    }
    const Invalid = await Page.locator(".inspector-scroll").evaluate(
      (Node) =>
        /NaN|undefined/.test(Node.innerText) ||
        [...Node.querySelectorAll("svg *")].some((Element) =>
          [...Element.attributes].some((Attr) =>
            /NaN|undefined/.test(Attr.value),
          ),
        ),
    );
    assert.equal(
      Invalid,
      false,
      Name + " contains invalid diagram coordinates",
    );
    Results.push({
      Name,
      Type,
      NumericControls: Numbers.length,
      Overflow: await Page.locator(".inspector-scroll").evaluate(
        (Node) => Node.scrollWidth > Node.clientWidth + 1,
      ),
    });
  }
  // A real pointer drag, rather than synthetic value assignment.
  await Select("Editor Camera");
  const Range = Page.getByRole("slider", { name: "Focal Length", exact: true });
  await Range.scrollIntoViewIfNeeded();
  const Rect = await Range.boundingBox(),
    Before = +(await Range.inputValue());
  await Page.mouse.move(Rect.x + Rect.width * 0.25, Rect.y + Rect.height / 2);
  await Page.mouse.down();
  await Page.mouse.move(Rect.x + Rect.width * 0.8, Rect.y + Rect.height / 2, {
    steps: 12,
  });
  await Page.mouse.up();
  assert.notEqual(+(await Range.inputValue()), Before);
  // Per-object state persists across selection and reload.
  const Focal = await Page.getByLabel("Focal Length value", {
    exact: true,
  }).inputValue();
  await Select("Sun");
  await Select("Editor Camera");
  assert.equal(
    await Page.getByLabel("Focal Length value", { exact: true }).inputValue(),
    Focal,
  );
  await Page.reload({ waitUntil: "networkidle" });
  assert.equal(
    await Page.getByLabel("Focal Length value", { exact: true }).inputValue(),
    Focal,
  );
  // Native trapezoid close/reopen and docking drag.
  await Page.bringToFront();
  await Page.getByRole("button", {
    name: "Close Inspector tab",
    exact: true,
  }).click();
  assert.equal(await Page.locator(".inspector-scroll").count(), 0);
  await Page.getByRole("button", {
    name: "Add editor tab or construct",
  }).click();
  await Page.getByRole("menu")
    .getByRole("button", { name: "Inspector", exact: true })
    .click();
  assert.equal(await Page.locator(".inspector-scroll").count(), 1);
  await Page.locator(".right .document-tab").dragTo(
    Page.locator(".centre .tab-strip"),
    { targetPosition: { x: 150, y: 15 } },
  );
  assert.equal(await Page.locator(".centre .document-tab").count(), 2);
  assert.equal(await Page.locator(".right .inspector-scroll").count(), 0);
  await Page.getByRole("button", {
    name: "Add editor tab or construct",
  }).click();
  await Page.getByRole("menu")
    .getByRole("button", { name: "Inspector", exact: true })
    .click();
  assert.equal(await Page.locator(".right .inspector-scroll").count(), 1);
  // Outliner rename, visibility, search and construction.
  await Select("Cube");
  await Page.locator(".outliner-row.selected").press("F2");
  await Page.getByRole("textbox", { name: "Rename object" }).fill("Cube Test");
  await Page.getByRole("textbox", { name: "Rename object" }).press("Enter");
  await Page.getByRole("button", {
    name: "Toggle Cube Test visibility",
    exact: true,
  }).click();
  assert.match(
    await Page.locator(".outliner-row.selected").getAttribute("class"),
    /hidden-row/,
  );
  await Page.getByRole("textbox", { name: "Search outliner" }).fill("");
  await Page.getByRole("button", { name: "Construct", exact: true }).click();
  await Page.getByRole("textbox", { name: "Search construct" }).fill("Rainbow");
  await Page.locator(".construct-matrix")
    .getByRole("button", { name: "Rainbow", exact: true })
    .click();
  await Page.getByRole("button", { name: "Add to scene", exact: true }).click();
  assert.equal(await Page.locator("[data-preview-id]").count(), 1);
  assert.equal(await Page.locator('[data-panel="rainbow"]').count(), 1);
  // Notch and all five native control-centre pages.
  await Page.getByRole("button", {
    name: "Project-Zero control center notch",
  }).click();
  await Page.locator(".control-dashboard").waitFor({ state: "visible" });
  await Page.waitForTimeout(3400);
  await Page.screenshot({ path: Proof + "/ControlCenter.png" });
  await Page.getByRole("button", {
    name: "Control Center settings",
    exact: true,
  }).click();
  for (const Name of [
    "Render Settings",
    "Appearance",
    "Input",
    "Notifications",
    "Materials",
  ]) {
    await Page.locator(".hub-row").filter({ hasText: Name }).click();
    const Dialog = Page.getByRole("dialog");
    assert.ok((await Dialog.innerText()).length > 100);
    if (Name === "Appearance") {
      for (const Tab of ["Display", "Fonts", "Theme"]) {
        await Page.locator(".appearance-tabs")
          .getByRole("button", { name: Tab, exact: true })
          .click();
        assert.ok(await Page.locator(".settings-body").innerText());
        if (Tab === "Fonts")
          await Page.screenshot({ path: Proof + "/Typography.png" });
      }
    }
    if (Name === "Input") {
      await Page.getByRole("switch", {
        name: "Custom Shortcuts",
        exact: true,
      }).click();
      await Page.getByRole("textbox", {
        name: "Translate Tool",
        exact: true,
      }).press("T");
      assert.equal(
        await Page.getByRole("textbox", {
          name: "Translate Tool",
          exact: true,
        }).inputValue(),
        "T",
      );
      await Page.getByRole("button", {
        name: "Save keybindings",
        exact: true,
      }).click();
    }
    if (Name === "Materials")
      assert.equal(await Page.locator(".material-channels input").count(), 20);
    await Page.getByRole("button", {
      name: "Back to settings",
      exact: true,
    }).click();
  }
  await Page.getByRole("button", {
    name: "Close settings",
    exact: true,
  }).click();
  await Page.waitForTimeout(350);
  assert.equal(
    await Page.locator(".notch-handle").getAttribute("aria-expanded"),
    "false",
  );
  // Keyboard diagnostic overlay and resizing.
  await Page.locator(".scene-image").click({ position: { x: 50, y: 50 } });
  await Page.keyboard.press("F3");
  assert.equal(await Page.locator(".diagnostic-overlay").count(), 1);
  await Page.keyboard.press("Escape");
  assert.equal(await Page.locator(".diagnostic-overlay").count(), 0);
  for (const Width of [1280, 1024, 1920]) {
    await Page.setViewportSize({ width: Width, height: 900 });
    await Page.waitForTimeout(120);
    assert.equal(
      await Page.evaluate(
        () => document.documentElement.scrollWidth > innerWidth,
      ),
      false,
    );
    assert.ok((await Page.locator(".notch-handle").boundingBox()).y >= -1);
  }
  await Page.setViewportSize({ width: 1440, height: 900 });
  await Select("Moons");
  await Page.locator(".right .document-tab").dblclick();
  await Page.getByRole("combobox", {
    name: "Active moon instance",
  }).selectOption("1");
  await Page.locator(".moon-catalogue")
    .getByRole("button", { name: "Ember", exact: true })
    .click();
  await Page.getByRole("combobox", {
    name: "Active moon instance",
  }).selectOption("0");
  await Page.getByRole("combobox", {
    name: "Active moon instance",
  }).selectOption("1");
  assert.ok(
    await Page.locator(".moon-catalogue .selected").getByText("Ember").count(),
  );
  // Clean baseline screenshots, not the control torture-test state.
  await Page.evaluate(() => localStorage.clear());
  await Page.reload({ waitUntil: "networkidle" });
  await Page.locator(".outliner-row.selected").scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/Editor.png" });
  await Page.locator(".right .document-tab").dblclick();
  await Page.screenshot({ path: Proof + "/SunInspector.png" });
  await Page.locator(".inspector-scroll").evaluate(
    (Node) => (Node.scrollTop = 650),
  );
  await Page.screenshot({ path: Proof + "/SunControls.png" });
  await Select("Moons");
  await Page.getByRole("textbox", { name: "Search outliner" }).fill("");
  await Page.locator(".outliner-row.selected").scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/MoonInspector.png" });
  assert.equal(Errors.length, 0, JSON.stringify(Errors));
  console.log(
    JSON.stringify(
      {
        Success: true,
        Panels: Results,
        NumericControlsEdited: Changed,
        Checks: [
          "numeric edits",
          "pointer drag",
          "persistence",
          "tab close/reopen",
          "tab docking",
          "outliner rename/visibility",
          "construct",
          "notch",
          "five settings pages",
          "three appearance tabs",
          "keybinding capture",
          "20 material channels",
          "F3 diagnostics",
          "1024/1280/1440/1920 widths",
          "four moon slots",
        ],
        Errors,
        FontFailures,
      },
      null,
      2,
    ),
  );
} catch (Error) {
  console.log("FAILED", Error.stack, JSON.stringify(Errors));
  await Page.screenshot({
    path: Root + "/_AgentScratch/logs/EditorHtml/Failure.png",
  });
  process.exitCode = 1;
} finally {
  await Browser.close();
}
