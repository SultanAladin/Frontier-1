# Experimental editor UI to native Frontier: port plan

> **Status:** Proposed plan only. No UI components have been ported or visually verified in this step.
>
> **Reference:** [`eosclient0001-rgb/Frontier`, branch `arena/d929ef36-frontier`](https://github.com/eosclient0001-rgb/Frontier/tree/arena%2Fd929ef36-frontier), inspected at commit `2fd4c6247b8697d18337669276da9ce88f6d6e00`. Pin this revision for the first parity baseline; record any later update and rebaseline deliberately.

## Decisions and boundaries

| Decision | Plan |
| --- | --- |
| Reference to investigate | The linked revision contains the complete React application in `Experimental/ProjectZeroEditor/` (`Editor.jsx`, `Editor.css`, `index.html`, `Build.mjs`, and many panel modules). Its `Experimental/FrontierEditor/` contains only six graphics/helper files, not a runnable editor. Start with ProjectZeroEditor as the likely intended latest editor; confirm this identification before implementation if the visual reference differs. Do not mix it with this repository's different `Experimental/FrontierEditor/` prototype without an explicit mapping. |
| Product scope | Editor UI only; no production/game UI or runtime dependency on React, HTML, browser, or editor assets. Preserve production engine functionality and public APIs. |
| Fidelity | Match the actual running HTML, including geometry, typography, icons, colours, hover/focus/disabled/selected states, animation and interactions. Do not call a static screenshot or a visually similar substitute an exact port. |
| Architecture | Reuse and extend existing native components where they satisfy the reference; extract shared primitives for genuinely repeated patterns. Feature adapters connect UI to the existing C++ equivalents instead of duplicating engine logic. |
| Delivery | Plan and preview first; port only after approval and after the reference has actually been rendered and inspected. |

## 1. Establish the real HTML reference before native work

1. Use an isolated checkout of the pinned source. Inspect `Experimental/ProjectZeroEditor/Build.mjs`, `index.html`, dependencies, assets, browser checks, and required services. Install only dependencies needed for the editor; preserve licenses and asset provenance. Serve on `0.0.0.0`, permit the preview host, and use relative URLs for any browser-to-service calls.
2. Render the actual HTML in a browser, not source-code guesses. Record viewport size, device pixel ratio, browser, font availability, scene fixture, theme, source revision, clock and random seed where applicable. Document missing services, assets or unsupported browser features rather than silently filling gaps.
3. Create a coverage matrix and capture each reachable screen and interaction: project browser; editor chrome, viewport, outliner and inspector; construct menu; content/assets and drawers; tabs and workspace surfaces; material/shader panels; render settings; environment, lights, fog, cloud, wind, camera and post-effects; object, vehicle and component inspectors. Include all additional panels and workspaces discovered by walking `Editor.jsx`, `Workspaces.jsx`, `Inspectors.jsx` and their imports. This list is a starting inventory, **not** a claim of completeness.
4. Capture representative idle, hover, keyboard focus, active, selected, expanded, disabled, error and empty states; drag/drop, scroll, resize, text editing, menus, tooltips and popup edges. Record animations with reproducible time points and input sequences, including intermediate frames, easing, duration and interruption/reversal. Capture icons at native size and detail scale; extract fonts, SVG paths, colours, gradients, borders, shadows, spacing and responsive rules from source and rendered output.
5. Inspect screenshots and motion recordings alongside CSS/JS; save reproducible fixtures and traceable source references. Mark inaccessible states as blocked and resolve them before claiming completion. Compare at multiple supported window sizes and DPI scales.

## 2. Inventory and C++ equivalence audit

Build a versioned ledger with one row per HTML component, state, command and animation. For each row record source file and selector, reference capture, shared primitive, native target, C++ model/command, input and validation case. Classify as **verified existing equivalent**, **existing but incomplete**, **missing**, or **not yet investigated**; do not presume every C++ equivalent is already feature-complete.

| HTML source family at pinned revision | Native reuse candidates to audit | Mapping and verification |
| --- | --- | --- |
| `Editor.jsx`, `Notch.jsx`, `DrawerPanel.jsx`, `Workspaces.jsx`, `WorkspaceSurface.jsx` | `Engine/Components/Docking/WorkspaceDockContainer`, `WorkspaceTabStrip`, `Notch/`, `Motion/` | Dock geometry, workspace navigation, drawer gestures and transitions; reuse motion integrators only if timing and trajectories match. |
| `Inspectors.jsx`, `InspectorTabs.jsx`, individual `*Panel.jsx` and cards | `Panes/InspectorPanel`, `Controls/`, `Appearance/` | Map every card, property, validation and edit command to its native model; preserve section order and all expansion states. |
| `AssetPanel.jsx`, `ProjectBrowser.jsx`, editor tree in `Editor.jsx` | `ContentBrowser/`, `Engine/Editor/Explorer/`, `Panes/OutlinerPanel` | Map asset/project operations, tree selection, drag/drop, context menus and persistence to native services. |
| `Viewport3D.jsx`, `ViewportRenderHeader.jsx`, `GizmoPanel.jsx`, `RenderComponents.jsx` | `Panes/ViewportPanel`, camera and world-render services | Match editor chrome and input while rendering real native scene output. Compare viewport contents only under equivalent scene, camera, renderer and colour-management settings; identify actual renderer differences honestly. |
| `ActionIcon.jsx`, `PrimitiveIcons.jsx`, `LightGlyphs.js`, icon/image assets | `VectorIcons/VectorIconRepository`, `EngineContent/Icons/` | Audit source paths, SVG features, font glyphs, licensing, strokes, scale and semantic colours; preserve original assets or convert faithfully. |
| `LiveGraph.jsx`, `SunDial.jsx`, `MoonGlobe.jsx`, `OpticalGraphic.jsx`, diagnostic and atmosphere cards | `PixelSpace`, controls, diagnostic canvas and relevant native models | Extract reusable chart, dial, field and animated graphic building blocks; port the exact formulas and interaction semantics, not placeholder art. |
| `ShaderPanel.jsx`, `MaterialPanel.jsx`, `ComponentViewerPanel.jsx` and specialized workspace modules | Existing native panels/services where present | Inventory actual native support first; do not imply unrelated standalone applications must be built or enabled. Track any missing backend separately from UI fidelity. |

This table is an initial architectural hypothesis, not an assertion that these candidates already have parity. Establish exact file and function ownership from the actual native code before choosing changes.

## 3. Build reusable editor-only foundations

- Establish source-of-truth design tokens for colours, alpha, typography, spacing, borders, shadows, radii and timing, with explicit units and traceability to the HTML. Verify font metrics and rasterization rather than assuming a font name is sufficient.
- Audit `CapsuleButton`, `ScalarSlider`, `DropdownSelector`, `TextInputField`, `ExclusiveSelector`, `ControlTooltip`, `ConfirmationDialogue`, `ThemeRepository`, `TypographyRepository`, `PixelSpace`, motion and icon facilities. Extend existing APIs where appropriate; avoid duplicate control families and bespoke one-off styling. Keep existing users working.
- Separate reusable presentation, editor interaction/motion, and feature-specific model adapters. Components accept typed inputs and emit commands/callbacks; business rules, undo/redo and persisted scene values remain in the native owner. Keep lifetime and ownership explicit.
- Build composable building blocks for repeated header/section/card/row, numerical field, selector, tree, icon, drawer, graph and dial patterns only after comparing actual variants. Provide accessible labels, keyboard navigation and pointer capture matching the reference; test drag cancellation and focus recovery.
- Use the editor profile/build boundaries already defined by `FRONTIER_PROFILE`; put new editor-only code and assets behind explicit Editor-only target/source/resource inclusion. Verify Shipping dependency graph, linked symbols and package contents contain no new editor UI. Shared engine facilities stay available to production only where independently required; do not accidentally remove existing runtime features.
- Follow the repository's coding, formatting and naming governance, unit-tagged constants and ownership rules. Track any legacy violations separately instead of claiming a clean audit without evidence.

## 4. Port in independently reviewable vertical slices

| Order | Slice | Exit condition |
| --- | --- | --- |
| 0 | Baseline and inventory | Runnable pinned HTML, inspected screenshots/video, complete ledger and verified native mappings. |
| 1 | Shared foundations | Tokens, typography, icons, common controls and motion verified in native component fixtures against HTML states. |
| 2 | Editor frame | Dock layout, toolbar/notch, viewport frame, outliner, inspector and drawers with resize/animation and keyboard behavior matching the reference. |
| 3 | Scene editing | Repeated cards and property controls wired to native selection, commands, undo/redo and persistence; environment, camera, lights and objects mapped one by one. |
| 4 | Assets and advanced panels | Project/asset browser, construct and specialized inspectors/workspaces, including shader/material/vehicle areas, after corresponding native APIs are verified. Track unsupported features as explicit blockers, not fake implementations. |
| 5 | Full regression and delivery | All matrix rows verified, performance and production-isolation gates pass, proof and unresolved differences documented, then commit and push on the active branch. |

Keep slices modular and reviewable; prioritize shared controls without suppressing any reference feature. The user can approve or reorder slices after reviewing the inventory.

## 5. Fidelity and functional verification

- Capture matching HTML and real C++ output from identical fixtures at fixed size, DPI, state, time and camera. Generate side-by-side sheets, overlays and per-pixel difference images; inspect both manually and numerically. Compare individual controls as well as complete screens.
- Define per-region tolerances *before* testing: exact token/icon geometry and layout where deterministic, and documented bounded anti-aliasing/font raster differences where platforms differ. A tolerance is not permission to omit controls or animations. Document any residual difference and reason; do not promise bit-identical pixels across GPU/font platforms without measurement.
- Replay the same interaction script for both: click, hover, keyboard, drag, scroll, focus, disabled states, undo/redo, persistence and error handling. Compare value ranges, steps, units, default values, command effects and state transitions with the native model. Verify every visible control has a real native action or is explicitly marked blocked.
- Sample animation sequences at start, intermediate points and completion under a controlled clock; verify easing, durations, interruption, reversal and reduced-motion behavior where applicable. Check icon colour, stroke, gradients, clipping and all variants, not just silhouettes.
- Run targeted native validation and visual regressions for the modified editor and shared controls; check pre-existing consumers for collateral regressions. Restrict builds and runs to `Frontier`, `FrontierSketcher` and directly relevant editor validation targets; do not run unrelated workspace executables/tests.
- Verify Editor builds and relevant host smoke tests; compile Shipping and inspect target/resource membership after editor changes. Record actual commands, environment, outcomes, screenshots, diff thresholds and unresolved failures.

## 6. Performance and acceptance

Profile the HTML baseline and native editor under the same window size and scene fixture, then measure idle CPU/GPU, UI frame time percentiles, allocations, draw calls, atlas use and input latency. Agree numeric budgets from measurements rather than inventing them. Avoid per-frame allocations, unnecessary redraws and redundant tessellation; batch where possible, update graphs only when needed, and reuse cached assets with correct invalidation. Confirm optimizations preserve animation and interaction parity.

**Completion means:** every inventoried reachable UI state and animation has a native equivalent and reviewed visual/interaction evidence; all controls are connected to verified C++ behavior; shared components remain reusable; editor-only code and resources do not enter Shipping; targeted tests and performance budgets pass; and any platform-specific pixel differences are explicitly documented. Until then each slice remains in progress.

## Risks and open decisions

1. The linked branch's full app is named `ProjectZeroEditor`, while `FrontierEditor` contains only helper files. Confirm that ProjectZeroEditor is the intended reference after rendering it.
2. External assets, generated assets or bridge services may be required to render the full reference; inventory these before setting implementation dates.
3. Native C++ equivalents and underlying behavior must be audited per control; missing capabilities block functional parity and must not be masked with mock controls.
4. Deterministic renderer and font output can vary by platform. The acceptance record must distinguish exact UI-token fidelity from measured raster differences.
5. The requested work is broad. The ledger and reference render determine size and sequencing; no effort or completion date is asserted yet.
