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
  path.join(Folder, "Screenshots/EntityContext");
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  headless: true,
  args: ["--no-sandbox", "--disable-gpu"],
});
const Page = await Browser.newPage({ viewport: { width: 1365, height: 768 } });
Page.setDefaultTimeout(12000);
const Address =
  process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
const Checks = [],
  Errors = [],
  FontFailures = [];
Page.on("pageerror", (Error) => Errors.push(Error.stack));
Page.on("requestfailed", (Request) => {
  if (Request.url().includes("fontshare.com")) FontFailures.push(Request.url());
});
const Row = (Name) =>
  Page.locator(".outliner-row")
    .filter({
      has: Page.locator(".row-name", { hasText: Name }),
    })
    .first();
const Metadata = (Name) => Row(Name).locator(".row-identity small").innerText();

try {
  await Page.goto(Address + "?inspect=precipitation");
  await Page.evaluate(() => localStorage.clear());
  await Page.reload();
  await Page.locator(".outliner-row").first().waitFor();
  await Page.waitForTimeout(500);

  const Labels = await Page.locator(".row-identity small").allTextContents();
  assert.equal(Labels.length, 35);
  assert(Labels.every((Label) => Label.trim().length));
  Checks.push(
    "All 35 default outliner entities expose a non-empty contextual value line",
  );

  assert.equal(await Metadata("Area Light"), "32 lx · Area · 0,0,0");
  assert.equal(await Metadata("Precipitation"), "Rain · 12 mm/h");
  assert.equal(await Metadata("Key Spot"), "90 cd · Spot · -6,8.5,5");
  assert.equal(await Metadata("Fill Point"), "10 cd · Point · -3.5,1.6,4.6");
  assert.equal(await Metadata("LED Strip"), "2,400 lm · Strip · 0,2,0");
  assert.equal(await Metadata("Moons"), "Lunar phase 223° · 62%");
  assert.equal(await Metadata("Wind"), "7 m/s · 250°");
  assert.equal(await Metadata("Cube"), "Position 0,0,0 m");
  Checks.push(
    "Light, precipitation, lunar, wind and geometry metadata use type-appropriate units and values",
  );

  await Row("Precipitation").click();
  await Page.getByRole("button", { name: "Snow", exact: true }).click();
  await Page.waitForFunction(() =>
    [...document.querySelectorAll(".outliner-row")].some(
      (Entry) =>
        Entry.querySelector(".row-name")?.textContent === "Precipitation" &&
        Entry.querySelector(".row-identity small")?.textContent.includes(
          "Snow",
        ),
    ),
  );
  assert.equal(await Metadata("Precipitation"), "Snow · 12 mm/h");
  Checks.push(
    "Precipitation type changes update the outliner context immediately",
  );

  for (const Entry of await Page.locator(".outliner-row").all()) {
    await Entry.scrollIntoViewIfNeeded();
    await Entry.click();
    assert.equal(
      await Page.locator(".inspector-scroll .entity-notes-add").count(),
      1,
    );
  }
  Checks.push(
    "Every default entity and collection exposes exactly one optional Add notes action",
  );

  await Row("Precipitation").click();
  await Page.getByRole("button", { name: "Add notes", exact: true }).click();
  const Notes = Page.getByRole("textbox", { name: "Instance notes" });
  await Notes.fill("Weather pass review");
  await Page.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1"))?.Values
        ?.precipitation?.Notes === "Weather pass review",
  );
  await Page.reload();
  await Page.getByRole("textbox", { name: "Instance notes" }).waitFor();
  assert.equal(
    await Page.getByRole("textbox", { name: "Instance notes" }).inputValue(),
    "Weather pass review",
  );
  Checks.push(
    "Entity notes open on demand and persist across reload without becoming mandatory",
  );

  await Page.screenshot({ path: path.join(Proof, "NotesAndMetadata.png") });
  await Page.getByRole("button", { name: "Hide", exact: true }).click();
  assert.equal(
    await Page.getByRole("button", { name: "Add notes" }).count(),
    1,
  );
  assert.equal(
    await Page.evaluate(
      () =>
        JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")).Values
          .precipitation.Notes,
    ),
    "Weather pass review",
  );
  Checks.push("Hiding the optional editor preserves its authored note");

  await Page.locator(".inspector-scroll").evaluate(
    (Node) => (Node.scrollTop = 0),
  );
  await Page.screenshot({ path: path.join(Proof, "OutlinerMetadata.png") });
} catch (Error) {
  Errors.push(Error.stack);
} finally {
  fs.writeFileSync(
    path.join(Proof, "Checks.json"),
    JSON.stringify({ Checks, Errors, FontFailures }, null, 2),
  );
  await Browser.close();
}
assert.deepEqual(Errors, []);
console.log("Entity context checks passed:", Checks.length);
