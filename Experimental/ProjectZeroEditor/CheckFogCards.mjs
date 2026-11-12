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
  const Models = [
    {
      Id: "height-fog",
      Technical: "Height and tint",
      Fields: [
        ["Density", "0", "0.2"],
        ["Falloff Height", "10", "3000"],
        ["Sun Scatter", "0", "2"],
      ],
    },
    {
      Id: "aerial-fog",
      Technical: "Spectral transmission",
      Fields: [
        ["Density", "0", "4"],
        ["Start", "0", "2000"],
        ["Mie Blend", "0", "1"],
      ],
    },
    {
      Id: "local-fog",
      Technical: "Local bounds",
      Fields: [
        ["Density", "0", "4"],
        ["Coverage", "0", "1"],
        ["Feature Scale", "10", "600"],
        ["Anisotropy", "-0.9", "0.9"],
      ],
    },
  ];
  for (const Model of Models) {
    await Open(Model.Id);
    assert.equal(
      await Page.locator(".reference-inspector-copy,.fog-sight,.fog-reading").count(),
      0,
      `${Model.Id} must use the shared native card family only`,
    );
    for (const Title of [
      "Fog settings",
      "Visibility through fog",
      "Medium",
      Model.Technical,
      "Wind binding",
    ])
      assert.equal(
        await Page.locator(`[data-card="${Title}"]`).count(),
        1,
        `${Model.Id} must contain ${Title}`,
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
      Model.Id === "local-fog" ? 1 : 0,
    );
    assert.equal(
      await Page.getByLabel("Colour value", { exact: true }).count(),
      Model.Id === "height-fog" ? 1 : 0,
    );
    const Beam = Page.locator('[data-card="Medium"] .fog-shared-beam');
    assert.equal(await Beam.count(), 1);
    assert.equal(
      await Beam.locator("svg").getAttribute("aria-label"),
      "Fog beam chamber",
    );
    const SpreadLabel =
        Model.Id === "height-fog"
          ? "Sun Scatter value"
          : Model.Id === "aerial-fog"
            ? "Mie Blend value"
            : "Anisotropy value",
      BeamBefore = await Beam.locator("svg").innerHTML();
    await Page.getByLabel(SpreadLabel, { exact: true }).fill(
      Model.Id === "height-fog" ? "1.7" : "0.8",
    );
    await Page.getByLabel(SpreadLabel, { exact: true }).press("Tab");
    assert.notEqual(
      await Beam.locator("svg").innerHTML(),
      BeamBefore,
      `${SpreadLabel} must repaint the shared chamber`,
    );
    const VisibilityPlot = Page.getByRole("slider", {
        name: "Fog distance probe",
        exact: true,
      }),
      VisibilityBefore = await VisibilityPlot.locator("circle").first().getAttribute("cx");
    await VisibilityPlot.focus();
    await Page.keyboard.press("End");
    assert.notEqual(
      await VisibilityPlot.locator("circle").first().getAttribute("cx"),
      VisibilityBefore,
    );
    Checks.push(
      `${Model.Id}: shared C++-aligned card order, model sliders, Visibility and Beam interaction`,
    );
  }

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
  assert.equal(await Page.getByLabel("Colour value", { exact: true }).count(), 1);
  Checks.push("Height and tint retains its interactive profile and canonical colour control");

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
