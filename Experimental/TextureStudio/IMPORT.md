# Import provenance

Imported locally on 2026-10-18 from
[`streamlinkinbox/Frontier@773b2f5`](https://github.com/streamlinkinbox/Frontier/commit/773b2f5a2672ae530accff000d65d779e9059b66)
("Add packed UV authoring and screen-space digital painting"), at the user's
request, to diagnose and fix the slow TextureStudio stroke in this checkout
(`SultanAladin/Frontier`, branch `arena/c02e286c-frontier`).

Excluded from the import: `site/` (prebuilt bundle), `deliverables/*.zip`
(teapot bake archives) and the stray root `.patch` file — none are inputs
to the studio source. Everything else (`src/`, `tests/`, `docs/`, scripts,
Vite/Playwright configs) is the upstream tree.

Local changes on top of the import are the stroke-performance fix; see
[docs/stroke-performance.md](docs/stroke-performance.md). Run with:

```sh
npm ci
npm run dev   # http://127.0.0.1:5173
node scripts/verify-stroke-incremental.mjs   # after: npm i --no-save @napi-rs/canvas
```
