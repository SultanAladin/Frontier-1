import Assert from "node:assert/strict";
import { readFile as ReadFile } from "node:fs/promises";
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

const Pixels = () =>
  Page.evaluate(() => {
    const Editor = fluidEditor,
      Gl = Editor.Engine.gl;
    Editor.Camera.update(1);
    Editor.Engine.render(Editor.Camera);
    const Bytes = new Uint8Array(
      Editor.Canvas.width * Editor.Canvas.height * 4,
    );
    Gl.readPixels(
      0,
      0,
      Editor.Canvas.width,
      Editor.Canvas.height,
      Gl.RGBA,
      Gl.UNSIGNED_BYTE,
      Bytes,
    );
    let Sum = 0,
      Lit = 0;
    for (let Index = 0; Index < Bytes.length; Index += 4) {
      Sum += Bytes[Index] + Bytes[Index + 1] + Bytes[Index + 2];
      if (Bytes[Index] + Bytes[Index + 1] + Bytes[Index + 2] > 12) Lit++;
    }
    return {
      sum: Sum,
      lit: Lit,
      error: Gl.getError(),
      lost: Gl.isContextLost(),
    };
  });
try {
  await Page.goto(
    `${process.env.FLUID_URL || "http://127.0.0.1:5173/"}?quality=low`,
  );
  await Page.waitForFunction(
    () => window.fluidEditor?.Engine?.stepCount > 4,
    null,
    { timeout: 120000 },
  );
  await Page.locator("#play-button").click();
  const Before = await Pixels();
  Assert.ok(Before.lit > 1000);
  await Page.locator("#overlays-button").click();
  await Page.waitForFunction(
    () => document.querySelectorAll("#domain-bounds line").length === 12,
  );
  Assert.equal(await Page.locator("#domain-bounds").isVisible(), true);
  const WithBounds = await Pixels();
  Assert.equal(WithBounds.sum, Before.sum);
  Assert.equal(WithBounds.error, 0);
  Assert.equal(WithBounds.lost, false);
  await Page.locator("#overlays-button").click();
  await Page.waitForFunction(() =>
    document.querySelector("#domain-bounds").hasAttribute("hidden"),
  );
  await Page.locator("#pyro-canvas").click({ position: { x: 100, y: 100 } });
  await Page.keyboard.press("b");
  await Page.waitForFunction(
    () => !document.querySelector("#domain-bounds").hasAttribute("hidden"),
  );
  for (const View of ["front", "side", "top", "perspective"]) {
    await Page.locator("#camera-view").selectOption(View);
    const Image = await Pixels();
    Assert.ok(Image.lit > 1000, View);
    Assert.equal(Image.error, 0);
  }
  Pass(
    "Bounds toolbar / keyboard toggle preserves rendered pixels; front, side and top views stay nonblack",
  );

  await Page.locator('[data-tab="rendering"]').click();
  await Page.locator("details")
    .filter({ has: Page.locator("#property-showBoundingBox") })
    .locator("summary")
    .click();
  await Page.locator("#property-showBoundingBox").uncheck();
  await Page.locator("#property-showBoundingBox").check();
  Assert.ok((await Pixels()).lit > 1000);
  for (const Key of [
    "showVoxelGridLines",
    "showFloorGrid",
    "showActiveVoxelCells",
    "showAtlasMinimap",
  ]) {
    await Page.locator(`#property-${Key}`).check();
    Assert.equal((await Pixels()).error, 0, Key);
    await Page.locator(`#property-${Key}`).uncheck();
    Assert.equal((await Pixels()).error, 0, Key);
  }
  for (let Channel = 0; Channel <= 8; Channel++) {
    await Page.locator("#render-channel").selectOption(String(Channel));
    const Image = await Pixels();
    Assert.equal(Image.error, 0, `channel ${Channel}`);
    Assert.ok(Image.lit > 1000, `channel ${Channel}`);
  }
  await Page.locator("#render-channel").selectOption("0");
  await Page.locator("#diagnostics-button").click();
  Assert.equal(await Page.locator("#diagnostics").isVisible(), true);
  for (let Field = 0; Field < 4; Field++) {
    await Page.evaluate((Value) => {
      fluidEditor.Parameters.atlasMinimapField = Value;
      fluidEditor.Parameters.showAtlasMinimap = true;
    }, Field);
    Assert.equal((await Pixels()).error, 0, `atlas ${Field}`);
  }
  await Page.locator("#atlas-toggle").uncheck();
  await Page.locator("#close-diagnostics").click();
  Pass(
    "Inspector bounds, voxel/floor/active-cell overlays, all nine debug channels and all four atlas fields",
  );

  await Page.locator('[data-tab="source"]').click();
  const Number = Page.locator("#property-emitterRate");
  await Number.fill("1");
  await Number.press("Tab");
  const Cell = await Number.boundingBox();
  await Page.mouse.move(Cell.x + Cell.width / 2, Cell.y + Cell.height / 2);
  await Page.mouse.down();
  await Page.mouse.move(
    Cell.x + Cell.width / 2 + 10,
    Cell.y + Cell.height / 2,
    { steps: 5 },
  );
  await Page.mouse.up();
  Assert.equal((await Snapshot()).parameters.emitterRate, 1.5);
  await Number.fill("1.25");
  await Number.press("Tab");
  Assert.equal((await Snapshot()).parameters.emitterRate, 1.25);
  Pass(
    "Numeric value supports horizontal drag scrubbing as well as direct typing",
  );

  const First = await Snapshot();
  await Page.evaluate(() => {
    window.OriginalFluidEngine = fluidEditor.Engine;
  });
  await Page.evaluate(() => {
    const Scene = JSON.stringify({
      format: "frontier-fluid-scene",
      version: 1,
      name: "Wrong document",
      params: { ...fluidEditor.Parameters },
      names: { ...fluidEditor.Names },
    });
    window.PendingImport = fluidEditor.OpenScene({
      size: Scene.length,
      text: () =>
        new Promise((Resolve) => {
          window.ResolveImport = () => Resolve(Scene);
        }),
    });
  });
  await Page.locator("#new-document").click();
  await Page.evaluate(() => {
    ResolveImport();
    return PendingImport;
  });
  Assert.notEqual(
    await Page.locator("#document-name").inputValue(),
    "Wrong document",
  );
  await Page.waitForFunction(
    () =>
      fluidEditor.Documents.Slots.length === 2 &&
      !fluidEditor.Switching &&
      fluidEditor.Engine,
  );
  Assert.notEqual(
    await Page.evaluate(() => fluidEditor.Engine === OriginalFluidEngine),
    true,
  );
  await Page.locator("#play-button").click();
  await Page.locator('[data-document="2"]').dblclick();
  const Rename = Page.locator('[data-document="2"] .document-rename');
  await Rename.fill("Smoke take");
  await Rename.press("Enter");
  Assert.equal(await Page.locator("#document-name").inputValue(), "Smoke take");
  await Page.locator("#property-emitterRate").fill("2");
  await Page.locator("#property-emitterRate").press("Tab");
  await Page.locator('[data-document="1"]').click();
  Assert.equal(
    await Page.evaluate(() => fluidEditor.Engine === OriginalFluidEngine),
    true,
  );
  Assert.equal((await Snapshot()).steps, First.steps);
  Assert.equal((await Snapshot()).parameters.emitterRate, 1.25);
  await Page.locator('[data-document="2"]').click();
  Assert.equal((await Snapshot()).parameters.emitterRate, 2);
  Assert.equal(await Page.locator("#document-name").inputValue(), "Smoke take");
  Page.once("dialog", (Dialog) => Dialog.accept());
  await Page.locator('[data-close-document="2"]').click();
  await Page.waitForFunction(() => fluidEditor.Documents.Slots.length === 1);
  Assert.equal(
    await Page.evaluate(() => fluidEditor.Engine === OriginalFluidEngine),
    true,
  );
  Assert.equal((await Pixels()).lost, false);
  Pass(
    "Document creation, double-click rename, GPU-state-preserving switching and confirmed close",
  );

  await Page.locator("#flipbook-button").click();
  await Page.locator("#bake-count").selectOption("4");
  await Page.locator("#bake-size").selectOption("64");
  await Page.locator("#bake-rate").selectOption("12");
  await Page.locator("#bake-warmup").fill("0");
  await Page.locator("#bake-burst").check();
  const LiveBefore = await Snapshot();
  await Page.locator("#bake-start").click();
  await Page.waitForFunction(
    () => !fluidEditor.Baking && fluidEditor.Flipbook.Result,
    null,
    { timeout: 180000 },
  );
  const Bake = await Page.evaluate(() => {
    const Result = fluidEditor.Flipbook.Result,
      Metadata = Result.Metadata;
    const Data = Result.Sheet.getContext("2d").getImageData(
      0,
      0,
      Metadata.width,
      Metadata.height,
    ).data;
    let Clear = 0,
      Coverage = 0,
      Partial = 0,
      Difference = 0;
    for (let Index = 3; Index < Data.length; Index += 4) {
      if (!Data[Index]) Clear++;
      else Coverage++;
      if (Data[Index] > 0 && Data[Index] < 255) Partial++;
    }
    for (let Row = 0; Row < 64; Row++)
      for (let Column = 0; Column < 64; Column++)
        Difference += Math.abs(
          Data[(Row * 128 + Column) * 4] -
            Data[((Row + 64) * 128 + Column + 64) * 4],
        );
    return {
      Metadata,
      Clear,
      Coverage,
      Partial,
      Difference,
      Bytes: Result.Blob.size,
    };
  });
  Assert.equal(Bake.Metadata.width, 128);
  Assert.equal(Bake.Metadata.frameCount, 4);
  Assert.ok(
    Bake.Clear > 0 &&
      Bake.Coverage > 0 &&
      Bake.Partial > 0 &&
      Bake.Difference > 0 &&
      Bake.Bytes > 100,
  );
  Assert.ok(
    Bake.Metadata.frames.every(
      (Frame, Index) =>
        Index === 0 ||
        Frame.simulationTime > Bake.Metadata.frames[Index - 1].simulationTime,
    ),
  );
  Assert.deepEqual((await Snapshot()).parameters, LiveBefore.parameters);
  Assert.equal((await Snapshot()).steps, LiveBefore.steps);
  const PngPromise = Page.waitForEvent("download");
  await Page.locator("#bake-png").click();
  const Png = await ReadFile(await (await PngPromise).path());
  Assert.equal(Png.subarray(1, 4).toString(), "PNG");
  Assert.equal(Png.readUInt32BE(16), 128);
  Assert.equal(Png.readUInt32BE(20), 128);
  const JsonPromise = Page.waitForEvent("download");
  await Page.locator("#bake-json").click();
  const Metadata = JSON.parse(
    await ReadFile(await (await JsonPromise).path(), "utf8"),
  );
  Assert.equal(Metadata.frames.length, 4);
  await Page.locator("#bake-play").uncheck();
  await Page.locator("#bake-frame").fill("3");
  Assert.equal(await Page.locator("#bake-frame-label").textContent(), "4 / 4");
  Pass(
    "Actual RGBA flipbook: distinct frames, transparent / partial coverage, PNG encoding, UV/timing JSON and scrub preview; live scene unchanged",
  );

  await Page.locator("#bake-warmup").fill("3");
  await Page.locator("#bake-start").click();
  await Page.waitForFunction(() => fluidEditor.Baking);
  await Page.locator("#bake-cancel").click();
  await Page.waitForFunction(() => !fluidEditor.Baking);
  Assert.match(
    await Page.locator("#bake-progress-label").textContent(),
    /cancelled/i,
  );
  await Page.locator("#close-flipbook").click();
  Assert.equal((await Snapshot()).steps, LiveBefore.steps);
  Assert.equal((await Pixels()).lost, false);
  Pass(
    "Cancellation releases the isolated bake without losing the active scene",
  );

  await Page.locator("#flipbook-button").click();
  await Page.locator("#bake-warmup").fill("0");
  await Page.locator("#bake-transparent").uncheck();
  await Page.locator("#bake-start").click();
  await Page.waitForFunction(
    () =>
      !fluidEditor.Baking &&
      fluidEditor.Flipbook.Result.Metadata.alphaMode === "opaque",
    null,
    { timeout: 180000 },
  );
  Assert.equal(
    await Page.evaluate(() => {
      const Result = fluidEditor.Flipbook.Result;
      const Bytes = Result.Sheet.getContext("2d").getImageData(
        0,
        0,
        128,
        128,
      ).data;
      return Bytes.every(
        (Channel, Index) => Index % 4 !== 3 || Channel === 255,
      );
    }),
    true,
  );
  await Page.locator("#close-flipbook").click();
  Assert.equal((await Snapshot()).steps, LiveBefore.steps);
  Pass(
    "Opaque studio flipbook keeps full background coverage and preserves the live solver",
  );

  for (let Index = 0; Index < 3; Index++) {
    await Page.locator("#new-document").click();
    await Page.waitForFunction(
      () => !fluidEditor.Switching && fluidEditor.Engine,
    );
    await Page.evaluate(() => fluidEditor.ApplyParameter("paused", true));
  }
  await Page.locator("#new-document").click();
  Assert.equal(
    await Page.evaluate(() => fluidEditor.Documents.Slots.length),
    4,
  );
  await Page.evaluate(() =>
    fluidEditor.Documents.Slots.find((Slot) => Slot.Identity === 3)
      .State.Engine.gl.getExtension("WEBGL_lose_context")
      .loseContext(),
  );
  await Page.waitForFunction(
    () =>
      !!fluidEditor.Documents.Slots.find((Slot) => Slot.Identity === 3).State
        .Fault,
  );
  Assert.equal(await Page.locator("#gpu-error").isVisible(), false);
  await Page.locator('[data-document="3"]').click();
  Assert.equal(await Page.locator("#gpu-error").isVisible(), true);
  await Page.locator("#retry-gpu").click();
  await Page.waitForFunction(
    () =>
      !fluidEditor.Switching &&
      fluidEditor.Engine &&
      !fluidEditor.Engine.gl.isContextLost() &&
      document.querySelector("#gpu-error").hidden,
  );
  const ConfirmClose = (Dialog) => Dialog.accept();
  Page.on("dialog", ConfirmClose);
  for (const Identity of [3, 4, 5])
    await Page.locator(`[data-close-document="${Identity}"]`).click();
  Page.off("dialog", ConfirmClose);
  Assert.equal(
    await Page.evaluate(() => fluidEditor.Engine === OriginalFluidEngine),
    true,
  );
  Assert.equal((await Pixels()).lost, false);
  Pass(
    "Four-document GPU budget, inactive-context loss isolation, retry recovery and cleanup",
  );

  for (const Selector of [
    "#focus-button",
    "#turntable-button",
    "#turntable-button",
    "#maximize-button",
    "#maximize-button",
  ])
    await Page.locator(Selector).click();
  await Page.locator("#help-button").click();
  Assert.equal(await Page.locator("#help-dialog").isVisible(), true);
  await Page.locator("#close-help").click();
  await Page.locator("#speed-select").selectOption("0.5");
  Assert.equal((await Snapshot()).parameters.timeScale, 0.5);
  for (const Tool of ["flamethrower", "detonate", "orbit"]) {
    await Page.locator(`[data-tool="${Tool}"]`).click();
    Assert.equal((await Snapshot()).parameters.interactionMode, Tool);
  }
  for (const Kind of ["shrapnel", "salvo", "collide", "mushroom"]) {
    await Page.locator("#burst-kind").selectOption(Kind);
    await Page.locator("#burst-button").click();
    Assert.ok((await Snapshot()).queued > 0);
    await Page.locator("#reset-button").click();
  }
  await Page.locator("#step-button").click();
  await Page.waitForFunction(() => fluidEditor.Engine.stepCount > 0);
  Assert.ok((await Pixels()).lit > 1000);
  Assert.deepEqual(Errors, []);
  Pass(
    "Focus, turntable, maximize, help, speed, tool buttons, every burst type and restart / step",
  );
  console.log(`${Count} expanded interaction checks passed.`);
} finally {
  if (Errors.length) console.error(Errors);
  await Browser.close();
}
