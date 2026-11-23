import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

// Captures the editor assets' icons (EditorAssets.js `CameraIcon`, VehicleAssets.js `VehicleIcon`, rendered in the editor's own browser bundle) as 96 px PNGs into
// Images/EditorAssets, which Build.mjs ships as icons. Run against a served editor, then rebuild:
//   FRONTIER_EDITOR_URL=http://127.0.0.1:8097/ node CaptureEditorAssetIcons.mjs && node Build.mjs
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Output = path.join(Folder, "Images/EditorAssets");
fs.mkdirSync(Output, { recursive: true });
const Browser = await chromium.launch({ executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium", args: ["--no-sandbox", "--disable-gpu", "--enable-unsafe-swiftshader"] });
try {
  const Page = await Browser.newPage();
  await Page.goto(process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/");
  await Page.waitForFunction(() => window.FrontierEditorAssets);
  await Page.waitForFunction(() => window.FrontierEditorAssets?.VehicleIcon);
  const Icons = await Page.evaluate(() => {
    const Assets = window.FrontierEditorAssets;
    return [
      ...Assets.CameraBodies.map((Body) => [Body.Icon, Assets.CameraIcon(Body.Id, 96)]),
      ...Assets.VehicleModels.map((Model) => [Model.Icon, Assets.VehicleIcon(Model.Id, 96)]),
    ];
  });
  for (const [Name, Picture] of Icons) {
    fs.writeFileSync(path.join(Output, Name + ".png"), Buffer.from(Picture.split(",")[1], "base64"));
    console.log("🟢", Name + ".png");
  }
} finally {
  await Browser.close();
}
