//============================================================================================================================================
//                                                           CHECKINSPECTORORDER.MJS
//============================================================================================================================================
// 📦 Cross-inspector section ordering, baking placement and live light-response regressions.

import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { PickTab } from "./TabReveal.mjs";
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"),
);
const { chromium } = Require("playwright");
const Proof =
  process.env.FRONTIER_PROOF_FOLDER ||
  path.join(Folder, "Screenshots/InspectorOrder");
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  args: ["--no-sandbox", "--disable-gpu"],
});
const Page = await Browser.newPage({ viewport: { width: 1440, height: 1000 } });
Page.setDefaultTimeout(12000);
const Checks = [],
  Errors = [],
  FontFailures = [],
  Inventory = [];
Page.on("pageerror", (Error) => Errors.push(Error.stack));
Page.on("requestfailed", (Request) => {
  if (Request.url().includes("fontshare.com")) FontFailures.push(Request.url());
});
const Address =
  process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Card = (Title) => `[data-card="${Title}"]`;
const Frame = (Slice) =>
  Page.frameLocator(
    Slice
      ? `[data-reference-slice="${Slice}"] iframe`
      : 'iframe[title^="Reference inspector"]',
  ).first();
const Node = (Entry) =>
  typeof Entry === "string"
    ? Page.locator(Entry).first()
    : Frame(Entry[0]).locator(Entry[1]).first();
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
async function Open(Id, Wide = false) {
  await Page.goto(
    Address + "?inspect=" + Id + (Wide ? "&workspace=inspector" : ""),
  );
  // A Controlled Vehicle component opens in its Component Viewer panel (Follow-up 62), whose Details come first in the page.
  await Page.locator(".inspector-scroll > [data-panel]").first().waitFor();
  for (const Iframe of await Page.locator(
    ".reference-inspector-copy iframe",
  ).all())
    await Iframe.contentFrame().locator(".mpanel > *").first().waitFor();
  await Page.waitForTimeout(150);
}
// Inspectors with lettered tabs (InspectorTabs.jsx): opens the tab holding an entry, returning its place among the tabs (or -1).
async function Reveal(Entry) {
  if (typeof Entry !== "string") return -1;
  const Tab = await Node(Entry).evaluate((Each) => Each.closest("[data-tab-page]")?.dataset.tabPage || null);
  if (!Tab) return -1;
  await PickTab(Page, Tab);
  return Page.locator(`[data-tab-page="${Tab}"]`).evaluate((Each) => [...Each.parentElement.querySelectorAll(":scope > [data-tab-page]")].indexOf(Each));
}
async function Sequence(Name, Entries) {
  let Bottom = -Infinity, Opened = -1;
  for (const Entry of Entries) {
    // Entries run through the tabs in order; each tab starts at its own top.
    const Tab = await Reveal(Entry);
    assert(Tab >= Opened, Name + " tab order " + JSON.stringify(Entry));
    if (Tab !== Opened) Bottom = -Infinity;
    Opened = Tab;
    const Rect = await Node(Entry).boundingBox();
    assert(Rect && Rect.height > 0, Name + " missing " + JSON.stringify(Entry));
    assert(
      Rect.y >= Bottom - 2,
      Name + " order/overlap " + JSON.stringify({ Entry, Rect, Bottom }),
    );
    Bottom = Rect.y + Rect.height;
  }
}
const Sections = {
  group: [
    ".collection-total",
    ".collection-status",
    ".collection-composition",
    ".collection-browser",
    ".collection-notes",
  ],
  // Showcase primitives: the cards come first (Follow-up 59: the category navigation no longer sits at the top); quick controls, then Notes holding the Purpose.
  geometry: [".pi-quick", ".pi-purpose"],
  post: [".entity-capabilities"],
  // Components (object: Mass, Rigid Body, Collision; camera: Render Engine): the owner link, then the cards.
  "object-component": [".oc-owner", ".pi-component [data-card], .rc-cards [data-card]"],
  ...Object.fromEntries(
    [
      "motion-blur",
      "heat-shimmer",
      "glitch",
      "chromatic-aberration",
      "film-grain",
      "lens-distortion",
      "scanlines",
      "sharpen",
      "environment-light",
    ].map((Panel) => [Panel, [".effect-study", ".entity-capabilities"]]),
  ),
  camera: [Card("Lens + field of view"), Card("Aperture study")],
  // Follow-up 48: God Rays (Project Zero specification §7), a generic inspector.
  "god-rays": [".entity-capabilities", "details.generic-card"],
  atmosphere: [
    ".atmosphere-lab-cards",
    Card("Atmosphere settings"),
    Card("Atmospheric scattering"),
    Card("Native atmosphere controls"),
    Card("Atmosphere bake"),
    Card("Baked atmosphere"),
  ],
  sun: [
    Card("Sun now"),
    ".sun-tiles",
    ".sun-properties",
    Card("Sun lighting bake"),
  ],
  flare: [
    Card("Flare composite"),
    Card("Flare settings"),
    Card("Flare source"),
    Card("Legacy preset settings"),
    Card("Lens flare image"),
  ],
  moon: [Card("Lunar phase"), Card("Moon settings"), Card("Moon catalogue")],
  stars: [
    Card("Star field"),
    Card("Stars settings"),
    Card("Twinkle"),
    Card("Renderer scale"),
    Card("Star field bake"),
  ],
  wind: [
    [null, ".wf-trace"],
    [null, ".mp-rail"],
    [null, ".mp-duo"],
    Card("Wind controls"),
  ],
  "height-fog": [Card("Fog layer"), Card("Density by altitude")],
  "aerial-fog": [Card("Aerial perspective"), Card("Spectral transmission")],
  "local-fog": [
    Card("Fog settings"),
    Card("Visibility through fog"),
    Card("Medium"),
  ],
  clouds: [
    ["summary", ".cl-hero"],
    ["summary", ".mp-duo"],
    Card("Cloud settings"),
    ".cloud-coverage-replacement",
    Card("Cloud base"),
  ],
  "local-cloud": [
    ["summary", ".cl-hero"],
    ["summary", ".mp-duo"],
    Card("Cloud settings"),
    ".cloud-coverage-replacement",
    Card("Cloud base"),
  ],
  precipitation: [
    Card("Precipitation type"),
    Card("Emission + collision"),
    Card("Fall + density"),
  ],
  rainbow: [
    Card("Optical preview"),
    Card("Visibility"),
    Card("Bow response"),
    Card("Bake / image"),
  ],
};
// Vehicle folder rows: every inspector opens with its headline figure, then its fields (Vehicle Movement is data only).
Sections["vehicle-component"] = [".vc-card .metric", ".vc-card .field"];
// Level vehicles (Follow-up 60): the model picture and its paints, then the dimensions with their plan and facts.
Sections["vehicle-asset"] = [".va-hero", ".va-paints", ".va-plan", ".va-facts"];
const BakeCards = {
  sun: ["Sun lighting bake", "Sun disk bake"],
  atmosphere: ["Atmosphere bake", "Baked atmosphere"],
  flare: ["Lens flare image"],
  stars: ["Star field bake"],
  rainbow: ["Bake / image"],
  // Showcase geometry keeps Fracture on its own carousel page, so it has no "last card" rule.
};
try {
  await Open("sun");
  const Rows = (await Saved()).Rows;
  for (const Wide of [false, true])
    for (const Row of Rows) {
      await Open(Row.Id, Wide);
      assert.equal(
        await Page.locator(".inspector-scroll h1").first().count(),
        1,
      );
      assert(await Page.locator(".inspector-scroll .breadcrumbs").count());
      if (Row.Panel === "light") {
        // The readings rail and the source group live inside the Preview card, in that order.
        await Sequence(Row.Id, [
          [null, ".lp-preview"],
          [null, ".lp-participation"],
        ]);
        await Sequence(Row.Id, [
          [null, ".lp-preview .lp-primary"],
          [null, ".lp-preview .lp-readings"],
          [null, ".lp-preview > .lp-output"],
        ]);
        const Controls = Row.ReferenceOnly ? Frame() : Frame("details");
        if (!Row.ReferenceOnly)
          await Sequence(Row.Id, [
            ["summary", ".lp-participation"],
            ".entity-capabilities",
            ["details", ".lp-shape"],
          ]);
        // The analytical study sits inside the optics card's visual, with no card or header of its own.
        assert(
          (await Controls.locator(".lp-shape .lp-shape-visual > .lp-response").count()) <= 1,
        );
        assert.equal(
          await Controls.locator(".lp-response > header").count(),
          0,
        );
        assert.equal(await Controls.locator(".lp-response.lp-card").count(), 0);
        if (Wide) {
          // Wide frames place optics and placement in neighbouring columns.
          const Shape = await Controls.locator(".lp-shape").boundingBox();
          const Transform = await Controls.locator(".lp-transform").boundingBox();
          assert(Shape && Transform && Transform.y >= Shape.y - 2);
        } else
          await Sequence(Row.Id, [
            [Row.ReferenceOnly ? null : "details", ".lp-shape"],
            [Row.ReferenceOnly ? null : "details", ".lp-transform"],
          ]);
      } else await Sequence(Row.Id, Sections[Row.Panel]);
      if (BakeCards[Row.Panel]) {
        const Names = await Page.locator(
          ".inspector-scroll [data-card]",
        ).evaluateAll((Nodes) => Nodes.map((Node) => Node.dataset.card));
        assert.deepEqual(
          Names.slice(-BakeCards[Row.Panel].length),
          BakeCards[Row.Panel],
          Row.Id + " baking must be last",
        );
        const Last = Page.locator(Card(BakeCards[Row.Panel].at(-1)));
        assert(
          await Last.evaluate((Node) => {
            const Root = Node.closest("[data-panel]");
            return [
              ...Root.querySelectorAll(
                "input,select,textarea,button,[data-card]",
              ),
            ].every(
              // The Orbit menu (Follow-up 60) that turns the pages is pinned after them on purpose.
              (Other) =>
                Node.contains(Other) ||
                Other.closest(".orbit-menu") ||
                !(
                  Node.compareDocumentPosition(Other) &
                  Node.DOCUMENT_POSITION_FOLLOWING
                ),
            );
          }),
          Row.Id + " no controls follow baking",
        );
      }
      assert(
        await Page.locator(".inspector-scroll").first().evaluate(
          (Node) => Node.scrollWidth <= Node.clientWidth + 1,
        ),
        Row.Id + " parent overflow",
      );
      for (const Iframe of await Page.locator(
        ".reference-inspector-copy iframe",
      ).all())
        assert(
          await Iframe.contentFrame()
            .locator("body")
            .evaluate((Body) => Body.scrollWidth <= innerWidth),
          Row.Id + " frame overflow",
        );
      Checks.push(
        Row.Id +
          (Wide ? " expanded" : " docked") +
          ": heading, available section order, baking last, no overflow",
      );
      Inventory.push({
        Id: Row.Id,
        Panel: Row.Panel,
        Wide,
        Cards: await Page.locator(".inspector-scroll [data-card]").evaluateAll(
          (Nodes) => Nodes.map((Node) => Node.dataset.card),
        ),
      });
    }
  if (process.env.FRONTIER_BASELINE_HTML) {
    const Previous = await Browser.newPage();
    await Previous.route("http://baseline.test/**", (Route) =>
      Route.fulfill({
        contentType: "text/html",
        body: fs.readFileSync(process.env.FRONTIER_BASELINE_HTML, "utf8"),
      }),
    );
    const Inputs = (Page) =>
      Page.locator(".inspector-scroll").first().evaluate((Root) =>
        [
          ...Root.querySelectorAll(
            "input[aria-label]:not([readonly]),select[aria-label],textarea[aria-label]",
          ),
        ]
          .filter(
            (Node) =>
              !Node.getAttribute("aria-label")?.toLowerCase().includes("notes"),
          )
          .map((Node) =>
            [Node.tagName, Node.type, Node.getAttribute("aria-label")].join(
              " ",
            ),
          )
          .sort(),
      );
    for (const Row of Rows.filter(
      (Row, Index, All) =>
        All.findIndex((Other) => Other.Panel === Row.Panel) === Index,
    )) {
      await Open(Row.Id);
      await Previous.goto("http://baseline.test/?inspect=" + Row.Id);
      await Previous.locator(".inspector-scroll").waitFor();
      const ActualInputs = await Inputs(Page),
        PreviousInputs = await Inputs(Previous),
        ExpectedInputs =
          Row.Panel.includes("fog")
            ? PreviousInputs.filter(
                (Input) => Input !== "INPUT number Fog probe altitude",
              )
            : PreviousInputs;
      assert.deepEqual(
        ActualInputs,
        ExpectedInputs,
        Row.Id + " native input contract",
      );
      Checks.push(
        Row.Panel +
          ": native editable input inventory matches C052, ignoring order",
      );
    }
    await Previous.close();
  }
  await Open("reference-fill-point", true);
  const Table = Frame().locator(".lp-response-samples");
  assert.deepEqual(await Table.locator("tbody tr").allTextContents(), [
    "1 m10.00 lx100.0%",
    "2 m2.50 lx25.0%",
    "5 m0.40 lx4.0%",
    "10 m0.10 lx1.0%",
  ]);
  const Image = () =>
    Frame()
      .locator(".lp-response canvas")
      .evaluate((Canvas) => Canvas.toDataURL());
  const Before = await Image();
  await Frame().getByLabel("Intensity value", { exact: true }).fill("50");
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
        "reference-fill-point"
      ].ReferenceInspector.Properties.intensity === 50,
  );
  await Frame().getByLabel("Decay exponent value", { exact: true }).fill("1");
  await Table.getByText("10.00 lx", { exact: true }).waitFor();
  assert.notEqual(await Image(), Before);
  await Open("reference-fill-point", true);
  await Table.getByText("10.00 lx", { exact: true }).waitFor();
  await Frame()
    .locator(".lp-shape")
    .evaluate((Node) => Node.scrollIntoView({ block: "start" }));
  await Page.screenshot({
    path: path.join(Proof, "PointResponseExpanded.png"),
  });
  await Page.locator('iframe[title^="Reference inspector"]').evaluate(
    (Node) => (Node.style.width = "240px"),
  );
  assert(
    await Frame()
      .locator("body")
      .evaluate((Node) => Node.scrollWidth <= innerWidth),
  );
  await Table.screenshot({ path: path.join(Proof, "PointSamplesNarrow.png") });
  Checks.push(
    "Point response: exact numeric samples, intensity/decay update chart and table, reload persistence, 240px fit",
  );
  for (const [Id, Wide, Name, Target] of [
    ["light", false, "AreaDocked", null],
    ["reference-softbox", true, "AreaExpanded", null],
    ["sun", true, "SunBakingLast", Card("Sun lighting bake")],
    ["atmosphere", true, "AtmosphereBakingLast", Card("Atmosphere bake")],
    ["stars", false, "StarsBakingLast", Card("Star field bake")],
    ["clouds", true, "CloudOrder", null],
    ["height-fog", false, "HeightFogOrder", Card("Fog layer")],
    ["wind", true, "WindOrder", Card("Wind controls")],
  ]) {
    await Open(Id, Wide);
    if (Target)
      await Page.locator(Target).evaluate((Node) =>
        Node.scrollIntoView({ block: "start" }),
      );
    await Page.screenshot({ path: path.join(Proof, Name + ".png") });
  }
  assert.deepEqual(Errors, []);
  console.log("Inspector order checks passed:", Checks.length);
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
    JSON.stringify({ Checks, Inventory, Errors, FontFailures }, null, 2) + "\n",
  );
  await Browser.close();
}
