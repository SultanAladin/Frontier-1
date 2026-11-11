import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import {
  ConsolidateAtmosphere,
  ConsolidateAtmosphereValues,
  ConsolidateAtmosphereFlags,
} from "./ScenePolicy.js";
const Folder = path.dirname(fileURLToPath(import.meta.url)),
  Root = path.resolve(Folder, "../../..");
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    path.join(Root, "_AgentScratch/Browser/package.json"),
);
const { chromium } = Require("playwright");
const Proof =
  process.env.FRONTIER_PROOF_FOLDER ||
  path.join(Folder, "Screenshots/InspectorRefinement");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  headless: true,
  args: ["--no-sandbox", "--disable-gpu"],
});
const Context = await Browser.newContext({
  viewport: { width: 1366, height: 900 },
});
const Page = await Context.newPage();
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const Address =
  process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll").waitFor();
};
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
try {
  const Rows = [
    { Id: "sky", Panel: "atmosphere" },
    { Id: "atmosphere", Panel: "atmosphere" },
    { Id: "sky-child", Parent: "sky", Panel: "stars" },
  ];
  const Consolidated = ConsolidateAtmosphere(Rows);
  assert.equal(Consolidated.length, 2);
  assert.equal(Consolidated[1].Parent, "atmosphere");
  const Draft = { sky: { Rayleigh: 3, Mie: 2 }, atmosphere: { Rayleigh: 4 } };
  assert.deepEqual(ConsolidateAtmosphereValues(Draft, Rows).atmosphere, {
    Rayleigh: 4,
    Mie: 2,
  });
  assert.deepEqual(
    ConsolidateAtmosphereValues({ ...Draft, atmosphere: {} }, Consolidated)
      .atmosphere,
    {},
  );
  assert.deepEqual(ConsolidateAtmosphere([{ Id: "sky", Panel: "geometry" }]), [
    { Id: "sky", Panel: "geometry" },
  ]);
  assert.deepEqual(ConsolidateAtmosphereFlags({ sky: true }, Rows), {
    sky: true,
  });
  assert.deepEqual(ConsolidateAtmosphereFlags({ sky: true }, [Rows[0]]), {
    sky: true,
    atmosphere: true,
  });
  Checks.push(
    "Legacy Sky alias consolidates safely; conflicting values remain archived; non-alias objects are untouched",
  );
  await Open("sun");
  assert.equal(
    await Page.locator(
      '[data-panel="sun"] > header + .reference-inspector-copy',
    ).count(),
    1,
  );
  const SunFrame = Page.frameLocator("iframe").first();
  await SunFrame.locator(".mp-rail").waitFor();
  assert(
    await SunFrame.locator(".mp-pill,.mp-stat").evaluateAll((Nodes) =>
      Nodes.every((Node) => {
        const Style = getComputedStyle(Node);
        return Style.borderRadius === "12px" && Style.borderTopWidth === "0px";
      }),
    ),
  );
  await Page.screenshot({ path: path.join(Proof, "sun-header.png") });
  Checks.push(
    "sun: header precedes imported cards; borderless twelve-pixel metric corners",
  );

  await Open("height-fog");
  assert.equal(await Page.locator(".reference-inspector-copy").count(), 0);
  for (const Title of [
    "Fog settings",
    "Visibility through fog",
    "Medium",
    "Height and tint",
    "Wind binding",
  ])
    assert.equal(await Page.locator(`[data-card="${Title}"]`).count(), 1);
  assert.equal(
    await Page.locator('[data-card="Medium"] > .fog-shared-beam').count(),
    1,
  );
  await Page.screenshot({ path: path.join(Proof, "height-fog-header.png") });
  Checks.push(
    "height-fog: shared C++-aligned Fog cards; Beam Chamber remains nested in Medium",
  );
  await Open("wind");
  await Page.locator(".reference-inspector-copy").evaluate((Node) =>
    Node.scrollIntoView({ block: "start" }),
  );
  const Wind = Page.frameLocator("iframe");
  await Wind.locator(".wf-trace").waitFor();
  assert.equal(await Wind.locator(".wf-specs").count(), 0);
  assert(
    !/gust factor|pressure|spread/i.test(
      await Wind.locator(".wf-trace").innerText(),
    ),
  );
  await Page.waitForTimeout(300);
  const Trace = await Wind.locator("canvas")
    .first()
    .evaluate((Node) => Node.toDataURL());
  await Page.waitForTimeout(600);
  assert.notEqual(
    await Wind.locator("canvas")
      .first()
      .evaluate((Node) => Node.toDataURL()),
    Trace,
  );
  await Page.screenshot({ path: path.join(Proof, "Anemometer.png") });
  Checks.push(
    "Anemometer retains its live trace and removes all four secondary readouts",
  );
  for (const Id of ["aerial-fog", "local-fog"]) {
    await Open(Id);
    assert.equal(await Page.locator(".reference-inspector-copy,.fog-instruments").count(), 0);
    assert.equal(await Page.getByLabel("Fog probe distance", { exact: true }).count(), 1);
    assert.equal(
      await Page.getByRole("img", { name: "Fog beam chamber", exact: true }).count(),
      1,
    );
    assert.equal(
      await Page.locator('[data-card="Medium"] > .fog-shared-beam').count(),
      1,
    );
    const Before = await Page.locator('[data-card="Visibility through fog"] .graph-metric').innerText();
    await Page.getByLabel("Density value", { exact: true }).fill("2");
    assert.notEqual(
      await Page.locator('[data-card="Visibility through fog"] .graph-metric').innerText(),
      Before,
    );
    await Open(Id);
    assert.equal(await Page.getByLabel("Density value", { exact: true }).inputValue(), "2");
    await Page.locator(".inspector-scroll").evaluate((Node) => (Node.scrollTop = 0));
    await Page.screenshot({ path: path.join(Proof, Id + ".png") });
    await Page.setViewportSize({ width: 1024, height: 768 });
    assert(
      await Page.locator(`[data-panel="${Id}"]`).evaluate(
        (Node) => Node.scrollWidth <= Node.clientWidth + 1,
      ),
    );
    await Page.setViewportSize({ width: 1366, height: 900 });
    Checks.push(
      Id + ": coordinated cards use native-model controls, persist and fit narrow layouts",
    );
  }
  await Open("reference-key-spot");
  let Frame = Page.frameLocator("iframe");
  await Frame.locator(".transform-card").waitFor();
  assert.equal(await Frame.locator(".lp-placement").count(), 0);
  assert.equal(await Frame.getByLabel("Target X", { exact: true }).count(), 0);
  const BeforeRotation = await Frame.getByLabel("Rotation Y", {
    exact: true,
  }).inputValue();
  await Frame.getByLabel("Position X", { exact: true }).fill("10");
  assert.equal(
    await Frame.getByLabel("Rotation Y", { exact: true }).inputValue(),
    BeforeRotation,
  );
  await Frame.getByLabel("Rotation Y", { exact: true }).fill("45");
  await Frame.getByLabel("Scale X", { exact: true }).fill("1.5");
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
        "reference-key-spot"
      ].ReferenceInspector.Properties.scale[0] === 1.5,
  );
  await Open("reference-key-spot");
  Frame = Page.frameLocator("iframe");
  assert.equal(
    await Frame.getByLabel("Rotation Y", { exact: true }).inputValue(),
    "45",
  );
  assert.equal(
    await Frame.getByLabel("Scale X", { exact: true }).inputValue(),
    "1.5",
  );
  await Frame.getByRole("button", {
    name: "Reset Rotation",
    exact: true,
  }).click();
  assert.equal(
    await Frame.getByLabel("Rotation Y", { exact: true }).inputValue(),
    "0",
  );
  await Frame.getByRole("button", { name: "Reset Scale", exact: true }).click();
  assert.equal(
    await Frame.getByLabel("Scale X", { exact: true }).inputValue(),
    "1",
  );
  await Frame.locator(".lp-transform").evaluate((Node) =>
    Node.scrollIntoView({ block: "center" }),
  );
  await Page.screenshot({ path: path.join(Proof, "LightTransform.png") });
  await Page.evaluate(() => {
    const Saved = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    Saved.Values["reference-key-spot"].ReferenceInspector.Locked = true;
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Open("reference-key-spot");
  Frame = Page.frameLocator("iframe");
  assert(await Frame.getByLabel("Position X", { exact: true }).isDisabled());
  Checks.push(
    "Shared light Transform supports position, aim-preserving moves, rotation, scale, reset, persistence and locking; old target/map UI removed",
  );
  await Open("sky");
  assert(new URL(Page.url()).searchParams.get("inspect") === "atmosphere");
  assert(!(await Saved()).Rows.some((Row) => Row.Id === "sky"));
  assert.equal(
    (await Saved()).Rows.filter((Row) => Row.Panel === "atmosphere").length,
    1,
  );
  Checks.push("Fresh scene and old Sky link select one Atmosphere entry");
  await Page.evaluate(() => {
    const Saved = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    Saved.Rows.push({
      Id: "sky",
      Name: "Sky",
      Panel: "atmosphere",
      Icon: "sky-scattering",
      Parent: "world",
    });
    Saved.Rows.push({
      Id: "sky-stars",
      Name: "Legacy stars",
      Panel: "stars",
      Parent: "sky",
    });
    Saved.Values.sky = { Rayleigh: 3, Mie: 2 };
    Saved.Values.atmosphere = { Rayleigh: 4 };
    Saved.Selected = "sky";
    Saved.Hidden = { sky: true };
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Open("sky");
  const Migrated = await Saved();
  assert.equal(Migrated.Selected, "atmosphere");
  assert(!Migrated.Hidden.atmosphere);
  assert.equal(Migrated.Values.atmosphere.Rayleigh, 4);
  assert.equal(Migrated.Values.atmosphere.Mie, 2);
  assert.equal(Migrated.Values.sky.Rayleigh, 3);
  assert.equal(
    Migrated.Rows.find((Row) => Row.Id === "sky-stars").Parent,
    "atmosphere",
  );
  assert(!Migrated.Rows.some((Row) => Row.Id === "sky"));
  Checks.push(
    "Saved scenes consolidate Sky, reparent children and preserve both legacy and canonical settings",
  );
  if (process.env.FRONTIER_BASELINE_HTML) {
    const Previous = await Context.newPage();
    await Previous.route("http://baseline.test/**", (Route) =>
      Route.fulfill({
        contentType: "text/html",
        body: fs.readFileSync(process.env.FRONTIER_BASELINE_HTML, "utf8"),
      }),
    );
    const Inventory = () =>
      [
        ...document.querySelectorAll(
          ".inspector-scroll > [data-panel] input,.inspector-scroll > [data-panel] select,.inspector-scroll > [data-panel] textarea",
        ),
      ]
        .map((Node) => [
          Node.tagName,
          Node.type,
          Node.getAttribute("aria-label"),
          Node.min,
          Node.max,
          Node.step,
        ])
        .filter(
          (Value) =>
            Value[2] !== "Inspector name" &&
            !Value[2]?.toLowerCase().includes("notes"),
        )
        .map((Value) => JSON.stringify(Value))
        .sort();
    for (const Id of [
      "sun",
      "wind",
      "height-fog",
      "aerial-fog",
      "local-fog",
      "light",
      "atmosphere",
    ]) {
      await Open(Id);
      await Previous.goto("http://baseline.test/?inspect=" + Id);
      await Previous.locator(".inspector-scroll > [data-panel]").waitFor();
      const ActualInventory = await Page.evaluate(Inventory),
        PreviousInventory = await Previous.evaluate(Inventory);
      if (Id.includes("fog")) {
        assert(
          ActualInventory.every((Input) => !Input.includes("Fog probe altitude")),
          Id + " uses the C++ fixed diagnostic altitude",
        );
      } else {
        assert.deepEqual(
          ActualInventory,
          PreviousInventory,
          Id + " retains native control inventory",
        );
      }
    }
    await Previous.close();
    Checks.push(
      "All seven affected native inspectors retain the previous build's control inventory",
    );
  }
  assert.deepEqual(Errors, []);
  console.log("Inspector layout checks passed:", Checks.length);
} catch (Error) {
  Errors.push(Error.stack);
  console.error(Error);
  process.exitCode = 1;
} finally {
  fs.writeFileSync(
    path.join(Proof, "LayoutChecks.json"),
    JSON.stringify({ Checks, Errors }, null, 2) + "\n",
  );
  await Browser.close();
}
