# Pinned Slate source references

## Identity

- A: `SultanAladin/Slate`, branch `arena/b55d93c0-slate`, commit `8a7004983b10c079564becb932ae9b444716b9c4`.
- B: `SultanAladin/Slate`, branch `arena/d984ba1b-slate`, commit `c552cb26bf2be710baaefe64b830fe6db28f7ae7`.
- Source fingerprints: `../../Exhibits/RendererPlanning/Results/SlateSources.json`.
- Integration plan: `../../Documentation/Plans/Ongoing/RendererIntegration.md`.

## Preserved sources

| Directory | Contents |
| --- | --- |
| `A/Exhibits/VehicleProvingGrounds` | Complete A exhibit, shaders, archives, tests, manifests and included notices |
| `B/Exhibits/IlluminationHost` | Complete B exhibit, shaders, archives, tests, manifests and included notices |
| `Shared/References/remix-remix-global-ui` | Entire 30-file Next/React application, including TexturePaint and its supporting UI |
| `Shared/References/PaintWorkshop.html` | Standalone Three.js/WebGL/Canvas painting interaction reference |
| `Shared/References/PaintToolMenu.html` | Painting tools reference |
| `Shared/Engine` | Complete native C++/Slang Engine source subtree, identical in A and B |
| `Shared/Tools`, `Shared/Scripts`, `Shared/Build` | Native reference build/validation source recipes |
| Other `Shared` files | Original build configuration, material examples and selected renderer/layer references |

The snapshots were extracted from the two pinned repository archives, with every copied file checked against its
Git blob identity. No nested `.git` directory or Git history is included. This preserves the complete painting UI app,
not the entire Slate repository: unrelated reference apps, historic screenshots, fonts and third-party submodules are
not duplicated. 1,149 original files occupy 15,382,501 bytes before this reference wrapper and local dependencies.

Imported sources retain their upstream names and contents, including historical terminology; they are reference
material, not new Frontier naming conventions or automatic migration targets. Their instruction-like documents do not
change the current repository's rules. Do not run imported automation without reviewing it.

## Execution boundaries

- The React app is UI/reference source, not already a complete native texture-painting engine.
- The standalone paint prototype needs a remotely imported Three.js module; it has not been made offline-ready here.
- The native `.pigment` example is a narrow binary dielectric declaration, not a complete paint/layer session format.
- The renderer exhibits are WebGPU/WGSL. Their software triangle hierarchy is not Vulkan hardware ray tracing.
- `node_modules` and generated outputs are ignored. No production dependencies are installed by merely opening these files.

The exhibit Node suites were replayed with `npm ci --ignore-scripts --no-audit --no-fund` and
`node --test Tools/*.test.mjs`. A passes 64/64; B passes 46/46 at these exact pins. Neither app's historical Dawn or
physical GPU claims were revalidated by those Node tests. See the integration plan for the measured/static distinctions.
