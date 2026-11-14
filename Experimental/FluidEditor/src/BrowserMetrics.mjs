import Assert from "node:assert/strict";
import { readFile as ReadFile, mkdir as MakeDirectory } from "node:fs/promises";
import { chromium } from "playwright";

const Browser = await chromium.launch({
  headless: true,
  ...(process.env.BROWSER_EXECUTABLE
    ? { executablePath: process.env.BROWSER_EXECUTABLE }
    : {}),
  args: [
    "--no-sandbox",
    "--disable-dev-shm-usage",
    "--enable-unsafe-swiftshader",
    "--use-gl=angle",
    "--use-angle=swiftshader",
    "--no-zygote",
  ],
});
const Page = await Browser.newPage({
  viewport: { width: 1440, height: 960 },
  deviceScaleFactor: 1,
});
Page.setDefaultTimeout(45000);
const Errors = [];
Page.on("pageerror", (ErrorValue) => Errors.push(ErrorValue.message));
Page.on("console", (Message) => {
  if (Message.type() === "error") Errors.push(Message.text());
});
let Count = 0;
const Pass = (Name) => {
  Count++;
  console.log(`PASS ${Count}: ${Name}`);
};
const Snapshot = () =>
  Page.evaluate(() => ({
    parameters: { ...fluidEditor.Parameters },
    steps: fluidEditor.Engine.stepCount,
    time: fluidEditor.Engine.time,
    grid: fluidEditor.Engine.gridRes,
    camera: fluidEditor.Camera.targetTheta,
    queued: fluidEditor.Bursts.length,
  }));
const ReadFields = () =>
  Page.evaluate(() => {
    const Engine = fluidEditor.Engine;
    const Gl = Engine.gl;
    Gl.bindFramebuffer(Gl.FRAMEBUFFER, Engine.fbos.velThermo0);
    Gl.readBuffer(Gl.COLOR_ATTACHMENT1);
    const Values = new Float32Array(Engine.atlasWidth * Engine.atlasHeight * 4);
    Gl.readPixels(
      0,
      0,
      Engine.atlasWidth,
      Engine.atlasHeight,
      Gl.RGBA,
      Gl.FLOAT,
      Values,
    );
    const ErrorCode = Gl.getError();
    Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
    return {
      finite: Values.every(Number.isFinite),
      energy: Values.reduce((Sum, Value) => Sum + Math.abs(Value), 0),
      error: ErrorCode,
    };
  });

try {
  await Page.goto(
    `${process.env.FLUID_URL || "http://127.0.0.1:5173/"}?quality=low`,
  );
  await Page.waitForFunction(
    () => window.fluidEditor?.Engine?.stepCount > 5,
    null,
    { timeout: 120000 },
  );
  Assert.equal(await Page.locator("#gpu-error").isVisible(), false);
  Assert.equal(await Page.evaluate(() => fluidEditor.Engine.gl.getError()), 0);
  Pass("Real WebGL2 shader compilation, simulation, and rendering");

  await Page.locator("#play-button").click();
  const Paused = await Snapshot();
  Assert.equal(Paused.parameters.paused, true);
  await Page.locator("#step-button").click();
  await Page.waitForFunction(
    (Steps) => fluidEditor.Engine.stepCount === Steps + 1,
    Paused.steps,
  );
  Assert.equal((await Snapshot()).parameters.paused, true);
  const Fields = await ReadFields();
  Assert.equal(Fields.error, 0);
  Assert.equal(Fields.finite, true);
  Assert.ok(Fields.energy > 0);
  Pass("Pause, exact single stepping, nonzero finite GPU voxel fields");

  const Theme = await Page.evaluate(async () => {
    await document.fonts.ready;
    const Range = document.querySelector(
      'input[type="range"][data-param="emitterRate"]',
    );
    return {
      fonts: [...document.fonts]
        .filter((Face) => Face.family === "DM Sans")
        .map((Face) => Face.status),
      family: getComputedStyle(document.body).fontFamily,
      card: getComputedStyle(document.querySelector(".property-group"))
        .borderRadius,
      pill: document.querySelector(".value-pill").getBoundingClientRect().width,
      unit: document.querySelector(".unit-cell").getBoundingClientRect().width,
      track: Range.getBoundingClientRect().height,
      row: document.querySelector(".scene-row").getBoundingClientRect().height,
      background: getComputedStyle(document.querySelector(".property-group"))
        .backgroundColor,
    };
  });
  Assert.deepEqual(Theme.fonts, ["loaded", "loaded", "loaded"]);
  Assert.match(Theme.family, /DM Sans/);
  Assert.equal(Theme.card, "22px");
  Assert.equal(Theme.background, "rgb(26, 26, 26)");
  Assert.equal(Theme.pill, 92);
  Assert.equal(Theme.unit, 34);
  Assert.equal(Theme.track, 26);
  Assert.equal(Theme.row, 39);
  Pass(
    "Native DM Sans fonts, charcoal cards and Project Zero control geometry",
  );

  const Range = Page.locator('input[type="range"][data-param="emitterRate"]');
  await Range.focus();
  await Range.press("End");
  Assert.equal((await Snapshot()).parameters.emitterRate, 2.5);
  Assert.equal(
    await Page.locator("#property-emitterRate").inputValue(),
    "2.50",
  );
  await Range.press("Home");
  Assert.equal((await Snapshot()).parameters.emitterRate, 0.1);
  await Range.press("ArrowRight");
  Assert.equal((await Snapshot()).parameters.emitterRate, 0.15);
  const Track = await Range.boundingBox();
  await Page.mouse.move(Track.x + 12, Track.y + 13);
  await Page.mouse.down();
  await Page.mouse.move(Track.x + Track.width - 12, Track.y + 13, { steps: 5 });
  await Page.mouse.up();
  Assert.equal((await Snapshot()).parameters.emitterRate, 2.5);
  Assert.equal(
    await Range.evaluate((Element) =>
      Element.style.getPropertyValue("--fraction"),
    ),
    "1",
  );
  Pass(
    "Split-pill typing, slider keyboard controls, dragging and live fill synchronization",
  );

  await Page.locator("#scene-search").fill("Fire");
  Assert.equal(await Page.locator("[data-object]").count(), 1);
  await Page.locator("#scene-search").fill("");
  await Page.locator('[data-scene-filter="light"]').click();
  Assert.equal(await Page.locator("[data-object]").count(), 1);
  Assert.equal(await Page.locator('[data-object="sun"]').count(), 1);
  await Page.locator('[data-scene-filter="all"]').click();
  await Page.locator("#collection-toggle").click();
  Assert.equal(await Page.locator("#scene-tree").isVisible(), false);
  await Page.locator("#collection-toggle").click();
  await Page.locator("#compact-outliner").click();
  Assert.equal(
    await Page.locator(".scene-row")
      .first()
      .evaluate((Element) => Element.getBoundingClientRect().height),
    34,
  );
  await Page.locator("#compact-outliner").click();
  await Page.locator('[data-toggle-object="emitter"]').click();
  Assert.equal(await Page.locator("#disabled-count").textContent(), "1");
  await Page.locator('[data-toggle-object="emitter"]').click();
  Assert.equal(await Page.locator("#enabled-count").textContent(), "3");
  await Page.locator('[data-object="emitter"]').dblclick();
  Assert.equal(
    await Page.locator("#object-name").evaluate(
      (Element) => Element === document.activeElement,
    ),
    true,
  );
  Pass(
    "Native-style outliner search, filters, folding, compact rows, status and double-click selection",
  );

  await Page.locator("#property-emitterRate").fill("1.5");
  await Page.locator("#property-emitterRate").press("Tab");
  Assert.equal((await Snapshot()).parameters.emitterRate, 1.5);
  Assert.equal(
    await Page.evaluate(
      () => fluidEditor.Engine.params === fluidEditor.Parameters,
    ),
    true,
  );
  Pass("Inspector edits the engine’s live shared parameters");

  const BeforeInsertion = await Snapshot();
  await Page.locator("#add-button").click();
  await Page.locator('[data-add="smoke"]').click();
  Assert.equal((await Snapshot()).parameters.emitterFuel, 0);
  Assert.equal((await Snapshot()).steps, BeforeInsertion.steps);
  await Page.locator("#add-button").click();
  await Page.locator('[data-add="sphere"]').click();
  Assert.equal((await Snapshot()).parameters.obstacleType, 1);
  Assert.equal((await Snapshot()).steps, BeforeInsertion.steps);
  Assert.equal(await Page.locator('[data-object="collider"]').count(), 1);
  await Page.locator("#property-obstacleX").fill("0.62");
  await Page.locator("#property-obstacleX").press("Tab");
  Assert.equal((await Snapshot()).parameters.obstacleX, 0.62);
  Pass("Insert source and collider, edit position, retain existing simulation");

  await Page.locator("#object-name").fill("<b>Deflector</b>");
  await Page.locator("#object-name").press("Tab");
  Assert.equal(
    await Page.locator('[data-object="collider"] .scene-label').textContent(),
    "<b>Deflector</b>",
  );
  Assert.equal(
    await Page.locator('[data-object="collider"] .scene-label b').count(),
    0,
  );
  Pass("Object renaming is escaped, not injected as HTML");

  await Page.locator("#burst-kind").selectOption("salvo");
  await Page.locator("#burst-button").click();
  Assert.equal((await Snapshot()).queued, 4);
  await Page.locator("#reset-button").click();
  Assert.equal((await Snapshot()).queued, 0);
  Assert.equal((await Snapshot()).steps, 0);
  Assert.equal((await ReadFields()).energy, 0);
  await Page.locator("#burst-kind").selectOption("burst");
  await Page.locator("#burst-button").click();
  await Page.locator("#step-button").click();
  await Page.waitForFunction(() => fluidEditor.Engine.stepCount === 1);
  Assert.ok((await ReadFields()).energy > 0);
  Pass(
    "Burst scheduling, reset cancellation, clear GPU fields, real detonation injection",
  );

  await Page.locator('[data-object="domain"]').click();
  await Page.locator("#property-gridResolution").selectOption("32");
  Assert.equal((await Snapshot()).grid, 32);
  Assert.equal((await Snapshot()).steps, 0);
  Pass("Changing voxel resolution reallocates the actual GPU grid");

  const DownloadPromise = Page.waitForEvent("download");
  await Page.locator("#export-button").click();
  const Download = await DownloadPromise;
  const SavedText = await ReadFile(await Download.path(), "utf8");
  const Saved = JSON.parse(SavedText);
  Assert.equal(Saved.format, "frontier-fluid-scene");
  Assert.equal(Saved.params.obstacleX, 0.62);
  await Page.locator("#property-gridResolution").selectOption("16");
  await Page.locator("#import-file").setInputFiles({
    name: "scene.fluid.json",
    mimeType: "application/json",
    buffer: Buffer.from(SavedText),
  });
  await Page.waitForFunction(
    () => fluidEditor.Parameters.gridResolution === 32,
  );
  Assert.equal((await Snapshot()).grid, 32);
  Assert.equal(
    await Page.evaluate(() => fluidEditor.Names.collider),
    "<b>Deflector</b>",
  );
  Pass("Settings export/download and validated import round-trip");

  await Page.locator("#import-file").setInputFiles({
    name: "invalid.json",
    mimeType: "application/json",
    buffer: Buffer.from(
      JSON.stringify({ ...Saved, params: { gridResolution: 10000 } }),
    ),
  });
  await Page.waitForFunction(() =>
    document.querySelector("#toast").textContent.includes("Scene not opened"),
  );
  Assert.equal((await Snapshot()).grid, 32);
  Pass("Invalid settings leave the live scene intact");

  await Page.locator("#render-channel").selectOption("2");
  Assert.equal((await Snapshot()).parameters.renderChannel, 2);
  await Page.locator("#diagnostics-button").click();
  await Page.locator("#atlas-toggle").check();
  Assert.equal((await Snapshot()).parameters.showAtlasMinimap, true);
  await Page.locator("#close-diagnostics").click();
  await Page.locator("#render-channel").selectOption("0");
  Pass("Debug render channels and atlas controls reach the renderer");

  const CameraBefore = (await Snapshot()).camera;
  const Rectangle = await Page.locator("#pyro-canvas").boundingBox();
  await Page.mouse.move(
    Rectangle.x + Rectangle.width / 2,
    Rectangle.y + Rectangle.height / 2,
  );
  await Page.mouse.down();
  await Page.mouse.move(
    Rectangle.x + Rectangle.width / 2 + 60,
    Rectangle.y + Rectangle.height / 2,
    { steps: 4 },
  );
  await Page.mouse.up();
  Assert.notEqual((await Snapshot()).camera, CameraBefore);
  Pass("Pointer orbit controls the real volume camera");

  await Page.locator("#preset-search").fill("ashfall");
  Assert.equal(await Page.locator("[data-preset]").count(), 1);
  await Page.locator("#reset-search").evaluate((Element) => Element.click());
  Assert.equal(await Page.locator("[data-preset]").count(), 18);
  await Page.locator("#preset-search").fill("Lightweight");
  await Page.locator('[data-preset="low_gpu_performance"]').click();
  Assert.equal((await Snapshot()).grid, 24);
  Pass("Preset search, reset, and gas preset loading");

  await Page.evaluate(() => {
    fluidEditor.Parameters.paused = true;
    fluidEditor.Parameters.renderScale = 0.5;
    fluidEditor.ResizeViewport();
  });
  await Page.setViewportSize({ width: 1100, height: 760 });
  Assert.equal(
    await Page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
    true,
  );
  await Page.setViewportSize({ width: 390, height: 844 });
  Assert.equal(
    await Page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
    true,
  );
  await Page.setViewportSize({ width: 1440, height: 960 });
  Pass("Desktop, compact desktop, and narrow viewport layout");

  await Page.evaluate(async () => {
    await fluidEditor.InitializeRenderer("webgpu");
  });
  const Backend = await Page.evaluate(() => fluidEditor.Backend);
  Assert.ok(["webgpu", "webgl2"].includes(Backend));
  Assert.equal(await Page.locator("#gpu-error").isVisible(), false);
  console.log(`Optional backend result: ${Backend}`);
  await Page.evaluate(async () => {
    await fluidEditor.InitializeRenderer("webgl2");
  });
  Assert.equal(await Page.evaluate(() => fluidEditor.Backend), "webgl2");
  Pass("Optional WebGPU selection or honest fallback and WebGL2 recovery");

  Assert.deepEqual(Errors, []);
  Pass("No browser JavaScript or GPU console errors");
  if (process.env.BROWSER_ARTIFACTS) {
    await MakeDirectory(process.env.BROWSER_ARTIFACTS, { recursive: true });
    await Page.screenshot({
      path: `${process.env.BROWSER_ARTIFACTS}/VerifiedWorkspace.png`,
    });
  }
  console.log(`${Count} browser checks passed.`);
} finally {
  if (Errors.length) console.error(Errors);
  await Browser.close();
}
