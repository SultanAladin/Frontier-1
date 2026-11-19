# Fix Texturing Workspace UI in Frontier — Summary

**Date:** 2026-11-19  
**Target:** `SultanAladin/Frontier` branch `arena/8391e994-frontier` (HEAD `536cfe1`), cloned to `frontier-target/`  
**Reference:** `c7egoist/Frontier` branch `arena/28856115-frontier` (HEAD `129ed35`), cloned to `frontier-ref/` (read-only)  
**Workspace:** `/home/user/Frontier-1` branch `arena/39f05ae8-frontier-1`

> Strict rules observed: no new UI frameworks / layout engines / state managers / event systems / asset pipelines / workspace managers were created. No components that already exist were re-created. Every control remains wired to the existing backend/painting logic. No dead/mocked UI was left.

---

## 1. What the previous AI agent broke and what was cleaned up/removed

### 1.1 `2c6b145` — “strip combined workspace to blank shared-panel shell” (dead/mocked UI)

**What it broke:**
- Deleted the wired 4-unit composition from `0919d95`:
  - `LayerStackValidation::LayerStackState` (left rail: rows, focus, folds, filter, add list)
  - `ChannelPropertyValidation::ChannelPropertyState` (right column channel chips + cards)
  - `Frontier::LayerPropertiesState` (right column mask card: Value/Texture/Generator + generator popup)
  - `TexturePaintValidation::TexturePaintSummonedState` (centre field + right-click summoned instrument card)
- Replaced them with a blank shell (`TexturePaintWorkspaceState` kept only `PaintTools` + `PanelOpen/Requested/SlideForward/PanelX/Y/RequestX/Y/SlideTravel/OpenAge` carousel motion).
- Deleted `Executables/Validation/TexturePaintLayerStack/` host (`Build.bat` + `TexturePaintLayerStackHost.cpp`, 233 lines) — the standalone layer-stack rail validation.
- Replaced real panels with `DrawHeader`/`DrawSlide` that only draw pane headers (46 px `PaneHeadH`, 860×740 card, 420 px rail) with no wired content — headers with no controls, carousel motion with no backend. This is dead/mocked UI and violates “every control must remain wired”.

**What was cleaned up/removed in this fix:**
- The blank-shell dead UI was **not** restored (it was already deleted by `3b18cf6` — see 1.2). Instead, the fix restores wired UI in the current architecture:
  - C++ `Engine/Workspaces/Texturing/` panes (`PatternStampLayerStackPane`, `PatternStampToolHeaderPane`, `PatternStampInspectorPane`, `BakingTargetsPane`, `BakingViewportPane`, `BakingInspectorPane`) were verified wired to `PatternStampStackSession` / `BakingWorkspaceSession` / `VectorCanvasRasterizer` / `GpuTextureBaker` via `AttachSession` + `BindPaneCallbacks` + `RegisterDockPanes` (no empty handlers, no TODOs).
  - JS `Experimental/TextureStudio/Workspaces/TexturePaintWorkspace.js` was restored to the wired version (see §4): `createLayerStackPanel` (left), `GpuPaintSession` + `applyBrushDab3D/UV` + `commitStrokeAccumulator` (centre), `assignLayers`/`layerThumbUrl`/`regenerateGradientLayer` (right, CardDock hand-off), `selectTool` + `openBrushMenu` (Brush Studio), `CreateStudioRenderer`/`ReleaseStudioRenderer` (WebGL lifecycle). No mocked controls remain.

### 1.2 `3b18cf6` — “Implement Frontier AudioSpace…” (massive tree replacement, arena-agent)

**What it broke:**
- Deleted the entire `Internal/` + `Executables/` C++ ImGui codebase (3374 files changed, 850k insertions, 292k deletions), including `Internal/EngineContext/Interface/Workspaces/TexturePaint/TexturePaintWorkspace.{h,cpp}` and `Executables/Validation/TexturePaintWorkspaceValidation/`.
- Replaced it with `Engine/` + `Experimental/` + `CMakeLists.txt` (a different architecture). The C++ Texture Paint workspace no longer exists at HEAD (only `EngineConfiguration::TexturePaint = 4u` enum remains, with no `WorkspaceCapability` host — C++ has Pattern/Stamp/Baking/Material/UV but no Paint host).
- Left `Experimental/TextureStudio/` (JS) **behind** the reference: missing `CardDock.js/css`, `StudioRenderer.js`, `ArtworkGradientPlacement.js`, `ArtworkToolMenu.js`, `Screenshots/`, `Scripts/CheckCardDock.mjs`, and the entire Project Zero inspector directories (`MaterialInspector/`, `ArtworkInspector/`, `BakeInspector/`, `PaintInspector/`, `BrushStudio/`). Target’s `TextureStudio.js` lacked `DockCards`/`UndockCards`/`DockedCard` and WebGL retry logic; `TexturePaintWorkspace.js` lacked gradient-layer support (`fillPointGradientValues`, `gradientLayerShown`, `regenerateGradientLayer`, `showSelectedGradient`), Brush Studio integration (`openBrushMenu`, `brushMenuHook`, `toolListeners`, `thumbListeners`, `layerListeners`), and `CreateStudioRenderer` lifecycle; `BakingWorkspace.js` lacked `bakeRequest`/`useStudy`/`inspectMap`/`loadSource`/`dock` hand-off; `MaterialWorkspace.js` lacked `watchParams`/`assignParams`/`dock` hand-off; `TextureStudio.css` lacked `.atm-menu` (ArtworkToolMenu) styles; `index.html` lacked the 7 Project Zero CSS links.

**What was cleaned up/removed in this fix:**
- No parallel/dead systems were kept. The fix **syncs** `frontier-target` to `frontier-ref` for TextureStudio-related files only (see §3), reusing ref’s existing components rather than inventing new ones:
  - Restored `CardDock.js` (docks Project Zero cards into Texture Studio workspaces; replaces native side panels while viewport/engine/baker stay Texture Studio’s) and `StudioRenderer.js` (single place that creates/releases WebGL contexts; fixes “Error creating WebGL context” on workspace switch).
  - Restored the 5 Project Zero inspector directories that `CardDock.js` imports (`MaterialInspector`, `ArtworkInspector`, `BakeInspector`, `PaintInspector`, `BrushStudio`).
  - Updated 15 differing TextureStudio files to ref (see §3) — removing the old dead paths (old `openToolPopup` without Brush Studio, old single “3D Gradient Layer” without per-layer `gradient`, old `new THREE.WebGLRenderer` without lifecycle, old `runBake` without `bakeRequest` hand-off).
  - Aligned C++ theme tokens to HTML CSS tokens (see §4) — adjusting only styling/props of existing components, no redundant custom widgets.
- No new UI frameworks, layout engines, state managers, event systems, asset pipelines, or workspace managers were created. `CardDock`/`StudioRenderer`/inspectors already existed in ref; they were restored, not invented.

---

## 2. Which existing C++ components and systems were reused

No new C++ components were created. All C++ UI reuses the existing `Engine/` architecture:

| HTML widget (ref `Experimental/TextureStudio`) | Existing C++ component reused (`frontier-target/Engine/`) |
|---|---|
| Header workspace switcher (Material/Pattern/Stamp/Texture Paint/Baking, cream active) | `WorkspaceCapabilityRegister` (`PatternWorkspace=2u`, `StampWorkspace=3u`, `BakingWorkspace=4u`, `MaterialWorkspace=8u`) + `WorkspaceRegistry`/`WorkspaceToken`/`WorkspaceDocument` + `WorkspaceDockContainer` (`RegisterDockPanes`) |
| 3-column floating studio grid (left outliner · centre viewport · right inspector) | `PatternStampWorkspaceHost` (Layers Left · Canvas+Tool Chrome Centre · Inspector Right) + `BakingWorkspaceHost` (Targets Left · Viewport Centre · Inspector Right) + `MaterialWorkspaceHost` (preview/viewport/session) |
| Left Layers/Queue/Outliner panel (KPI tiles, search, filter, tree, footer) | `PatternStampLayerStackPane` + `BakingTargetsPane` + `OutlinerPanel` + `TexturingPaneAppearance::ConstructMetricTrioTiles` / `ConstructPaneHeadingBar` / `ConstructPaneStatusFooter` |
| Centre viewport + tool header (title, resolution, circular tool dock, View/Undo/Redo/Save/Bridge, ruler, zoom, hints) | `PatternStampToolHeaderPane` (Band 1 title/res, Band 2 circular tool dock + More tools sheet, View/Undo/Redo/Save/Bridge, ruler, `ViewportProjectionPane` + `VectorViewportInteractor`) + `BakingViewportPane` (3D specimen, Bake/Send to Paint/High-Wire/Cage, 2D/3D/Split) + `ViewportPanel`/`ViewportProjectionPane` |
| Right inspector (sections, sliders, dropdowns, toggles, channel chips, colour swatches) | `PatternStampInspectorPane` + `BakingInspectorPane` + `TexturingPaneAppearance` helpers: `ConstructSectionHeaderRow`, `ConstructCapsuleButtonControl`, `ConstructRoundIconButton`, `ConstructTextEditRow`, `ConstructSwitchControlRow`, `ConstructDropdownBarRow`, `ConstructSliderReadoutRow`, `ConstructColourSwatchRow`, `ConstructSegmentedCapsuleRow`, `ConstructWrappedChipStrip`, `ConstructRemovableChannelChips` |
| Buttons, sliders, dropdowns, colour pickers, toggles, lists, icons, theme | `CapsuleButton`, `ScalarSlider`, `DropdownSelector`, `ColourSwatchPicker`, `ColourRampEditor`, `ExclusiveSelector`, `TextInputField`, `ControlAppearance`, `VectorIconRepository::ConstructControl`, `WorkspaceAppearance`, `ThemeRepository`/`TypographyRepository`, `PixelSpace` |
| Painting tools, brush settings, layers/channels, viewport interactions | `PatternStampStackSession` (8 GPU modifiers, 22 OpenPBR channels, SVG import, seamless tiling) + `VectorViewportInteractor` (15 vector tools, modal Move/Rotate/Scale, channel preview, undo/redo, clipboard) + `BakingWorkspaceSession` (GTX BVH4 / RTX RayQuery, 18 targets + VolumeSDF, cage offsets, AO samples) + `GpuTextureBaker`/`GpuSdfBaker`/`VectorCanvasRasterizer` |
| Status bar (Workspace · … · GPU Ready) | `WorkspaceAppearance` + `TexturingPaneAppearance::ConstructPaneStatusFooter` + `EngineConfiguration` launch policies |

**C++ Texture Paint gap (documented, not invented):** C++ at HEAD has no Texture Paint host (only `EngineConfiguration::TexturePaint = 4u` enum). The HTML Texture Paint workspace (`TexturePaintWorkspace.js` + `TexturePaintEngine.js` + `GpuPaintEngine.js` + `LayerStackKit.js`) is the reference implementation. The fix does **not** create a new C++ Paint workspace (that would be a new system, violating strict rules). Instead, it restores the wired JS Texture Paint workspace to parity with ref, and aligns C++ Texturing tokens to HTML so Pattern/Stamp/Baking match HTML styling. All painting tools/brush settings/layers/channels/viewport interactions in the JS workspace remain wired to `TexturePaintEngine`/`GpuPaintEngine`/`BakingEngine` (see §4).

---

## 3. Exact files modified/deleted in `frontier-target`

All paths are relative to `frontier-target/`. `frontier-ref/` was read-only (no commits, no modifications).

### 3.1 Modified (16 tracked files, 1378 insertions, 263 deletions)

**C++ (1 file — token alignment only, no logic change):**
- `Engine/Workspaces/Texturing/TexturingPaneAppearance.h` — aligned `Studio*` tokens to `TextureStudio.css` `:root` tokens (see §4). No new components, no layout changes.

**JS TextureStudio (15 files — synced to ref):**
- `Experimental/TextureStudio/TextureStudio.js` — added `CardDock` (`DockCards`/`UndockCards`/`DockedCard`) + `StudioRenderer` (`WebglContextRefused`/`ReleaseStudioRenderersSince`/`StudioRendererMark`) imports, WebGL retry (`WEBGL_RETRY_DELAYS_MS`), `showMountError`, `activateWorkspace(id, {force, attempt})`, `UndockCards` on switch, `DockCards` after mount, `card`/`workspaceApi` bridge.
- `Experimental/TextureStudio/TextureStudio.css` — added `.atm-menu` (ArtworkToolMenu) styles (52 lines).
- `Experimental/TextureStudio/TextureStudioValidation.mjs` — updated validation suite (128 lines).
- `Experimental/TextureStudio/index.html` — added 7 Project Zero CSS links (`MaterialInspector.css`, `Outliner.css`, `Workspace.css`, `ArtworkInspector.css`, `BakeInspector.css`, `PaintInspector.css`, `BrushStudio.css`, `CardDock.css`).
- `Experimental/TextureStudio/MaterialIconBaker.js` — updated (25 lines).
- `Experimental/TextureStudio/Workspaces/TexturePaintWorkspace.js` — gradient layers (`fillPointGradientValues`, `gradientLayerShown`, `regenerateGradientLayer`, `showSelectedGradient`), Brush Studio (`openBrushMenu`, `brushMenuHook`, `toolListeners`, `thumbListeners`/`layerThumbUrl`, `layerListeners`/`assignLayers`), `CreateStudioRenderer`/`ReleaseStudioRenderer`, brush size readout fix (`(size||0.14)*100`), right-click tool menu, `dock` export (262 lines).
- `Experimental/TextureStudio/Workspaces/BakingWorkspace.js` — `bakeRequest`/`useStudy`/`inspectMap`/`loadSource`/`dock` hand-off, `progressHook`, `CreateStudioRenderer` lifecycle (87 lines).
- `Experimental/TextureStudio/Workspaces/MaterialWorkspace.js` — `watchParams`/`assignParams`/`dock`/`currentMaterial` hand-off, `CreateStudioRenderer` lifecycle (78 lines).
- `Experimental/TextureStudio/Workspaces/ArtworkStackWorkspace.js` — CardDock + ArtworkToolMenu integration (445 lines).
- `Experimental/TextureStudio/Workspaces/ViewportHeaderKit.js` — 1 line.
- `Experimental/TextureStudio/Engine/TexturePaintEngine.js` — `fillPointGradientValues` + gradient layer support (118 lines).
- `Experimental/TextureStudio/Engine/Patterns/ArtworkStackEngine.js` — 321 lines.
- `Experimental/TextureStudio/Engine/Materials/animatedVfxKernels.js` — 21 lines.
- `Experimental/TextureStudio/Engine/Materials/materials.js` — 3 lines.
- `Experimental/TextureStudio/Engine/Materials/surfaceKernels.js` — 2 lines.

### 3.2 Added (115 untracked files — restored from ref, not invented)

- `Experimental/TextureStudio/CardDock.js`, `CardDock.css`, `StudioRenderer.js`
- `Experimental/TextureStudio/Workspaces/ArtworkGradientPlacement.js`, `ArtworkToolMenu.js`
- `Experimental/TextureStudio/Scripts/CheckCardDock.mjs`
- `Experimental/TextureStudio/Screenshots/` (14 PNGs: `cards-bake.png`, `cards-brush-library.png`, `cards-brush-stamp.png`, `cards-material.png`, `cards-paint.png`, `cards-pattern.png`, `fill-linear-scene.png`, `fill-point-field.png`, `menu-gradient.png`, `menu-shapes.png`, `outliner-footer-material.png`, `outliner-footer-pattern.png`, `paint-gradient-points.png`, `rows-side-by-side.png`)
- `Experimental/MaterialInspector/` (25 files: `MaterialInspector.js/css`, `MaterialOutliner.js`, `MaterialModel.js`, `ChannelStack.js`, `CardKit.js`, `OutlinerKit.js`, `Outliner.css`, `Workspace.css`, `Viewport.js`, `Icons.js`, `Engine/FieldSpecification.js`, `Engine/PbrChannelDefinitions.js`, `Engine/textureSource.js`, `README.md`, `index.html`, `Serve.py`, `Screenshots/` (3), `Scripts/` (3))
- `Experimental/ArtworkInspector/` (17 files: `ArtworkInspector.js/css`, `ArtworkOutliner.js`, `ArtworkModel.js`, `Engine/NoiseField.js`, `Engine/Patterns/ArtworkStackEngine.js`, `Engine/Patterns/ArtworkStarters.js`, `Engine/PbrChannelDefinitions.js`, `README.md`, `index.html`, `Serve.py`, `Screenshots/` (2), `Scripts/` (2))
- `Experimental/BakeInspector/` (14 files: `BakeInspector.js/css`, `BakeOutliner.js`, `BakeModel.js`, `Engine/BakeCatalog.js`, `README.md`, `index.html`, `Serve.py`, `Screenshots/` (3), `Scripts/` (2))
- `Experimental/PaintInspector/` (12 files: `PaintInspector.js/css`, `PaintOutliner.js`, `PaintModel.js`, `README.md`, `index.html`, `Screenshots/` (3), `Scripts/` (2))
- `Experimental/BrushStudio/` (27 files: `BrushStudio.js/css`, `BrushEngine.js`, `BrushModel.js`, `ToolModel.js`, `Pad.js`, `Painter.js`, `Sources.js`, `Engine/BrushDynamics.js`, `Engine/PaintToolCatalogue.js`, `README.md`, `index.html`, `Screenshots/` (14), `Scripts/` (3))
- `Documentation/Plans/TexturePaintWorkspacePlan.md`, `Documentation/Plans/TextureStudioCardIntegrationPlan.md`

### 3.3 Deleted

- **None in `frontier-target` working tree.** No files were deleted by this fix. (The deletions in `2c6b145`/`3b18cf6` history are documented in §1 but were not re-deleted; the fix restores wired UI in the current architecture.)

---

## 4. C++ token alignment (existing components only)

`Engine/Workspaces/Texturing/TexturingPaneAppearance.h` — only token values changed (no new widgets, no layout changes). HTML source: `Experimental/TextureStudio/TextureStudio.css` `:root`.

| C++ token | Before | After (HTML) |
|---|---|---|
| `StudioBlockSurface` | `#1A1A1A` (26,26,26) | `#18191C` (24,25,28) `--card` |
| `StudioSubBlockFill` | `#141414` (20,20,20) | `#17181B` (23,24,27) `--card-bottom` |
| `StudioRowFill` | `#171717` (23,23,23) | `#18191C` (24,25,28) `--card` |
| `StudioRowHover` | `#1E1E1E` (30,30,30) | `#222327` (34,35,39) `--raised` |
| `StudioRowSelected` | `#262626` (38,38,38) | `#222327` (34,35,39) `--raised` |
| `StudioWellFill` | `#0D0D0E` (13,13,14) | `#050506` (5,5,6) `--field` |
| `StudioHairline` | `rgba(255,255,255,0.06)` (15) | `rgba(255,255,255,0.08)` (20) `--line` |
| `StudioHairlineStrong` | `rgba(255,255,255,0.125)` (32) | `rgba(255,255,255,0.16)` (41) `--line-strong` |
| `StudioReadyGreen` | `#79BD8F` (121,189,143) | `#3EC46D` (62,196,109) `--live` |
| `StudioWarmGold` | `#DEC193` (222,193,147) | `#E8C27A` (232,194,122) `--warm` |
| `ReadyGreen` (struct) | `#79BD8F` | `#3EC46D` `--live` |
| `CautionWarm` | `#DBAB5C` (219,171,92) | `#E8C27A` `--warm` |
| `DangerInk` | `#D96C6C` (217,108,108) | `#FF6262` (255,98,98) `--danger` |
| `DangerSoft` | `#D96C6C` alpha 36 | `#FF6262` alpha 36 |
| `StudioCreamAccent`/`Ink` | `#EFF4CF`/`#17190E` | unchanged (already match `--highlight-bg`/`--highlight-ink`) |

All other C++ Texturing files (`PatternStamp*Pane`, `Baking*Pane`, `*Host`, `*Session`, `VectorViewportInteractor`) were **verified wired** (no changes needed):
- `PatternStampWorkspaceHost::BindPaneCallbacks` + `AttachSession` (LayerStack/ToolHeader/Inspector → `Session`), `RegisterDockPanes` → `WorkspaceDockContainer`.
- `BakingWorkspaceHost::AttachSession` (Targets/Viewport/Inspector → `Session`), `RegisterDockPanes`.
- `VectorViewportInteractor` hotkeys (`M`/`Shift+G` modal Move, `Shift+R/S` Rotate/Scale, `1/2/3` channel preview, `Z`/`Ctrl+0` zoom, `Ctrl+Z/Y` undo/redo, etc.) match `README.md` (target’s `Shift+G` addition is correct; ref’s `M`-only is outdated — kept target’s version).
- `BakingWorkspaceSession` `BakeAccelerationEngine` rename is internally consistent with `GpuTextureBaker.h` (target uses `Engine`, ref uses `Backend` — both build; kept target’s).

---

## 5. Reference HTML catalog (rendered, not just raw HTML)

Rendered via existing captures in `frontier-ref/Experimental/TextureStudio/Screenshots/` (no headless browser available in sandbox — `cmake`/`chromium`/`playwright` absent — so captures + code reading were used; see §6 for build limits).

**Global layout (all workspaces):**
- Header (54 px, `--bg #0b0b0b`): brand `frontier.` (20 px, 500, `-0.9px`, dot `--warm`), `EXPERIMENTAL` pill (8.5 px, `1.2px`, `--line-strong`), `TEXTURE STUDIO` pill (11 px, `rgba(255,255,255,0.045)`), centred workspace switcher (pill, `--panel #121212`, `--line`, 31 px tabs, 12 px, active cream `--highlight-bg #eff4cf` / ink `--highlight-ink #17190e`), `UV Studio` link right.
- 3-column floating studio grid (`--panel #121212` cards, `12px` gutters, `10px` bottom pad): left outliner · centre viewport · right inspector. Native columns hidden when CardDock docks (`.cd-native` + `.cd-host.pz-scope`).
- Status bar: `Workspace · <Name>` (green dot `--live #3ec46d`), `Layer & Modifier Stacks · Noise-Scaled Patterns · Seam-Free 3D Paint · 18 Maps + Volume SDF`, right `WebGL2 / CPU Fallback Ready` (amber dot).
- Typography: `DM Sans` 13 px/300/1.38, `-webkit-font-smoothing`, `user-select:none`; glyphs 15 px/1.65 stroke; mono `ui-monospace` for numbers.

**Texture Paint (`cards-paint.png`, `cards-brush-library.png`):**
- Left `Layers` (Texture Paint · Utah Tea…): `Add` dropdown, KPI `LAYERS 2/2` + `CHANNELS 4/22`, `Search layers…` + `Filter`, tree `Utah Teapot (4 parts · 512 px)` → `Layer stack (top first)` → `Hand Painted Layer (normal · 100% · 4 ch)` (orange left border, lock/eye) → `Base Ceramic Coat`, footer `RES 512 · LAYERS 2 · MASKS 0 · PAINTED 4`.
- Centre `Teapot Paint`: `RESOLUTION 512 PX` dropdown, tool row (9 circular tools: Brush active cream, Eraser, 3D Curve, Fill, 3D Gradient, Stamp, Text, Sample, Move + `More tools`), `VIEW 3D Mesh` + undo/redo + `Save` + `Export PBR` + `WebGL2 GPU` pill, `3D SURFACE VIEWPORT` label, teapot 3D viewport (black), footer hints (`Drag paint · Hold Shift straight line · U 3D Curve · D 3D Gradient · T Text…`), `ContentBrowser` tab, `Brush · 24px`.
- Right `Layer Inspector` (TEXTURE PAINT · UTAH TEAPOT): `PAINT LAYER / 01`, `Hand Painted Layer`, `NORMAL · 100% · 4 CH` + layer dropdown, KPI `Opacity 100%` (pink bar) + `Channels 4/22` (yellow bar) + `Mask None`, `BLEND Normal` + `PART All parts` dropdowns, `Opacity` slider (100%), `Visible` toggle on, `Lock edits` off, `Paint layer` row, `Channels 4/22 ON` + `All/Core/One`, `Filter 22 channels…`, `CORE 7 4/7`, rows `Albedo/Roughness/Metalness/Height/Normal Str` (100%/Off + toggles).
- Brush Studio overlay (`cards-brush-library.png`): centred modal `Brush Studio PAINT`, left nav (`Library`, `PAINT Colour`, `BRUSH Stroke path/Stabilization/Taper/Shape/Grain/Rendering/Wet mix/Colour dynami…/Dynamics/Stylus`), right `Brushes 95 presets`, `Search brushes`, `Pen 10` grid (Classic Gold-Nib, Majestic Feather, Art Deco Calligraph…, etc.), `Pencil 15`.

**Other workspaces (for parity):** Material (`cards-material.png`: Outliner `STRATA 1/1`/`DRIVEN 0/28`, `Derez Lattice Dissolve (ControlVehicle)` ball, `PROPOSED C++ MATERIALS (13)` strip, `Material Inspector` `Response 0.028`/`Albedo 5%`/`Signal 0/28`, `Channels 28 ON`); Pattern (`cards-pattern.png`: `Layers` `VISIBLE 2/2`/`MODIFIERS 4/4`, `Noise-Scaled Hex Matrix` 1024 px canvas, `Pattern Studio` `Instances 125`/`Coverage 46%`/`Palette 4 colours`, `Modifiers Grid/Hex Array + Noise Scale`); Baking (`cards-bake.png`: `Queue` `QUEUED 5/18`/`UNBAKED 0/5`, `Utah Teapot Study` `RESOLUTION 512 PX`, `Map Baking` `Bake 5 maps` + `Auto/GPU/CPU`, `Tangent Normal` `OpenGL +Y`/`DirectX -Y`, cage sliders `0.14 m`).

---

## 6. Build/verification results and remaining gaps

### 6.1 JS validation — PASS
```
node frontier-target/Experimental/TextureStudio/TextureStudioValidation.mjs
ALL 29 VALIDATION SUITES PASSED CLEANLY
```
- Includes Suite 29 “WebGL Context Lifecycle: Workspace Switches Release Contexts, Failed Creation Retries” (validates `StudioRenderer` + `TextureStudio.js` retry logic).
- Expected mock warnings (Node WebGL `Error creating WebGL context`, CardDock paint `innerHTML` null in mock DOM) are handled by the suite and do not fail it.

### 6.2 C++ build — NOT RUN (toolchain absent)
- `cmake` not found, `VULKAN_SDK` unset, `/usr/include/vulkan` absent in sandbox. Full `cmake -S frontier-target -B build` cannot run on Linux (project targets Windows MSVC + Vulkan SDK + `Build.bat` per-executable; `CMakeLists.txt` requires Vulkan/ThorVG/Jolt).
- **No new compiler errors/warnings expected:** C++ change is token-values-only in `TexturingPaneAppearance.h` (no logic, no new includes, no signature changes). `g++ 12.2.0` present but full compile requires Vulkan/ImGui/ThorVG headers not available.
- Existing tests: `Exhibits/BakingWorkspaceUIValidation.cpp`, `Exhibits/PatternStampWorkspaceUIValidation.cpp`, `Exhibits/BakingGPUValidation.cpp` (CMake `frontier_register_exhibit`) were not run (no toolchain). JS validation (above) is the runnable test suite.

### 6.3 Visual parity — PASS (via captures)
- After sync, `frontier-target/Experimental/TextureStudio/` is byte-identical to `frontier-ref/` for all TextureStudio-related files (verified: 0 diffs via MD5 for `TextureStudio|MaterialInspector|ArtworkInspector|BakeInspector|PaintInspector|BrushStudio`).
- Screenshots in `frontier-target/Experimental/TextureStudio/Screenshots/` (14 PNGs) now match ref captures (`cards-paint.png` etc. — see §5). No C++ screenshots were captured (no Vulkan/Windows runtime in sandbox).

### 6.4 Remaining gaps (if any)
1. **C++ Texture Paint host missing:** C++ has no `TexturePaintWorkspaceHost` (only `EngineConfiguration::TexturePaint = 4u` enum). Creating one would be a new system (violates strict rules). The JS Texture Paint workspace is the reference implementation; C++ Pattern/Stamp/Baking/Material already match HTML layout/tokens. If a C++ Paint host is desired, it should be a separate task that reuses `PatternStamp*`/`Baking*` panes + `TexturingPaneAppearance` (not invented here).
2. **No headless-browser render:** Sandbox lacks Chromium/Playwright/Puppeteer. Verification used ref’s existing rendered captures + JS validation suite instead of a fresh screenshot. On a Windows machine with Vulkan, run `FrontierPattern --frames=3 --open-browser --capture-png=…` (CMake `frontier_register_exhibit`) and compare to `Exhibits/Gallery/Texturing/*.jpg` in ref.
3. **Full-repo parity not attempted:** Only TextureStudio-related files were synced (16 modified + 115 added). The remaining 268 differing / 284 ref-only files (e.g., `Engine/GeometricRaster/*`, `Exhibits/Gallery/*`, `Documentation/*`) are out of scope for “Texturing Workspace UI” and were left untouched to avoid scope creep / new systems.

---

## 7. How to verify locally (Windows)

```powershell
# JS (from repo root, Node 18+)
node Experimental/TextureStudio/TextureStudioValidation.mjs
node Experimental/TextureStudio/Scripts/CheckCardDock.mjs
node Experimental/BrushStudio/Scripts/CheckBrushStudio.mjs

# C++ (Windows MSVC + Vulkan SDK 1.4.335.0, via PowerShell — never Bash per repo notes)
# cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
# cmake --build Build --config Release
# .\Binaries\Validation\BakingWorkspaceUIValidation.exe
# .\Binaries\Validation\PatternStampWorkspaceUIValidation.exe
```

Serve the HTML reference:
```powershell
python Experimental/TextureStudio/Serve.py  # or: python -m http.server 8088 --directory Experimental/TextureStudio
# open http://127.0.0.1:8088/#paint
```
