import { test, expect } from "@playwright/test";
import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import config from "../vite.config.js";
import {
  createTextureDocument,
  TEXTURE_DRAFT_KEY,
} from "../src/textureDocument.js";

const studioOrigin = process.env.STUDIO_TEST_ORIGIN;
function address(path) {
  return studioOrigin ? new URL(path, studioOrigin).href : path;
}
function runtimeErrors(page) {
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("console", (message) => {
    if (
      message.type() === "error" ||
      /Invalid hook call|Multiple instances of Three/.test(message.text())
    )
      errors.push(message.text());
  });
  return errors;
}
async function ready(page, workspace) {
  await expect(page.locator(".studio-workspace-error")).toHaveCount(0);
  if (workspace === "pattern")
    await expect(
      page.getByLabel("Pattern design canvas", { exact: true }),
    ).toBeVisible();
  else if (workspace === "texture")
    await expect(page.locator(".tp-studio canvas")).toHaveAttribute(
      "data-material-ready",
      "true",
    );
  else if (workspace === "stamp")
    await expect(
      page.getByAltText("Composed stamp preview", { exact: true }),
    ).toBeVisible();
  else if (workspace === "baking")
    await expect(page.locator(".bk-studio canvas")).toHaveAttribute(
      "data-mesh-ready",
      "true",
    );
  else
    await expect(
      page.locator(".studio-material > div:not([aria-hidden]) canvas"),
    ).toHaveAttribute("data-material-ready", "true");
}

test("dev configuration prebundles every browser dependency and deduplicates React / Three", async () => {
  expect(config.resolve.dedupe).toEqual(
    expect.arrayContaining(["react", "react-dom", "three"]),
  );
  expect(config.optimizeDeps.noDiscovery).toBe(true);
  expect(config.optimizeDeps.force).toBe(true);
  expect(config.cacheDir).not.toBe("node_modules/.vite");
  expect(config.server.headers["Cache-Control"]).toBe("no-store");
  const sourceRoot = fileURLToPath(new URL("../src/", import.meta.url));
  for (const file of await readdir(sourceRoot, { recursive: true })) {
    if (!/\.(?:js|jsx)$/.test(file)) continue;
    const source = await readFile(`${sourceRoot}/${file}`, "utf8");
    for (const [, dependency] of source.matchAll(
      /(?:from\s*|import\s*)["']([^"']+)["']/g,
    )) {
      if (/^(?:\.|\/|@fontsource)/.test(dependency)) continue;
      expect(
        config.optimizeDeps.include,
        `Missing frozen dependency ${dependency} in ${file}`,
      ).toContain(dependency);
    }
  }
});

test("cached reloads and repeated workspace switches keep the hooks and renderer healthy", async ({
  page,
}) => {
  const errors = runtimeErrors(page),
    versions = new Set();
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (
      url.pathname.includes("/.vite-studio/deps/") &&
      url.searchParams.has("v")
    )
      versions.add(url.searchParams.get("v"));
  });
  await page.goto(address("/?studio=texture"));
  await ready(page, "texture");
  for (let cycle = 0; cycle < 2; cycle++) {
    for (const [button, workspace] of [
      ["Pattern", "pattern"],
      ["Texture", "texture"],
      ["Stamp", "stamp"],
      ["Baking", "baking"],
      ["Material", "material"],
    ]) {
      await page
        .getByRole("navigation", { name: "Studio workspace" })
        .getByRole("button", { name: new RegExp(`^${button}(?: studio)?$`) })
        .click();
      await ready(page, workspace);
    }
    await page.goto(address("/?studio=texture"));
    await page.reload();
    await ready(page, "texture");
  }
  expect(versions.size).toBeLessThanOrEqual(1);
  expect(errors).toEqual([]);
});

test("deferred dev module imports do not create a second React or Three runtime", async ({
  page,
  request,
}) => {
  const html = await (await request.get(address("/"))).text();
  test.skip(
    !html.includes("/src/main.jsx"),
    "Dev-only module test; built preview is tested by the reload/iframe cases.",
  );
  const errors = runtimeErrors(page),
    versions = new Set();
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (
      url.pathname.includes("/.vite-studio/deps/") &&
      url.searchParams.has("v")
    )
      versions.add(url.searchParams.get("v"));
  });
  await page.goto(address("/?studio=texture"));
  await ready(page, "texture");
  await page.getByRole("button", { name: "Pattern", exact: true }).click();
  await ready(page, "pattern");
  await page.evaluate(async () => {
    const probe = await import("/tests/fixtures/runtime-probe.jsx");
    const host = document.createElement("div");
    host.id = "runtime-probe-host";
    document.body.appendChild(host);
    probe.mountRuntimeProbe(host);
    const materialModule = await import("/src/materials.js");
    const material = materialModule.createMaterial(materialModule.materials[0]);
    if (!(material instanceof probe.runtimeThree.MeshPhysicalMaterial))
      throw new Error("The deferred import loaded a second Three constructor.");
    material.dispose();
  });
  await expect(page.getByTestId("runtime-hook-probe")).toHaveText(
    "Runtime hook ready",
  );
  // The fixture is behind the full-screen editor; exercise its event without
  // changing the editor layout or suppressing any runtime errors.
  await page.getByTestId("runtime-hook-probe").dispatchEvent("click");
  await expect(page.getByTestId("runtime-hook-probe")).toHaveText(
    "Runtime hook updated",
  );
  await page.evaluate(async () => {
    (await import("/tests/fixtures/runtime-probe.jsx")).unmountRuntimeProbe();
    document.getElementById("runtime-probe-host").remove();
  });
  expect(versions.size).toBe(1);
  expect(errors).toEqual([]);
});

for (const [workspace, hook] of [
  ["pattern", "usePatternHistory.js"],
  ["texture", "useTextureDocument.js"],
]) {
  test(`a replayed stale ${workspace} React generation is reproduced and Reload studio repairs it without clearing projects`, async ({
    page,
    request,
  }) => {
    const sourceResponse = await request.get(address(`/src/${hook}`));
    const source = await sourceResponse.text();
    test.skip(
      !source.includes("/.vite-studio/deps/react.js"),
      "Dev cache replay only; built preview has no optimizer generations.",
    );
    expect(sourceResponse.headers()["cache-control"]).toBe("no-store");
    const reactURL = source.match(/"([^"\n]+\/react\.js\?v=([^"\n]+))"/);
    expect(reactURL).toBeTruthy();
    expect(
      (await request.get(address(reactURL[1]))).headers()["cache-control"],
    ).toBe("no-store");
    const current = reactURL[2],
      replay = `stale-${current}`;
    // This emulates the user's immutable cached modules, not a different npm React
    // package: byte-identical React at different ?v URLs gets two dispatchers.
    await page.route(
      /\/node_modules\/\.vite-studio\/deps\/.*\?v=stale-/,
      async (route) => {
        const fresh = new URL(route.request().url());
        fresh.searchParams.set("v", current);
        const response = await request.get(fresh.href);
        const body = (await response.text()).replaceAll(
          `?v=${current}`,
          `?v=${replay}`,
        );
        await route.fulfill({
          status: 200,
          contentType: "text/javascript",
          body,
        });
      },
    );
    // Parent modules can carry Vite HMR timestamps after an editor change.
    // Replay the hook at either spelling rather than silently missing ?t URLs.
    await page.route(
      new RegExp(`/src/${hook.replaceAll(".", "\\.")}(?:\\?.*)?$`),
      (route) =>
        route.fulfill({
          status: 200,
          contentType: "text/javascript",
          body: source.replaceAll(`?v=${current}`, `?v=${replay}`),
        }),
    );
    const doc = createTextureDocument();
    doc.layers.at(-1).name = "Keep my saved base";
    const initial = workspace === "pattern" ? "texture" : "pattern";
    await page.goto(address(`/?studio=${initial}`));
    await ready(page, initial);
    // Seed once, not in an init script: a reload must preserve the existing value,
    // rather than letting the test silently recreate a project that was erased.
    await page.evaluate(
      ({ key, doc }) => localStorage.setItem(key, JSON.stringify(doc)),
      { key: TEXTURE_DRAFT_KEY, doc },
    );
    await page.goto(address(`/?studio=${workspace}`));
    await expect(page.getByRole("alert")).toContainText(
      "different versions of the studio runtime",
    );
    await expect(
      page.getByRole("button", { name: "Reload studio", exact: true }),
    ).toBeVisible();
    await page.unrouteAll();
    await page
      .getByRole("button", { name: "Reload studio", exact: true })
      .click();
    await ready(page, workspace);
    if (workspace !== "texture")
      await page.getByRole("button", { name: "Texture", exact: true }).click();
    await ready(page, "texture");
    await expect(
      page.getByRole("button", {
        name: "Select Keep my saved base",
        exact: true,
      }),
    ).toBeVisible();
  });
}

test("the embedded preview switches and reloads all workspaces without invalid hooks", async ({
  page,
  baseURL,
}) => {
  const errors = runtimeErrors(page),
    url = new URL("/?studio=texture", studioOrigin || baseURL).href;
  await page.setContent(
    `<iframe title="Surface Studio preview" src="${url}" style="width:1400px;height:900px;border:0"></iframe>`,
  );
  const preview = page.frameLocator("iframe");
  await expect(preview.locator(".tp-studio canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  for (const [label, selector] of [
    ["Pattern", ".pe-overlay"],
    ["Stamp", ".stamp-studio"],
    ["Baking", ".bk-studio"],
    ["Texture", ".tp-studio"],
  ]) {
    await preview.getByRole("button", { name: label, exact: true }).click();
    await expect(preview.locator(selector)).toBeVisible();
    await expect(preview.locator(".studio-workspace-error")).toHaveCount(0);
  }
  await page.evaluate(() => {
    const iframe = document.querySelector("iframe");
    iframe.src = iframe.src;
  });
  await expect(preview.locator(".tp-studio canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  expect(errors).toEqual([]);
});
