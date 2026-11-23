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
  path.join(Folder, "Screenshots/SharedCloudLighting");
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  headless: true,
  args: ["--no-sandbox", "--disable-gpu"],
});
const Page = await Browser.newPage({ viewport: { width: 1366, height: 650 } }),
  Checks = [],
  Errors = [];
Page.on("pageerror", (Error) => Errors.push(Error.stack));
Page.setDefaultTimeout(12000);
const Address =
  process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll").waitFor();
};
const Slice = (Name) =>
  Page.frameLocator('[data-reference-slice="' + Name + '"] iframe');
const Light = () =>
  Page.frameLocator('iframe[title^="Reference inspector"]').first();
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );
const Wide = async () => {
  await Page.getByRole("button", {
    name: "Add editor tab or construct",
    exact: true,
  }).click();
  await Page.getByRole("button", {
    name: "Inspector workspace",
    exact: true,
  }).click();
  await Page.waitForTimeout(180);
};
const Capture = async (Name) => {
  await Page.waitForTimeout(180);
  await Page.screenshot({ path: path.join(Proof, Name + ".png") });
};
try {
  for (const Id of ["clouds", "local-cloud"]) {
    await Open(Id);
    await Slice("summary").locator(".cl-hero").waitFor();
    await Slice("coverage").locator(".cl-cover").waitFor();
    assert.equal(
      await Page.locator(
        `[data-panel="${Id}"] > header + .reference-inspector-copy`,
      ).count(),
      1,
    );
    assert.equal(await Page.locator("iframe").count(), 2);
    assert.equal(await Page.locator('[data-card="Cloud base"]').count(), 1);
    assert.equal(
      await Page.locator('[data-card="Layer thickness"]').count(),
      1,
    );
    assert.equal(await Page.locator(".cloud-deck-visual").count(), 1);
    await Capture(Id + "Dock");
    await Page.getByLabel("Coverage value", { exact: true }).fill("0.65");
    await Page.waitForFunction(
      (Id) =>
        JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
          Id
        ]?.Coverage === 0.65,
      Id,
    );
    await Slice("coverage")
      .locator(".cl-cover .mp-num .i")
      .filter({ hasText: "65" })
      .waitFor();
    const Plot = Slice("coverage").locator("canvas");
    await Page.locator('[data-reference-slice="coverage"]').evaluate(
      (Section) => Section.scrollIntoView({ block: "start" }),
    );
    await Plot.click({
      position: { x: (await Plot.boundingBox()).width * 0.4, y: 40 },
    });
    await Page.waitForFunction(
      (Id) =>
        Math.abs(
          JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1"))
            .Values[Id]?.Coverage - 0.4,
        ) < 0.01,
      Id,
    );
    assert(
      Math.abs(
        Number(
          await Page.getByLabel("Coverage value", { exact: true }).inputValue(),
        ) - 0.4,
      ) < 0.01,
    );
    await Open(Id);
    assert(
      Math.abs(
        Number(
          await Page.getByLabel("Coverage value", { exact: true }).inputValue(),
        ) - 0.4,
      ) < 0.01,
    );
    await Wide();
    await Capture(Id + "Wide");
    Checks.push(
      Id +
        ": same summary/coverage/deck/thickness cards, header-first, bidirectional coverage and persistence, docked and expanded views",
    );
  }
  await Open("local-cloud");
  await Slice("summary").locator(".cl-hero").waitFor();
  assert.equal(
    await Slice("summary").locator(".cl-local-domain").innerText(),
    "LOCAL VOLUME · SCHEMATIC",
  );
  const Centre = Page.getByRole("spinbutton", {
      name: "Centre Z",
      exact: true,
    }),
    Half = Page.getByRole("spinbutton", { name: "Half Size Z", exact: true });
  await Centre.fill("450");
  await Half.fill("150");
  await Page.waitForFunction(() => {
    const Values = JSON.parse(
      localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
    ).Values["local-cloud"];
    return Values.Centre?.[2] === 450 && Values["Half Size"]?.[2] === 150;
  });
  assert.equal(
    await Page.getByRole("slider", {
      name: "Cloud deck base altitude",
      exact: true,
    }).getAttribute("aria-valuenow"),
    "300",
  );
  await Slice("summary")
    .locator(".mp-rail")
    .filter({ hasText: "300" })
    .waitFor();
  assert(
    (await Page.locator('[data-card="Layer thickness"]').innerText()).includes(
      "0.30",
    ),
  );
  const Deck = Page.getByRole("slider", {
    name: "Cloud deck base altitude",
    exact: true,
  });
  await Deck.focus();
  await Deck.press("ArrowUp");
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
        "local-cloud"
      ].Centre[2] === 460,
  );
  assert.equal(await Half.inputValue(), "150");
  await Open("local-cloud");
  assert.equal(await Centre.inputValue(), "460");
  Checks.push(
    "Local base = centre Z − half height; thickness = twice half height; keyboard deck edits translate bounds without changing size and persist",
  );
  await Centre.fill("-200");
  await Half.fill("100");
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
        "local-cloud"
      ]["Half Size"][2] === 100,
  );
  assert.equal(await Deck.getAttribute("aria-valuenow"), "-300");
  await Deck.focus();
  await Deck.press("ArrowDown");
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values[
        "local-cloud"
      ].Centre[2] === -210,
  );
  await Page.locator('[data-card="Cloud base"]').evaluate((Card) =>
    Card.scrollIntoView({ block: "start" }),
  );
  await Capture("LocalNegativeDeck");
  Checks.push(
    "Local deck represents negative elevations without global-cloud clamping",
  );
  // Both source bindings remain independent after editing the new local reference cards.
  const Contents = await Saved();
  assert(Math.abs(Contents.Values.clouds.Coverage - 0.4) < 0.01);
  assert.equal(Contents.Values.clouds.Centre, undefined);
  assert(await Page.locator('[data-card="Local bounds"]').count());
  assert(await Page.getByLabel("Feature Scale value", { exact: true }).count());
  assert(await Page.getByLabel("Anisotropy value", { exact: true }).count());
  Checks.push(
    "Local bounds/body controls remain and do not mutate the global-cloud record",
  );
  const Lights = [
    "light",
    "reference-softbox",
    "reference-ece-low-beam",
    "reference-ies-downlight",
    "reference-led",
    "reference-led-strip",
    "reference-rim-point",
    "reference-key-spot",
    "reference-studio-tube",
  ];
  for (const Id of Lights) {
    await Open(Id);
    const Controls =
      Id === "light"
        ? Page.frameLocator('[data-reference-slice="details"] iframe')
        : Light();
    await Controls.locator(".lp-response").waitFor();
    const Image = Light().locator(".lp-source-icon img");
    await Image.evaluate((Image) => Image.decode());
    const Record = (await Saved()).Rows.find((Row) => Row.Id === Id);
    const Asset = await Page.evaluate(
      (Name) => window.NativeAssets.Icons[Name],
      Record.Icon,
    );
    assert.equal(await Image.getAttribute("src"), Asset);
    assert(!Record.Icon.startsWith("light-arealight"));
    if (["light", "reference-softbox"].includes(Id))
      assert.equal(Record.Icon, "editor-area-light");
    await Wide();
    const Source = await Light().locator(".lp-preview").boundingBox();
    const Rail = await Light().locator(".lp-readings").boundingBox();
    const Output = await Controls.locator(".lp-output").boundingBox();
    const Response = await Controls.locator(".lp-response").boundingBox();
    const Shape = await Controls.locator(".lp-shape").boundingBox();
    assert(Source.width > 650);
    assert(
      Rail.y - Source.y - Source.height >= 0 &&
        Rail.y - Source.y - Source.height < 30,
    );
    assert(Output.y > Rail.y + Rail.height);
    assert(Math.abs(Output.y - Shape.y) < 2);
    assert(
      Response.y > Output.y &&
        Response.y + Response.height <= Output.y + Output.height,
    );
    assert.equal(
      await Controls.locator(".lp-output > .lp-response").count(),
      1,
    );
    assert(
      await Light()
        .locator("body")
        .evaluate((Body) => Body.scrollWidth <= innerWidth),
    );
    await Capture(Id + "Wide");
    await Page.locator('iframe[title^="Reference inspector"]').evaluateAll(
      (Iframes) => Iframes.forEach((Iframe) => (Iframe.style.width = "240px")),
    );
    await Page.waitForTimeout(100);
    assert(
      await Light()
        .locator("body")
        .evaluate((Body) => Body.scrollWidth <= innerWidth),
    );
    Checks.push(
      Id +
        ": shipped library icon, ordered summary/statistics/flags above independent control columns; merged response, visible controls/readings, no 240px overflow",
    );
  }
  assert.deepEqual(Errors, []);
  console.log("Shared-card checks passed:", Checks.length);
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
