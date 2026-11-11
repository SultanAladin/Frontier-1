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
  hasTouch: true,
});
Page.setDefaultTimeout(12000);
const Errors = [];
Page.on("pageerror", (Error) => Errors.push(Error.message));
Page.on("console", (Message) => {
  if (Message.type() === "error") Errors.push(Message.text());
});
const Top = Page.locator('[data-drawer="top"]'),
  Bottom = Page.locator('[data-drawer="bottom"]'),
  Assets = Page.locator(".asset-browser");
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
async function Settled() {
  await Page.waitForTimeout(350);
}
async function Drag(Node, DX, DY, Hold = 180) {
  const Box = await Node.boundingBox();
  const Backdrop = await Node.evaluate((Item) =>
    Item.classList.contains("drawer-backdrop"),
  );
  const X = Box.x + (Backdrop ? 50 : Box.width / 2),
    Y = Box.y + Box.height * (Backdrop ? 0.8 : 0.5);
  await Page.mouse.move(X, Y);
  await Page.mouse.down();
  await Page.mouse.move(X + DX, Y + DY, { steps: Hold === 0 ? 3 : 14 });
  await Page.waitForTimeout(Hold);
  await Page.mouse.up();
  await Settled();
}
const Stage = async (Node, Expected) =>
  assert.equal(await Node.getAttribute("data-stage"), Expected);
async function OpenAssets() {
  if (
    (await Page.locator(".asset-notch-handle").getAttribute(
      "aria-expanded",
    )) === "false"
  ) {
    await Page.locator(".asset-notch-handle").click();
    await Settled();
  }
}
async function Filter(Origin, Kind) {
  await Assets.getByRole("group", { name: "Asset origin" })
    .getByRole("button", { name: Origin, exact: true })
    .click();
  await Assets.getByRole("navigation", { name: "Asset categories" })
    .getByRole("button", { name: new RegExp("^" + Kind + "\\b") })
    .click();
}
async function Select(Name) {
  await Assets.getByRole("button", {
    name: "Select asset " + Name,
    exact: true,
  }).click();
}
const CDP = await Page.context().newCDPSession(Page);
async function Touch(Node, DY, Cancel = false) {
  const Box = await Node.boundingBox(),
    X = Box.x + Math.min(65, Box.width / 2),
    Y =
      Box.y +
      ((await Node.evaluate((Item) =>
        Item.classList.contains("drawer-backdrop"),
      ))
        ? Box.height * 0.85
        : Math.min(20, Box.height / 2));
  await CDP.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: [{ x: X, y: Y }],
  });
  for (let Step = 1; Step <= 8; Step++) {
    await CDP.send("Input.dispatchTouchEvent", {
      type: "touchMove",
      touchPoints: [{ x: X, y: Y + (DY * Step) / 8 }],
    });
  }
  await Page.waitForTimeout(180);
  await CDP.send("Input.dispatchTouchEvent", {
    type: Cancel ? "touchCancel" : "touchEnd",
    touchPoints: [],
  });
  await Settled();
}
try {
  await Page.goto(process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:5180/", {
    waitUntil: "networkidle",
  });
  await Page.evaluate(() => document.fonts.ready);
  // Both handles drag in mirrored directions, and share all three stops.
  await Drag(Page.locator(".notch-handle"), 0, 380);
  await Stage(Top, "half");
  await Drag(Page.locator(".notch-handle"), 0, 410);
  await Stage(Top, "full");
  await Drag(Top.locator(".drawer-backdrop"), 0, -480);
  await Stage(Top, "half");
  await Drag(Page.locator(".notch-handle"), 0, -400);
  await Stage(Top, "closed");
  await Drag(Page.locator(".asset-notch-handle"), 0, -380);
  await Stage(Bottom, "half");
  await Page.screenshot({ path: path.join(Proof, "AssetDrawerHalf.png") });
  await Drag(Page.locator(".asset-notch-handle"), 0, -410);
  await Stage(Bottom, "full");
  await Drag(Assets.locator(".asset-browser-header h1"), 0, 400);
  await Stage(Bottom, "half");
  await Drag(Page.locator(".asset-notch-handle"), 0, 410);
  await Stage(Bottom, "closed");
  const BeforeX = (await Page.locator(".notch-handle").boundingBox()).x;
  await Drag(Page.locator(".notch-handle"), 80, 0);
  await Stage(Top, "closed");
  assert.ok(
    (await Page.locator(".notch-handle").boundingBox()).x > BeforeX + 60,
  );
  await Drag(Page.locator(".notch-handle"), -80, 0);
  await Touch(Page.locator(".notch-handle"), 120, true);
  await Stage(Top, "closed");
  await Touch(Page.locator(".notch-handle"), 700);
  await Stage(Top, "full");
  await Touch(Top.locator(".drawer-backdrop"), -700);
  await Stage(Top, "closed");
  await Touch(Page.locator(".asset-notch-handle"), -700);
  await Stage(Bottom, "full");
  await Touch(Assets.locator(".asset-browser-header h1"), 700);
  await Stage(Bottom, "closed");
  // A fast, short closing swipe uses release velocity, not only distance.
  await Page.locator(".notch-handle").click();
  await Settled();
  await Drag(Top.locator(".drawer-backdrop"), 0, -160, 0);
  await Stage(Top, "closed");
  await Page.locator(".notch-handle").click();
  await Settled();
  await Page.getByRole("button", {
    name: "Control Center settings",
    exact: true,
  }).click();
  await Page.locator(".hub-row").filter({ hasText: "Render Settings" }).click();
  await Drag(Page.locator(".notch-handle"), 0, -420);
  await Stage(Top, "half");
  const HalfSurface = await Top.locator(".drawer-surface").boundingBox(),
    HalfDialog = await Top.locator(".settings-dialog").boundingBox();
  assert.ok(
    HalfDialog.y >= HalfSurface.y &&
      HalfDialog.y + HalfDialog.height <=
        HalfSurface.y + HalfSurface.height + 1,
    "Settings fit half-open drawer",
  );
  await Top.locator(".settings-body").hover();
  await Page.mouse.wheel(0, 450);
  await Stage(Top, "half");
  await Page.locator(".notch-handle").focus();
  await Page.keyboard.press("Escape");
  await Page.keyboard.press("Escape");
  await Settled();
  await Stage(Top, "closed");
  await OpenAssets();
  assert.equal(await Assets.locator(".asset-cell").count(), 9);
  await Filter("Engine", "Fonts");
  assert.equal(await Assets.locator(".asset-cell").count(), 2);
  await Select("DM Sans Light");
  await Assets.getByText("Font loaded · preview only", {
    exact: true,
  }).waitFor();
  await Page.screenshot({ path: path.join(Proof, "AssetFonts.png") });
  await Filter("Engine", "Images");
  assert.equal(await Assets.locator(".asset-cell").count(), 6);
  await Select("Luna albedo");
  assert.equal(await Assets.locator(".asset-file-preview img").count(), 1);
  await Filter("Engine", "Icons");
  assert.equal(await Assets.locator(".asset-cell").count(), 161);
  await Assets.locator(".asset-catalogue").hover();
  await Page.mouse.wheel(0, 650);
  await Page.waitForTimeout(150);
  assert.ok(
    (await Assets.locator(".asset-catalogue").evaluate(
      (Node) => Node.scrollTop,
    )) > 100,
  );
  await Stage(Bottom, "full");
  await Touch(Assets.locator(".asset-catalogue"), -180);
  await Stage(Bottom, "full");
  await Assets.getByLabel("Search assets", { exact: true }).fill(
    "No matching item",
  );
  assert.equal(await Assets.locator(".asset-cell").count(), 0);
  await Assets.getByRole("button", {
    name: "Clear filters",
    exact: true,
  }).click();
  assert.ok((await Assets.locator(".asset-cell").count()) > 170);
  await Filter("All", "Materials");
  await Assets.getByRole("button", {
    name: "list asset view",
    exact: true,
  }).click();
  assert.equal(await Assets.locator(".asset-catalogue.list").count(), 1);
  await Assets.getByRole("button", {
    name: "grid asset view",
    exact: true,
  }).click();
  // Create, edit, rename and apply a library material without sharing mutable object state.
  await Assets.getByRole("button", { name: "+ Material", exact: true }).click();
  let Material = (await Saved()).Assets.find(
    (Item) => Item.Kind === "Materials",
  );
  assert.ok(Material);
  await Assets.getByRole("button", {
    name: "Edit in ShaderEditor",
    exact: true,
  }).click();
  await Stage(Bottom, "closed");
  const Shader = Page.getByRole("dialog", {
    name: "ShaderEditor window",
    exact: true,
  });
  await Shader.getByLabel("Material name", { exact: true }).fill(
    "Library jade",
  );
  await Shader.getByLabel("Material type", { exact: true }).selectOption(
    "Metal",
  );
  await Shader.getByLabel("Base colour fill hex", { exact: true }).fill(
    "#65ac99",
  );
  await Shader.getByLabel("Base colour fill hex", { exact: true }).press("Tab");
  await Shader.getByRole("button", {
    name: "Close ShaderEditor window",
    exact: true,
  }).click();
  await OpenAssets();
  await Select("Library jade");
  await Assets.getByLabel("Material assignment target", {
    exact: true,
  }).selectOption("cube");
  await Assets.getByRole("button", {
    name: "Apply copy to Cube",
    exact: true,
  }).click();
  assert.equal(
    (await Saved()).Values.cube.Material.Channels.colour.Fill,
    "#65ac99",
  );
  await Assets.getByLabel("Asset name", { exact: true }).fill(
    "Library jade revised",
  );
  await Assets.getByRole("button", { name: "Rename", exact: true }).click();
  assert.equal((await Saved()).Values.cube.Material.Name, "Library jade");
  Material = (await Saved()).Assets.find((Item) => Item.Id === Material.Id);
  assert.equal(Material.Name, "Library jade revised");
  const Download = Page.waitForEvent("download");
  await Assets.getByRole("button", {
    name: "Download material",
    exact: true,
  }).click();
  const File = await Download;
  assert.equal(
    JSON.parse(fs.readFileSync(await File.path(), "utf8")).Format,
    "FrontierMaterial.v1",
  );
  // Imported original bytes live in IndexedDB, not filename-only placeholder entries.
  const Image = await Page.evaluate(() => {
    const Canvas = document.createElement("canvas");
    Canvas.width = 80;
    Canvas.height = 40;
    const Context = Canvas.getContext("2d");
    Context.fillStyle = "#758e78";
    Context.fillRect(0, 0, 80, 40);
    return Canvas.toDataURL().split(",")[1];
  });
  await Assets.getByLabel("Import assets", { exact: true }).setInputFiles([
    {
      name: "Moss.png",
      mimeType: "image/png",
      buffer: Buffer.from(Image, "base64"),
    },
    {
      name: "StoredShape.obj",
      mimeType: "text/plain",
      buffer: Buffer.from("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3"),
    },
    {
      name: "Jade.material.json",
      mimeType: "application/json",
      buffer: Buffer.from(
        JSON.stringify({
          Format: "FrontierMaterial.v1",
          Material: Material.Material,
        }),
      ),
    },
  ]);
  await Assets.getByText(/3 imported · originals saved/).waitFor();
  await Select("Moss.png");
  await Assets.getByText("80 × 40", { exact: true }).waitFor();
  await Page.screenshot({ path: path.join(Proof, "AssetImages.png") });
  await Assets.getByLabel("Import assets", { exact: true }).setInputFiles({
    name: "Broken.png",
    mimeType: "image/png",
    buffer: Buffer.from("not an image"),
  });
  await Assets.getByText(/0 imported/).waitFor();
  assert.equal(
    (await Saved()).Assets.some((Item) => Item.Name === "Broken.png"),
    false,
  );
  await Page.reload({ waitUntil: "networkidle" });
  await OpenAssets();
  await Filter("Imported", "Images");
  await Select("Moss.png");
  await Assets.getByText("80 × 40", { exact: true }).waitFor();
  const ImageDownload = Page.waitForEvent("download");
  await Assets.getByRole("button", {
    name: "Download file",
    exact: true,
  }).click();
  assert.deepEqual(
    fs.readFileSync(await (await ImageDownload).path()),
    Buffer.from(Image, "base64"),
  );
  await Assets.getByRole("button", {
    name: "Remove asset",
    exact: true,
  }).click();
  await Assets.getByText(
    "Removed from this browser library; object material copies are unchanged.",
    { exact: true },
  ).waitFor();
  assert.equal(
    (await Saved()).Assets.some((Item) => Item.Name === "Moss.png"),
    false,
  );
  await Filter("All", "Materials");
  await Select("Glass surface");
  await Assets.locator('canvas[data-preview-status="Preview ready"]').waitFor();
  await Page.screenshot({ path: path.join(Proof, "AssetBrowser.png") });
  // Drawer resize and full/half layouts remain within the viewport.
  for (const Width of [1024, 1280, 1366, 1920]) {
    await Page.setViewportSize({
      width: Width,
      height: Width === 1366 ? 768 : 900,
    });
    await Settled();
    assert.equal(
      await Assets.evaluate((Node) => Node.scrollWidth > Node.clientWidth + 1),
      false,
    );
  }
  await Page.setViewportSize({ width: 1440, height: 900 });
  await Settled();
  await Assets.getByRole("button", {
    name: "Close Asset Browser",
    exact: true,
  }).click();
  await Settled();
  await Page.getByRole("button", {
    name: "Viewport settings",
    exact: true,
  }).click();
  const Prefs = Page.getByRole("dialog", {
    name: "Viewport settings",
    exact: true,
  });
  await Prefs.getByLabel("Viewport debug view", { exact: true }).selectOption(
    "7",
  );
  assert.match(await Page.locator(".diagnostic-overlay").innerText(), /Normal/);
  await Prefs.getByLabel("HiZ overlay", { exact: true }).uncheck();
  await Page.screenshot({ path: path.join(Proof, "ViewportSettings.png") });
  await Page.keyboard.press("Escape");
  assert.equal(await Prefs.count(), 0);
  await Stage(Top, "closed");
  assert.deepEqual(Errors, []);
  const Result = {
    Checks: [
      "Shared top/bottom closed-half-full gesture stops",
      "Handle and open-page dragging",
      "Horizontal handle repositioning",
      "Real touch opening/closing/cancellation",
      "Velocity closing flick",
      "Independent catalogue wheel/touch scrolling",
      "Engine fonts/images/icons and grid/list/search",
      "Create/edit/rename/apply independent material copies",
      "Material export",
      "IndexedDB originals, reload and exact-byte download",
      "Invalid image rejection and removal",
      "1024/1280/1366/1920 layouts",
      "Viewport-only settings/debug controls",
    ],
    Errors,
  };
  fs.writeFileSync(
    path.join(Proof, "Assets.json"),
    JSON.stringify(Result, null, 2),
  );
  console.log(JSON.stringify(Result, null, 2));
} finally {
  await Browser.close();
}
