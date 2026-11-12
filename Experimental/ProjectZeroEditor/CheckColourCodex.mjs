import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import {
  ParseColourCodexToml,
  SerializeColourCodexToml,
} from "./ColourCodex.js";

const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(
  path.resolve(Folder, "../FrontierEditor/package.json"),
);
const { JSDOM } = Require("jsdom");

const Html = fs.readFileSync(path.join(Folder, "index.html"), "utf8");
const Dom = new JSDOM(Html, {
  url: "http://127.0.0.1:8095/",
  runScripts: "dangerously",
  pretendToBeVisual: true,
  beforeParse(Win) {
    Win.HTMLElement.prototype.scrollIntoView = () => {};
    Win.HTMLElement.prototype.setPointerCapture = () => {};
    Win.HTMLElement.prototype.releasePointerCapture = () => {};
    Win.HTMLElement.prototype.hasPointerCapture = () => false;
    Win.ResizeObserver = class {
      observe() {}
      unobserve() {}
      disconnect() {}
    };
    Win.HTMLCanvasElement.prototype.getContext = () => ({
      clearRect() {},
      fillRect() {},
      beginPath() {},
      arc() {},
      fill() {},
      stroke() {},
      moveTo() {},
      lineTo() {},
      closePath() {},
      save() {},
      restore() {},
      translate() {},
      scale() {},
      rotate() {},
      drawImage() {},
      createLinearGradient: () => ({ addColorStop() {} }),
      createRadialGradient: () => ({ addColorStop() {} }),
      createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }),
      putImageData() {},
      getImageData: (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }),
      measureText: () => ({ width: 20 }),
      fillText() {},
      setTransform() {},
    });
    if (!Win.URL.createObjectURL) {
      Win.URL.createObjectURL = () => "blob:mock";
      Win.URL.revokeObjectURL = () => {};
    }
  },
});
const { window } = Dom;
const { document } = window;

const Tick = (Ms = 35) => new Promise((Resolve) => setTimeout(Resolve, Ms));
await Tick(120);

const Click = (Element) => {
  assert.ok(Element, "Expected clickable element to exist");
  Element.dispatchEvent(new window.MouseEvent("mousedown", { bubbles: true }));
  Element.dispatchEvent(new window.MouseEvent("click", { bubbles: true }));
};

const DoubleClick = (Element) => {
  assert.ok(Element, "Expected double-clickable element to exist");
  Click(Element);
  Element.dispatchEvent(new window.MouseEvent("dblclick", { bubbles: true }));
};

function MakeDragTransfer() {
  const Data = new Map();
  return {
    effectAllowed: "all",
    dropEffect: "none",
    get types() {
      return [...Data.keys()];
    },
    setData(Type, Value) {
      Data.set(Type, String(Value));
    },
    getData(Type) {
      return Data.get(Type) || "";
    },
  };
}

// 1. Select a geometry object ("Cube") in the Outliner so Inspector renders MaterialPanel
const CubeRow = [...document.querySelectorAll(".outliner-row, [data-row-id]")].find(
  (Node) => /Cube/i.test(Node.textContent || ""),
);
if (CubeRow) Click(CubeRow);
await Tick();

// 2. Open Asset Browser and verify Materials baseline + Colours category
const AssetHandle = document.querySelector(".asset-notch-handle");
assert.ok(AssetHandle, "Asset Browser notch handle exists");
Click(AssetHandle);
await Tick();

const Browser = document.querySelector(".asset-browser");
assert.ok(Browser, "ContentBrowser mounted");
assert.match(
  Browser.querySelector(".asset-browser-header h1").textContent,
  /ContentBrowser/,
  "Header renamed to ContentBrowser",
);
assert.equal(
  Browser.querySelector(".colour-filters-bar"),
  null,
  "Colour Filters bar is hidden on Materials category",
);
assert.equal(
  Browser.querySelectorAll(".asset-view-switch button").length,
  2,
  "Non-Colours category shows standard 2-way Grid/List switch",
);
assert.equal(
  Browser.querySelectorAll(".asset-cell").length,
  9,
  "Default Materials category preserves 9 built-in materials",
);

const ColoursCategoryBtn = [
  ...Browser.querySelectorAll(".asset-category-rail button"),
].find((Btn) => Btn.textContent.trim().startsWith("Colours"));
assert.ok(ColoursCategoryBtn, "Colours category exists in rail");
Click(ColoursCategoryBtn);
await Tick();

assert.ok(
  Browser.querySelector(".colour-filters-bar"),
  "Dedicated Colour Filters bar appears on Colours category",
);
assert.equal(
  Browser.querySelectorAll(".colour-view-switch button").length,
  3,
  "Colours category shows 3-way Grid / Vertical List / Horizontal List switch",
);

const ColourCells = [...Browser.querySelectorAll(".asset-cell")];
assert.ok(
  ColourCells.length >= 38,
  `Expected >=38 built-in ColourCodex entries, found ${ColourCells.length}`,
);

for (const ExpectedAlias of [
  "Fig Purple",
  "Pineapple Yellow",
  "Charcoal",
  "Midnight",
  "Royal Blue",
  "Arctic",
  "Strawberry",
  "Cherry",
  "Banana",
]) {
  const Found = Browser.querySelector(
    `button[aria-label="Select asset ${ExpectedAlias}"]`,
  );
  assert.ok(Found, `ColourCodex contains alias "${ExpectedAlias}"`);
  const Circle = Found.querySelector(".asset-colour-circle");
  assert.ok(Circle, `"${ExpectedAlias}" renders flat .asset-colour-circle`);
}

// 3. Verify Asset Details flat swatch card for Fig Purple (white ink) & Pineapple Yellow (black ink)
const FigCell = Browser.querySelector(
  'button[aria-label="Select asset Fig Purple"]',
);
Click(FigCell);
await Tick();

let SwatchCard = Browser.querySelector(".codex-swatch-card");
assert.ok(SwatchCard, "Fig Purple renders .codex-swatch-card");
assert.equal(SwatchCard.getAttribute("data-contrast"), "White ink");
assert.equal(SwatchCard.style.backgroundColor, "rgb(59, 31, 58)");
assert.match(SwatchCard.textContent, /FIG/);
assert.match(SwatchCard.textContent, /PURPLE/);
assert.match(SwatchCard.textContent, /HEX:\s*#3B1F3A/);
assert.match(SwatchCard.textContent, /RGB:\s*59, 31, 58/);
assert.match(SwatchCard.textContent, /CMYK:\s*0, 47, 2, 77/);

const PineappleCell = Browser.querySelector(
  'button[aria-label="Select asset Pineapple Yellow"]',
);
Click(PineappleCell);
await Tick();

SwatchCard = Browser.querySelector(".codex-swatch-card");
assert.equal(SwatchCard.getAttribute("data-contrast"), "Black ink");
assert.equal(SwatchCard.style.backgroundColor, "rgb(246, 184, 0)");
assert.match(SwatchCard.textContent, /PINEAPPLE/);
assert.match(SwatchCard.textContent, /YELLOW/);
assert.match(SwatchCard.textContent, /HEX:\s*#F6B800/);
assert.match(SwatchCard.textContent, /RGB:\s*246, 184, 0/);
assert.match(SwatchCard.textContent, /CMYK:\s*0, 25, 100, 4/);

// 4. Verify Double-Click applies colour to selected geometry Base colour
DoubleClick(FigCell);
await Tick();

let SavedState = JSON.parse(
  window.localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
);
const GeometryIds = Object.keys(SavedState.Values).filter(
  (Key) => SavedState.Values[Key]?.Material?.Channels?.colour,
);
assert.ok(
  GeometryIds.some(
    (Id) =>
      SavedState.Values[Id].Material.Channels.colour.Fill === "#3b1f3a",
  ),
  "Double-clicking Fig Purple updated geometry Base colour to #3b1f3a",
);

// 5. Verify Drag-and-Drop auto-reveals workspace ("half" split) and drops onto colour slot
const RoyalBlueCell = Browser.querySelector(
  'button[aria-label="Select asset Royal Blue"]',
);
const Transfer = MakeDragTransfer();
const DragStartEvent = new window.Event("dragstart", { bubbles: true, cancelable: true });
Object.defineProperty(DragStartEvent, "dataTransfer", { value: Transfer });
RoyalBlueCell.dispatchEvent(DragStartEvent);
await Tick();

const BottomDrawer = document.querySelector('[data-drawer="bottom"]');
assert.equal(
  BottomDrawer.getAttribute("data-stage"),
  "half",
  "Starting colour drag from full drawer automatically snaps to half split view",
);
const MainWorkspace = document.querySelector("main.workspace");
assert.equal(
  MainWorkspace.hasAttribute("inert"),
  false,
  "Workspace is interactive (not inert) during split / colour drag",
);

const SheenChannelSlot = document.querySelector(
  '.material-panel [data-material-channel="sheen"]',
);
if (SheenChannelSlot) {
  const DragOverEvent = new window.Event("dragover", { bubbles: true, cancelable: true });
  Object.defineProperty(DragOverEvent, "dataTransfer", { value: Transfer });
  SheenChannelSlot.dispatchEvent(DragOverEvent);

  const DropEvent = new window.Event("drop", { bubbles: true, cancelable: true });
  Object.defineProperty(DropEvent, "dataTransfer", { value: Transfer });
  SheenChannelSlot.dispatchEvent(DropEvent);
  await Tick();

  SavedState = JSON.parse(
    window.localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
  );
  assert.ok(
    Object.values(SavedState.Values).some(
      (Val) => Val?.Material?.Channels?.sheen?.Fill === "#2454ff",
    ),
    "Dropping Royal Blue onto Sheen colour slot updated sheen Fill to #2454ff",
  );
}

const DragEndEvent = new window.Event("dragend", { bubbles: true });
RoyalBlueCell.dispatchEvent(DragEndEvent);
await Tick();

// 6. Verify dropping onto .colour-input (Base colour fill) AND + Colour / TOML import
const StrawberryCell = Browser.querySelector(
  'button[aria-label="Select asset Strawberry"]',
);
const Transfer2 = MakeDragTransfer();
const DragStart2 = new window.Event("dragstart", { bubbles: true, cancelable: true });
Object.defineProperty(DragStart2, "dataTransfer", { value: Transfer2 });
StrawberryCell.dispatchEvent(DragStart2);
await Tick();

const FillSlot = document.querySelector(".material-panel .colour-input");
assert.ok(FillSlot, "MaterialPanel .colour-input exists");
const Drop2 = new window.Event("drop", { bubbles: true, cancelable: true });
Object.defineProperty(Drop2, "dataTransfer", { value: Transfer2 });
FillSlot.dispatchEvent(Drop2);
StrawberryCell.dispatchEvent(new window.Event("dragend", { bubbles: true }));
await Tick();

SavedState = JSON.parse(
  window.localStorage.getItem("Frontier.ProjectZeroHtml.v1"),
);
assert.ok(
  Object.values(SavedState.Values).some((Val) =>
    Object.values(Val?.Material?.Channels || {}).some(
      (Ch) => Ch.Fill === "#e63956",
    ),
  ),
  "Dropping Strawberry onto .colour-input updated channel Fill to #e63956",
);

// Create custom Project colour via + Colour & edit hex
const CreateColourBtn = [
  ...Browser.querySelectorAll(".asset-header-actions button"),
].find((Btn) => Btn.textContent.trim() === "+ Colour");
assert.ok(CreateColourBtn, "+ Colour button exists");
Click(CreateColourBtn);
await Tick();

const HexInput = Browser.querySelector(
  'input[aria-label="Edit codex colour hex"]',
);
assert.ok(HexInput, "Custom Project colour shows editable hex field");

// Import a multi-entry .toml file through Import assets
const ImportInput = Browser.querySelector('input[aria-label="Import assets"]');
const CustomToml = [
  'Format = "FrontierColourCodex.v1"',
  "",
  "[Colours.DeepOcean]",
  'Name = "Deep Ocean"',
  'Hex = "#0B3C5D"',
  'Family = "Marine"',
  "",
  "[Colours.SunburstGold]",
  'Name = "Sunburst Gold"',
  'Hex = "#FFBE0B"',
  'Family = "Solar"',
].join("\n");
const MockTomlFile = new window.File([CustomToml], "StudioPalette.toml", {
  type: "application/toml",
});
if (typeof MockTomlFile.text !== "function") {
  MockTomlFile.text = async () => CustomToml;
}
Object.defineProperty(ImportInput, "files", {
  value: [MockTomlFile],
  configurable: true,
});
ImportInput.dispatchEvent(new window.Event("change", { bubbles: true }));
await Tick(120);

assert.ok(
  Browser.querySelector('button[aria-label="Select asset Deep Ocean"]'),
  "Imported .toml added 'Deep Ocean' to ColourCodex",
);
assert.ok(
  Browser.querySelector('button[aria-label="Select asset Sunburst Gold"]'),
  "Imported .toml added 'Sunburst Gold' to ColourCodex",
);

// 6b. Verify Colour Filters ("August Beige & Brown") + Vertical & Horizontal Strip List Views
Click(
  [...Browser.querySelectorAll(".asset-origin-tabs button")].find(
    (Btn) => Btn.textContent.trim() === "Engine",
  ),
);
await Tick();

const AugustFilterBtn = [
  ...Browser.querySelectorAll(".colour-filter-chips button"),
].find((Btn) => Btn.textContent.includes("August Beige & Brown"));
assert.ok(AugustFilterBtn, "'August Beige & Brown' Colour Filter exists");
Click(AugustFilterBtn);
await Tick();

const VerticalListBtn = Browser.querySelector(
  'button[aria-label="vertical list colour view"]',
);
Click(VerticalListBtn);
await Tick();

const VerticalDeck = Browser.querySelector(".colour-strip-deck.list-vertical");
assert.ok(VerticalDeck, "Vertical colour strip deck mounted");
assert.equal(
  VerticalDeck.querySelector(".colour-strip-banner")?.textContent,
  "August Beige & Brown",
);
const VerticalHexLabels = [
  ...VerticalDeck.querySelectorAll(".colour-strip-vertical-hex"),
].map((Node) => Node.textContent.trim());
for (const ExpectedHex of [
  "FF7B00",
  "FF8800",
  "FF9500",
  "FFA200",
  "FFAA00",
  "FFB700",
  "FFC300",
  "FFD000",
  "FFDD00",
  "FFEA00",
]) {
  assert.ok(
    VerticalHexLabels.includes(ExpectedHex),
    `Vertical strip list contains ${ExpectedHex}`,
  );
}
const BlazeBand = Browser.querySelector(
  'button[aria-label="Select asset August Blaze"]',
);
const MarigoldBand = Browser.querySelector(
  'button[aria-label="Select asset August Marigold"]',
);
assert.equal(BlazeBand.style.color, "rgb(255, 255, 255)");
assert.equal(MarigoldBand.style.color, "rgb(17, 17, 17)");

const HorizontalListBtn = Browser.querySelector(
  'button[aria-label="horizontal list colour view"]',
);
Click(HorizontalListBtn);
await Tick();

const HorizontalDeck = Browser.querySelector(
  ".colour-strip-deck.list-horizontal",
);
assert.ok(HorizontalDeck, "Horizontal colour strip deck mounted");
assert.ok(
  HorizontalDeck.querySelectorAll(".colour-strip-band").length >= 10,
  "Horizontal colour strip deck renders flush colour bands",
);

// 7. Verify TOML serialization & parsing round-trip
const TomlSample = SerializeColourCodexToml([
  { Name: "Fig Purple", Hex: "#3B1F3A", Family: "Botanical" },
  { Name: "Pineapple Yellow", Hex: "#F6B800", Family: "Botanical" },
  { Name: "Arctic", Hex: "#F8FAFC", Family: "Neutral" },
]);
assert.match(TomlSample, /Format = "FrontierColourCodex\.v1"/);
assert.match(TomlSample, /\[Colours\.FigPurple\]/);
const RoundTripped = ParseColourCodexToml(TomlSample);
assert.equal(RoundTripped.length, 3);
assert.equal(RoundTripped[0].Name, "Fig Purple");
assert.equal(RoundTripped[0].Hex, "#3B1F3A");
assert.deepEqual(RoundTripped[0].RGB, [59, 31, 58]);
assert.deepEqual(RoundTripped[0].CMYK, [0, 47, 2, 77]);
assert.equal(RoundTripped[1].Name, "Pineapple Yellow");
assert.equal(RoundTripped[1].Hex, "#F6B800");
assert.deepEqual(RoundTripped[1].RGB, [246, 184, 0]);
assert.deepEqual(RoundTripped[1].CMYK, [0, 25, 100, 4]);

console.log(
  "CheckColourCodex PASSED: 38 built-in ColourCodex aliases, ContentBrowser rename, Colours-only Colour Filters, Grid / Vertical Strips / Horizontal Strips views, double-click & drag-and-drop assignment, and TOML round-trip verified.",
);
Dom.window.close();
