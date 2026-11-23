# Painting-channel UI, instrument library and embedded Material workspace

> **Digital painting update:** the default popup is now the functional digital brush studio, with layer/mask Paint / Erase / Fill, channel properties and a shared-engine live preview. The 102-instrument catalogue described below is retained separately as configuration-only legacy source authoring. See [digital painting](digital-painting.md).

## Reference ports

The UI is adapted from the user-supplied prototypes:

- [Frontier ChannelPropertyPanel.html](https://github.com/SultanAladin/Frontier/blob/master/Documentation/Prototypes/ChannelPropertyPanel.html): chip-based target picker, folded source/value cards and grouped generators.
- [Frontier PaintToolMenu.html](https://github.com/SultanAladin/Frontier/blob/master/Documentation/Prototypes/PaintToolMenu.html): all **102 instruments in 10 families**, their original full/tip SVG artwork, defaults, swatches, conditional parameter schemas and family/property navigation.
- [Slate References/UI.html](https://github.com/SultanAladin/Slate/blob/master/References/UI.html): the bottom, upward-draggable asset drawer and its three-column structure.

The Slate file's bundled React/Framer runtime is **not** embedded. There are no iframes, local Windows file URLs, imported demonstration atlases or second copies of React/Three. The supplied prototype HTML is research material, not a deployed runtime dependency.

## Use

Open **Texture** in the studio navigation.

- In the right layer inspector, the **Channels** card uses chips, removal buttons, Clear all and the `+` target picker. All 14 targets are available. The rounded cards reuse the editor's value capsules, sliders and source pills. Open a target to choose **Value / Texture / Generator**. Texture covers future painting and actual imported raster sources; legacy `paint` source modes migrate to `texture`.
- Use the instrument button in the top bar, or **right-click the teapot viewport**, to open the painting-tool menu. Each instrument has a true circular icon well with centred artwork and a separate text label below, not a pill-shaped card. Choose a family, search its instruments, select one and edit its properties. Escape returns from properties to the library; another Escape closes it.
- Drag the **Content browser's top edge upward**, or click its title/expand button. Its desktop layout is exactly:

  **Asset outliner | asset grid/list | preview + inspector together**

- The browser is an **absolute overlay**, not a flex drawer that resizes the scene. A constant 38 px collapsed-launcher slot is reserved; expanding, collapsing or dragging the browser never changes the scene or dock bounds underneath it.
- Browse materials, brushes/instruments and generators from the left tree-style **Asset library**. Search, grid/list view, families and saved-material filtering are available. The full filtered collection is a **scrolling grid**, not paginated. Grid tiles stay 172 px high with 124 px contained previews regardless of the browser's height. Phone layouts expose asset-type and Assets / Preview + inspector controls without horizontal overflow.
- The right material pane uses the **same `Viewport`, full `MaterialInspectorPanel` and underlying material controls as the main Material studio**. Parameters, selected/saved materials, favorites, inspector tab, geometry, lighting, rotation, wireframe and camera commands share the main App state. Leaving Texture for Material continues with those edited values; it is not another editor instance with a separate library.
- Save as preset writes through the same shared preset service. The browser has its own accessible save dialog because the root Material modal is hidden while an authoring workspace is open. JSON and self-contained Three.js shader exports use the same functions in both layouts.
- Assign an asset to the selected unlocked layer, or drag a tile onto a layer row. This stores a lightweight source link. Assigning a generator stores its staged parameters as a channel source; assigning a brush preserves the active instrument's edited parameters. Assigning/dropping onto an unlocked folder creates an assigned child in one undo step.

### History and actual source-stroke previews

The right dock has **Inspector / History** angled tabs. Phones expose **Layers / Inspector / History** in the existing single-dock switch. History is the real document undo timeline, not a separate action log: it labels configuration, channel, folder, mask, source and stroke changes; continuous fields retain coalescing and discrete actions stay separate. Inspecting a state does not mutate the project; **Restore this state**, Undo and Redo travel the same validated snapshots. A new edit discards only redo states. The session keeps at most 80 undo steps; reopening retains the saved current document, not an invented persistent undo stack.

For a real stroke preview, select a layer → a channel's **Texture** source → **Paint texture**. The bounded 2D source canvas captures pointer gestures and pressure, paints round-tip source pixels or erases existing pixels, and commits one transaction at pointer-up. Escape/pointer-cancel, inherited locks, changed sources and zero opacity/flow/erase strength produce no stroke transaction. The actual resulting raster appears in the layer stack; an empty paint source shows an explicitly empty checkerboard tile, not a brush icon pretending to be paint.

History's **Last stroke** card and stroke-event thumbnails use the isolated PNG footprint of the actual gesture at that state. It is not the representative tool-settings illustration. No recorded stroke yields a truthful empty state. One bounded `lastStroke` record (known instrument settings, up to 1024 normalized points, layer/channel identity and bounded PNG preview) is saved with the project and survives draft recovery/reopen. The current PNG source and last gesture image are distinct; history restore/undo rolls them back together.

This is **basic round-tip 2D source drawing**, not a simulation of all 102 instruments, mesh hit-testing, chart/UV painting, procedural channel evaluation or teapot composition. The canvas and previews state that explicitly; the existing neutral teapot remains unchanged.

The drawer's separator supports Arrow Up/Down, Home (collapse), End (expand), and double-click. Its height is optional local layout preference data, separate from project content. Dialogs trap Tab focus and return it when closed.

## Document and history

The existing `alloy.texture.v1` schema and `alloy-texture-project-v1` local-draft key are retained. Legacy flat projects receive defaults without losing their order, identities, masks or source settings. The same schema now also supports a validated folder hierarchy and bounded raster source records.

- `painting` records the active instrument ID, ink colour and the finite, schema-bounded settings of that instrument. Reopening/reselecting the active instrument does not reset its edited configuration.
- A layer's `parentId` is either an existing folder ID or `null` (root). Records are canonicalized into top-to-bottom **preorder**; invalid parents, duplicate IDs, cycles and more than eight ancestor levels are rejected rather than dropping records. New folders appear in the layer stack and inspector, can be folded, renamed, nested, reordered, copied or ungrouped. Membership can be changed by the inspector's Folder selector or by dropping a layer on a folder.
- Folder visibility and locks are inherited without overwriting a child's own settings. Copy/delete/reorder/ungroup operate on complete subtrees and respect locked descendants. Up/down buttons operate on siblings. Folder deletion confirms removal of descendants; ungrouping preserves children and warns before discarding non-default group settings.
- Masks can be **Fill** (white/black) or **Colour**, on a layer or folder. Colour masks store selected sRGB colour, normalized RGB-distance tolerance, softness, inversion, enable and strength. Zero remains a valid value. The inspector previews matching imported source pixels; a folder uses a sample child source, explicitly not composited group/teapot coverage. Without a source, labeled colour-match examples are shown.
- Each layer records its enabled `channels`, normalized `channelSettings` (including disabled targets' retained values), and an optional `{ type, id, name }` `sourceAsset` link.
- Canonical targets are `baseColor`, `metalness`, `roughness`, `height`, `normal`, `opacity`, `emission`, `occlusion`, `anisotropy`, `anisotropyAngle`, `clearcoat`, `refractionIndex`, `sheen`, and `subsurface`. Enabled-target imports accept `baseColour`, `metallic` and `ambientOcclusion` aliases.
- Normal is a **Height-derived authoring target**: enabling Normal also enables Height; disabling Height disables Normal. Imported targets obey the same dependency. All targets may be disabled without deleting their source values.
- Generator configurations accept only the 10 defined generators and bounded parameters. Instrument configurations accept only schema-declared scalar/enum/switch fields. Unknown executable fields, artwork, external resources and arbitrary scripts are not stored. Tool opacity, flow and erase strength allow zero as an explicit no-op, an Alloy adaptation of the reference's positive-only ranges.
- Layer target/source changes, instrument selection/settings and asset assignments use the existing validated Texture history, local save and JSON import/export path. Discrete target operations (especially Clear all) are separate undo steps. Continuous sliders coalesce by their own field. Locked layers reject target/source/assignment edits.
- **Texture import** accepts PNG/JPEG/WebP raster files up to 4 MiB, 8192 px per edge and 16 million input pixels. Dimensions/magic are checked before decode; sources are resampled to a maximum 256 px edge and stored as bounded self-contained PNG data URLs with checked header/dimensions (at most 300,000 characters). SVG, executable content and external source URLs are rejected. Import/replace/remove are undoable; failed imports or decodes resolving after a lock/selection change do not replace the active source.
- Layer icons are recognizable type glyphs; imported channel textures, cached real Material thumbnails and assigned instrument artwork replace the glyph when an actual source preview is available. Paint-source tiles now show actual recorded raster content (or an empty texture tile); assigned instrument artwork is not substituted for a painted layer. There are no fabricated painted thumbnails.
- Capacity remains **64 layer/folder records and 1 MB per texture project**, including imported source pixels. A draft contains no copied SVG tool artwork or allocated paint atlases. Its optional last 2D source-stroke record is bounded separately and included in the same 1 MB budget. Oversized documents are rejected without replacing the project.

## Real previews and honest scope

**The embedded Material preview is real shader rendering and editing.** Its material exports are portable, including the existing procedural, pattern, shape/SVG and layered-sand features.

**The catalogue's family-specific controls are configuration, not a full instrument simulator.** The added round-tip 2D source canvas does deposit real source pixels, but does not claim that each instrument has its physical/spray/glitter simulation, that generators have evaluated into paint channels, that Height-derived normals have been baked, or that ordinary painted/source-channel layers composite onto the teapot. The subsequent [layer-evaluation extension](texture-layer-evaluation.md) orders native material snapshots and decals, not ordinary source paint. Value swatches and representative marks are labeled accordingly. Generator tiles are configuration artwork, not generated mesh masks. Material thumbnails are actual bounded, on-demand shader renders; unrendered tiles are explicitly labeled as pending.

Imported source thumbnails and colour-selection previews are real raster data, but are **not** teapot UV atlases or painted layer compositing. The selected material asset preview remains separate from the scene. Direct scene/authoring-UV material drops now create native, part-scoped assignments on the teapot, without changing the other regions; see [part materials and UVs](texture-parts-uv.md). Its canvas pauses idle updates while the browser/menu is active, but still redraws a dirty frame after resizing so genuine dock/window resizing does not blank the scene. Collapsed zero-sized views do not produce infinite camera aspects. An IntersectionObserver watches the scrolling grid (140 px prefetch margin); debounced requests are bounded to 32 visible/prefetch material thumbnails and canceled when no longer needed. They reuse the existing renderer rather than restarting a hidden all-material job.

Mesh-map baking, the audited teapot deliverables and true 3D volume SDF remain in the separate Baking workspace, without a node editor.

## Verification

- `tests/paint-authoring-model.spec.js`: all 102 artworks/schemas, channel dependencies, bounded/null-safe settings, aliases, disabled-value retention, independent duplicates and locks.
- `tests/paint-authoring-studio.spec.js`: target toggles and undo, menu families/properties/focus/persistence, drag resizing, shared Material controls/presets/portable exports, asset assignment/drop, locks and phone layout.
- `tests/texture-folders-model.spec.js`: validated hierarchy, depth/record/byte limits, inherited visibility/locks, subtree copy/delete/move/ungroup, colour matching, safe source metadata and legacy Texture modes.
- `tests/texture-folders-studio.spec.js`: invariant scene/dock/canvas bounds, fixed tile sizing and scrolling to the final asset at desktop/tablet widths; folder navigation/history/locks; real source pixels and layer/folder colour masks; folder drops; rejected and asynchronous imports.
- `tests/texture-history-model.spec.js`: timeline coalescing, cursor restoration/branching/limits, stroke provenance/bounds/round-trip and explicit zero drawing controls.
- `tests/texture-history-studio.spec.js`: circular icon geometry, centring and below-circle labels at desktop/tablet/phone sizes; true History states and restore; pointer-captured paint/erase and actual PNG previews; cancel/locks/no-ops; reload and phone tabs.
- Existing Texture, Material, granular, runtime, dock, baking, surface-preview/SDF and standalone suites cover compatibility with the earlier workflows.

### History, source strokes and circular instruments (9 October 2026)

- Production and standalone builds pass; the rebuilt single-file `site/index.html` is **3.43 MB**.
- **53 painting/Texture cases pass** (29 model and 24 UI cases, 4.9 minutes), including all preceding overlay/folder/channel workflows. Instrument portraits are numerically checked for equal width/height, centred artwork and fully-below labels at 1440, 940 and 390 px; the phone modal/close control stay inside the viewport.
- **10 standalone cases pass** (4.5 minutes), now including actual source gestures, last-stroke/layer previews and source undo from the offline file, with no external assets.
- **10 additional Vite dock/runtime cases pass** (1.9 minutes), including cached reload/switches, deferred modules and stale React-generation recovery without clearing projects.
- Actual source paint/erase, point/preview validation, timeline coalescing/restore/branching/80-step cap, cancellation, inherited locks, zero controls, source-pixel snapshots and saved last-stroke recovery are verified. History uses the same snapshots as the existing undo/redo controls; its inspection action does not mutate the project.
- These previews are explicitly **2D source content**. No instrument-specific physics, scene hit-testing, packed-chart mesh painting or teapot compositor is claimed.

### Overlay, hierarchy and source-mask follow-up (9 October 2026)

- Production and standalone builds pass. The rebuilt `site/index.html` is **3.40 MB**, with all runtime assets embedded.
- **42 painting/Texture tests pass** (23 model cases, 19 UI cases; final complete run 4.2 minutes).
- **10 offline standalone tests pass** (4.5 minutes), including the new overlay/folder/raster-import/colour-mask case. They verify that no external asset requests are needed; the existing Material, shape/curve/text/SVG, layered-sand, baking and true-volume-SDF workflows remain operational offline.
- At **1440 px and 940 px**, opening/resizing/collapsing the browser leaves scene, canvas, dock and layout bounds unchanged. All 127 materials and 102 instruments can be scrolled to their final tile; tiles stay 172 px with 124 px media rather than shrinking with the drawer.
- **16 additional compatibility cases pass against Vite** (3.4 minutes): Material/sand/Zinc editing and portable exports/bakes; all four studios at desktop/tablet/phone widths; cached reloads, deferred imports and replayed stale React-generation recovery without clearing projects.
- Actual PNG, JPEG and WebP imports decode, resample and round-trip as bounded PNG sources. Tests compare colour-mask preview pixels against known red/blue source pixels, including zero tolerance/softness/strength and inversion, for both layers and folders.
- Native layer-to-folder and asset-to-folder drags pass. During asset drag, the overlay fades and releases pointer hit-testing **after** the native drag image is captured, without moving or unmounting its source; this avoids Chromium's drag-start cancellation and reveals covered layer targets.
- Rejected/corrupt/oversized imports and decodes resolving after a parent lock or selection change preserve the existing source/project. Folder cycles, inherited locks and subtree limits are independently checked.

### Original port baseline (9 October 2026; before overlay/folder follow-up)

- Production and standalone builds pass; `site/index.html` is a self-contained **3.39 MB** artifact.
- All **27 painting/Texture tests** pass, including the final shared-preset modal implementation.
- The broad Material, granular, Texture, baking/SDF, dock and runtime run passed **82 cases** in the compiled preview. Its one source-module-dependent cloth test and three dev-only cache/recovery cases were separately run against Vite: **all four pass**. These are 86 distinct targeted cases checked in their appropriate environments, not a claim that production serves development source files.
- All **nine offline standalone tests** pass, including instrument/channel configuration and the embedded Material preview/export without external requests.
- After the final scoped canvas-layout correction, all six new UI cases and both affected offline Texture/paint cases pass again. The tests check the resized neutral teapot's actual framebuffer and canvas bounds, not only a ready label.

## Shared instrument UI / custom Material generators / 3D point gradients

- Instrument sliders now import `Slider` from `MaterialControls.jsx`; cards, segmented pills and switches share the editor vocabulary. `ColourPicker.jsx` supplies keyboard/pointer HSV controls, hue, hex and swatches instead of the old labeled native Tool-colour input.
- `materialGeneratorModel.js` accepts only bounded, known procedural Material fields (64 KB preset budget); scripts, external images and pattern/SVG payloads are not made into generator code. `renderMaterialGenerator.js` reuses `bakeMaterial` for one unlit channel, supports transfer levels/gamma/inversion and Scratch-cavities extraction, and stores a checked 256/224 px PNG. A configuration stamp invalidates old pixels after edits. The async UI cancels/guards mode, selection, locks, stamp and unmount; recipe controls are shared, not a second Material runtime.
- `pointGradient.js` validates up to 12 unique points in `object-sphere` coordinates, bounded radius/weight/colour and a fallback colour. Its CPU slice sampler and the mesh shader use Gaussian-distance, linear-RGB colour blending. Zero radius or weight is an explicit mute. No script/function/pixel atlas is serialized as a gradient.
- `pointGradientViewport.js` packs the selected fill and up to nine own/ancestor Fill/Gradient masks in one bounded float texture. Existing physical shading/lights remain; a selected base-colour gradient or masked value/gradient fill changes actual mesh diffuse colour. Strength/inversion/enable/ancestor opacity are evaluated. Other paint/material/generator layers and blend modes are not composited by this narrow preview.
- Viewport balls are camera-projected object-space gizmos. Surface ray hits place or drag their points; ordinary orbit remains available outside them. Draft dragging updates uniforms, then one validated history commit happens on pointer-up; cancel, source changes and inherited locks reject/revert a late drag. Arrow-key movement and deletion are accessible. Field-slice thumbnails are explicitly not mesh coverage/UV exports.

### Final follow-up verification

- Production build and standalone build pass; current standalone is **3.47 MB**.
- **66 painting/Texture tests pass** (37 model and 29 UI cases), including real native ball dragging/one-step undo, point colours/radius/weight, actual gradient-mask framebuffer changes, inversion/zero strength/folder inheritance, Material-source pixels/regeneration/late-source protection, shared HSV picker and all preceding browser/folder/stroke workflows.
- **11 standalone tests pass**, including the same real gradient points/mask/Material generator/shared colour picker with no external assets or network requests.
- Earlier 65-pass/one-failure run was a test selector typo (`Select Base material mask` versus the existing accessible `Select mask of Base material`). The corrected full run passes; it did not require weakening pixel or input assertions.
