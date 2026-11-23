# Stamp workspace and surface decals

Stamp is the fifth workspace in the existing Alloy React application. Material, Pattern, Texture, Stamp and Baking share the theme, header and left-library / right-inspector layout. The material/pattern Apply and export workflows, source-stroke painting, mesh-map baking and teapot deliverables are unchanged.

## Make and reuse a stamp

1. Open **Stamp** (`/?studio=stamp`). Its simpler composition canvas combines **Text**, **SVG** and **Image** components on a transparent 512 × 512 square.
2. Text is a real editable rich-text field. Select a character range and change its font, size, semibold weight or colour. Different runs share a baseline, including mixed fonts/sizes and line breaks. Without a range, formatting applies to the whole text. DM Sans and Space Grotesk, at weights 400/600, are bundled locally. Size changes update the artwork geometry, not just the editor appearance. Unsupported glyphs are reported.
3. Paste **SVG markup** into its source editor and apply it, or import an SVG file. SVG is validated, scoped and rendered as static artwork, **never executable JavaScript**. Scripts, events, external resources, foreign content, invalid references and excessive expansion are rejected. PNG/JPEG/WebP imports become bounded embedded PNGs.
4. Drag/resize components on the canvas, or use **G** (move), **R** (rotate), **S** (proportional scale) and the matching toolbar buttons. Move the mouse, then **left-click / Enter** to confirm or **Esc / right-click** to cancel. **X / Y** constrain move or scale; press the same axis again to release it. **Shift** snaps movement to 8px, rotation to 15° and scale to 10% increments. Each confirmed gesture is one undo entry; previews and cancelled/no-op gestures never change the saved draft. Hotkeys do not intercept rich text, SVG, names, numeric inputs or IME typing, and hidden/locked components cannot be transformed. The inspector uses the same rounded sliders/value capsules as Texture, including exact zero values. Visibility, opacity and locks are independent. Undo/redo, a locally recovered draft and project/SVG/PNG export are available.
5. Choose channel targets and retained values. Normal requires Height. Base colour comes from the artwork; height and normals derive from its luminance. Select alpha or luminance × alpha interpretation for stamp masks.
6. **Save preset** updates the current preset. **Save as new preset** creates an independent entry. **New stamp project** begins another composition. Presets can be opened or removed from the left library. **Use in Texture** saves the preset and opens the Texture stamp brush. Presets also appear in the shared overlay Content Browser.

Canvas component buttons are transparent selection/transform hit areas in every hover/active state; their UI hover styling never paints a grey rectangle over the artwork. Corner resizing follows the component's rotated local axes. Switching selection, editing the document, changing phone panels, pointer cancellation/lost capture, window blur/resize and unmount cancel any unconfirmed transform.

Text exports are actual font outlines, not platform-dependent `<text>` placeholders. SVG/image/text composition exports a self-contained SVG, a transparent PNG, or editable `alloy.stamp.v1` JSON. The embedded surface PNG is at most 256 × 256; SVG export retains vector paths.

## Both decal hierarchies

- A regular non-folder layer can contain many **decal components**, alongside its existing channel sources and mask.
- Add **Decal layer** to create a dedicated container for many components.
- The left tree shows each component's actual stamp artwork and mask/decal role. The right inspector edits its name, transform, opacity, visibility, lock, mask role and channel targets. Duplicate and remove are real document operations.
- A stamp snapshot is embedded once and referenced by component IDs. Re-saving a library preset does **not** silently rewrite existing placements. **Replace artwork & targets with selected preset** explicitly replaces one component's content/targets while retaining its position, orientation, dimensions and mask role; this replacement is undoable.
- Parent visibility, opacity and locks are inherited. A component lock protects its properties and deletion; its eye and unlock remain available. Moving/deleting/duplicating a subtree containing locked content is blocked.

## Both placement modes

**Stamp brush:** select an unlocked layer, select a preset, choose Stamp, aim at the teapot and click. The translucent aim preview follows a genuine mesh ray hit; each click commits one placed component. Brush size, rotation, opacity and mask role are configurable. Zero brush opacity is a no-op. Dragging does not spray a continuous stroke or orbit the camera. Choose **Navigate** or Escape to leave placement mode.

**Transform placement:** choose Transform, click the surface to create the initial anchor, then use the real 3D **Move / Rotate / Scale** handles. Existing components can be selected directly. **Re-anchor to surface** snaps an existing component to a new ray hit without creating another component. Inspector numeric controls adjust position, width, height, depth and roll.

These use clipped actual specimen triangles, filtered against opposite-facing surfaces and stored in specimen-local coordinates. The authoring-aware teapot clipper retains its real UV/part attributes; geometry without those attributes retains the legacy Three `DecalGeometry` route. No unique source-UV assumption is made. Off-surface or too-shallow projectors can produce no triangles; re-anchor or adjust depth to recover them. Coordinates are saved relative to the mesh bounding sphere (`object-sphere`).

Transform gestures use a draft and **one history commit on release**, not one per pointer move. Escape, pointer cancellation, lost capture, selection/lock/document changes and unmount cancel stale drafts and restore orbit interaction. The same safeguards apply to composition drags and asynchronous imports/preset rendering.

## Channels and mask scope

Real projected physical-material overlays support all 14 existing targets: Base color, Metalness, Roughness, Height, Normal, Opacity, Emission, Ambient occlusion, Anisotropy, Anisotropy angle, Clearcoat, Refraction index, Sheen and Subsurface. Disabled values are retained. RGB-disabled components retain the underlying selected base-colour preview rather than painting a white quad. With no targets enabled, a non-mask component is muted.

- Height is luminance-derived bump shading; Normal is a derived tangent-space normal map. This does not displace mesh vertices.
- Ambient occlusion modulates indirect illumination. Refraction index changes physical dielectric reflectance; enabling it does not make an opaque surface transmissive.
- Subsurface is a bounded wrap-light colour approximation, **not volumetric scattering**.

**Use as layer mask** converts a component into projected coverage instead of an extra visible decal mesh. Alpha/luminance interpretation, inversion and exact zero strength work on the surface. Multiple stamp masks on one owner combine as a union; ancestor coverage multiplies. Existing Fill/Gradient masks also clip physical decal overlays. Stamp coverage is evaluated through a bounded 1024² image atlas and a 7 × 64 float transform table. Object-space masks use local positions/normals; UV masks use the teapot's separate authoring UV attribute and part scope.

Mask coverage affects the **selected fill/gradient preview, native material coverage and the layer's projected decal components**. The subsequent [layer-evaluation extension](texture-layer-evaluation.md) gives ordinary opaque/alpha material passes and decals one bottom-to-top order, so an upper car-paint material covers lower artwork; the packed UV appearance follows that evaluation. Source strokes remain bounded 2D source images. This is **not a whole-stack UV painter/compositor or a baked decal texture atlas**; layer blend modes outside the live normal-overlay preview are still document settings. Material map exports and geometric baking are not newly relabelled as decal-composited maps.

## Layer drag/drop and the asset browser

The dedicated grip reorders a layer or its complete subtree. Leaf top/bottom halves place before/after; folder top/bottom quarters reorder around the folder and the middle half nests inside it. Valid zones are highlighted; inside drops expand the folder. A root drop target ungroups a moved subtree. Cycles, inherited/component locks and excessive nesting are rejected. Each completed move has one undo entry; identical moves are no-ops. Alt-Up/Down and the Folder inspector remain keyboard alternatives. Search/filtered trees disable reorder grips to avoid ambiguous order.

The Content Browser remains an **overlay**, never a scene-resizing panel. Its **list rows span the available centre-pane width**. The fixed 172px grid cards / 124px media and scrolling grid are preserved. Stamp assets can be inspected or assigned/dragged onto an unlocked layer row; row drops arm placement instead of inventing a front-facing anchor. A folder asset drop creates a Decal child layer. A direct drop onto the 3D scene or an authoring UV island creates a real hit-based component immediately. Material scene drops replace only the hit semantic part. See [part materials, UVs and UDIM decals](texture-parts-uv.md).

## Formats, storage and bounds

- Stamp project: `alloy.stamp.v1`; local draft `alloy-stamp-project-v1`; library `alloy-stamp-presets-v1`.
- 24 artwork components, 128 style runs, 1,024 text characters, sizes 8–160, 32,000 outline commands; 600KB project limit.
- 20 local presets / 4MB library. Exceeding capacity or browser storage quota is reported; saving does not silently evict another preset. Export is the portable backup.
- Texture retains `alloy.texture.v1`, its 64 layers / eight folder levels / 1MB project bound and 80-step history. Legacy projects without `stamps`/`decals` load without deleting their sources.
- 64 decal components / 16 distinct embedded stamp snapshots per Texture project. Shared snapshots are reused across repeated clicks; unreferenced snapshots are pruned after removal/replacement. Duplicating a layer creates independent component identities.
- Surface textures, aim geometry, transform helpers and mask tables are bounded and disposed on replacement/unmount, with material disposal deferred when a renderer compilation is in flight.

## Checks

```sh
npm run test:stamps
npm run test:painting
npm run test:runtime
npm run build
npm run build:githack
npm run test:standalone
```

The model tests cover rich-run edits/validation, channel dependencies, snapshot deduplication, transform projection, GPU table packing, locks/caps, tree drop positions and history. Browser checks exercise actual rich editing, SVG/raster import/export, aimed surface clicks, 3D gizmos, mask pixel changes, channel compilation, full-width list rows, native layer drags, cache reloads and phone layout. Use a supported Playwright Chromium installation (or `PLAYWRIGHT_EXECUTABLE_PATH`) with WebGL enabled.

### Verification checkpoint — 9 October 2026

- **32 Stamp/Decal checks passed** in the final uninterrupted run (21 model + 11 browser cases). This includes real gizmo drag/cancellation, rendered mask changes, explicit artwork replacement, rich-run editing/rejection, static SVG/raster import/export, both decal hierarchies, native drag/drop and phone actions.
- **66 existing painting/Texture cases passed across the regression run and focused rechecks.** The initial run exposed centre-drop compatibility and a resource-contention timeout in material-browser testing; the corrected cases passed together in an uninterrupted focused rerun. The old paint, masks, generators, gradient handles and history checks remained intact.
- **10 workspace-runtime/dock cases passed across the suite and focused cache-replay check.** The replay harness now intercepts hook module URLs with Vite HMR timestamps as well as bare paths, so the deliberate stale Texture React generation is actually exercised. Recovery retains the existing saved project.
- **12 offline standalone cases passed** against the rebuilt self-contained HTML, including rich Stamp text/surface decals, Pattern Apply/export, inline mesh baking and the real volume-SDF worker; no external runtime asset requests were accepted.
- Production and standalone builds passed; only the existing large-bundle warning remains. `git diff --check` is clean. Audited teapot deliverables are unchanged.

### Stamp interaction fix — 10 October 2026

- Removed the opaque shared button-hover background from all canvas hit areas. A browser regression compares actual artwork pixels before/after hover and also checks SVG/image/active hit areas.
- Added Blender-style G/R/S modal transforms, toolbar shortcuts, confirm/cancel, axis constraints, snapping and one history transaction per gesture. Corner resizing now follows rotated local axes. Drafts are cancelled safely on selection/document changes, pointer cancellation/lost capture, blur, resize, phone-panel changes and unmount; native input/IME typing is not intercepted.
- Stamp transform/channel sliders reuse Texture's existing shared rounded tracks and numeric capsules; negative positions, zero opacity/channel values and disabled states are verified. Rotation controls cover the model's full −360° to 360° range.
- **47 Stamp/Decal checks passed** in the full regression (32 existing + 15 new). The **15 new checks were re-run and passed** after final pointer-cancellation hardening (7 model + 8 browser cases).
- **Four shared desktop/phone dock cases passed**, covering all five workspaces at 1440, 1024 and 780 pixels and phone navigation. Stamp transform/file/export buttons remain reachable on a 390px phone.
- The rebuilt **offline Stamp workflow passed**, now exercising hover transparency and G/R/S before SVG export and actual surface decal/mask placement; no external runtime requests were accepted. This was a focused standalone check, not a repeat of the complete 12-case offline suite.
- Production/standalone builds and source formatting checks passed. The production preview remains on port 5173 with no-store headers and the preview host allowed. Saved project/library keys, schemas and audited baking deliverables were not changed.

### Part materials and authoring UVs — 10 October 2026

Texture now has a third **UV / Texture** view, scoped native part materials and real UV decal placement/editing. Source UV0 remains unchanged/read-only because its 32 patch coordinates overlap; Authoring UDIMs 1001–1004 are separate non-overlapping mesh attributes. Both decal hierarchies, masks, project formats and old 3D placement remain available. See [the part/UV usage and verification ledger](texture-parts-uv.md).

The final compatibility run passed **all 100 existing Stamp/Texture checks**, including all 47 Stamp/Decal cases and the 53 Texture/folder/history/gradient cases. New part/UV checks passed **22 cases** in their own uninterrupted run. No claim is made that 2D source strokes or native PBR materials are baked into a complete UV-stack atlas.
