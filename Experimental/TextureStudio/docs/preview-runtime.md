# Preview runtime and invalid-hook recovery

## Failure addressed

An existing development-preview session failed to open Pattern and Texture with `Cannot read properties of null (reading 'useState')`, an invalid-hook warning, and a multiple-Three-instances warning. The hook modules referenced optimized dependencies with `?v=0e583d91`, while the renderer referenced `?v=311a71e5`.

There was one installed React / React DOM version and one installed Three version. That does not guarantee one browser runtime: ES modules with different query strings have different identities, even if they contain the same package bytes. Late Vite dependency discovery and immutable cached optimizer generations could leave an open preview with two hook dispatchers.

Retrying the failed component cannot repair a split dispatcher. The runtime-specific recovery action reloads the whole document, without deleting material presets, pattern drafts, or texture-layer projects. Other workspace exceptions still offer component retry and return-to-Material controls.

## Stable preview

```sh
npm ci
npm run studio
```

This builds the application and serves `dist/` on `0.0.0.0:5173`. React, its renderer, and Three are bundled into a single production module graph, rather than browser-facing development optimizer generations. The preview accepts Arena's `.e2b.app` hosts and sends `Cache-Control: no-store`.

After changing application code, stop and restart `npm run studio` to rebuild. Only one server can occupy port 5173. For active development, use `npm run dev` instead.

When transitioning an old development tab to this preview, reload the **preview document** once. A tab that still holds the old recovery component may show **Retry workspace**, so use the preview's reload control rather than retrying that component. There is no need to erase browser storage, change credentials, or sign out.

## Development safeguards

- `node_modules/.vite-studio` isolates the repaired optimizer URLs from the previous cache namespace.
- `resolve.dedupe` covers React, React DOM, and Three.
- An explicit dependency inventory prebundles React entry points, icons, Three add-ons/loaders, the BVH library, archive tools, fonts' parser, and vector geometry dependencies before opening a workspace.
- `noDiscovery` prevents a deferred workspace from silently creating a new optimizer generation; `force` prepares the configured inventory at server startup.
- Development sources and optimized modules receive `Cache-Control: no-store`.
- Three's teapot add-on uses the same canonical `three/addons/` spelling in every workspace.

When adding a bare JavaScript dependency or Three add-on, update `studioDependencies` in `vite.config.js`. The regression inventory checks source imports against that list. Self-hosted font CSS does not need JavaScript prebundling.

## Regression checks

```sh
npm run test:runtime
npm run test:texture
npm run test:baking
npm run test:standalone
```

The runtime suite checks:

1. Complete dependency prebundling, deduplication, and cache configuration.
2. Repeated Material / Pattern / Texture / Stamp / Baking switches and document reloads in an existing browser context, without hook failures or duplicate-Three warnings.
3. A deferred real component's hook render/update, plus Three constructor identity after an additional import.
4. Deliberately replayed stale React query URLs for both `usePatternHistory` and `useTextureDocument`: each actual null-dispatcher failure must appear, then **Reload studio** must recover the failed workspace and reopen an existing saved Texture project. The saved project is seeded only once, not recreated on reload.
5. Workspace switches and reloads inside an iframe, matching the embedded-preview use case.

The deferred-import and two development-only cache-replay cases skip against a compiled server, which exposes no `/src/` optimizer modules. Run the suite against the development server to exercise those cases; the remaining checks also run against the compiled preview. A temporary development server on another port can be targeted with `STUDIO_TEST_ORIGIN=http://127.0.0.1:5174` while the main preview remains on 5173.

The standalone suite serves the rebuilt `site/index.html` without Vite or external runtime assets, and exercises Pattern, Texture, Stamp/Decals, the inline mesh-baking worker, and exports.
