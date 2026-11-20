import fs from "node:fs";
import {
  Normalize,
  Signature,
} from "../FractureEditor/FractureSpecification.js";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const Folder = path.dirname(fileURLToPath(import.meta.url)),
  Root = path.resolve(Folder, "../../.."),
  Require = createRequire(
    process.env.FRONTIER_BROWSER_PACKAGE ||
      path.join(Root, "_AgentScratch/Browser/package.json"),
  ),
  { chromium } = Require("playwright");
const Address =
  process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Proof =
    process.env.FRONTIER_PROOF_FOLDER ||
    path.join(Folder, "Screenshots/SharedCloudLighting/Lighting"),
  Checks = [],
  Errors = [],
  FontFailures = [];
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  headless: true,
  args: [
    "--no-sandbox",
    "--enable-unsafe-swiftshader",
    "--use-angle=swiftshader",
  ],
});
const Context = await Browser.newContext({
  viewport: { width: 1366, height: 900 },
});
Context.on("page", (Page) => {
  Page.on("pageerror", (Error) => Errors.push(Error.stack));
  Page.on("requestfailed", (Request) => {
    if (Request.url().includes("fontshare.com"))
      FontFailures.push(Request.url());
  });
});
const Page = await Context.newPage();
Page.setDefaultTimeout(12000);
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll").waitFor();
  if (await Page.locator('iframe[title^="Reference inspector"]').count()) {
    const Frame = Page.frameLocator(
      'iframe[title^="Reference inspector"]',
    ).first();
    await Frame.locator(".sheet").waitFor();
    return Frame;
  }
};
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
async function Square(Frame) {
  const Reading = await Frame.locator(".mp-pill,.mp-stat").evaluateAll(
    (Tiles) =>
      Tiles.map((Tile) => ({
        Radius: getComputedStyle(Tile).borderRadius,
        Border: getComputedStyle(Tile).borderTopWidth,
        Overflow: Tile.scrollWidth > Tile.clientWidth + 1,
      })),
  );
  const Lighting = await Frame.locator(".lighting-panel").count();
  assert(Reading.length >= (Lighting ? 2 : 4));
  assert(
    Reading.every(
      (Tile) =>
        Tile.Radius === (Lighting ? "6px" : "12px") &&
        Tile.Border === "0px" &&
        !Tile.Overflow,
    ),
    JSON.stringify(Reading),
  );
}
async function Top() {
  await Page.locator(".inspector-scroll").evaluate(
    (Scroll) => (Scroll.scrollTop = 0),
  );
}
try {
  assert.equal(
    Normalize({ PieceSdf: "true", SdfResolution: 100000 }).PieceSdf,
    false,
  );
  assert.equal(Normalize({ SdfResolution: 100000 }).SdfResolution, 64);
  assert.equal(
    Normalize({ PieceSdf: true, SdfResolution: 32 }).SdfResolution,
    32,
  );
  const Owner = { Primitive: "sphere", Scale: [1, 1, 1] };
  assert.equal(
    Signature(Owner, {}),
    Signature(Owner, { PieceSdf: true, SdfResolution: 128 }),
  );
  Checks.push(
    "SDF requests normalize safely and do not invalidate stored fragment geometry",
  );
  for (const [Id, Property, Next] of [
    ["reference-rim-point", "intensity", 8],
    ["reference-fill-point", "distance", 45],
    ["reference-key-spot", "angle", 48],
    ["reference-ece-low-beam", "cone", 84],
    ["reference-softbox", "width", 5],
    ["reference-studio-tube", "length", 4],
    ["light", "height", 3],
    ["reference-led", "diameter", 65],
    ["reference-led-strip", "length", 3.6],
    ["reference-ies-downlight", "cone", 45],
  ]) {
    const Frame = await Open(Id);
    await Frame.locator(".lp-preview").waitFor();
    await Square(Frame);
    const Controls =
      Id === "light"
        ? Page.frameLocator('[data-reference-slice="details"] iframe')
        : Frame;
    assert.equal(
      await Frame.locator(".lighting-panel .lp-card").count(),
      Id === "light" ? 2 : 5,
    );
    if (Id === "light")
      assert.equal(
        await Controls.locator(".lighting-panel .lp-card").count(),
        3,
      );
    assert.equal(await Page.locator(".fracture-card").count(), 0);
    const Before = await Frame.locator(".lp-preview canvas").evaluate(
      (Canvas) => Canvas.toDataURL(),
    );
    await Controls.locator(
      'input[type=number][data-light-property="' + Property + '"]',
    ).fill(String(Next));
    await Page.waitForFunction(
      ({ Id, Property, Next }) =>
        JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
          Id
        ]?.ReferenceInspector?.Properties[Property] === Next,
      { Id, Property, Next },
    );
    assert.notEqual(
      await Frame.locator(".lp-preview canvas").evaluate((Canvas) =>
        Canvas.toDataURL(),
      ),
      Before,
      "live emitter preview " + Id,
    );
    await Open(Id);
    const Reopened =
      Id === "light"
        ? Page.frameLocator('[data-reference-slice="details"] iframe')
        : Page.frameLocator('iframe[title^="Reference inspector"]').first();
    await Reopened.locator(".lp-shape").waitFor();
    assert.equal(
      await Reopened.locator(
        'input[type=number][data-light-property="' + Property + '"]',
      ).inputValue(),
      String(Next),
    );
    if (Id === "light") {
      assert(await Page.locator('[data-panel="light"]').count());
      assert(
        await Page.getByLabel("Intensity value", { exact: true }).count(),
        "Original native light control retained",
      );
    }
    await Top();
    await Page.screenshot({ path: path.join(Proof, Id + ".png") });
    Checks.push(
      Id +
        ": complete cards, borderless tiles, live preview and persisted controls",
    );
  }
  let Frame = await Open("reference-ece-low-beam");
  await Frame.getByRole("button", { name: "High Beam", exact: true }).click();
  assert.equal(
    await Frame.getByRole("button", {
      name: "High Beam",
      exact: true,
    }).getAttribute("aria-pressed"),
    "true",
  );
  await Frame.getByRole("spinbutton", {
    name: "Cut-off pitch value",
    exact: true,
  }).fill("-2");
  assert.equal(
    await Frame.getByRole("spinbutton", {
      name: "Cut-off pitch value",
      exact: true,
    }).inputValue(),
    "-2",
  );
  await Frame.getByRole("spinbutton", { name: "Position X", exact: true }).fill(
    "6",
  );
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
        "reference-ece-low-beam"
      ].ReferenceInspector.Properties.pos[0] === 6,
  );
  await Frame.getByRole("spinbutton", { name: "Rotation Y", exact: true }).fill(
    "35",
  );
  await Frame.getByRole("spinbutton", { name: "Scale Z", exact: true }).fill(
    "2",
  );
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
        "reference-ece-low-beam"
      ].ReferenceInspector.Properties.scale?.[2] === 2,
  );
  assert.equal(await Frame.locator(".lp-placement").count(), 0);
  await Open("reference-ece-low-beam");
  Frame = Page.frameLocator('iframe[title^="Reference inspector"]');
  assert.equal(
    await Frame.getByRole("spinbutton", {
      name: "Position X",
      exact: true,
    }).inputValue(),
    "6",
  );
  assert.equal(
    await Frame.getByRole("spinbutton", {
      name: "Rotation Y",
      exact: true,
    }).inputValue(),
    "35",
  );
  assert.equal(
    await Frame.getByRole("spinbutton", {
      name: "Scale Z",
      exact: true,
    }).inputValue(),
    "2",
  );
  const Shadow = Frame.getByRole("switch", {
      name: "Cast shadows",
      exact: true,
    }),
    OldShadow = await Shadow.getAttribute("aria-checked");
  await Shadow.click();
  assert.notEqual(await Shadow.getAttribute("aria-checked"), OldShadow);
  await Page.evaluate(() => {
    const Saved = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    Saved.Values["reference-ece-low-beam"].ReferenceInspector.Locked = true;
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Open("reference-ece-low-beam");
  Frame = Page.frameLocator('iframe[title^="Reference inspector"]');
  assert(
    await Frame.locator(".lp-output input[type=number]").first().isDisabled(),
  );
  await Page.getByRole("button", {
    name: "Unlock editing",
    exact: true,
  }).click();
  await Frame.getByRole("spinbutton", {
    name: "Cut-off pitch value",
    exact: true,
  }).fill("-1");
  Checks.push(
    "IES profiles, negative cut-off, shared Transform position/rotation/scale persistence, shadows and locking respond correctly",
  );
  Frame = await Open("reference-softbox");
  const TwoSided = Frame.getByRole("switch", {
    name: "Two-sided emission",
    exact: true,
  });
  await TwoSided.click();
  assert(
    (await Frame.locator(".lp-readings").innerText()).includes("Two-sided"),
  );
  Checks.push("Area-light sidedness updates its square reading");
  const ColourBefore = await Frame.locator(".lp-preview canvas").evaluate(
    (Canvas) => Canvas.toDataURL(),
  );
  await Frame.getByLabel("Emission colour", { exact: true }).fill("#b7dbff");
  assert.notEqual(
    await Frame.locator(".lp-preview canvas").evaluate((Canvas) =>
      Canvas.toDataURL(),
    ),
    ColourBefore,
  );
  await Open("reference-softbox");
  Frame = Page.frameLocator('iframe[title^="Reference inspector"]');
  assert.equal(
    await Frame.getByLabel("Emission colour", { exact: true }).inputValue(),
    "#b7dbff",
  );
  await Frame.getByRole("spinbutton", {
    name: "Luminous flux value",
    exact: true,
  }).fill("");
  await Frame.getByRole("spinbutton", {
    name: "Width value",
    exact: true,
  }).focus();
  assert.equal(
    await Frame.getByRole("spinbutton", {
      name: "Luminous flux value",
      exact: true,
    }).inputValue(),
    "2400",
  );
  Checks.push(
    "Emission tint changes the preview and survives reload; incomplete number edits restore the saved value on blur",
  );
  for (const Id of ["sun", "wind"]) {
    const Environment = await Open(Id);
    await Environment.locator(".mp-rail").waitFor();
    await Square(Environment);
    assert.equal(await Environment.locator(".lighting-panel").count(), 0);
    if (Id === "wind") {
      assert.equal(await Environment.locator(".wf-trace").count(), 1);
      assert.equal(await Environment.locator(".wf-hero").count(), 0);
    }
    await Page.locator(".reference-inspector-copy")
      .first()
      .evaluate((Node) => Node.scrollIntoView({ block: "start" }));
    await Page.waitForTimeout(300);
    await Page.screenshot({ path: path.join(Proof, Id + "-tiles.png") });
    Checks.push(
      Id +
        ": borderless, lightly rounded statistics; original card selection retained",
    );
  }
  await Open("height-fog");
  assert.equal(await Page.locator(".reference-inspector-copy").count(), 0);
  assert.equal(await Page.locator('[data-card="Visibility through fog"]').count(), 1);
  assert.equal(
    await Page.locator('[data-card="Medium"] > .fog-shared-beam').count(),
    1,
  );
  await Page.screenshot({ path: path.join(Proof, "height-fog-tiles.png") });
  Checks.push(
    "height-fog: coordinated shared Visibility and Medium-owned Beam Chamber",
  );

  Frame = await Open("reference-ece-low-beam");
  await Page.locator('iframe[title^="Reference inspector"]').evaluate(
    (Iframe) => (Iframe.style.width = "240px"),
  );
  await Frame.locator(".lp-readings").waitFor();
  await Square(Frame);
  assert(
    await Frame.locator("body").evaluate(
      (Body) => Body.scrollWidth <= innerWidth,
    ),
  );
  await Frame.getByRole("spinbutton", {
    name: "Luminous flux value",
    exact: true,
  }).fill("8000");
  await Square(Frame);
  Checks.push(
    "240px light inspector keeps readings visible without horizontal overflow",
  );
  for (const Id of ["cube", "sphere", "cylinder", "cone", "torus"]) {
    await Open(Id);
    assert.equal(await Page.locator(".fracture-card").count(), 1);
    const Enable = Page.getByRole("switch", {
      name: "Enable fracture",
      exact: true,
    });
    if ((await Enable.getAttribute("aria-checked")) !== "true")
      await Enable.click();
    await Page.getByRole("button", { name: "Baked", exact: true }).click();
    await Page.getByRole("switch", {
      name: "Bake SDF per piece",
      exact: true,
    }).click();
    await Page.getByLabel("SDF resolution per piece", {
      exact: true,
    }).selectOption("128");
    assert.equal((await Saved()).Values[Id].Fracture.SdfResolution, 128);
    Checks.push(
      Id + ": fracture and per-piece SDF authoring controls available",
    );
  }
  await Page.evaluate(() => {
    const Saved = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    Saved.Rows.push({
      Id: "geometry-imported-check",
      Name: "Imported surface",
      Panel: "geometry",
      Icon: "editor-surface",
      Parent: "showcase",
      Description: "Imported geometry authoring",
    });
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Open("geometry-imported-check");
  await Page.getByRole("switch", {
    name: "Enable fracture",
    exact: true,
  }).click();
  await Page.getByRole("button", { name: "Baked", exact: true }).click();
  await Page.getByRole("switch", {
    name: "Bake SDF per piece",
    exact: true,
  }).click();
  assert(
    (await Page.locator(".fracture-card").innerText()).includes(
      "Source geometry preview is pending",
    ),
  );
  Checks.push(
    "Imported geometry entries can author settings without claiming an available mesh preview",
  );
  await Open("sphere");
  await Page.locator(".fracture-card").scrollIntoViewIfNeeded();
  await Page.screenshot({ path: path.join(Proof, "FractureSdf.png") });
  const Wait = Context.waitForEvent("page");
  await Page.getByRole("button", {
    name: "Expand fracture editor",
    exact: true,
  }).click();
  const Fracture = await Wait;
  await Fracture.waitForFunction(
    () => window.FrontierFracture?.snapshot().Settings.SdfResolution === 128,
  );
  assert(await Fracture.locator("#piece-sdf").isChecked());
  await Fracture.locator("#bake").click();
  await Fracture.waitForFunction(
    () =>
      window.FrontierFracture.snapshot().Baked &&
      !window.FrontierFracture.snapshot().Busy,
  );
  assert(
    (await Fracture.locator("#bake-status").innerText()).includes(
      "SDF pending",
    ),
  );
  const BeforeSignature = await Fracture.evaluate(
    () => window.FrontierFracture.snapshot().Receipt.Signature,
  );
  await Fracture.locator("#sdf-resolution").selectOption("32");
  assert.equal(
    await Fracture.evaluate(
      () => window.FrontierFracture.snapshot().Receipt.Signature,
    ),
    BeforeSignature,
  );
  assert(
    await Fracture.evaluate(() => window.FrontierFracture.snapshot().Baked),
  );
  const Export = await Fracture.evaluate(() =>
    window.FrontierFracture.export(),
  );
  assert.equal(Export.Sdf.Requested, true);
  assert.equal(Export.Sdf.PerFragment, true);
  assert.equal(Export.Sdf.Generated, false);
  assert(Export.Geometry.length > 1);
  assert.equal(Export.Sdf.Resolution, 32);
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values
        .sphere.Fracture.SdfResolution === 32,
  );
  await Fracture.locator(".bake-card").scrollIntoViewIfNeeded();
  await Fracture.screenshot({ path: path.join(Proof, "SdfAuthoring.png") });
  Checks.push(
    "Per-piece SDF settings sync, survive geometry baking and export as ungenerated requests; geometry signature stays valid",
  );
  assert.deepEqual(Errors, []);
  console.log("Lighting and authoring checks passed:", Checks.length);
} catch (Error) {
  Errors.push(Error.stack);
  console.error(Error);
  await Page.screenshot({
    path: path.join(Proof, "Failure.png"),
    timeout: 5000,
  }).catch(() => {});
  process.exitCode = 1;
} finally {
  fs.writeFileSync(
    path.join(Proof, "Checks.json"),
    JSON.stringify(
      { Checks, FontFailures: [...new Set(FontFailures)], Errors },
      null,
      2,
    ) + "\n",
  );
  await Browser.close();
}
