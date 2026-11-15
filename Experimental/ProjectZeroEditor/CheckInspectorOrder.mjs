//============================================================================================================================================
//                                                           CHECKINSPECTORORDER.MJS
//============================================================================================================================================
// 📦 Cross-inspector section ordering, baking placement and live light-response regressions.

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
  await Page.locator(".inspector-scroll > [data-panel]").waitFor();
  for (const Iframe of await Page.locator(
    ".reference-inspector-copy iframe",
  ).all())
    await Iframe.contentFrame().locator(".mpanel > *").first().waitFor();
  await Page.waitForTimeout(150);
}
async function Sequence(Name, Entries) {
  let Bottom = -Infinity;
  for (const Entry of Entries) {
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
  geometry: [
    ".transform-card",
    ".entity-capabilities",
    ".material-panel",
    ".fracture-card",
  ],
  post: [".entity-capabilities"],
  camera: [Card("Lens + field of view"), Card("Aperture study")],
  atmosphere: [
    ".atmosphere-lab-cards",
    Card("Atmosphere settings"),
    Card("Atmospheric scattering"),
    Card("Native atmosphere controls"),
    Card("Atmosphere bake"),
    Card("Baked atmosphere"),
  ],
  sun: [
    [null, ".mp-hero"],
    [null, ".mp-rail"],
    [null, ".mp-duo"],
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
    Card("Wind field"),
  ],
  "height-fog": [
    Card("Fog settings"),
    Card("Visibility through fog"),
    Card("Medium"),
    Card("Wind binding"),
  ],
  "aerial-fog": [
    Card("Fog settings"),
    Card("Visibility through fog"),
    Card("Medium"),
    Card("Wind binding"),
  ],
  "local-fog": [
    Card("Fog settings"),
    Card("Visibility through fog"),
    Card("Medium"),
    Card("Wind binding"),
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
const BakeCards = {
  sun: ["Sun lighting bake", "Sun disk bake"],
  atmosphere: ["Atmosphere bake", "Baked atmosphere"],
  flare: ["Lens flare image"],
  stars: ["Star field bake"],
  rainbow: ["Bake / image"],
  geometry: ["Fracture"],
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
        await Sequence(Row.Id, [
          [null, ".lp-preview"],
          [null, ".lp-readings"],
          [null, ".lp-participation"],
        ]);
        const Controls = Row.ReferenceOnly ? Frame() : Frame("details");
        if (!Row.ReferenceOnly)
          await Sequence(Row.Id, [
            ["summary", ".lp-participation"],
            ".entity-capabilities",
            ["details", ".lp-output"],
          ]);
        const Quick = await Frame().locator(".lp-participation").boundingBox();
        const Output = await Controls.locator(".lp-output").boundingBox();
        assert(Output.y >= Quick.y + Quick.height - 2);
        assert.equal(
          await Controls.locator(".lp-output > .lp-response").count(),
          1,
        );
        assert.equal(await Controls.locator(".lp-response.lp-card").count(), 0);
        if (Wide) {
          const Shape = await Controls.locator(".lp-shape").boundingBox();
          const Transform =
            await Controls.locator(".lp-transform").boundingBox();
          assert(Math.abs(Output.y - Shape.y) < 2);
          assert(
            Transform.y - Shape.y - Shape.height >= 0 &&
              Transform.y - Shape.y - Shape.height < 30,
          );
        } else
          await Sequence(Row.Id, [
            [Row.ReferenceOnly ? null : "details", ".lp-output"],
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
              (Other) =>
                Node.contains(Other) ||
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
        await Page.locator(".inspector-scroll").evaluate(
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
      Page.locator(".inspector-scroll").evaluate((Root) =>
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
    .locator(".lp-output")
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
    ["height-fog", false, "HeightFogOrder", Card("Fog settings")],
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
