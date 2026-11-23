import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

// Follow-up 48: the Sun first card (the rendered sky and sun, three framings A/B/C) and the Atmosphere first card (the planet's limb
// from orbit with a drag handle for the atmosphere's height). Both are drawn by SkyShader.js, so the browser needs WebGL2.
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Require = createRequire(process.env.FRONTIER_BROWSER_PACKAGE || path.resolve(Folder, "../../../_AgentScratch/Browser/package.json"));
const { chromium } = Require("playwright");
const Proof = process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/FirstCards");
fs.mkdirSync(Proof, { recursive: true });
const Checks = [],
  Errors = [];
const Browser = await chromium.launch({ executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium", args: ["--no-sandbox", "--disable-gpu", "--enable-unsafe-swiftshader"] });
const Page = await Browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 2 });
Page.setDefaultTimeout(20000);
const Address = process.env.FRONTIER_EDITOR_URL || "http://127.0.0.1:4173/ProjectZeroEditor/";
Page.on("pageerror", (Error) => Errors.push(Error.stack));
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll > [data-panel]").waitFor();
  await Page.waitForTimeout(250);
};
const Saved = () => Page.evaluate(() => JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")));
const StoredBecomes = (Id, Key, Test) =>
  Page.waitForFunction(
    ([Id, Key, Source]) => {
      const Values = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1") || "{}")?.Values?.[Id] || {};
      return new Function("Stored", "return " + Source)(Values[Key]);
    },
    [Id, Key, Test],
  );
const Patch = (Id, Values) =>
  Page.evaluate(
    ([Id, Values]) => {
      const Scene = JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1") || "{}");
      Scene.Values ||= {};
      Scene.Values[Id] = Object.assign(Scene.Values[Id] || {}, Values);
      localStorage.setItem("Frontier.ProjectZeroHtml.v1", JSON.stringify(Scene));
    },
    [Id, Values],
  );
const Card = (Title) => Page.locator(`[data-card="${Title}"]`);

// Average colour of a small square of a painted canvas, as fractions of its size.
const Pixel = (Locator, X, Y) =>
  Locator.evaluate(
    (Canvas, [X, Y]) => {
      const Pixels = Canvas.getContext("2d").getImageData(Math.round(X * Canvas.width) - 2, Math.round(Y * Canvas.height) - 2, 5, 5).data;
      const Mean = [0, 0, 0];
      for (let At = 0; At < Pixels.length; At += 4) for (let Channel = 0; Channel < 3; Channel++) Mean[Channel] += Pixels[At + Channel] / 25;
      return Mean.map(Math.round);
    },
    [X, Y],
  );
const Painted = (Locator, Before = "") => Page.waitForFunction(([Selector, Before]) => { const Mark = document.querySelector(Selector)?.dataset.painted; return Mark && Mark !== Before; }, [Locator, Before]);

try {
  await Page.goto(Address);
  await Page.evaluate(() => localStorage.clear());

  // ----------------------------------------------------------------------------------------------------------------------- Sun
  await Patch("sun", { "Local Hours": 15.5 });
  await Open("sun");
  const Sun = Card("Sun now");
  const SunCanvas = Sun.locator("[data-sun-picture]");
  await Painted("[data-sun-picture]");
  assert.deepEqual(await Sun.locator("[data-sun-design-pick]").evaluateAll((Each) => Each.map((Button) => [Button.dataset.sunDesignPick, Button.textContent])), [["sky", "A"], ["day", "B"], ["disc", "C"]]);
  // A · Sky: facing the sun; the time, one line, and the day bar with sunrise and sunset. Nothing else.
  assert.equal(await Sun.locator('[data-sun-design="sky"]').count(), 1);
  assert.equal(await Sun.locator("[data-sun-time]").innerText(), "15:30");
  assert.equal(await Sun.locator("[data-sun-line]").innerText(), "34° up, facing west");
  assert.deepEqual(await Sun.locator("[data-sun-fact]").allInnerTexts(), ["05:43", "18:08"]);
  const Zenith = await Pixel(SunCanvas, 0.5, 0.04);
  assert(Zenith[2] > Zenith[0] + 30, "afternoon sky is blue: " + Zenith);
  await Sun.screenshot({ path: path.join(Proof, "SunSky.png") });
  let Mark = await SunCanvas.evaluate((Canvas) => Canvas.dataset.painted);
  await Sun.locator(".sun-picture").focus();
  await Page.keyboard.press("ArrowRight");
  await StoredBecomes("sun", "Local Hours", "Stored === 15.75");
  await Painted("[data-sun-picture]", Mark);
  const Bar = await Sun.locator("[data-sun-day-bar]").boundingBox();
  await Page.mouse.click(Bar.x + Bar.width * 0.75, Bar.y + Bar.height / 2);
  await StoredBecomes("sun", "Local Hours", "Math.abs(Stored - 18) < 0.1");
  Checks.push(`Sun A · Sky: 15:30, "34° up, facing west", sunrise 05:43 / sunset 18:08 only; blue zenith rgb(${Zenith.join(", ")}); → moves 15 min; three quarters along the day bar sets 18:00`);

  // B · Day: the whole path over the horizon; clicking its top sets the highest point.
  await Patch("sun", { "Local Hours": 15.5 });
  await Open("sun");
  await Sun.locator('[data-sun-design-pick="day"]').click();
  await StoredBecomes("sun", "Sun card design", 'Stored === "day"');
  await Painted("[data-sun-picture]");
  assert.equal(await Sun.locator("[data-sun-line]").innerText(), "12 h 25 of daylight · highest 68° at 11:54");
  assert.deepEqual(await Sun.locator("[data-sun-mark]").evaluateAll((Each) => Each.map((Mark) => Mark.getAttribute("aria-label"))), ["Sunrise 05:43", "Sunset 18:08"]);
  const Top = await Sun.locator("[data-sun-path]").evaluate((Line) => {
    const Box = Line.ownerSVGElement.getBoundingClientRect(), Scale = Box.width / Line.ownerSVGElement.viewBox.baseVal.width;
    const Points = [...Line.points].sort((A, B) => A.y - B.y);
    return [Box.left + Points[0].x * Scale, Box.top + Points[0].y * Scale];
  });
  await Sun.screenshot({ path: path.join(Proof, "SunDay.png") });
  await Page.mouse.click(Top[0], Top[1]);
  await StoredBecomes("sun", "Local Hours", "Math.abs(Stored - 11.9) < 0.35");
  Checks.push("Sun B · Day: 12 h 25 of daylight, highest 68° at 11:54, rise 05:43 and set 18:08 marked on the horizon; clicking the top of the path sets midday");

  // C · Disc: the true-size sun is white in the afternoon and orange two degrees up.
  await Patch("sun", { "Local Hours": 15.5 });
  await Open("sun");
  await Sun.locator('[data-sun-design-pick="disc"]').click();
  await StoredBecomes("sun", "Sun card design", 'Stored === "disc"');
  await Painted("[data-sun-picture]");
  const White = await Pixel(SunCanvas, 0.5, 0.5);
  assert(Math.min(...White) > 225, "afternoon disc is white: " + White);
  await Sun.screenshot({ path: path.join(Proof, "SunDisc.png") });
  await Patch("sun", { "Local Hours": 17.95 });
  await Open("sun");
  await Painted("[data-sun-picture]");
  assert.match(await Sun.locator("[data-sun-line]").innerText(), /^1\.\d° above the horizon$/);
  const Orange = await Pixel(SunCanvas, 0.5, 0.5);
  assert(Orange[0] > Orange[2] * 1.5, "low disc is orange: " + Orange);
  await Sun.screenshot({ path: path.join(Proof, "SunDiscLow.png") });
  Checks.push(`Sun C · Disc: afternoon disc rgb(${White.join(", ")}); at 17:57 it is ${await Sun.locator("[data-sun-line]").innerText()} and orange rgb(${Orange.join(", ")}); the design survives reload`);

  // ---------------------------------------------------------------------------------------------------------------- Atmosphere
  await Open("atmosphere");
  const Air = Card("Atmosphere overview");
  const Limb = Air.locator("[data-atmosphere-limb]");
  await Painted("[data-atmosphere-limb]");
  // Only the picture, its handle and the sun slider: no design picker, figure tiles or swatches.
  assert.equal(await Air.locator("[data-atmosphere-design-pick], [data-figure], [data-swatch]").count(), 0);
  assert.equal(await Air.locator("[data-top-outline]").count(), 1);
  const Handle = Air.locator("[data-top-handle]");
  assert.equal(await Handle.getAttribute("aria-valuetext"), "60 km");
  const Gap = await Air.evaluate((Card) => Card.querySelector("[data-atmosphere-limb]").getBoundingClientRect().top - Card.querySelector("h3").getBoundingClientRect().bottom);
  assert(Gap < 20, "picture sits under the title: " + Gap);
  // The lit limb is on the left (the sun's side): the brightest point of the air band in a column near each edge.
  const Brightest = (X) =>
    Limb.evaluate((Canvas, X) => {
      const Pixels = Canvas.getContext("2d").getImageData(Math.round(X * Canvas.width), 0, 1, Canvas.height).data;
      let Best = [0, 0, 0];
      for (let At = 0; At < Pixels.length; At += 4) if (Pixels[At] + Pixels[At + 1] + Pixels[At + 2] > Best[0] + Best[1] + Best[2]) Best = [Pixels[At], Pixels[At + 1], Pixels[At + 2]];
      return Best;
    }, X);
  const [Day, Night] = [await Brightest(0.06), await Brightest(0.94)];
  assert(Day.reduce((A, B) => A + B) > Night.reduce((A, B) => A + B) * 1.15, `day ${Day} vs night ${Night}`);
  await Air.screenshot({ path: path.join(Proof, "AtmosphereLimb.png") });
  await Handle.focus();
  await Page.keyboard.press("ArrowUp");
  await StoredBecomes("atmosphere", "Atmosphere", "Stored === 61000");
  await Page.keyboard.press("PageDown");
  await StoredBecomes("atmosphere", "Atmosphere", "Stored === 51000");
  const Grip = await Handle.boundingBox();
  await Page.mouse.move(Grip.x + Grip.width / 2, Grip.y + Grip.height / 2);
  await Page.mouse.down();
  await Page.mouse.move(Grip.x + Grip.width / 2, Grip.y - 40, { steps: 6 });
  await Page.mouse.up();
  await StoredBecomes("atmosphere", "Atmosphere", "Stored > 70000");
  const Dragged = (await Saved()).Values.atmosphere.Atmosphere;
  await Air.locator('input[aria-label="Sun at the limb"]').fill("-4");
  await StoredBecomes("atmosphere", "Limb sun elevation", "Stored === -4");
  await Painted("[data-atmosphere-limb]");
  await Air.screenshot({ path: path.join(Proof, "AtmosphereLimbTwilight.png") });
  Checks.push(`Atmosphere · Limb: picture only (no tiles or swatches), ${Gap.toFixed(0)} px under the title; day rgb(${Day.join(", ")}) left of night rgb(${Night.join(", ")}); handle 60 km → ↑ 61 km → PgDn 51 km → drag up ${(Dragged / 1000).toFixed(1)} km; sun at the limb −4° stored`);

  assert.deepEqual(Errors, []);
  console.log(Checks.map((Check) => "🟢 " + Check).join("\n"));
  console.log(`${Checks.length} FirstCards checks passed`);
} finally {
  await Browser.close();
}
