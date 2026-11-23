import { defineConfig } from "@playwright/test";
import studioConfig from "./playwright.config.js";

// This suite must not use Vite: intercepting a synthetic origin serves exactly
// the committed HTML, with no development server to fill in missing assets.
const { webServer, testIgnore, ...shared } = studioConfig;
export default defineConfig({
  ...shared,
  testMatch: "standalone.spec.js",
  timeout: 420000,
  use: {
    ...shared.use,
    baseURL: "https://alloy-standalone.invalid",
  },
});
