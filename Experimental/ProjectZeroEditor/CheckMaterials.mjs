import path from "node:path";
import fs from "node:fs";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { MaterialChannels, MaterialTypes } from "./MaterialSpecification.js";
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
Page.setDefaultTimeout(12000);
const Errors = [];
Page.on("pageerror", (Error) => Errors.push(Error.message));
Page.on("console", (Message) => {
  if (Message.type() === "error") Errors.push(Message.text());
});
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
async function Select(Name) {
  await Page.getByLabel("Search outliner", { exact: true }).fill(Name);
  await Page.locator(".row-name").getByText(Name, { exact: true }).click();
  await Page.getByLabel("Search outliner", { exact: true }).fill("");
  await Page.getByLabel("Search outliner", { exact: true }).blur();
}
const Window = Page.getByRole("dialog", {
  name: "ShaderEditor window",
  exact: true,
});
const Channel = async (Id) =>
  Window.locator(`[data-material-channel="${Id}"]`).click();
const Mode = async (Name) =>
  Window.getByRole("button", { name: Name, exact: true }).click();
const Canvas = () => Window.locator("canvas");
async function PreviewChanged(Before) {
  await Page.waitForFunction((Before) => {
    const Canvas = document.querySelector(".shader-window canvas");
    return Canvas && Canvas.toDataURL() !== Before;
  }, Before);
}
try {
  await Page.goto(process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:5180/", {
    waitUntil: "networkidle",
  });
  await Page.evaluate(() => document.fonts.ready);
  await Select("Cube");
  assert.equal(await Page.locator(".transform-table tbody tr").count(), 3);
  for (const [Key, Value] of [
    ["Position X", "-1.25"],
    ["Position Y", "2.5"],
    ["Rotation Z", "45"],
    ["Scale Y", "1.5"],
  ]) {
    await Page.getByLabel(Key, { exact: true }).fill(Value);
    await Page.getByLabel(Key, { exact: true }).press("Tab");
    assert.equal(
      await Page.getByLabel(Key, { exact: true }).inputValue(),
      Value,
    );
  }
  const Position = Page.getByLabel("Position Z", { exact: true }),
    Box = await Position.boundingBox();
  await Page.mouse.move(Box.x + Box.width / 2, Box.y + Box.height / 2);
  await Page.mouse.down();
  await Page.mouse.move(Box.x + Box.width / 2 + 30, Box.y + Box.height / 2, {
    steps: 8,
  });
  await Page.mouse.up();
  assert.ok(+(await Position.inputValue()) > 0.1);
  await Page.getByRole("button", {
    name: "Reset Rotation",
    exact: true,
  }).click();
  assert.equal(
    await Page.getByLabel("Rotation Z", { exact: true }).inputValue(),
    "0",
  );
  await Page.getByRole("button", { name: "Locked", exact: true }).click();
  assert.ok(await Position.isDisabled());
  await Page.getByRole("button", { name: "Locked", exact: true }).click();
  await Page.locator(".inspector-scroll").evaluate(
    (Node) => (Node.scrollTop = 0),
  );
  await Page.screenshot({ path: path.join(Proof, "TransformTable.png") });
  await Page.getByRole("button", {
    name: "Expand ShaderEditor",
    exact: true,
  }).click();
  await Window.locator('canvas[data-preview-status="Preview ready"]').waitFor();
  assert.equal(await Window.locator("[data-material-channel]").count(), 20);
  const FillBefore = await Canvas().evaluate((Node) => Node.toDataURL());
  await Window.getByLabel("Base colour fill hex", { exact: true }).fill(
    "#64bb96",
  );
  await Window.getByLabel("Base colour fill hex", { exact: true }).press("Tab");
  await PreviewChanged(FillBefore);
  const MaterialId = (await Saved()).Values.cube.Material;
  assert.equal(MaterialId.Channels.colour.Fill, "#64bb96");
  // A native descriptor is authored against the pinned target, not the changing scene selection.
  await Select("Sphere");
  assert.equal(
    await Window.locator("[data-material-owner]").getAttribute(
      "data-material-owner",
    ),
    "cube",
  );
  await Window.getByLabel("Material name", { exact: true }).fill(
    "Brushed jade",
  );
  assert.equal((await Saved()).Values.cube.Material.Name, "Brushed jade");
  assert.equal((await Saved()).Values.sphere?.Material, undefined);
  for (const Type of MaterialTypes) {
    await Window.getByLabel("Material type", { exact: true }).selectOption(
      Type,
    );
    assert.equal((await Saved()).Values.cube.Material.Type, Type);
  }
  await Window.getByLabel("Material type", { exact: true }).selectOption(
    "Metal",
  );
  // Every channel exposes four mutually exclusive sources, with a correctly typed fill.
  for (const Field of MaterialChannels) {
    await Channel(Field.Id);
    assert.equal(
      await Window.locator('.source-tabs button[aria-pressed="true"]').count(),
      1,
    );
    assert.equal(await Window.locator(".source-tabs button").count(), 4);
    assert.equal(
      await Window.getByLabel(Field.Label + " fill", {
        exact: true,
      }).getAttribute("type"),
      Field.Kind === "Colour" ? "color" : "number",
    );
  }
  await Window.getByLabel("Search material channels", { exact: true }).fill(
    "no such channel",
  );
  await Window.getByText("No matching channels.", { exact: true }).waitFor();
  await Window.getByLabel("Search material channels", { exact: true }).fill(
    "roughness",
  );
  assert.equal(await Window.locator("[data-material-channel]").count(), 3);
  await Window.getByLabel("Search material channels", { exact: true }).fill("");
  await Channel("anisotropy");
  const Signed = Window.getByLabel("Anisotropy fill", { exact: true });
  await Signed.focus();
  await Signed.press("Control+a");
  await Signed.pressSequentially("-.25");
  await Signed.press("Tab");
  assert.equal(
    await Signed.inputValue(),
    "-0.25",
    "Signed scalar typing must preserve minus",
  );
  await Channel("ior");
  await Window.getByLabel("IOR (refraction) fill", { exact: true }).fill(
    "1.72",
  );
  await Channel("reflectance");
  assert.equal(
    await Window.getByLabel("Reflectance / IOR fill", {
      exact: true,
    }).inputValue(),
    "1.72",
  );
  await Channel("colour");
  await Mode("Gradient");
  await Mode("+ Add stop");
  assert.equal(await Window.locator(".gradient-track button").count(), 3);
  await Window.getByLabel("Stop value hex", { exact: true }).fill("#74b79a");
  await Window.getByLabel("Stop value hex", { exact: true }).press("Tab");
  await Window.getByLabel("Stop position", { exact: true }).fill("38");
  await Window.getByLabel("Gradient axis", { exact: true }).selectOption("V");
  const Stop = Window.getByRole("button", {
    name: "Gradient stop 3",
    exact: true,
  });
  await Stop.focus();
  await Stop.press("ArrowRight");
  assert.equal(
    await Window.getByLabel("Stop position", { exact: true }).inputValue(),
    "39",
  );
  const Track = await Window.locator(".gradient-track").boundingBox();
  await Stop.hover();
  await Page.mouse.down();
  await Page.mouse.move(Track.x + Track.width * 0.6, Track.y + Track.height, {
    steps: 8,
  });
  await Page.mouse.up();
  assert.ok(
    +(await Window.getByLabel("Stop position", { exact: true }).inputValue()) >
      50,
  );
  for (let Index = 3; Index < 8; Index++) await Mode("+ Add stop");
  assert.ok(
    await Window.getByRole("button", {
      name: "+ Add stop",
      exact: true,
    }).isDisabled(),
  );
  for (let Index = 8; Index > 3; Index--) await Mode("Remove stop");
  const Gradient = (await Saved()).Values.cube.Material.Channels.colour.Stops;
  await Mode("Fill");
  assert.equal(
    await Window.getByLabel("Base colour fill hex", {
      exact: true,
    }).inputValue(),
    "#64bb96",
  );
  await Mode("Gradient");
  assert.deepEqual(
    (await Saved()).Values.cube.Material.Channels.colour.Stops,
    Gradient,
  );
  await Window.locator(".channel-editor").evaluate(
    (Node) => (Node.scrollTop = 0),
  );
  await Page.waitForTimeout(250);
  await Page.screenshot({ path: path.join(Proof, "ShaderGradient.png") });
  // Texture import is embedded as a bounded preview and changes actual ball pixels.
  const Texture = await Page.evaluate(() => {
    const Canvas = document.createElement("canvas");
    Canvas.width = Canvas.height = 32;
    const Context = Canvas.getContext("2d");
    Context.fillStyle = "#ff321e";
    Context.fillRect(0, 0, 32, 32);
    Context.fillStyle = "#aaff66";
    for (let X = 0; X < 32; X += 8) Context.fillRect(X, 0, 4, 32);
    return Canvas.toDataURL().split(",")[1];
  });
  await Mode("Texture");
  const BeforeTexture = await Canvas().evaluate((Node) => Node.toDataURL());
  await Window.getByLabel("Upload channel texture", {
    exact: true,
  }).setInputFiles({
    name: "StripeStudy.png",
    mimeType: "image/png",
    buffer: Buffer.from(Texture, "base64"),
  });
  await Window.getByAltText("StripeStudy.png").waitFor();
  await PreviewChanged(BeforeTexture);
  await Window.getByLabel("Texture repeat", { exact: true }).fill("2");
  assert.equal(
    (await Saved()).Values.cube.Material.Channels.colour.Texture.Name,
    "StripeStudy.png",
  );
  await Page.screenshot({ path: path.join(Proof, "ShaderTexture.png") });
  await Window.getByLabel("Upload channel texture", {
    exact: true,
  }).setInputFiles({
    name: "Invalid.png",
    mimeType: "image/png",
    buffer: Buffer.from("Not an image"),
  });
  await Window.getByText(
    "Image could not be decoded. The previous texture is unchanged.",
    { exact: true },
  ).waitFor();
  assert.equal(
    (await Saved()).Values.cube.Material.Channels.colour.Texture.Name,
    "StripeStudy.png",
  );
  await Mode("Code link");
  await Window.getByLabel("Code provider", { exact: true }).selectOption(
    "Slang / HLSL",
  );
  await Window.getByLabel("Code symbol", { exact: true }).fill(
    "Vehicle.Paint.BaseColour",
  );
  await Window.getByLabel("Channel code", { exact: true }).fill(
    "// Native producer descriptor only; not executed in this HTML preview.\nreturn PaintColour;",
  );
  assert.match(
    await Window.locator(".code-warning").innerText(),
    /Nothing is compiled or executed/,
  );
  await Page.screenshot({ path: path.join(Proof, "ShaderCodeLink.png") });
  await Mode("Texture");
  assert.equal(await Window.locator(".texture-drop img").count(), 1);
  await Mode("Fill");
  await Channel("roughness");
  await Mode("Gradient");
  await Window.getByLabel("Stop value", { exact: true }).fill("0.17");
  await Mode("+ Add stop");
  assert.equal(
    (await Saved()).Values.cube.Material.Channels.roughness.Stops.length,
    3,
  );
  // Floating drag, docking, native tab drag, undock and narrow layouts.
  const Title = await Window.locator(".shader-window-title").boundingBox(),
    BeforeWindow = await Window.boundingBox();
  await Page.mouse.move(Title.x + 190, Title.y + 20);
  await Page.mouse.down();
  await Page.mouse.move(Title.x + 150, Title.y + 25, { steps: 6 });
  await Page.mouse.up();
  assert.notEqual((await Window.boundingBox()).x, BeforeWindow.x);
  const BeforeResize = await Window.boundingBox();
  await Page.mouse.move(
    BeforeResize.x + BeforeResize.width - 3,
    BeforeResize.y + BeforeResize.height - 3,
  );
  await Page.mouse.down();
  await Page.mouse.move(
    BeforeResize.x + BeforeResize.width - 113,
    BeforeResize.y + BeforeResize.height - 43,
    { steps: 8 },
  );
  await Page.mouse.up();
  assert.ok(
    (await Window.boundingBox()).width < BeforeResize.width - 80,
    "Floating window resizes",
  );
  await Window.getByRole("button", {
    name: "Dock ShaderEditor Centre",
    exact: true,
  }).click();
  assert.equal(await Window.count(), 0);
  assert.equal(
    await Page.locator(".centre [data-material-owner=cube]").count(),
    1,
  );
  await Page.locator(".centre .document-tab")
    .filter({ hasText: "ShaderEditor" })
    .dragTo(Page.locator(".right .tab-strip"), {
      targetPosition: { x: 160, y: 15 },
    });
  assert.equal(
    await Page.locator(".right [data-material-owner=cube]").count(),
    1,
  );
  await Page.getByRole("button", {
    name: "Undock ShaderEditor",
    exact: true,
  }).click();
  await Window.waitFor();
  await Window.getByRole("button", {
    name: "Dock ShaderEditor Left",
    exact: true,
  }).click();
  assert.equal(
    await Page.locator(".left [data-material-owner=cube]").count(),
    1,
  );
  await Page.getByRole("button", {
    name: "Undock ShaderEditor",
    exact: true,
  }).click();
  await Window.waitFor();
  for (const Width of [1024, 1280, 1366, 1440, 1920]) {
    await Page.setViewportSize({
      width: Width,
      height: Width === 1366 ? 768 : 900,
    });
    await Page.waitForTimeout(120);
    const Bounds = await Window.boundingBox();
    assert.ok(Bounds.x >= 0 && Bounds.x + Bounds.width <= Width + 1);
    const Overflow = await Window.evaluate((Node) =>
      [
        ...Node.querySelectorAll(
          ".material-panel,.channel-editor,.shader-document-body",
        ),
      ].some((Item) => Item.scrollWidth > Item.clientWidth + 2),
    );
    assert.equal(Overflow, false, Width + " horizontal overflow");
  }
  await Page.setViewportSize({ width: 1440, height: 900 });
  await Window.getByRole("button", {
    name: "Dock ShaderEditor Centre",
    exact: true,
  }).click();
  await Page.locator(
    '.centre canvas[data-preview-status="Preview ready"]',
  ).waitFor();
  const Overlap = await Page.locator(".centre .material-workbench").evaluate(
    (Node) => {
      const Boxes = [...Node.children].map((Item) =>
        Item.getBoundingClientRect(),
      );
      return Boxes.some((A, I) =>
        Boxes.slice(I + 1).some(
          (B) =>
            Math.min(A.right, B.right) - Math.max(A.left, B.left) > 1 &&
            Math.min(A.bottom, B.bottom) - Math.max(A.top, B.top) > 1,
        ),
      );
    },
  );
  assert.equal(Overlap, false, "Docked cards must not overlap");
  await Page.screenshot({ path: path.join(Proof, "ShaderDocked.png") });
  await Page.getByRole("button", {
    name: "Close ShaderEditor tab",
    exact: true,
  }).click();
  await Select("Cube");
  assert.equal(
    await Page.getByLabel("Position X", { exact: true }).inputValue(),
    "-1.25",
  );
  const BeforeReload = (await Saved()).Values.cube.Material;
  await Page.reload({ waitUntil: "networkidle" });
  assert.deepEqual((await Saved()).Values.cube.Material, BeforeReload);
  await Page.getByRole("button", {
    name: "Expand ShaderEditor",
    exact: true,
  }).click();
  await Channel("colour");
  await Mode("Code link");
  assert.equal(
    await Window.getByLabel("Code symbol", { exact: true }).inputValue(),
    "Vehicle.Paint.BaseColour",
  );
  await Mode("Texture");
  await Window.getByRole("button", {
    name: "Remove texture",
    exact: true,
  }).click();
  assert.equal(await Window.locator(".texture-drop img").count(), 0);
  await Window.getByRole("button", {
    name: "Close ShaderEditor window",
    exact: true,
  }).click();
  assert.equal(await Window.count(), 0);
  assert.deepEqual(Errors, []);
  const Result = {
    Channels: 20,
    Types: MaterialTypes,
    Checks: [
      "XYZ table edit/scrub/reset/lock/persistence",
      "pinned object ownership",
      "20 typed channel fills and four sources",
      "shared native IOR carrier",
      "gradient add/remove/drag/keyboard/axis and stop limits",
      "source draft preservation",
      "texture pixels, embedding, rejection and removal",
      "code descriptor and honest fallback",
      "material type changes",
      "real shader-ball mesh pixels",
      "floating drag, three-column tab docking, close/reopen",
      "1024/1280/1366/1440/1920 layouts",
      "reload persistence",
    ],
    Errors,
  };
  fs.writeFileSync(
    path.join(Proof, "Materials.json"),
    JSON.stringify(Result, null, 2),
  );
  console.log(JSON.stringify(Result, null, 2));
} finally {
  await Browser.close();
}
