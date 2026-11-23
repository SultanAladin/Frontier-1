import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"),
);
const { chromium } = Require("playwright");
const Proof =
  process.env.FRONTIER_PROOF_FOLDER ||
  path.join(Folder, "Screenshots/SharedCloudLighting/LightDesign");
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  headless: true,
  args: ["--no-sandbox", "--disable-gpu"],
});
const Page = await Browser.newPage({ viewport: { width: 1366, height: 900 } });
const Checks = [],
  Errors = [],
  FontFailures = [];
Page.on("pageerror", (Error) => Errors.push(Error.stack));
Page.on("requestfailed", (Request) => {
  if (Request.url().includes("fontshare.com")) FontFailures.push(Request.url());
});
Page.setDefaultTimeout(12000);
const Address =
  process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll > [data-panel]").waitFor();
};
const Frame = () =>
  Page.frameLocator('iframe[title^="Reference inspector"]').first();
const Ready = async (Id) => {
  await Open(Id);
  await Frame().locator(".lp-preview").waitFor();
};
const Stored = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
const Persist = async (Id, Key, Next) =>
  Page.waitForFunction(
    ({ Id, Key, Next }) =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1"))?.Values?.[
        Id
      ]?.ReferenceInspector?.Properties?.[Key] === Next,
    { Id, Key, Next },
  );
const Edit = async (Id, Key, Next) => {
  await Frame()
    .locator('input[type=number][data-light-property="' + Key + '"]')
    .fill(String(Next));
  await Persist(Id, Key, Next);
};
const Plot = () =>
  Frame()
    .locator(".lp-preview canvas")
    .evaluate((Canvas) => Canvas.toDataURL());
const Capture = async (Name) => {
  await Page.locator(".inspector-scroll").evaluate(
    (Scroll) => (Scroll.scrollTop = 0),
  );
  await Page.waitForTimeout(180);
  await Page.screenshot({ path: path.join(Proof, Name + ".png") });
};
// Park the app before editing a fixture so asynchronous iframe messages cannot overwrite it.
await Page.route(Address + "Fixture", (Route) =>
  Route.fulfill({
    contentType: "text/html",
    body: "<!doctype html><title>Scene fixture</title>",
  }),
);
const Park = () => Page.goto(Address + "Fixture");
try {
  const Sources = [
    "reference-rim-point",
    "reference-key-spot",
    "reference-ies-downlight",
    "reference-softbox",
    "reference-studio-tube",
    "reference-led",
    "reference-led-strip",
  ];
  const Pictures = [];
  for (const Id of Sources) {
    await Ready(Id);
    assert.equal(await Frame().locator(".ident").count(), 0);
    assert.equal(await Frame().locator(".lp-source-icon img").count(), 1);
    assert.equal(await Frame().locator(".lp-readings .mp-pill").count(), 2);
    assert.equal(
      await Frame().locator(".lp-transform .transform-card").count(),
      1,
    );
    assert(
      await Frame()
        .locator(".lp-primary")
        .evaluate(
          (Reading) => parseFloat(getComputedStyle(Reading).fontSize) >= 38,
        ),
    );
    Pictures.push(await Plot());
    await Capture(Id);
    await Page.locator('iframe[title^="Reference inspector"]').evaluate(
      (Iframe) => (Iframe.style.width = "240px"),
    );
    await Page.waitForTimeout(100);
    assert(
      await Frame()
        .locator("body")
        .evaluate((Body) => Body.scrollWidth <= innerWidth),
      Id + " narrow overflow",
    );
    assert(
      await Frame()
        .locator(".mp-pill")
        .evaluateAll((Tiles) =>
          Tiles.every((Tile) => Tile.scrollWidth <= Tile.clientWidth + 1),
        ),
      Id + " narrow metrics",
    );
    if (Id === "reference-led-strip") await Capture("StripNarrow");
    Checks.push(
      Id +
        ": library icon and source diagram, strong numeric hierarchy, standard Transform and 240px layout",
    );
  }
  assert.equal(new Set(Pictures).size, 7);
  const Fresh = await Stored();
  const Icons = Sources.map(
    (Id) => Fresh.Rows.find((Row) => Row.Id === Id).Icon,
  );
  assert.equal(new Set(Icons).size, 7);
  Checks.push(
    "Seven source families have distinct diagram pixels and outliner icons",
  );
  await Ready("reference-led");
  await Edit("reference-led", "watts", 12);
  await Edit("reference-led", "efficacy", 125);
  await Edit("reference-led", "dimmer", 0.5);
  assert.equal(await Frame().locator(".lp-integer").innerText(), "750");
  const Emitter = await Plot();
  await Edit("reference-led", "angle", 45);
  assert.notEqual(await Plot(), Emitter);
  await Ready("reference-led");
  assert.equal(await Frame().locator(".lp-integer").innerText(), "750");
  assert(
    (await Frame().locator(".lp-primary-caption").innerText()).includes(
      "Estimated",
    ),
  );
  Checks.push(
    "LED wattage × efficacy × dimmer is explicitly estimated, updates live and survives reload; optic changes diagram",
  );
  await Ready("reference-led-strip");
  await Edit("reference-led-strip", "length", 3);
  await Edit("reference-led-strip", "lumensPerMetre", 1200);
  await Edit("reference-led-strip", "wattsPerMetre", 12);
  await Edit("reference-led-strip", "ledsPerMetre", 120);
  await Edit("reference-led-strip", "dimmer", 0.5);
  assert.equal(await Frame().locator(".lp-integer").innerText(), "1,800");
  assert((await Frame().locator(".lp-readings").innerText()).includes("36 W"));
  assert(
    (await Frame().locator(".lp-readings").innerText()).includes("360 LEDs"),
  );
  const HorizontalStrip = await Plot();
  assert(HorizontalStrip.length > 0);
  await Frame()
    .getByRole("switch", { name: "Opal diffuser", exact: true })
    .click();
  await Persist("reference-led-strip", "diffuser", true);
  assert.notEqual(await Plot(), HorizontalStrip);
  await Ready("reference-led-strip");
  assert.equal(
    await Frame()
      .getByRole("slider", { name: "Strip length", exact: true })
      .inputValue(),
    "3",
  );
  assert.equal(
    await Frame()
      .getByRole("switch", { name: "Opal diffuser", exact: true })
      .getAttribute("aria-checked"),
    "true",
  );
  await Capture("StripHorizontal");
  Checks.push(
    "Strip length/output/density/load estimates, horizontal run and diffuser respond and persist",
  );
  await Ready("reference-ies-downlight");
  const Lobes = [];
  for (const Profile of [
    "Wall wash",
    "Downlight",
    "Batwing",
    "High Beam",
    "Fog Lamp",
  ]) {
    await Frame().getByRole("button", { name: Profile, exact: true }).click();
    await Persist("reference-ies-downlight", "profile", Profile);
    Lobes.push(await Plot());
  }
  assert.equal(new Set(Lobes).size, 5);
  assert(
    (await Frame().locator(".lp-shape .lp-note").innerText()).includes(
      "IES file import pending",
    ),
  );
  Checks.push(
    "Architectural and automotive IES presets have distinct synthetic lobes and clearly disclose pending file import",
  );
  await Ready("reference-softbox");
  const Rectangle = await Plot();
  await Frame().getByRole("button", { name: "Disk", exact: true }).click();
  await Persist("reference-softbox", "aperture", "Disk");
  assert.notEqual(await Plot(), Rectangle);
  await Ready("reference-softbox");
  assert.equal(
    await Frame()
      .getByRole("button", { name: "Disk", exact: true })
      .getAttribute("aria-pressed"),
    "true",
  );
  Checks.push(
    "Area rectangle/disk aperture changes the illustration and survives reload",
  );
  await Park();
  await Page.evaluate(() => {
    const Saved = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    Saved.Values["reference-led-strip"].ReferenceInspector.Locked = true;
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Ready("reference-led-strip");
  assert(
    await Frame()
      .locator(".lighting-panel input,.lighting-panel button")
      .evaluateAll((Controls) => Controls.every((Control) => Control.disabled)),
  );
  await Page.getByRole("button", {
    name: "Unlock editing",
    exact: true,
  }).click();
  await Edit("reference-led-strip", "voltage", 12);
  Checks.push(
    "LED strip lock covers numeric fields, diffuser and standard Transform; parent unlock restores editing",
  );
  await Page.getByRole("button", { name: "Construct", exact: true }).click();
  await Page.locator(".construct-matrix").waitFor();
  await Page.locator(".construct-categories")
    .getByRole("button", { name: "Lighting", exact: true })
    .click();
  assert.equal(await Page.locator(".construct-entity").count(), 10);
  await Page.getByRole("button", { name: "LED Strip", exact: true }).click();
  const Construct = Page.frameLocator(".construct-properties iframe");
  await Construct.locator(".lp-preview").waitFor();
  await Construct.getByLabel("Strip length value", { exact: true }).fill("4.5");
  await Page.getByLabel("Construct entity name", { exact: true }).fill(
    "Cove accent",
  );
  await Page.getByRole("button", { name: "Add to scene", exact: true }).click();
  await Page.waitForFunction(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Rows.some(
      (Row) => Row.Name === "Cove accent",
    ),
  );
  const Added = (await Stored()).Rows.find((Row) => Row.Name === "Cove accent");
  assert.equal(Added.ReferenceType, "ledstrip");
  await Ready(Added.Id);
  assert.equal(
    await Frame()
      .getByLabel("Strip length value", { exact: true })
      .inputValue(),
    "4.5",
  );
  Checks.push(
    "Construct offers all ten light entries; new LED strip can be edited before placement and retains its authored parameters",
  );
  await Page.getByRole("button", {
    name: "Outliner menu",
    exact: true,
  }).click();
  const Downloading = Page.waitForEvent("download");
  await Page.getByRole("button", {
    name: "Export HTML UI state…",
    exact: true,
  }).click();
  const Download = await Downloading;
  const Exported = JSON.parse(fs.readFileSync(await Download.path(), "utf8"));
  assert.equal(Exported.LightDesignRevision, 1);
  assert.equal(
    Exported.Values[Added.Id].ReferenceInspector.Properties.length,
    4.5,
  );
  Checks.push(
    "Scene JSON export includes the light migration revision and newly authored LED strip properties",
  );
  await Page.locator(
    'input[type=file][accept="application/json"]',
  ).setInputFiles({
    name: "Lights.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(Exported)),
  });
  await Page.getByRole("status")
    .filter({ hasText: "HTML UI state imported" })
    .waitFor();
  await Ready(Added.Id);
  assert.equal(
    await Frame()
      .getByLabel("Strip length value", { exact: true })
      .inputValue(),
    "4.5",
  );
  Checks.push(
    "Exported scene reimports with its LED source family, authored parameters and revision intact",
  );
  await Park();
  await Page.evaluate(() => {
    const Saved = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    delete Saved.LightDesignRevision;
    Saved.Rows = Saved.Rows.filter(
      (Row) =>
        ![
          "reference-key-spot",
          "reference-led",
          "reference-led-strip",
          "reference-ies-downlight",
        ].includes(Row.Id),
    );
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Ready("reference-led");
  let Migrated = await Stored();
  assert.equal(Migrated.LightDesignRevision, 1);
  assert(!Migrated.Rows.some((Row) => Row.Id === "reference-key-spot"));
  for (const Id of [
    "reference-led",
    "reference-led-strip",
    "reference-ies-downlight",
  ])
    assert.equal(Migrated.Rows.filter((Row) => Row.Id === Id).length, 1);
  assert.equal(
    Migrated.Rows.find((Row) => Row.Id === Added.Id).Name,
    "Cove accent",
  );
  Checks.push(
    "C049 scene migration adds only the three new defaults once; deleted old lights and user-created sources remain respected",
  );
  await Park();
  await Page.evaluate(() => {
    const Saved = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    Saved.Rows = Saved.Rows.filter((Row) => Row.Id !== "reference-led");
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Open("sun");
  assert(!(await Stored()).Rows.some((Row) => Row.Id === "reference-led"));
  Checks.push(
    "Deleting a new light after migration does not resurrect it on reload",
  );
  await Open("wind");
  await Frame().locator(".wf-trace").waitFor();
  assert.equal(
    await Page.locator(
      '[data-panel="wind"] > header + .reference-inspector-copy',
    ).count(),
    1,
  );
  const Order = await Page.locator(
    '[data-panel="wind"] > .reference-inspector-copy,[data-panel="wind"] > [data-card="Wind field"]',
  ).evaluateAll((Cards) =>
    Cards.map((Card) => Card.dataset.card || "Anemometer"),
  );
  assert.deepEqual(Order, ["Anemometer", "Wind field"]);
  await Capture("WindOrder");
  Checks.push(
    "Wind heading → Anemometer and statistics → Composite Flow, retaining the live trace",
  );
  for (const Id of ["height-fog", "aerial-fog", "local-fog"]) {
    await Open(Id);
    const Visibility = Page.locator('[data-card="Visibility through fog"] [data-series]'),
      Before = await Visibility.getAttribute("d");
    await Page.getByLabel("Density value", { exact: true }).fill(
      Id === "height-fog" ? "0.08" : "2",
    );
    assert.notEqual(await Visibility.getAttribute("d"), Before);
    assert.equal(
      await Page.locator('[data-card="Medium"] .fog-shared-beam').count(),
      1,
    );
    await Capture(Id);
    Checks.push(Id + ": shared Visibility and Medium cards respond to model density");
  }
  assert.deepEqual(Errors, []);
  console.log("Light design checks passed:", Checks.length);
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
    JSON.stringify(
      { Checks, FontFailures: [...new Set(FontFailures)], Errors },
      null,
      2,
    ) + "\n",
  );
  await Browser.close();
}
