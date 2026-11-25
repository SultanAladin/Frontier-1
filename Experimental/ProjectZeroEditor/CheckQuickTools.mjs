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
  viewport: { width: 1366, height: 768 },
  deviceScaleFactor: 1,
});
Page.setDefaultTimeout(10000);
const Errors = [],
  Actions = [];
Page.on("pageerror", (Error) => Errors.push(Error.message));
Page.on("console", (Message) => {
  if (Message.type() === "error") Errors.push(Message.text());
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
const Symbols = new Set([
  "visible",
  "sunlight",
  "disc",
  "stars",
  "twinkle",
  "cycle",
  "motion",
  "pause",
  "orbit",
  "wind",
  "shear",
  "cloud",
  "cloud-spawn",
  "rain",
  "collision",
  "fog",
  "rainbow",
  "band",
  "bake",
  "image",
  "anamorphic",
  "streaks",
  "starburst",
]);
try {
  await Page.goto(process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:5180/", {
    waitUntil: "networkidle",
  });
  await Page.evaluate(() => document.fonts.ready);
  for (const Name of [
    "Sun",
    "Stars",
    "Moons",
    "Lens Flare",
    "Wind",
    "Precipitation",
    "Rainbow",
    "Clouds",
    "Local Cloud",
    "Height Fog",
    "Atmospheric Fog",
    "Local Fog",
  ]) {
    await Select(Name);
    const Tiles = await Page.locator(".inspector-scroll .quick-tile").all();
    assert.ok(Tiles.length, Name + " has quick actions");
    for (const Tile of Tiles) {
      const Label = await Tile.getAttribute("data-quick-action");
      const Icon = Tile.locator(".action-icon");
      const Symbol = await Icon.getAttribute("data-action-icon");
      assert.ok(
        Symbols.has(Symbol),
        Name + ": " + Label + " needs a semantic icon",
      );
      assert.equal(await Icon.getAttribute("fill"), "currentColor");
      assert.equal(await Icon.getAttribute("stroke"), "none");
      assert.ok((await Icon.boundingBox()).width >= 25);
      if (await Tile.isDisabled()) {
        assert.equal(
          await Tile.locator(".quick-status").innerText(),
          "UNAVAILABLE",
        );
      } else {
        const Before = await Tile.getAttribute("aria-pressed");
        await Tile.click();
        assert.notEqual(
          await Tile.getAttribute("aria-pressed"),
          Before,
          Label + " toggles",
        );
        assert.equal(
          await Tile.locator(".quick-status").innerText(),
          Before === "true" ? "OFF" : "ON",
        );
        await Tile.press("Space");
        assert.equal(
          await Tile.getAttribute("aria-pressed"),
          Before,
          Label + " toggles by keyboard",
        );
      }
      Actions.push({ Panel: Name, Label, Symbol });
    }
    // A quick-action group must not collapse back to repeated power/check symbols.
    for (const Group of await Page.locator(
      ".inspector-scroll .tiles, .moon-settings-tiles",
    ).all()) {
      const Names = await Group.locator("[data-action-icon]").evaluateAll(
        (Nodes) => Nodes.map((Node) => Node.dataset.actionIcon),
      );
      assert.equal(
        new Set(Names).size,
        Names.length,
        Name + " needs distinct group icons",
      );
    }
  }
  const Frame = Page.getByRole("button", {
    name: "Frame selected",
    exact: true,
  });
  assert.ok(
    await Frame.isDisabled(),
    "Reference rows cannot frame a constructed marker",
  );
  const Projection = Page.getByRole("combobox", {
    name: "Viewport projection",
  });
  const Split = Page.getByRole("button", {
    name: "Split viewport",
    exact: true,
  });
  await Projection.selectOption("ORTHO");
  await Split.click();
  assert.equal(await Page.locator(".checker-pane").count(), 2);
  assert.equal(await Projection.inputValue(), "ORTHO");
  await Projection.selectOption("TOP");
  assert.equal(await Split.getAttribute("aria-pressed"), "true");
  await Split.click();
  assert.equal(await Page.locator(".checker-pane").count(), 1);
  assert.equal(await Projection.inputValue(), "TOP");
  await Projection.selectOption("PERSP");
  const Diagnostics = Page.getByRole("button", {
    name: "Viewport diagnostics",
    exact: true,
  });
  await Diagnostics.click();
  assert.equal(await Diagnostics.getAttribute("aria-pressed"), "true");
  assert.equal(await Page.locator(".diagnostic-overlay").count(), 1);
  await Page.keyboard.press("F3");
  assert.equal(await Diagnostics.getAttribute("aria-pressed"), "true");
  await Page.keyboard.press("Shift+F3");
  await Page.keyboard.press("Shift+F3");
  assert.equal(await Diagnostics.getAttribute("aria-pressed"), "false");
  assert.equal(await Page.locator(".diagnostic-overlay").count(), 0);
  for (const Mode of ["Sim", "Play", "Edit"]) {
    await Page.getByRole("button", {
      name: Mode + " mode",
      exact: true,
    }).click();
    assert.equal(
      await Page.locator('.viewport-modes button[aria-pressed="true"]').count(),
      1,
    );
  }
  await Page.getByRole("button", {
    name: "Viewport settings",
    exact: true,
  }).click();
  await Page.getByRole("dialog", {
    name: "Viewport settings",
    exact: true,
  }).waitFor();
  await Page.getByLabel("Viewport debug view", { exact: true }).selectOption(
    "6",
  );
  assert.equal(await Diagnostics.getAttribute("aria-pressed"), "true");
  assert.equal(
    await Page.locator(".notch-handle").getAttribute("aria-expanded"),
    "false",
  );
  await Page.getByLabel("Viewport debug view", { exact: true }).selectOption(
    "0",
  );
  await Page.keyboard.press("Escape");
  assert.equal(
    await Page.getByRole("dialog", {
      name: "Viewport settings",
      exact: true,
    }).count(),
    0,
  );
  await Page.getByRole("button", { name: "Construct", exact: true }).click();
  await Page.getByRole("textbox", {
    name: "Search construct",
    exact: true,
  }).fill("Cube");
  await Page.locator(".construct-entity").click();
  await Page.getByRole("button", { name: "Add to scene", exact: true }).click();
  assert.ok(await Frame.isEnabled());
  await Frame.click();
  assert.match(
    await Page.locator(".viewport-identity").innerText(),
    /1 constructed/,
  );
  await Page.locator(".toast").waitFor({ state: "hidden" });
  for (const Width of [1024, 1280, 1366, 1440, 1920]) {
    await Page.setViewportSize({
      width: Width,
      height: Width === 1366 ? 608 : 900,
    });
    await Page.waitForFunction(
      () =>
        Math.abs(
          document.querySelector(".notch-handle").getBoundingClientRect().top,
        ) < 1,
    );
    const Layout = await Page.locator(".viewport-toolbar").evaluate(
      (Toolbar) => {
        const Bounds = Toolbar.getBoundingClientRect();
        return {
          Overflow: Toolbar.scrollWidth > Toolbar.clientWidth + 1,
          Missing: [...Toolbar.querySelectorAll("button, select")]
            .filter((Node) => {
              const Box = Node.getBoundingClientRect();
              return (
                Box.width === 0 ||
                Box.height === 0 ||
                Box.left < Bounds.left ||
                Box.right > Bounds.right + 1 ||
                Box.bottom > Bounds.bottom + 1 ||
                !Node.contains(
                  document.elementFromPoint(
                    Box.x + Box.width / 2,
                    Box.y + Box.height / 2,
                  ),
                )
              );
            })
            .map((Node) => Node.getAttribute("aria-label")),
        };
      },
    );
    assert.equal(Layout.Overflow, false, Width + " toolbar overflow");
    assert.deepEqual(
      Layout.Missing,
      [],
      Width + " clipped or missing controls",
    );
    await Page.screenshot({
      path: path.join(Proof, "Toolbar" + Width + ".png"),
    });
  }
  await Page.setViewportSize({ width: 1366, height: 768 });
  await Select("Local Fog");
  await Page.locator(".outliner-row.selected").evaluateAll((Nodes) =>
    Nodes.forEach((Node) => Node.scrollIntoView({ block: "nearest" })),
  );
  await Page.waitForFunction(
    () =>
      Math.abs(
        document.querySelector(".notch-handle").getBoundingClientRect().top,
      ) < 1,
  );
  await Page.screenshot({ path: path.join(Proof, "QuickTilesFog.png") });
  await Select("Stars");
  await Page.getByRole("button", { name: "Twinkle", exact: true }).click();
  await Page.locator(".outliner-row.selected").scrollIntoViewIfNeeded();
  await Page.screenshot({ path: path.join(Proof, "QuickTilesStars.png") });
  assert.deepEqual(Errors, []);
  const Result = {
    Actions,
    Checks: [
      "12 inspector sheets with filled semantic icons",
      "distinct icons per quick group",
      "ON/OFF/unavailable states",
      "click and Space toggle",
      "independent projection and split",
      "diagnostics and F3 state",
      "three exclusive mode selectors",
      "viewport settings/debug routing (not Control Centre)",
      "Construct and frame eligibility",
      "1024/1280/1366/1440/1920 toolbar layouts",
    ],
    Errors,
  };
  fs.writeFileSync(
    path.join(Proof, "QuickTools.json"),
    JSON.stringify(Result, null, 2),
  );
  console.log(JSON.stringify(Result, null, 2));
} finally {
  await Browser.close();
}
