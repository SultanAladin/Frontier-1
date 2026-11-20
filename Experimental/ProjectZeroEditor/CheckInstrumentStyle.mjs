import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { RecolourInstrument } from "./InstrumentSpecification.js";
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"),
);
const { chromium } = Require("playwright");
const Proof =
  process.env.FRONTIER_PROOF_FOLDER ||
  path.join(Folder, "Screenshots/InstrumentStyle");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];
for (const Name of ["wind.js", "fog.js"]) {
  const Source = fs.readFileSync(
    path.join(Folder, "InspectorDepot/panels", Name),
    "utf8",
  );
  const Adapted = RecolourInstrument(Name, Source);
  assert.notEqual(Adapted, Source);
  assert.throws(() => RecolourInstrument(Name, "Missing upstream anchors"));
}
Checks.push(
  "Build-time colour adaptation rejects changed anchors; pinned source files are not edited",
);
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  headless: true,
  args: ["--no-sandbox", "--disable-gpu"],
});
const Page = await Browser.newPage({ viewport: { width: 1366, height: 768 } });
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const Address =
  process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Frame = () =>
  Page.frameLocator('iframe[title^="Reference inspector"]').first();
const Open = async (Id, Wide = true) => {
  await Page.goto(
    Address + "?inspect=" + Id + (Wide ? "&workspace=inspector" : ""),
  );
  await Page.locator(".inspector-scroll").waitFor();
};
const Capture = async (Name) => {
  await Page.waitForTimeout(120);
  await Page.screenshot({ path: path.join(Proof, Name + ".png") });
};
try {
  for (const Id of ["wind", "sun", "clouds", "local-cloud"]) {
    await Open(Id);
    await Frame().locator(".mp-stat").first().waitFor();
    const Tiles = await Frame()
      .locator(".mp-pill,.mp-stat")
      .evaluateAll((Tiles) =>
        Tiles.map((Tile) => ({
          Radius: getComputedStyle(Tile).borderRadius,
          Border: getComputedStyle(Tile).borderTopWidth,
          Overflow: Tile.scrollWidth > Tile.clientWidth + 1,
        })),
      );
    assert(Tiles.length >= 6);
    assert(
      Tiles.every(
        (Tile) =>
          Tile.Radius === "12px" && Tile.Border === "0px" && !Tile.Overflow,
      ),
    );
    await Capture(Id + "Wide");
    Checks.push(
      Id +
        ": both statistic rows have 12px borderless corners and no clipped readings",
    );
  }
  await Open("wind");
  await Frame().locator(".wf-trace canvas").waitFor();
  const Colours = () =>
    Frame()
      .locator(".wf-trace canvas")
      .evaluate((Canvas) => {
        const Pixels = Canvas.getContext("2d").getImageData(
          0,
          0,
          Canvas.width,
          Canvas.height,
        ).data;
        let Green = 0,
          Neutral = 0,
          Amber = 0;
        for (let Index = 0; Index < Pixels.length; Index += 4) {
          const [Red, GreenChannel, Blue, Alpha] = Pixels.slice(
            Index,
            Index + 4,
          );
          if (Alpha < 10) continue;
          if (GreenChannel > Red + 15 && GreenChannel > Blue + 8) Green++;
          if (
            Red > 150 &&
            Math.abs(Red - GreenChannel) < 3 &&
            Math.abs(Red - Blue) < 3
          )
            Neutral++;
          if (Red > GreenChannel + 20 && GreenChannel > Blue + 20) Amber++;
        }
        return { Green, Neutral, Amber };
      });
  const Reading = await Colours();
  assert.equal(Reading.Green, 0);
  assert(Reading.Neutral > 100);
  assert(Reading.Amber > 0);
  const Before = await Frame()
    .locator(".wf-trace canvas")
    .evaluate((Canvas) => Canvas.toDataURL());
  await Page.waitForTimeout(700);
  assert.notEqual(
    await Frame()
      .locator(".wf-trace canvas")
      .evaluate((Canvas) => Canvas.toDataURL()),
    Before,
  );
  await Frame()
    .getByRole("button", { name: "Taller trace", exact: true })
    .click();
  assert.equal(
    await Frame()
      .locator(".wf-trace canvas")
      .evaluate((Canvas) => Canvas.style.height),
    "170px",
  );
  assert.equal((await Colours()).Green, 0);
  await Capture("WindTraceTall");
  await Open("wind", false);
  await Frame().locator(".wf-trace canvas").waitFor();
  await Page.locator('iframe[title^="Reference inspector"]').evaluate(
    (Iframe) => (Iframe.style.width = "240px"),
  );
  await Page.waitForTimeout(150);
  assert.equal((await Colours()).Green, 0);
  assert(
    await Frame()
      .locator("body")
      .evaluate((Body) => Body.scrollWidth <= innerWidth),
  );
  await Capture("WindNarrow");
  Checks.push(
    "Anemometer pixels have no green, retain neutral highlights/amber sample, animate, expand and resize to 240px",
  );
  for (const Id of ["height-fog", "aerial-fog", "local-fog"]) {
    await Open(Id);
    const Beam = Page.locator('[data-card="Medium"] .fog-shared-beam');
    assert.equal(await Beam.count(), 1);
    assert.equal(
      await Beam.evaluate((Card) => getComputedStyle(Card).backgroundColor),
      "rgb(25, 25, 25)",
    );
    await Beam.scrollIntoViewIfNeeded();
    await Capture(Id + "Chamber");
    Checks.push(`${Id}: one shared Beam Chamber style and Medium-card ownership`);
  }
  await Open("reference-softbox");
  await Frame().locator(".lp-readings").waitFor();
  assert(
    await Frame()
      .locator(".mp-pill")
      .evaluateAll((Tiles) =>
        Tiles.every((Tile) => getComputedStyle(Tile).borderRadius === "6px"),
      ),
  );
  Checks.push(
    "Light-card rounding and layout remain outside this environment-only change",
  );
  assert.deepEqual(Errors, []);
  console.log("Instrument styling checks passed:", Checks.length);
} catch (Error) {
  Errors.push(Error.stack);
  console.error(Error);
  process.exitCode = 1;
  await Page.screenshot({ path: path.join(Proof, "Failure.png") }).catch(
    () => {},
  );
} finally {
  fs.writeFileSync(
    path.join(Proof, "Checks.json"),
    JSON.stringify({ Checks, Errors }, null, 2) + "\n",
  );
  await Browser.close();
}
