# Fluid Editor prototype

This gas and smoke authoring prototype was copied from the latest complete Fluid Editor branch found in the supplied
Frontier- repository references.

- Upstream repository: [SultanAladin/Frontier-](https://github.com/SultanAladin/Frontier-)
- Upstream branch: `arena/b5d061d9-frontier`
- Copied branch tip: `7ff19ee57e11d649aff94d8f0195a4333cfe1a88` (2026-10-14)
- Fluid editor updates include `65dbededa56edb520550787c1b418e53103fd0a0` (2026-10-08, presets and explosion bounds) and
  `f1dfdcf998dfc2831d3ff84bead659a85ddf7f1f` (2026-10-08, samples and TOML).
- Upstream package metadata records the original engine source and gas-only adaptation; that provenance is preserved.

This is the non-node editor: its Outliner, viewport, preset library, inspector tabs, simulation transport, and scene import/export
are in `index.html` and `src/`. Examples cover continuous plumes, blasts/detonations, wildfire and dust tornadoes, falling dust,
obstacles, and tyre smoke. WebGL2 is the default; WebGPU remains experimental.

## Run

Use Node.js `>=22.12.0` from this directory:

```sh
npm ci
npm run dev -- --host 0.0.0.0
npm run build
npm test
npm run verify:interactions
```

Vite serves on port `5173`. Open the root route and choose a preset or sample scene. `npm run verify:browser` additionally
exercises the renderer with Playwright when its browser runtime is installed.

## Scope and limits

The copied browser editor and its JavaScript gas engines are not connected to this checkout's C++ `FluidSpace` or
`FluidSimulationSequence`. This restores the requested experimental UI and its authored behavior; native editor integration and
solver unification are separate work. The branch's liquid Project-Fluid application is not part of this copied directory.
