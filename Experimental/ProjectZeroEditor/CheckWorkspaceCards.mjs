import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { FolderInventory } from "./FolderInventory.mjs";
const Require = createRequire(
    process.env.FRONTIER_BROWSER_PACKAGE ||
      new URL("../FrontierEditor/package.json", import.meta.url),
  ),
  { chromium } = Require("playwright"),
  Proof =
    process.env.FRONTIER_PROOF_FOLDER ||
    path.resolve("Frontier/Experimental/ProjectZeroEditor/Screenshots");
fs.mkdirSync(Proof, { recursive: true });
const Results = [],
  Errors = [];
const Large = [
  { Id: "root", Name: "Root", Panel: "group", Parent: null },
  ...Array.from({ length: 10000 }, (_, I) => ({
    Id: "n" + I,
    Name: "Node " + I,
    Panel: I % 50 === 0 ? "group" : "geometry",
    Parent: I ? "n" + (I - 1) : "root",
  })),
];
const Start = performance.now(),
  Inventory = FolderInventory(Large, "root", { n500: true });
assert.equal(Inventory.Entries.length, 10000);
assert.equal(Inventory.Depth, 10000);
assert.equal(Inventory.Visible, 500);
const Elapsed = performance.now() - Start;
assert.ok(Elapsed < 5000);
assert.equal(
  FolderInventory(
    [
      { Id: "a", Parent: "b" },
      { Id: "b", Parent: "a" },
    ],
    "a",
  ).Entries.length,
  1,
);
Results.push(
  `Indexed 10,000-level hierarchy without recursion in ${Elapsed.toFixed(1)} ms; inherited visibility and cycle guard correct`,
);
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || undefined,
  args: ["--no-sandbox", "--disable-gpu"],
  headless: true,
});
const Page = await Browser.newPage({
  viewport: { width: 1366, height: 768 },
  deviceScaleFactor: 1,
});
Page.setDefaultTimeout(15000);
Page.on("pageerror", (E) => Errors.push(E.stack));
const FontFailures = [];
Page.on("console", (Message) => {
  if (Message.type() !== "error") return;
  // The reference font is an external dependency; report its availability separately from application errors.
  if (Message.location().url?.startsWith("https://cdn.fontshare.com/"))
    FontFailures.push({ Url: Message.location().url, Error: Message.text() });
  else Errors.push(Message.text());
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
async function Drag(Locator, DX, DY) {
  await Locator.scrollIntoViewIfNeeded();
  const B = await Locator.boundingBox();
  await Page.mouse.move(B.x + B.width * 0.5, B.y + B.height * 0.5);
  await Page.mouse.down();
  await Page.mouse.move(B.x + B.width * 0.5 + DX, B.y + B.height * 0.5 + DY, {
    steps: 8,
  });
  await Page.mouse.up();
}
try {
  await Page.goto(process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:5180/", {
    waitUntil: "networkidle",
  });
  await Page.evaluate(() => document.fonts.ready);
  const Baseline = await Saved();
  for (const Name of [
    "Construct",
    "Frame selected",
    "Split viewport",
    "Viewport diagnostics",
    "Viewport settings",
  ]) {
    const Radius = await Page.getByRole("button", {
      name: Name,
      exact: true,
    }).evaluate((N) => parseFloat(getComputedStyle(N).borderRadius));
    assert.ok(Radius >= 100, Name + " is pill-shaped");
  }
  await Select("World");
  assert.equal(
    await Page.locator("[data-collection-total]").getAttribute(
      "data-collection-total",
    ),
    "13",
  );
  assert.equal(await Page.locator(".collection-entry").count(), 13);
  await Page.getByLabel("Collection scope", { exact: true }).selectOption(
    "direct",
  );
  assert.equal(await Page.locator(".collection-entry").count(), 10);
  await Page.getByLabel("Collection scope", { exact: true }).selectOption(
    "all",
  );
  await Page.getByRole("button", { name: "Add notes", exact: true }).click();
  await Page.getByLabel("Instance notes", { exact: true }).fill(
    "Environment review notes",
  );
  await Page.getByLabel("Collection tint", { exact: true }).fill("#589979");
  await Page.reload({ waitUntil: "networkidle" });
  assert.equal(
    await Page.getByLabel("Collection tint", { exact: true }).inputValue(),
    "#589979",
  );
  assert.equal(
    await Page.getByLabel("Instance notes", { exact: true }).inputValue(),
    "Environment review notes",
  );
  await Page.getByRole("button", {
    name: "Hide collection entry Sun",
    exact: true,
  }).click();
  assert.equal(
    await Page.getByLabel("Collection visibility", {
      exact: true,
    }).inputValue(),
    "all",
  );
  await Page.getByLabel("Collection visibility", { exact: true }).selectOption(
    "hidden",
  );
  assert.equal(await Page.locator(".collection-entry").count(), 2);
  assert.match(
    await Page.locator(".collection-results").innerText(),
    /Hidden by ancestor/,
  );
  await Page.getByLabel("Collection visibility", { exact: true }).selectOption(
    "all",
  );
  await Page.getByRole("button", {
    name: "Show collection entry Sun",
    exact: true,
  }).click();
  await Page.locator(".inspector-scroll").evaluate((N) => (N.scrollTop = 0));
  await Page.getByRole("button", {
    name: "Viewport diagnostics",
    exact: true,
  }).click();
  const Card = Page.getByRole("region", {
    name: "Viewport performance diagnostics",
    exact: true,
  });
  await Page.waitForFunction(
    () => !document.querySelector(".debug-number").textContent.includes("—"),
  );
  assert.ok(parseFloat(await Page.locator(".debug-number").innerText()) > 0);
  await Page.getByLabel("Diagnostics history duration", {
    exact: true,
  }).selectOption("15");
  await Drag(
    Page.getByRole("button", { name: "Resize diagnostics card", exact: true }),
    140,
    150,
  );
  let Box = await Card.boundingBox();
  assert.ok(Box.width > 450 && Box.height > 490);
  await Page.getByRole("button", {
    name: "Move diagnostics card",
    exact: true,
  }).focus();
  const BeforeX = Box.x;
  await Page.keyboard.press("ArrowRight");
  Box = await Card.boundingBox();
  assert.ok(Box.x > BeforeX);
  await Drag(
    Page.getByRole("button", { name: "Move diagnostics card", exact: true }),
    20,
    0,
  );
  // Exercise measured browser stalls, never fabricated telemetry or native GPU data.
  await Page.evaluate(async () => {
    for (let I = 0; I < 6; I++) {
      await new Promise((R) => setTimeout(R, 120));
      const End = performance.now() + 35;
      while (performance.now() < End) {}
    }
  });
  await Page.waitForTimeout(14000);
  await Page.getByRole("button", {
    name: "Pause samples",
    exact: true,
  }).click();
  const Number = await Page.locator(".debug-number").innerText();
  await Page.waitForTimeout(700);
  assert.equal(await Page.locator(".debug-number").innerText(), Number);
  assert.equal(await Card.getAttribute("data-paused"), "true");
  await Page.locator(".debug-card-body").evaluate((N) => (N.scrollTop = 0));
  await Page.screenshot({ path: Proof + "/FolderDiagnostics.png" });
  await Page.getByRole("slider", {
    name: "Inspect diagnostics history",
    exact: true,
  }).focus();
  await Page.keyboard.press("Home");
  assert.equal(
    await Page.getByRole("slider", {
      name: "Inspect diagnostics history",
      exact: true,
    }).getAttribute("aria-valuenow"),
    "0",
  );
  await Page.keyboard.press("End");
  await Page.getByLabel("Diagnostics metric", { exact: true }).selectOption(
    "Frame",
  );
  assert.match(await Page.locator(".debug-number").innerText(), /ms/);
  await Page.screenshot({ path: Proof + "/FrameTimingCard.png" });
  await Page.getByLabel("Diagnostics presentation", {
    exact: true,
  }).selectOption("stats");
  assert.equal(await Page.locator(".debug-history").count(), 0);
  assert.equal(await Page.locator(".debug-stats").count(), 1);
  await Page.getByLabel("Diagnostics metric", { exact: true }).selectOption(
    "GPU",
  );
  assert.match(await Card.innerText(), /Unavailable/);
  assert.match(await Page.locator(".debug-number").innerText(), /—/);
  await Page.getByLabel("Diagnostics presentation", {
    exact: true,
  }).selectOption("both");
  assert.equal(await Page.locator(".debug-history").count(), 0);
  await Page.screenshot({ path: Proof + "/GpuTimingUnavailable.png" });
  await Page.getByLabel("Diagnostics metric", { exact: true }).selectOption(
    "Scene",
  );
  assert.equal(
    parseInt(await Page.locator(".debug-number").innerText()),
    Baseline.Rows.length,
  );
  await Page.getByRole("button", {
    name: "Resume samples",
    exact: true,
  }).click();
  const Stored = await Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.DiagnosticsCard.v1")),
  );
  await Page.getByRole("button", {
    name: "Close diagnostics",
    exact: true,
  }).click();
  assert.equal(await Card.count(), 0);
  await Page.keyboard.press("F3");
  assert.equal(
    await Page.getByLabel("Diagnostics metric", { exact: true }).inputValue(),
    "Scene",
  );
  assert.equal(
    await Page.evaluate(
      () =>
        JSON.parse(localStorage.getItem("Frontier.DiagnosticsCard.v1")).Width,
    ),
    Stored.Width,
  );
  for (const Width of [1024, 1280, 1366, 1920]) {
    await Page.setViewportSize({ width: Width, height: 768 });
    await Page.waitForTimeout(100);
    const C = await Card.boundingBox(),
      P = await Page.locator(".scene-image").boundingBox();
    assert.ok(
      C.x >= P.x &&
        C.y >= P.y &&
        C.x + C.width <= P.x + P.width + 1 &&
        C.y + C.height <= P.y + P.height + 1,
    );
    assert.equal(
      await Card.evaluate((N) => N.scrollWidth > N.clientWidth + 1),
      false,
    );
  }
  await Page.getByLabel("Diagnostics metric", { exact: true }).focus();
  await Page.keyboard.press("Escape");
  assert.equal(await Card.count(), 0);
  Results.push(
    "Header pills; folder counts, inherited visibility and saved notes; live rAF FPS/interval history, pause/probe, metric/view switches, honest GPU absence, pointer/keyboard resizing/moving, persisted layout and responsive bounds",
  );
  const Fixture = {
    ...Baseline,
    Selected: "archive",
    Rows: [
      ...Baseline.Rows,
      {
        Id: "archive",
        Name: "Asset archive",
        Panel: "group",
        Icon: "folder-generic",
        Parent: null,
        Description: "Browser test fixture · 2,500 asset records",
      },
      {
        Id: "empty",
        Name: "Empty collection",
        Panel: "group",
        Icon: "folder-generic",
        Parent: null,
      },
      ...Array.from({ length: 8 }, (_, I) => ({
        Id: "batch" + I,
        Name: "Batch " + (I + 1),
        Panel: "group",
        Icon: "folder-generic",
        Parent: "archive",
      })),
      ...Array.from({ length: 2500 }, (_, I) => ({
        Id: "asset" + I,
        Name: "Asset " + String(I + 1).padStart(5, "0"),
        Panel: "geometry",
        Icon: "editor-cube",
        Parent: "batch" + (I % 8),
        Description: "Test fixture record, not imported geometry",
      })),
    ],
    Collapsed: {
      showcase: true,
      cameras: true,
      world: true,
      lighting: true,
      archive: true,
    },
  };
  await Page.evaluate(
    (F) =>
      localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(F)),
    Fixture,
  );
  await Page.reload({ waitUntil: "networkidle" });
  await Page.setViewportSize({ width: 1366, height: 768 });
  assert.equal(
    await Page.locator("[data-collection-total]").getAttribute(
      "data-collection-total",
    ),
    "2508",
  );
  assert.equal(await Page.locator(".collection-entry").count(), 25);
  await Page.getByLabel("Collection type", { exact: true }).selectOption(
    "geometry",
  );
  assert.match(
    await Page.locator(".collection-pagination").innerText(),
    /2,500/,
  );
  await Page.getByRole("button", {
    name: "Next collection page",
    exact: true,
  }).click();
  assert.match(
    await Page.locator(".collection-pagination").innerText(),
    /26–50/,
  );
  await Page.getByLabel("Collection page size", { exact: true }).selectOption(
    "100",
  );
  assert.equal(await Page.locator(".collection-entry").count(), 100);
  await Page.getByLabel("Search collection contents", { exact: true }).fill(
    "Asset 01234",
  );
  assert.equal(await Page.locator(".collection-entry").count(), 1);
  await Page.getByRole("button", {
    name: "Hide collection entry Asset 01234",
    exact: true,
  }).click();
  assert.equal((await Saved()).Hidden.asset1233, true);
  await Page.getByLabel("Search collection contents", { exact: true }).fill(
    "no matching entry",
  );
  assert.equal(await Page.locator(".collection-entry").count(), 0);
  await Page.getByRole("button", {
    name: "Clear filters",
    exact: true,
  }).click();
  await Page.getByLabel("Collection page size", { exact: true }).selectOption(
    "25",
  );
  await Page.locator(".inspector-scroll").evaluate((N) => (N.scrollTop = 0));
  await Page.screenshot({ path: Proof + "/LargeCollection.png" });
  await Page.locator(".inspector-scroll").evaluate((N) => {
    const C = N.querySelector(".collection-browser");
    N.scrollTop +=
      C.getBoundingClientRect().top - N.getBoundingClientRect().top - 16;
  });
  await Page.screenshot({ path: Proof + "/CollectionBrowser.png" });
  await Page.getByLabel("Search collection contents", { exact: true }).fill(
    "Asset 01234",
  );
  await Page.locator(".collection-entry-name").click();
  assert.equal((await Saved()).Selected, "asset1233");
  assert.equal((await Saved()).Collapsed.archive, false);
  assert.equal((await Saved()).Collapsed.batch1, false);
  await Select("Cameras");
  assert.equal(
    await Page.getByRole("button", {
      name: "Hide collection entry Editor Camera",
      exact: true,
    }).isDisabled(),
    true,
  );
  await Select("Empty collection");
  assert.equal(
    await Page.locator("[data-collection-total]").getAttribute(
      "data-collection-total",
    ),
    "0",
  );
  assert.match(
    await Page.locator(".folder-inspector").innerText(),
    /This folder is empty/,
  );
  Results.push(
    "2,500-record browser fixture: exact aggregate counts, bounded DOM pagination, search, type filtering, page-size changes, visibility writes, drill-through/ancestor expansion, protected camera and empty state",
  );
  assert.deepEqual(Errors, []);
} catch (Error) {
  Errors.push(Error.stack);
  await Page.screenshot({ path: Proof + "/WorkspaceCardsFailure.png" }).catch(
    () => {},
  );
  process.exitCode = 1;
} finally {
  fs.writeFileSync(
    Proof + "/WorkspaceCards.json",
    JSON.stringify({ Results, Errors, FontFailures }, null, 2),
  );
  console.log(JSON.stringify({ Results, Errors, FontFailures }, null, 2));
  await Browser.close();
}
