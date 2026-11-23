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
  process.env.FRONTIER_PROOF_FOLDER || path.join(Folder, "Screenshots/FogCards");
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  args: ["--no-sandbox", "--disable-gpu"],
});
const Page = await Browser.newPage({ viewport: { width: 1365, height: 900 } });
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
const Open = async (Id) => {
  await Page.goto(Address + "?inspect=" + Id);
  await Page.locator(".inspector-scroll > [data-panel]").waitFor();
  await Page.waitForTimeout(250);
};
const Saved = () =>
  Page.evaluate(() =>
    JSON.parse(localStorage.getItem("Frontier.ProjectZeroHtml.v1")),
  );

try {
  // Height Fog and Atmospheric Fog have their own cards; Local Fog keeps the shared fog card family.
  const Models = [
    {
      Id: "height-fog",
      Cards: ["Fog layer", "Density by altitude", "Light in the fog"],
      Fields: [
        ["Density", "0", "0.2"],
        ["Falloff Height", "10", "3000"],
        ["Sun Scatter", "0", "2"],
      ],
      Probe: "Fog falloff height",
    },
    {
      Id: "aerial-fog",
      Cards: ["Aerial perspective", "Spectral transmission"],
      Fields: [
        ["Density", "0", "4"],
        ["Start", "0", "2000"],
        ["Mie Blend", "0", "1"],
      ],
      Probe: "Fog distance probe",
    },
    {
      Id: "local-fog",
      Cards: ["Fog settings", "Visibility through fog", "Medium", "Fog volume shape"],
      Fields: [
        ["Density", "0", "4"],
        ["Coverage", "0", "1"],
        ["Feature Scale", "10", "600"],
        ["Anisotropy", "-0.9", "0.9"],
      ],
      Probe: "Fog distance probe",
    },
  ];
  for (const Model of Models) {
    await Open(Model.Id);
    assert.equal(
      await Page.locator(".reference-inspector-copy,.fog-sight,.fog-reading").count(),
      0,
      `${Model.Id} must not import reference cards`,
    );
    assert.deepEqual(
      await Page.locator("[data-panel] [data-card]").evaluateAll((Nodes) =>
        Nodes.map((Node) => Node.dataset.card),
      ),
      Model.Cards,
      `${Model.Id} card list`,
    );
    for (const [Field, Minimum, Maximum] of Model.Fields) {
      const Input = Page.getByLabel(`${Field} value`, { exact: true });
      assert.equal(
        await Input.count(),
        1,
        `${Model.Id} must expose the C++ ${Field} slider once`,
      );
      assert.equal(await Input.getAttribute("min"), Minimum);
      assert.equal(await Input.getAttribute("max"), Maximum);
    }
    assert.equal(
      await Page.getByRole("button", { name: "Follow Wind", exact: true }).count(),
      0,
      `${Model.Id} has no wind binding`,
    );
    assert.equal(
      await Page.locator('input[type="color"][aria-label="Colour"]').count(),
      Model.Id === "height-fog" ? 1 : 0,
    );
    if (Model.Id === "local-fog") {
      const Beam = Page.locator('[data-card="Medium"] .fog-shared-beam');
      assert.equal(await Beam.count(), 1);
      const BeamBefore = await Beam.locator("svg").innerHTML();
      await Page.getByLabel("Anisotropy value", { exact: true }).fill("0.8");
      await Page.getByLabel("Anisotropy value", { exact: true }).press("Tab");
      assert.notEqual(await Beam.locator("svg").innerHTML(), BeamBefore, "Anisotropy must repaint the chamber");
    } else assert.equal(await Page.locator(".fog-shared-beam").count(), 0);
    const Probe = Page.getByRole("slider", { name: Model.Probe, exact: true }),
      ProbeBefore = await Probe.locator("circle").first().getAttribute(Model.Id === "height-fog" ? "cy" : "cx");
    await Probe.focus();
    await Page.keyboard.press("End");
    assert.notEqual(
      await Probe.locator("circle").first().getAttribute(Model.Id === "height-fog" ? "cy" : "cx"),
      ProbeBefore,
    );
    await Page.screenshot({ path: path.join(Proof, Model.Id + ".png"), fullPage: true });
    Checks.push(`${Model.Id}: own card list, model sliders and probe interaction`);
  }
  await Open("height-fog");
  await Page.getByRole("slider", { name: "Fog falloff height", exact: true }).focus();
  await Page.keyboard.press("End");
  assert.equal((await Saved()).Values["height-fog"]["Falloff Height"], 3000);
  Checks.push("Height Fog layer handle edits Falloff Height");

  await Open("height-fog");
  const DensityProfile = Page.getByRole("img", {
    name: "Interactive Height Fog density profile",
  });
  const DensityBefore = await Page.getByLabel("Density value").inputValue(),
    FalloffBefore = await Page.getByLabel("Falloff Height value").inputValue();
  await DensityProfile.click({ position: { x: 190, y: 75 } });
  assert.notEqual(await Page.getByLabel("Density value").inputValue(), DensityBefore);
  assert.notEqual(
    await Page.getByLabel("Falloff Height value").inputValue(),
    FalloffBefore,
  );
  assert.equal(await Page.locator('input[type="color"][aria-label="Colour"]').count(), 1);
  Checks.push("Density by altitude retains its interactive profile; Light in the fog holds the colour control");

  await Open("aerial-fog");
  const Spectrum = Page.getByRole("slider", {
      name: "Aerial fog spectrum",
      exact: true,
    }),
    SpectrumBefore = await Spectrum.locator("circle").first().getAttribute("cx");
  await Spectrum.focus();
  await Page.keyboard.press("Home");
  assert.notEqual(await Spectrum.locator("circle").first().getAttribute("cx"), SpectrumBefore);
  Checks.push("Atmospheric Fog retains interactive spectral transmission");
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
console.log("Height Fog correction checks passed:", Checks.length);
