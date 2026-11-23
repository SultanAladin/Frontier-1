import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { OpenTabWith } from "./TabReveal.mjs";
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
  args: ["--no-sandbox", "--disable-gpu"],
  headless: true,
});
const Page = await Browser.newPage({
  viewport: { width: 1440, height: 1000 },
  deviceScaleFactor: 1,
});
Page.setDefaultTimeout(15000);
const Errors = [],
  Checks = [];
Page.on("pageerror", (Error) => Errors.push(Error.message));
Page.on("requestfailed", (Request) => {
  if (!Request.url().includes("fontshare.com"))
    Errors.push(`${Request.url()}: ${Request.failure()?.errorText}`);
});
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
async function Select(Name) {
  const Search = Page.getByLabel("Search outliner", { exact: true });
  await Search.fill(Name);
  await Page.locator(".row-name").getByText(Name, { exact: true }).click();
  await Search.fill("");
  await Search.blur();
}
const Card = (Name) => Page.locator(`[data-card="${Name}"]`);
async function Drag(Graph, T) {
  await Graph.scrollIntoViewIfNeeded();
  const B = await Graph.boundingBox();
  await Page.mouse.move(B.x + B.width * 0.25, B.y + B.height * 0.5);
  await Page.mouse.down();
  await Page.mouse.move(B.x + B.width * T, B.y + B.height * 0.45, { steps: 6 });
  await Page.mouse.up();
}
try {
  await Page.goto(process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:5180/", {
    waitUntil: "networkidle",
  });
  await Page.evaluate(() => document.fonts.ready);
  await Select("Height Fog");
  await Page.getByRole("button", { name: "Enabled", exact: true }).click();
  assert.equal(await Card("Fog layer").count(), 1);
  assert.equal(await Card("Visibility through fog").count(), 0);
  await Page.getByLabel("Density value", { exact: true }).fill("0.02");
  await Page.getByLabel("Density value", { exact: true }).press("Tab");
  await Card("Fog layer").scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/HeightFogLayer.png" });

  await Select("Atmospheric Fog");
  assert.equal(await Card("Aerial perspective").count(), 1);
  await Card("Aerial perspective").getByRole("slider", { name: "Fog distance probe", exact: true }).focus();
  await Page.keyboard.press("End");
  assert.equal((await Saved()).Values["aerial-fog"]["Probe distance"], 2000);

  await Select("Local Fog");
  const Visibility = Card("Visibility through fog");
  assert.equal(await Visibility.locator(".graph-metric").count(), 1);
  await Page.getByRole("button", { name: "Enabled", exact: true }).click();
  assert.match(await Visibility.locator(".graph-status").innerText(), /LIVE|PREVIEW/);
  await Visibility.getByRole("slider", {
    name: "Fog distance probe",
    exact: true,
  }).focus();
  await Page.keyboard.press("End");
  assert.equal((await Saved()).Values["local-fog"]["Probe distance"], 2000);
  await Visibility.scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/LocalFogVisibility.png" });
  Checks.push(
    "Height Fog and Atmospheric Fog own their cards and probes; Local Fog keeps the shared Visibility card",
  );
  await Select("Sun");
  // The Sun's graphs sit on its lettered tabs (B · Light, C · Day cycle).
  await OpenTabWith(Page, "Sun daylight graph");
  const Day = Page.getByRole("slider", {
    name: "Sun daylight graph",
    exact: true,
  });
  await Drag(Day, 0.8);
  assert.ok((await Saved()).Values.sun["Local Hours"] > 17);
  const DayPath = await Day.locator("[data-series]").getAttribute("d");
  await OpenTabWith(Page, "Sun seasonal graph");
  await Page.getByRole("slider", {
    name: "Sun seasonal graph",
    exact: true,
  }).focus();
  await Page.keyboard.press("Home");
  assert.equal((await Saved()).Values.sun.Month, 1);
  assert.equal((await Saved()).Values.sun["Day of Month"], 1);
  // The daylight graph is on B · Light; it draws the new date when shown.
  await OpenTabWith(Page, "Sun daylight graph");
  await Page.waitForTimeout(200);
  assert.notEqual(
    await Day.locator("[data-series]").getAttribute("d"),
    DayPath,
  );
  await OpenTabWith(Page, "Dynamic settings");
  await Card("Dynamic settings").scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/SunSeasonalGraph.png" });
  await OpenTabWith(Page, "Sunlight gain graph");
  await Page.getByRole("slider", {
    name: "Sunlight gain graph",
    exact: true,
  }).focus();
  await Page.keyboard.press("End");
  assert.equal((await Saved()).Values.sun.Intensity, 60);
  Checks.push(
    "Solar daylight and seasonal plots update real time/date controls using the existing solver; relative intensity graph edits gain",
  );
  await Select("Atmosphere");
  assert.equal(await Card("Atmosphere overview").count(), 1);
  assert.equal(await Card("Sky presets").count(), 0);
  assert.equal(await Page.locator('[aria-label^="Atmosphere preset"]').count(), 0);
  assert.equal(await Card("Baked atmosphere").count(), 1);
  await Page.getByRole("button", {
    name: "Expand AtmosphereLab",
    exact: true,
  }).click();
  const Lab = Page.getByRole("dialog", { name: "AtmosphereLab", exact: true });
  await Lab.waitFor();
  const Canvas = Lab.locator("canvas");
  await Page.waitForFunction(
    () =>
      !!document.querySelector(".atmosphere-lab-window canvas")?.dataset.render,
  );
  assert.equal(await Lab.locator('[aria-label^="Atmosphere preset"]').count(), 0);
  await Page.screenshot({ path: Proof + "/AtmosphereLab.png" });
  await Page.setViewportSize({ width: 1366, height: 768 });
  await Page.screenshot({ path: Proof + "/AtmosphereDesktop.png" });
  await Page.setViewportSize({ width: 1440, height: 1000 });
  await Lab.getByLabel("Limb thickness · visual ×", { exact: true }).focus();
  await Page.keyboard.press("Tab");
  assert.equal(
    await Lab.getByRole("button", {
      name: "Close AtmosphereLab",
      exact: true,
    }).evaluate((Node) => document.activeElement === Node),
    true,
  );

  let Image = await Canvas.evaluate((Node) => Node.toDataURL());
  const Box = await Canvas.boundingBox();
  await Page.mouse.move(Box.x + Box.width * 0.5, Box.y + Box.height * 0.5);
  await Page.mouse.down();
  await Page.mouse.move(Box.x + Box.width * 0.7, Box.y + Box.height * 0.55, {
    steps: 5,
  });
  await Page.mouse.up();
  await Page.waitForFunction(
    (Previous) =>
      document.querySelector(".atmosphere-lab-window canvas").toDataURL() !==
      Previous,
    Image,
  );
  const Yaw = (await Saved()).Values.atmosphere["Space yaw"];
  await Canvas.focus();
  await Page.keyboard.press("ArrowLeft");
  assert.notEqual((await Saved()).Values.atmosphere["Space yaw"], Yaw);
  await Lab.getByRole("button", { name: "Animate orbit", exact: true }).click();
  const Tick = (await Saved()).Values.atmosphere["Space yaw"];
  await Page.waitForTimeout(650);
  assert.notEqual((await Saved()).Values.atmosphere["Space yaw"], Tick);
  await Lab.getByRole("button", { name: "Pause orbit", exact: true }).click();
  for (const Width of [1024, 1280, 1366, 1920]) {
    await Page.setViewportSize({ width: Width, height: 900 });
    assert.ok(
      await Lab.evaluate((Node) => Node.scrollWidth <= Node.clientWidth + 1),
    );
    assert.ok(
      await Lab.locator(".atmosphere-lab-body").evaluate(
        (Node) => Node.scrollWidth <= Node.clientWidth + 1,
      ),
    );
  }
  await Page.keyboard.press("Escape");
  assert.equal(await Lab.count(), 0);
  assert.equal(await Page.locator("#Editor").getAttribute("inert"), null);
  assert.equal(
    await Page.getByRole("button", {
      name: "Expand AtmosphereLab",
      exact: true,
    }).evaluate((Node) => document.activeElement === Node),
    true,
  );
  await Page.getByRole("button", {
    name: "Animate orbit",
    exact: true,
  }).click();
  await Page.getByRole("button", {
    name: "Expand AtmosphereLab",
    exact: true,
  }).click();
  assert.equal(
    await Lab.getByRole("button", { name: "Pause orbit", exact: true }).count(),
    1,
  );
  await Lab.getByRole("button", { name: "Pause orbit", exact: true }).click();
  await Page.keyboard.press("Escape");
  const PausedYaw = (await Saved()).Values.atmosphere["Space yaw"];
  await Page.waitForTimeout(500);
  assert.equal((await Saved()).Values.atmosphere["Space yaw"], PausedYaw);
  await Page.reload({ waitUntil: "networkidle" });
  await Select("Atmosphere");
  assert.equal(await Card("Atmosphere overview").count(), 1);
  Checks.push(
    "Atmosphere overview card (F53) without presets; responsive orbital single-scatter preview; changed pixels on orbit; animation, modal focus/inert and reload",
  );
  // Stars, Moons, Lens Flare, Precipitation, Rainbow and Editor Camera only had the removed bar maps.
  for (const Name of [
    "Clouds",
    "Local Cloud",
    "Height Fog",
    "Atmospheric Fog",
    "Local Fog",
  ]) {
    await Select(Name);
    assert.ok(
      await Page.locator("[data-live-plot]").count(),
      Name + " has interactive data",
    );
  }
  // Generic "Live parameter map" bar charts were removed; none may appear anywhere.
  for (const Name of ["Post Process", "Area Light", "Clouds", "Cube"]) {
    await Select(Name);
    assert.equal(await Page.locator(".property-graph").count(), 0, Name);
  }
  await Select("Cube");
  assert.equal(await Page.locator(".environment-live").count(), 0);
  Checks.push(
    "Environment/camera cards expose actual data graphs; no generic parameter bar maps anywhere",
  );

  await Select("Atmospheric Fog");
  if (!(await Saved()).Values["aerial-fog"].Enabled)
    await Page.getByRole("button", { name: "Enabled", exact: true }).click();
  const Spectrum = Page.getByRole("slider", {
      name: "Aerial fog spectrum",
      exact: true,
    }),
    SpectralBefore = await Spectrum.locator("[data-series]").getAttribute("d");
  await Card("Spectral transmission")
    .getByRole("slider", { name: "Mie Blend", exact: true })
    .focus();
  await Page.keyboard.press("End");
  assert.notEqual(
    await Spectrum.locator("[data-series]").getAttribute("d"),
    SpectralBefore,
  );
  await Spectrum.focus();
  await Page.keyboard.press("Home");
  assert.equal(
    (await Saved()).Values["aerial-fog"]["Spectral wavelength"],
    380,
  );
  await Select("Local Cloud");
  const Bounds = Card("Local bounds").locator(".cloud-bounds-plot"),
    BoundPath = await Bounds.locator("path").first().getAttribute("d");
  await Page.getByRole("slider", {
    name: "Cloud half size X graph",
    exact: true,
  }).focus();
  await Page.keyboard.press("ArrowUp");
  assert.equal((await Saved()).Values["local-cloud"]["Half Size"][0], 101);
  assert.notEqual(
    await Bounds.locator("path").first().getAttribute("d"),
    BoundPath,
  );
  assert.match(
    await Card("Local bounds").locator(".cloud-bounds-data").innerText(),
    /202/,
  );
  await Page.getByRole("slider", {
    name: "Cloud vertical section",
    exact: true,
  }).focus();
  await Page.keyboard.press("ArrowRight");
  assert.ok(
    Number.isFinite(
      (await Saved()).Values["local-cloud"]["Cloud probe altitude"],
    ),
  );
  const CloudX = Page.getByRole("slider", {
    name: "Cloud half size X graph",
    exact: true,
  });
  await CloudX.scrollIntoViewIfNeeded();
  const Handle = await CloudX.locator("circle").boundingBox();
  await Page.mouse.move(
    Handle.x + Handle.width / 2,
    Handle.y + Handle.height / 2,
  );
  await Page.mouse.down();
  await Page.mouse.move(
    Handle.x + Handle.width / 2 + 20,
    Handle.y + Handle.height / 2 + 10,
    { steps: 4 },
  );
  await Page.mouse.up();
  assert.ok((await Saved()).Values["local-cloud"]["Half Size"][0] > 101);
  await Page.setViewportSize({ width: 1366, height: 768 });
  await Card("Local bounds").scrollIntoViewIfNeeded();
  await Page.screenshot({ path: Proof + "/LocalCloudBounds.png" });
  await Select("Local Fog");
  const TouchPlot = Card("Visibility through fog").getByRole("slider", {
    name: "Fog distance probe",
    exact: true,
  });
  await TouchPlot.scrollIntoViewIfNeeded();
  const TouchBox = await TouchPlot.boundingBox(),
    CDP = await Page.context().newCDPSession(Page);
  await CDP.send("Emulation.setTouchEmulationEnabled", {
    enabled: true,
    maxTouchPoints: 1,
  });
  await CDP.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: [
      {
        x: TouchBox.x + TouchBox.width * 0.25,
        y: TouchBox.y + TouchBox.height * 0.5,
      },
    ],
  });
  await CDP.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: [
      {
        x: TouchBox.x + TouchBox.width * 0.97,
        y: TouchBox.y + TouchBox.height * 0.5,
      },
    ],
  });
  await CDP.send("Input.dispatchTouchEvent", {
    type: "touchEnd",
    touchPoints: [],
  });
  assert.equal((await Saved()).Values["local-fog"]["Probe distance"], 2000);
  await CDP.detach();
  Checks.push(
    "Aerial spectral mixture, cloud vertical probes, editable local bounds/derived extents, and real touch drag/clamping",
  );
  assert.deepEqual(Errors, []);
} catch (Error) {
  Errors.push(Error.stack);
  await Page.screenshot({ path: Proof + "/EnvironmentFailure.png" }).catch(
    () => {},
  );
  process.exitCode = 1;
} finally {
  fs.writeFileSync(
    Proof + "/EnvironmentGraphs.json",
    JSON.stringify({ Checks, Errors }, null, 2),
  );
  console.log(JSON.stringify({ Checks, Errors }, null, 2));
  await Browser.close();
}
