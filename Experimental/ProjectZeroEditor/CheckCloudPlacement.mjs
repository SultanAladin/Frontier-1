import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
const Folder = path.dirname(fileURLToPath(import.meta.url)),
  Root = path.resolve(Folder, "../../..");
const Require = createRequire(
  process.env.FRONTIER_BROWSER_PACKAGE ||
    path.join(Root, "_AgentScratch/Browser/package.json"),
);
const { chromium } = Require("playwright");
const Proof =
  process.env.FRONTIER_PROOF_FOLDER ||
  path.join(Folder, "Screenshots/CloudPlacement");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  headless: true,
  args: ["--no-sandbox", "--disable-gpu"],
});
const Page = await Browser.newPage({ viewport: { width: 1366, height: 900 } });
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const Address =
  process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll").waitFor();
};
const Frame = (Slice) =>
  Page.frameLocator(`[data-reference-slice="${Slice}"] iframe`);
try {
  await Open("clouds");
  await Frame("summary").locator(".cl-hero").waitFor();
  await Frame("coverage").locator(".cl-cover").waitFor();
  assert.equal(
    await Page.locator(
      "[data-panel=clouds] > header + [data-reference-slice=summary]",
    ).count(),
    1,
  );
  assert.equal(await Page.locator('[data-card="Cloud coverage"]').count(), 1);
  assert.equal(
    await Frame("summary").locator(".cl-cover,.cl-layer,.ident").count(),
    0,
  );
  assert.equal(
    await Frame("coverage").locator(".cl-hero,.cl-layer,.ident").count(),
    0,
  );
  assert.equal(
    await Page.locator('[data-card="Cloud base"] .cloud-deck-visual').count(),
    1,
  );
  assert.equal(
    await Page.locator('[data-card="Cloud coverage"] > .graphic').count(),
    0,
  );
  Checks.push(
    "Cloud summary is header-first; ported Coverage replaces old card; Cloud base contains the adapted ported deck; no copied identity or duplicate deck",
  );
  await Page.screenshot({ path: path.join(Proof, "CloudsTop.png") });
  await Page.getByLabel("Coverage value", { exact: true }).fill("0.75");
  const CoverageFrame = await (
    await Page.locator("[data-reference-slice=coverage] iframe").elementHandle()
  ).contentFrame();
  await CoverageFrame.waitForFunction(
    () => document.querySelector(".cl-cover .mp-num .i")?.textContent === "75",
  );
  await Page.locator(".cloud-coverage-replacement").evaluate((Node) =>
    Node.scrollIntoView({ block: "start" }),
  );
  const Plot = Frame("coverage").locator("canvas");
  await Plot.click({
    position: { x: (await Plot.boundingBox()).width * 0.6, y: 40 },
  });
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values
        .clouds.Coverage === 0.6,
  );
  assert.equal(
    await Page.getByLabel("Coverage value", { exact: true }).inputValue(),
    "0.6",
  );
  await Open("clouds");
  assert.equal(
    await Page.getByLabel("Coverage value", { exact: true }).inputValue(),
    "0.6",
  );
  Checks.push(
    "Coverage graph and retained slider synchronize in both directions and survive reload",
  );
  await Page.locator('[data-card="Cloud base"]').evaluate((Node) =>
    Node.scrollIntoView({ block: "center" }),
  );
  const Deck = Page.locator(".cloud-deck-visual");
  const Before = await Deck.evaluate((Node) => Node.toDataURL());
  await Page.getByLabel("Base value", { exact: true }).fill("12000");
  assert.notEqual(await Deck.evaluate((Node) => Node.toDataURL()), Before);
  assert.equal(await Deck.getAttribute("aria-valuenow"), "12000");
  await Deck.focus();
  await Deck.press("ArrowDown");
  assert.equal(
    await Page.getByLabel("Base value", { exact: true }).inputValue(),
    "11990",
  );
  await Page.getByLabel("Base value", { exact: true }).fill("1800");
  await Deck.click({ position: { x: 120, y: 94 } });
  assert(
    Number(await Page.getByLabel("Base value", { exact: true }).inputValue()) >
      100,
  );
  await Page.getByLabel("Base value", { exact: true }).fill("1800");
  const Image = await Deck.evaluate((Node) => Node.toDataURL());
  await Page.getByLabel("Thickness value", { exact: true }).fill("2000");
  assert.notEqual(await Deck.evaluate((Node) => Node.toDataURL()), Image);
  await Page.locator('[data-card="Cloud base"]').evaluate((Node) =>
    Node.scrollIntoView({ block: "center" }),
  );
  await Page.screenshot({ path: path.join(Proof, "CloudBase.png") });
  Checks.push(
    "Cloud deck responds to native base/thickness, supports keyboard adjustment and handles full native altitude range",
  );
  for (const Id of [
    "light",
    "reference-key-spot",
    "reference-rim-point",
    "reference-fill-point",
    "reference-ece-low-beam",
    "reference-softbox",
    "reference-studio-tube",
  ]) {
    await Open(Id);
    const Light = Frame(Id === "light" ? "summary" : "all");
    const Controls = Frame(Id === "light" ? "details" : "all");
    await Light.locator(".lp-preview").waitFor();
    assert.equal(await Light.locator(".ident").count(), 0);
    assert.equal(await Page.locator(".inspector-scroll .ident").count(), 0);
    assert.equal(
      await Page.locator(
        "[data-panel=light] > header + .reference-inspector-copy",
      ).count(),
      1,
    );
    assert.equal(
      await Controls.locator(".lp-transform .transform-card").count(),
      1,
    );
    if (Id === "light") {
      assert.equal(
        await Page.getByLabel("Intensity value", { exact: true }).count(),
        1,
      );
      assert.equal(
        await Page.getByRole("button", {
          name: "Add notes",
          exact: true,
        }).count(),
        1,
      );
      await Page.screenshot({ path: path.join(Proof, "AreaLightTop.png") });
    }
    Checks.push(
      Id +
        ": new cards at top, no duplicate identity strip, Transform retained",
    );
  }
  await Page.evaluate(() => {
    const Saved = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    Saved.Values["reference-studio-tube"] = {
      ...Saved.Values["reference-studio-tube"],
      ReferenceInspector: { Properties: {}, Locked: true },
    };
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Open("reference-studio-tube");
  assert(
    await Frame("all").getByLabel("Position X", { exact: true }).isDisabled(),
  );
  await Page.getByRole("button", {
    name: "Unlock editing",
    exact: true,
  }).click();
  await Frame("all").getByLabel("Position X", { exact: true }).fill("5");
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
        "reference-studio-tube"
      ]?.ReferenceInspector?.Properties?.pos?.[0] === 5,
  );
  Checks.push(
    "Previously locked lights can be unlocked without restoring the removed identity strip",
  );
  await Page.evaluate(() => {
    const Saved = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    );
    Saved.Values.clouds = {
      ReferenceInspector: {
        Properties: { coverage: 0.42, altitude: 220, density: 0.5 },
        Locked: true,
      },
    };
    localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Saved));
  });
  await Open("clouds");
  assert.equal(
    await Page.getByLabel("Coverage value", { exact: true }).inputValue(),
    "0.42",
  );
  assert.equal(
    await Page.getByLabel("Base value", { exact: true }).inputValue(),
    "220",
  );
  assert.equal(
    await Page.getByLabel("Density value", { exact: true }).inputValue(),
    "2",
  );
  await Page.getByRole("button", {
    name: "Unlock editing",
    exact: true,
  }).click();
  await Page.getByLabel("Coverage value", { exact: true }).fill("0.8");
  await Open("clouds");
  assert.equal(
    await Page.getByLabel("Coverage value", { exact: true }).inputValue(),
    "0.8",
  );
  Checks.push(
    "Legacy imported cloud values are preserved until overridden; previously locked Clouds can be unlocked",
  );
  await Open("local-cloud");
  assert.equal(await Page.locator("iframe").count(), 2);
  assert.equal(await Page.locator('[data-card="Cloud coverage"]').count(), 1);
  assert.equal(await Page.locator('[data-card="Local bounds"]').count(), 1);
  Checks.push(
    "Local Cloud now shares both reference slices and retains its native coverage and bounds controls",
  );
  await Page.setViewportSize({ width: 1024, height: 768 });
  await Open("clouds");
  assert(
    await Frame("summary")
      .locator("body")
      .evaluate((Node) => Node.scrollWidth <= innerWidth),
  );
  assert(
    await Frame("coverage")
      .locator("body")
      .evaluate((Node) => Node.scrollWidth <= innerWidth),
  );
  await Page.screenshot({ path: path.join(Proof, "CloudsNarrow.png") });
  Checks.push(
    "Cloud slices fit the narrow inspector without horizontal overflow",
  );
  if (process.env.FRONTIER_BASELINE_HTML) {
    const Previous = await Browser.newPage();
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
        .filter(
          (Node) =>
            !["Inspector name", "Instance notes"].includes(
              Node.getAttribute("aria-label"),
            ),
        )
        .map((Node) =>
          JSON.stringify([
            Node.tagName,
            Node.type,
            Node.getAttribute("aria-label"),
            Node.min,
            Node.max,
            Node.step,
          ]),
        )
        .sort();
    for (const Id of ["clouds", "light", "local-cloud"]) {
      await Open(Id);
      await Previous.goto("http://baseline.test/?inspect=" + Id);
      await Previous.locator(".inspector-scroll > [data-panel]").waitFor();
      assert.deepEqual(
        await Page.evaluate(Inventory),
        await Previous.evaluate(Inventory),
        Id + " control inventory",
      );
    }
    await Previous.close();
    Checks.push(
      "Clouds, Area Light and Local Cloud preserve the previous build's native control inventory, excluding removed identity text",
    );
  }
  assert.deepEqual(Errors, []);
  console.log("Cloud and Light placement checks passed:", Checks.length);
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
