# Texture studio — layer setup and engine-specific painting plan

> **Implementation checkpoint:** bounded digital layer/mask Paint / Erase / Fill, per-channel shader consumers and lower-only raw-field inheritance are now implemented. See [current workflow and limits](digital-painting.md). This planning document remains historical guidance for later imported-source composition, interchange and PBR atlas export.

> **Layer-stack extension (10 October 2026):** existing native material snapshots and decals now share bottom-to-top Normal alpha-over surface evaluation, an automatic locked white Layer 0 and genuine compatible AO / normal / bevel support. The packed UV preview renders the same lit appearance. See [layer evaluation](texture-layer-evaluation.md). This does not implement ordinary source brush deposition, general source-layer/channel blending or composited PBR texture export.
>
> **Previous extension (10 October 2026):** part-scoped native materials, geometry-backed UV / UDIM decals, and actual mask painting / region selection are implemented. See [part materials and UV authoring](texture-parts-uv.md) and [mask painting and authoring modes](texture-mask-painting.md). The roadmap below still describes the separate, not-yet-implemented full source-stroke UV deposition/compositing engine; it must not be read as saying current materials/decals are only metadata.

## Previous source-only phase (historical): bounded 2D sources, history, layer documents and shared asset editing

Open **Texture studio** from the material workspace, or `/?studio=texture`.

This pass provides:

- A real, orbitable Three.js **teapot**, rendered through the existing `Viewport` and material engine. Neutral ceramic, shaded/wireframe modes, ground grid, zoom and framing are preview controls, not authoring operations.
- **Layer stack on the left and inspector on the right** of the viewport, following the updated layout request. Tablets retain this order with narrower docks; phones use Layers/Inspector/History tabs below the viewport. Desktop dock widths are resizable by pointer or arrow keys.
- Stable layer identities, top-to-bottom preorder, Paint/Fill/Material/Generator source classifications and **nested folders**. Selection/search, inherited visibility/locks, sibling moves, subtree drag/copy/delete, parent selection and ungrouping preserve the hierarchy.
- Editable layer name, blend mode, opacity and channel-target metadata. Layer **and folder** masks have separate selection, enable, Fill/Colour type, black/white fill or colour/tolerance/softness, invert and strength settings. Colour masks have a real imported-source pixel preview, labeled source-only.
- Transactional undo/redo, browser-local recovery, validated project-file open/save and a **64 layer/folder record** limit. Files are versioned `alloy.texture.v1` JSON, bounded to 1 MB. Layer identities are never inferred from their names or list positions.
- All **102 painting instruments / 10 families**, with prototype SVG artwork and bounded, persistent tool properties. All **14 channel targets** have rounded theme-matched cards/sliders/pills, with **Value / Texture / Generator** sources; disabled values are retained and Normal depends on Height. Texture can store an imported PNG/JPEG/WebP raster, resampled to a bounded self-contained 256 px PNG source.
- An upward-draggable **overlay Content browser** (the scene never shrinks): Outliner | asset grid/list | preview + inspector together. It reuses the full Material renderer/inspector, parameters, saved library, favorites, preview controls and portable exports. The tree-style Asset library and full scrolling grid use fixed-size contained previews, independent of opening height. Asset assignment/drop stores a lightweight link, not a rendered layer; dropping on a folder inserts an assigned child.
- **Inspector / History** tabs share the existing document snapshots, with named edit/stroke states, inspection without mutation, restore, undo/redo and an 80-step session cap. **Paint texture** opens a bounded round-tip source canvas: real pressure-aware 2D paint/erase strokes become PNG sources and layer thumbnails. History shows the actual last gesture footprint, not a representative tool mark. Zero controls, cancel, locks and changed sources cannot commit a late stroke. The saved last gesture has at most 1024 normalized points and a bounded PNG; it is not a mesh/UV stroke record.
- Existing material and pattern workspaces remain available in the same application. The root all-material thumbnail job is suspended during authoring; the open browser renders a bounded visible-material subset on demand.

**Not implemented at that previous checkpoint (see the digital update above):** ordinary material/channel mesh/UV brush deposition, instrument-specific simulation, fill/generator evaluation, general texture-source evaluation on layers, weighted per-channel PBR compositing or composited texture-layer export. Mesh baking and true 3D SDF have their own Baking workspace. The painting-tool and asset-assignment UI stores configuration; raster import and basic round-tip paint/erase store actual bounded 2D source pixels; colour masks evaluate only their labeled source preview. General source/stroke layers are not composited onto the teapot. A selected base-colour point-gradient fill or value/gradient fill with Fill/Gradient masks now has an actual object-space surface preview; arbitrary layer/UV composition is still not implemented. The UI states this explicitly. The layer document stores no painted PBR atlases. Temporary, geometry-backed mask textures are now evaluated by the renderer; see the mask-painting extension for its actual coverage and bounds.

### Shortcuts and safeguards

- `Ctrl/⌘ Z`: undo; `Ctrl/⌘ Shift Z` or `Ctrl Y`: redo.
- `Ctrl/⌘ D`: duplicate selected unlocked layer; `Alt ↑/↓`: reorder.
- `Ctrl/⌘ S`: save a **layer-project JSON file**, including bounded imported sources, not composited textures; the optional bounded genuine mesh-support maps now persist in this JSON. Within the browser's Material pane, this opens the shared material-preset save dialog instead.
- `F` / `R`: frame/reset the teapot. Native typing shortcuts remain with text/number fields.
- Arrow keys navigate layer selection; left/right fold a row. The add menu supports arrow keys and Escape.
- Locks on ancestor folders also protect their contents. Locked layers cannot be renamed, reordered, duplicated, deleted, or have their content settings changed. Their visibility and lock remain editable.
- Mask duplication copies metadata independently. Delete/remove and project import can be undone. Invalid imports preserve the current project.
- Drafts use `alloy-texture-project-v1`, separate from material presets and pattern drafts. Local storage is device-local, not cloud backup; project-file saving remains available if storage fails.

## Reference research: latest editor, not the older design

Reference branch reviewed at **`b9244db0ea19f83c4cea4f58b5ef94719c00b524`**:

- [Latest experimental editor — ProjectZeroEditor](https://github.com/SultanAladin/Frontier-/tree/b9244db0ea19f83c4cea4f58b5ef94719c00b524/Experimental/ProjectZeroEditor): `Editor.jsx`, `Editor.css`, `WorkspaceCards.css` and the current outliner metadata/dock/card vocabulary. The editor files were added/updated in commit `e5a33a89ea96400b89d77c6e1204fdb0ce526b04`, **7 October 2026**. The older `Experimental/FrontierEditor/src.jsx` has an earlier history and was not used as the layout baseline.
- The latest native folder/outliner evidence was also inspected. This implementation adapts the new angled document tabs, charcoal docks, pill controls, status tiles, selected-row marker, light DM Sans typography and rounded inspector cards. It does not claim to port the native engine or all of its editor panels.
- The updated request moves the inspector **back to the right**, with the outliner/layer stack on the left. The newer editor's dock/card visual language is retained.

Older repository reviewed at **`4a77bfd17f23536e32781718553ec6a171e39021`**:

- [Production PBR channel plan](https://github.com/SultanAladin/Frontier/blob/4a77bfd17f23536e32781718553ec6a171e39021/Documentation/PLAN-ProductionPbrChannels.md).
- [Completed layer-stack validation](https://github.com/SultanAladin/Frontier/blob/4a77bfd17f23536e32781718553ec6a171e39021/Documentation/COMPLETED-LayerStackValidation.md).
- [Texture layer-stack prototype](https://github.com/SultanAladin/Frontier/blob/4a77bfd17f23536e32781718553ec6a171e39021/Documentation/Prototypes/TexturePaintLayerStack.html), `TexturePaintInspector.html`, and [PaintingSurface modules](https://github.com/SultanAladin/Frontier/tree/4a77bfd17f23536e32781718553ec6a171e39021/Documentation/Prototypes/PaintingSurface).

A related PBR plan and working painting/layer prototypes were found, **not one standalone file titled “texture painting plan.”** Their useful lessons are retained: distinguish layer kinds from capabilities, keep masks outside the channel set, preserve identity across reorder, allocate image storage lazily, and require a real shader consumer before presenting a channel as rendered. The old Vulkan vertex-stream migration, five packed atlases, WebGPU painting prototype, native class names and fixed twelve-GPU-layer budget are **not copied into this engine**.

## What the current engine actually has

| Existing part                                                                                                 | Reuse and constraint                                                                                                                                                            |
| ------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| React/Vite workspaces in `main.jsx`                                                                           | One application, material/pattern/texture navigation; authoring keyboard and focus isolation. No second editor server.                                                          |
| Three.js `WebGLRenderer`, `OrbitControls`, physical lights and generated studio environment in `Viewport.jsx` | Reuse the renderer, camera, compile ownership and disposal. Initial painting implementation should target WebGL2, not silently assume WebGPU/compute.                           |
| `createMaterial` / `normalizeMaterial` in `materials.js` and recipes in `materialProfiles.js`                 | Preserve physical material rules, procedural shading, finishes and current exports. A texture must reach an actual material input, not just an inspector record.                |
| `patternRuntime.js` / `attachSurfaceSources`                                                                  | An existing example of authored sources reaching raster/material channels. Reuse its source/evaluation separation; do not flatten editable pattern documents into brush pixels. |
| `alloy.pattern.v1`, vector text outlines and sanitized SVG sources                                            | Pattern/vector layers can eventually reference these documents as editable sources. SVG remains inert markup, never executable JavaScript.                                      |
| `bakeMaterial.js` / `BakePanel`                                                                               | Existing material-map export stays separate and unchanged. Texture-layer baking is a later integration, not this pass.                                                          |
| `textureDocument.js` / `useTextureDocument.js`                                                                | New CPU-only project schema, stable ordering, bounded revisions and recovery. Future image caches must remain derived resources, not be inserted into every history snapshot.   |

### Important teapot blocker before painting

Three's stock `TeapotGeometry` supplies positions, normals and UVs, so the old engine's **missing vertex attributes** diagnosis does not apply. However, its UV assignment resets `u = 1 − t`, `v = 1 − s` for each Bézier patch. Many patches occupy the same 0–1 square.

**The current teapot is a preview mesh, not a validated paint atlas.** Painting directly into these stock UVs would repeat one mark across unrelated patches. A non-overlapping chart layout, seam metadata and padding are a prerequisite for the first paint implementation; do not mark UV readiness from the mere presence of a `uv` attribute.

## Revised implementation sequence — future work, not part of this delivery

### P0 — UI and document foundation (this pass)

Keep the viewport genuinely rendered while authoring metadata remains honest. Gate: operations round-trip, identities survive reorder/import/history, layer stack stays left and inspector right on desktop/tablet, phones remain usable, and material/pattern workspaces still work. The subsequent prototype-UI pass adds brush/channel/source configuration and the shared Material asset browser without claiming paint deposition or evaluated layers. See [the port's provenance and model](paint-tools-and-content-browser.md). Mesh baking remains separate, without a node editor.

### P1 — Surface and chart contract

1. Give the teapot a non-overlapping chart layout, preserving geometry and normals. Record chart IDs, boundaries, adjacency, orientation and texel density; avoid a teapot-only paint engine.
2. Audit overlaps, zero-area UV triangles, winding, coverage and island padding. Keep lid/body/handle/spout seams explicit.
3. Define the normal frame: tangents with handedness or a tested derivative frame compatible with Three.js normal-map sampling.
4. Add geometry/UV diagnostics **only then**. Model import is separately scoped; it must validate the same contract rather than bypass it.

Gate: a checker/unique-island test covers the complete teapot without unrelated repeated marks. Front-facing surface picks identify the correct chart and UV. Camera projection and world-to-surface conversions must agree at non-square viewport sizes.

### P2 — One actual channel consumer, then deterministic composition

1. Implement a **fill-layer, base-color-only vertical slice** first: metadata → lazy render target → compositor → physical material → visible teapot. Still no brush.
2. Use the document's topmost-first order consistently. Evaluate bottom to top. Lower/base material remains unchanged when no layer contributes.
3. Keep opacity, source coverage and the optional mask separate:
   `coverage = visibility × sourceAlpha × layerOpacity × maskCoverage`.
   A disabled/absent mask contributes 1; an unwritten paint layer contributes 0. Enabling a white mask must not erase a layer.
4. Resolve all six advertised blend modes before treating their controls as rendered. Blend colors in linear space. Do not apply a layer to a disabled channel.
5. Use explicit ping-pong targets; never sample a texture while rendering into the same attachment. Dirty keys must reflect content, order, visibility, opacity, mask, channel and source changes.

Gate: render comparisons for order, opacity zero, disabled channels, hidden layers, blank paint layers, masks, modes and undo. Changing one layer must not mutate another layer's image storage. The current procedural material must look identical with an empty contribution.

### P3 — Channel set and material/pattern sources

The current UI's seven channels are **target intents**, not seven allocated image channels. Start with base color, roughness and metalness, then add height, normal, occlusion and emission with a visible consumer for each.

- Base color and emission carry color-space metadata; scalar, normal, height and mask storage are linear/non-color data. Color UI hex values must be converted at the boundary, not blended as sRGB bytes.
- Roughness/metalness blending is bounded scalar composition, not an RGB blend blindly reused. Define height modes and normal-vector blending separately.
- Decide explicit-normal versus height-derived-normal precedence. AO affects indirect lighting, not every light indiscriminately. Height storage alone does not establish parallax/displacement support.
- Material layers reference validated existing material presets. Pattern/generator layers reference editable pattern documents and their parameters, including outlined text and safe SVG. Do not serialize executable factories as sources.
- The current backward-compatible v1 extension stores optional bounded raster sources and folder parents. The optional last 2D source-stroke record is bounded and explicitly distinct from future mesh/UV strokes. Explicitly version/migrate when mesh stroke/atlas content and a compositor are introduced. Do not reinterpret today's configured layers or imported sources as completed painting.

Gate: inspect/export each channel and prove it reaches the intended material input without changing unrelated channels or breaking existing material/pattern exports.

### P4 — Brush authoring (requires a separate request)

Only after P1–P3: tools, stroke capture, pressure, spacing and a tested ray/surface projection. Enforce paintability in the write path, not just by a disabled button. Do not deposit into Fill/Material/Generator layers.

Store stroke commands with stable surface/chart identities, channel targets and material values. Keep them replayable; pixel images are derived caches. Resolve seams and padding from chart adjacency. Reject back-facing/occluded surfaces rather than relying solely on the mesh's double-sided preview material.

Gate: orbit/zoom never deposit paint; pointer cancel/touch/multi-pointer handling is transactional; stroke replay at two working resolutions agrees within filtering tolerance; one undo removes one stroke.

### P5 — Resource budget and durable projects

Allocate targets on first write; unwritten layers and disabled masks cost no paint image memory. Add capability checks for required formats/extensions and explicit errors. Budget resolution × active channels × bytes × copies/temporary targets, not simply “64 layers allowed.” The UI-only 64-layer limit is **not** a promise of 64 full-resolution GPU layers.

Bound dirty regions/recomputation, caches, device pixel ratio, undo payloads and async work. Reconstruct resources after context loss; dispose targets on project/source removal and workspace teardown. Large image assets need a dedicated persistent store/project bundle, not megabyte data URLs in local storage. Preserve editable originals plus versioned derived-cache keys.

Gate: measured memory and timing at several resolutions/channel counts, rapid workspace switching during compile, storage quota failure, context loss and recovery; no silent dropping of stroke/source content.

### P6 — Baking and interchange (later, explicitly out of scope now)

Connect the evaluated layer document to the existing material-map export, with consistent color spaces, resolution, normal handedness and channel packing. Export maps as a derived delivery; retain the layered source document. Reuse existing bake tests and compare preview against exported maps before exposing a Bake action.

## Verification commands

```sh
npm run test:texture
npm run test:editor
npm run build
npm run test:standalone
```

The standalone suite includes an offline teapot/layer-project case. Visual QA covers desktop, tablet and phone. Passing these UI tests does not establish painting/compositing or texture-bake support; those require the gates above.

## Implemented selected point-gradient slice

The viewport now supports surface ray hits for point-gradient balls, with persisted normalized object coordinates, colours, influence radius and weight. The selected Base-colour gradient fill / masked fill reaches the physical preview shader directly, independent of invalid stock teapot UVs. It is not UV stroke painting or a whole-stack compositor. Gradient masks on a layer/folder evaluate point luminance, strength, inversion, enable and inherited folder opacity. UI thumbnails are labeled field slices.

Custom Material-studio generators reuse existing unlit procedural-channel baking and recipe controls; their bounded generated PNGs are genuine 2D source content, not mesh-map passes or automatic teapot composition. Shared instrument controls and the HSV picker are configuration plus the already implemented round-tip 2D source drawing.

Keep mesh/UV strokes, full blend composition and compositor export versioned separately; do not reinterpret either 2D source strokes or object-space gradient records as packed-UV painting.
