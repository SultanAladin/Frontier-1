import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { createHash } from "node:crypto";
import assert from "node:assert/strict";
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    path.resolve(Folder, "../FrontierEditor/package.json"),
);
const { chromium } = Require("playwright");
const Proof =
  process.env.FRONTIER_PROOF_FOLDER ||
  path.join(Folder, "Screenshots/Arrangement");
fs.mkdirSync(Proof, { recursive: true });
const Manifest = JSON.parse(
  fs.readFileSync(path.join(Folder, "InspectorDepot/Provenance.json")),
);
for (const [Name, Hash] of Object.entries(Manifest.unchangedFiles))
  assert.equal(
    createHash("sha256")
      .update(fs.readFileSync(path.join(Folder, "InspectorDepot", Name)))
      .digest("hex"),
    Hash,
  );
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || undefined,
  args: ["--no-sandbox", "--disable-gpu"],
  headless: true,
});
const Page = await Browser.newPage({ viewport: { width: 1440, height: 1000 } });
const Results = [],
  Errors = [],
  FontFailures = [];
Page.on("pageerror", (Error) => Errors.push(Error.stack));
Page.on("requestfailed", (Request) => {
  if (Request.url().includes("fontshare.com")) FontFailures.push(Request.url());
});
const Address = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/";
let Previous;
if (process.env.FRONTIER_BASELINE_HTML) {
  Previous = await Browser.newPage({ viewport: { width: 1440, height: 1000 } });
  await Previous.route("http://baseline.test/**", (Route) =>
    Route.fulfill({
      contentType: "text/html",
      body: fs.readFileSync(process.env.FRONTIER_BASELINE_HTML, "utf8"),
    }),
  );
}
function Inventory() {
  const Root = document.querySelector(".inspector-scroll > [data-panel]");
  if (!Root) return null;
  return {
    Panel: Root.dataset.panel,
    Cards: [...Root.querySelectorAll("[data-card]")]
      .map((Node) => Node.dataset.card)
      .sort(),
    Headings: [...Root.querySelectorAll("h1,h2,h3")]
      .map((Node) => Node.textContent)
      .sort(),
    Controls: [...Root.querySelectorAll("input,select,textarea")]
      .filter((Node) => !Node.getAttribute("aria-label")?.includes("notes"))
      .map((Node) => [Node.tagName, Node.type, Node.getAttribute("aria-label")])
      .sort((A, B) => JSON.stringify(A).localeCompare(JSON.stringify(B))),
  };
}
async function Open(Id) {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll").waitFor();
  if (await Page.locator(".reference-inspector-copy").count()) {
    const Frame = Page.frames().find((Value) => Value.url() === "about:srcdoc");
    await Frame.locator(".sheet").waitFor();
    return Frame;
  }
}
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
try {
  for (const Id of [
    "world",
    "showcase",
    "lighting",
    "camera",
    "moon",
    "sun",
    "wind",
    "clouds",
    "height-fog",
    "light",
  ]) {
    await Open(Id);
    const Actual = await Page.evaluate(Inventory);
    assert.ok(Actual, Id + " original inspector exists");
    if (Previous) {
      await Previous.goto("http://baseline.test/?inspect=" + Id);
      await Previous.locator(".inspector-scroll > [data-panel]").waitFor();
      const Expected = await Previous.evaluate(Inventory);
      if (Id === "height-fog") {
        assert.deepEqual(
          Actual.Cards,
          [
            "Fog settings",
            "Visibility through fog",
            "Medium",
            "Height and tint",
            "Wind binding",
          ],
        );
      }
      if (Id === "lighting") {
        assert(Actual.Headings.includes("Browse contents 10 matches"));
        Expected.Headings = Expected.Headings.map((Heading) =>
          Heading === "Browse contents 7 matches"
            ? "Browse contents 10 matches"
            : Heading,
        );
      }
      if (Id !== "height-fog")
        assert.deepEqual(
          Actual,
          Expected,
          Id + " original cards/controls retained with requested order",
        );
    }
    if (
      [
        "world",
        "showcase",
        "lighting",
        "camera",
        "moon",
        "height-fog",
      ].includes(Id)
    ) {
      assert.equal(await Page.locator(".reference-inspector-copy").count(), 0);
      if (["world", "showcase", "lighting"].includes(Id))
        assert.equal(await Page.locator(".folder-inspector").count(), 1);
    } else {
      assert.equal(
        await Page.locator(".reference-inspector-copy").count(),
        Id === "height-fog"
          ? 3
          : ["clouds", "light"].includes(Id)
            ? 2
            : 1,
      );
      const Placement = await Page.evaluate(() => {
        const Native = document.querySelector(
          ".inspector-scroll > [data-panel]",
        );
        const Reference = document.querySelector(".reference-inspector-copy");
        if (Native.contains(Reference)) return "inline";
        return Reference.compareDocumentPosition(Native) &
          Node.DOCUMENT_POSITION_FOLLOWING
          ? "before"
          : "after";
      });
      assert.equal(
        Placement,
        ["wind", "sun", "height-fog", "clouds", "light"].includes(Id)
          ? "inline"
          : "after",
      );
    }
    if (["world", "wind", "clouds"].includes(Id))
      await Page.screenshot({ path: path.join(Proof, Id + "-original.png") });
    Results.push(
      Id +
        ": original inspector retained; correct imported-panel presence and requested placement" +
        (Previous ? "; baseline card/control inventory matches" : ""),
    );
  }
  for (const [Id, Selectors] of [
    ["sun", [".mp-hero", ".mp-rail", ".mp-duo"]],
    ["wind", [".wf-trace", ".mp-rail", ".mp-duo"]],
    ["clouds", [".cl-hero", ".mp-rail", ".mp-duo"]],
    [
      "reference-rim-point",
      [
        ".lp-preview",
        ".mp-rail",
        ".lp-output",
        ".lp-shape",
        ".lp-transform",
        ".lp-participation",
      ],
    ],
    [
      "reference-fill-point",
      [
        ".lp-preview",
        ".mp-rail",
        ".lp-output",
        ".lp-shape",
        ".lp-transform",
        ".lp-participation",
      ],
    ],
    [
      "reference-key-spot",
      [
        ".lp-preview",
        ".mp-rail",
        ".lp-output",
        ".lp-shape",
        ".lp-transform",
        ".lp-participation",
      ],
    ],
    [
      "reference-ece-low-beam",
      [
        ".lp-preview",
        ".mp-rail",
        ".lp-output",
        ".lp-shape",
        ".lp-transform",
        ".lp-participation",
      ],
    ],
    [
      "reference-softbox",
      [
        ".lp-preview",
        ".mp-rail",
        ".lp-output",
        ".lp-shape",
        ".lp-transform",
        ".lp-participation",
      ],
    ],
    [
      "reference-studio-tube",
      [
        ".lp-preview",
        ".mp-rail",
        ".lp-output",
        ".lp-shape",
        ".lp-transform",
        ".lp-participation",
      ],
    ],
    [
      "light",
      [
        ".lp-preview",
        ".mp-rail",
        ".lp-output",
        ".lp-shape",
        ".lp-transform",
        ".lp-participation",
      ],
    ],
  ]) {
    const Frame = await Open(Id);
    const DetailFrame =
      Id === "light"
        ? Page.frames().filter((Value) => Value.url() === "about:srcdoc")[1]
        : Frame;
    if (Selectors.includes(".lp-preview")) {
      for (const Selector of [...Selectors, ".lp-response"])
        assert.equal(
          await (
            Id === "light" &&
            [
              ".lp-output",
              ".lp-shape",
              ".lp-transform",
              ".lp-response",
            ].includes(Selector)
              ? DetailFrame
              : Frame
          )
            .locator(Selector)
            .count(),
          1,
        );
      assert.equal(
        await Frame.locator(".lighting-panel .lp-card").count(),
        Id === "light" ? 2 : 5,
      );
      assert.equal(
        await DetailFrame.locator(
          ".lp-source-column,.lp-control-column",
        ).count(),
        2,
      );
    } else {
      assert.deepEqual(
        await Frame.locator(".mpanel > *").evaluateAll(
          (Nodes, Selectors) =>
            Nodes.map((Node, Index) =>
              Node.matches(Selectors[Index] || ".missing"),
            ),
          Selectors,
        ),
        Selectors.map(() => true),
      );
    }
    assert.equal(await Frame.locator(".sheet > .pcard").count(), 0);
    assert.equal(
      await Frame.evaluate(
        () => document.documentElement.scrollWidth > innerWidth,
      ),
      false,
    );
    assert.equal(
      await Frame.locator(".flow-controls").count(),
      0,
      "Rejected C041 controls are absent",
    );
    if (["sun", "wind", "clouds", "reference-rim-point"].includes(Id)) {
      await Page.locator(".reference-inspector-copy")
        .first()
        .evaluate((Node) => Node.scrollIntoView({ block: "start" }));
      await Page.waitForTimeout(400);
      await Page.screenshot({ path: path.join(Proof, Id + "-added.png") });
    }
    Results.push(Id + ": only selected imported cards are added");
  }
  for (const Id of ["wind", "sun"]) {
    const Frame = await Open(Id);
    assert.equal(await Frame.locator(".ident").count(), 0);
    if (Id === "wind") {
      assert.deepEqual(
        await Page.locator(
          '[data-panel="wind"] > .property-card, [data-panel="wind"] > .reference-inspector-copy',
        ).evaluateAll((Nodes) =>
          Nodes.slice(0, 3).map((Node) => Node.dataset.card || "Reference"),
        ),
        ["Reference", "Wind controls", "Wind field"],
      );
      assert.equal(
        await Frame.locator(".mpanel > :first-child").getAttribute("class"),
        "pcard mp-metric wf-trace",
      );
      assert.equal(await Frame.locator(".wf-hero").count(), 0);
      assert.equal(await Frame.locator(".wf-specs").count(), 0);
      await Page.locator(".reference-inspector-copy")
        .first()
        .evaluate((Node) => Node.scrollIntoView({ block: "start" }));
      await Page.waitForTimeout(400);
      const Trace = () =>
        Frame.locator(".wf-trace canvas").evaluate((Node) => Node.toDataURL());
      const Before = await Trace();
      await Page.waitForTimeout(600);
      assert.notEqual(
        await Trace(),
        Before,
        "Anemometer animates after removal of the standalone hero",
      );
      Results.push(
        "Wind: Anemometer and statistics first, composite second; unnecessary trace readouts removed; chart animates",
      );
    } else {
      assert(
        await Page.locator(
          `[data-panel="${Id}"] > header + .reference-inspector-copy`,
        ).count(),
        "Preview follows the native inspector header",
      );
      if (Id === "height-fog") {
        const DetailFrame = Page.frames().filter(
          (Value) => Value.url() === "about:srcdoc",
        )[1];
        assert.deepEqual(
          await DetailFrame.locator(".fg-scatter > *").evaluateAll((Nodes) =>
            Nodes.map((Node) => Node.className),
          ),
          ["mp-meter fg-chamber"],
        );
        assert.equal(
          await DetailFrame.locator(".fg-scatter canvas").count(),
          1,
        );
        await Page.locator(".inspector-scroll").evaluate((Node) => {
          const Frame = Node.querySelector("iframe");
          Node.scrollTop =
            Frame.offsetTop + Frame.clientHeight - Node.clientHeight;
        });
        await Page.screenshot({
          path: path.join(Proof, "height-fog-chamber.png"),
        });
      }
      Results.push(
        Id +
          ": header then preview; copied identity removed" +
          (Id === "height-fog"
            ? "; Light transport contains only the beam chamber"
            : ""),
      );
    }
  }
  await Open("height-fog");
  assert.equal(await Page.locator(".reference-inspector-copy").count(), 0);
  assert.equal(await Page.locator('[data-card="Visibility through fog"]').count(), 1);
  assert.equal(
    await Page.locator('[data-card="Medium"] > .fog-shared-beam').count(),
    1,
  );
  await Page.locator('[data-card="Medium"]').scrollIntoViewIfNeeded();
  await Page.screenshot({
    path: path.join(Proof, "height-fog-chamber.png"),
  });
  Results.push(
    "Height Fog uses the coordinated native Fog card family and nests Beam Chamber in Medium",
  );

  await Page.setViewportSize({ width: 1100, height: 900 });
  const Narrow = await Open("wind");
  assert.equal(
    await Narrow.evaluate(
      () => document.documentElement.scrollWidth > innerWidth,
    ),
    false,
  );
  await Page.screenshot({ path: path.join(Proof, "wind-narrow.png") });
  Results.push("Narrow Wind inspector: no horizontal iframe overflow");
  await Page.setViewportSize({ width: 1440, height: 1000 });
  let Frame = await Open("clouds");
  const Control = Page.getByLabel("Base value", { exact: true });
  const Before = Number(await Control.inputValue());
  await Control.fill(String(Before + 7));
  await Control.press("Enter");
  await Page.waitForTimeout(150);
  Frame = await Open("clouds");
  assert.equal(
    Number(await Page.getByLabel("Base value", { exact: true }).inputValue()),
    Before + 7,
  );
  assert.ok(
    (await Page.locator('[data-panel="clouds"] [data-card]').count()) > 0,
  );
  Results.push(
    "Added Cloud deck edits persist without removing original Cloud controls",
  );
  await Open("world");
  await Page.getByRole("button", { name: "Add notes", exact: true }).click();
  await Page.getByLabel("Instance notes", { exact: true }).fill(
    "Original folder retained",
  );
  await Page.reload();
  assert.equal(
    await Page.getByLabel("Instance notes", { exact: true }).inputValue(),
    "Original folder retained",
  );
  assert.equal(await Page.locator("iframe").count(), 0);
  Results.push(
    "Original Folder notes persist; no imported Folder elements or iframe",
  );
  await Open("wind");
  assert.equal(
    await Page.getByRole("button", {
      name: "Expand WindEditor",
      exact: true,
    }).count(),
    1,
  );
  await Page.getByRole("button", {
    name: "Expand WindEditor",
    exact: true,
  }).click();
  await Page.getByRole("dialog", { name: "WindEditor", exact: true }).waitFor();
  Results.push("Pre-C041 composite WindEditor is restored and opens");
  assert.deepEqual(Errors, []);
} catch (Error) {
  Errors.push(Error.stack);
  throw Error;
} finally {
  const Report = {
    Browser: Browser.version(),
    Baseline: Previous ? "b424bc3" : null,
    HtmlSha256: createHash("sha256")
      .update(fs.readFileSync(path.join(Folder, "index.html")))
      .digest("hex"),
    Results,
    Errors,
    FontFailures: [...new Set(FontFailures)],
  };
  fs.writeFileSync(
    path.join(Proof, "Checks.json"),
    JSON.stringify(Report, null, 2) + "\n",
  );
  console.log(JSON.stringify(Report, null, 2));
  await Browser.close();
}
