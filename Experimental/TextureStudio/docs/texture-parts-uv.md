# Texture: part materials, UVs and UDIM decals

> **Layer evaluation update:** materials and decals now share bottom-to-top alpha-over evaluation, automatic white Layer 0, genuine baked mesh support and a lit packed-UV appearance preview. See [layer evaluation](texture-layer-evaluation.md). Digital layer/mask Paint / Erase / Fill now extends these same views; see [digital painting](digital-painting.md) for channel consumers, brush controls and bounds. Legacy imported-source composition and PBR atlas export remain separate.

This extends the existing Texture workspace, document/history and Three.js renderer. It is not another editor, a mesh-import workflow, a baked texture atlas or a full UV-layer compositor.

## Apply a material to one part

1. Open **Texture** (`/?studio=texture`). Open the shared **Content Browser**, choose **Materials**, and drag a material card onto the teapot.
2. A genuine scene raycast determines the target: **Body**, **Lid**, **Handle** or **Spout**. The hover outline / “Drop → … only” label identifies it. Dropping ceramic on the lid replaces **only the lid's material**, including its knob; the other three regions retain their existing finish.
3. The operation creates a **part-scoped Material layer**, or replaces the current visible assignment for that part. Its native, declarative material snapshot is embedded in the project. An edited currently selected library material is captured with its current values, not just its preset ID. Subsequent library editing/removal cannot silently change the assignment.
4. The right inspector shows **Lid only / Native PBR** (or the equivalent part), followed by the **same recipe controls as Material**. These edit the embedded snapshot and affect that part's real shader, rather than pretending ordinary channel-source defaults are its material values.
5. Layer/folder eyes and opacity are inherited. Hiding/removing a part assignment or setting its inherited opacity to zero restores the neutral/next visible assignment. Nonzero assignment opacity affects the native surface's transparency. The ordered surface passes retain lower assignments behind partial coverage. An opaque unmasked upper assignment covers a lower stamp; putting it below the stamp reverses that order. This remains Normal alpha-over surface evaluation, not arbitrary per-channel blend-mode composition.

A material drop is one undo transaction. It preserves unrelated layers, masks and sources. Empty-space drops, unsupported asset types, unknown library IDs and locked material targets do not change the document. A locked part assignment also blocks new stamps and property/deletion changes to decals targeting that part; visibility/unlock controls remain available. Existing row/folder source-link drops remain separate from scene material assignment.

The underlying mesh is not duplicated as a whole-object recolour. A part mesh contains only that region's original triangles, uses the existing `createMaterial` shader, and replaces the corresponding neutral surface/shadow region. The specimen's original geometry remains available for accurate picking and decal projection.

## UV / Texture view

Choose **UV / Texture**, beside **Shaded** and **Wireframe**. The viewer displays actual indexed mesh topology, 32 charts, tile numbers and part labels. Middle-drag / Alt-drag pans; the wheel zooms around the pointer. **F / Frame** fits the selected part's tile, or all tiles when **All parts** is selected. UV and 3D zoom/frame commands are independent. View switching retains the same WebGL canvas and scene; idle hidden 3D drawing is suspended while the 2D view is active.

### Why there are two UV sets

Three's built-in teapot repeats its 32 patch UVs in the same 0–1 square. Those overlapping source coordinates cannot support unambiguous decal placement. They remain unchanged as **Source UV0**, available for **read-only inspection** with an overlap warning.

A separate, non-overlapping mesh attribute, **Authoring UDIMs** (`teapot-part-udim-v1`), provides:

| Part   | Source patch IDs (zero-based) | Authoring UDIM | Charts |
| ------ | ----------------------------- | -------------- | ------ |
| Body   | 0–11 and 28–31                | 1001           | 16     |
| Lid    | 20–27                         | 1002           | 8      |
| Handle | 12–15                         | 1003           | 4      |
| Spout  | 16–19                         | 1004           | 4      |

The authoring layout is generated mesh data with gutters, not an image pretending to be UVs. Positions, normals, indices and original UV0 are unchanged. It is **not** an imported UDIM image set; it is also distinct from the independently audited Baking UV packer, which is unchanged.

Material drops onto an authoring UV island resolve its actual part and perform the same scoped assignment as a 3D drop. Source UV0 accepts no material/decal placement. A gutter or empty tile produces no surface hit and no transaction.

## Place and edit UV decals

- Select a visible, unlocked regular surface layer, a part Material layer, or a dedicated **Decal layer**. Select a preset and use **Stamp**, then click an authoring UV island. Alternatively, drag a **Stamp asset directly onto an island**; this creates its component immediately, rather than placing an arbitrary front-facing quad.
- The hit is barycentric on a real UV triangle. It records the owning part, authoring U/V, UDIM, and corresponding object-local anchor/normal. The UV rectangle is clipped onto real mesh triangles in that part. Switching to Shaded shows the resulting artwork on that surface; it never repeats across the original overlapping patches or bleeds into another part.
- UV components retain `space: "uv"`, the explicit UV-set ID, U/V position, UV-unit width/height, roll, targets, opacity and mask settings in the same Texture project. Legacy object-sphere projectors retain their own coordinate space and defaults; switching views does not reinterpret one as the other.
- Choose **Transform**, then drag the selected artwork. **Move / Rotate / Scale** choose the UV gesture; Shift snaps movement to 0.01 UV, rotation to 15°, or proportional scale to 10% increments. Inspector U/V and size controls use UV units; projection depth and XYZ/3D handles are not incorrectly displayed for a UV placement.
- **Re-anchor in UV space** relocates an existing UV component to another actual island hit, subject to the layer's part scope. Selection of a UV component opens its authoring view; a 3D component opens a 3D view. A UV component never attaches to a world-space TransformControls projector.
- Pointer-up confirms **one** history entry. Escape, pointer cancel/lost capture, window blur, resize, view/selection/lock/document changes and unmount cancel an unconfirmed draft. Draft poses do not enter local storage or export history. Identical placements/transforms remain no-ops.

UV topology backgrounds are cached; clipping uses boundary contours extracted from indexed triangles, including missing/degenerate corners, rather than retessellating thousands of overlapping subpaths each pointer move. Transform-only 3D geometry changes retain the existing decal material/program. Temporary geometries, textures, outlines and material resources are retired on replacement/unmount, safely around pending renderer compilation.

## Masks and honest preview scope

UV stamps can be **layer masks**, with alpha/luminance interpretation, inversion, inherited coverage and exact zero strength. The existing bounded 7 × 64 GPU mask table now carries UV projectors and part identifiers, and evaluates the same authoring UV attribute as the mesh/decals. Multiple masks union per owner; ancestor masks multiply. Fill/Gradient and stamp masks affect real part-material/decal coverage. The subsequent [mask-painting extension](texture-mask-painting.md) adds actual brush/region coverage and selected-mask previews, with source-RGB colour fields explicitly distinguished from matching the final material.

The packed 2D viewer now shows a **lit appearance preview of the same ordered material/decal/digital-paint passes**, under topology and transform guides. Selected Mask mode uses its separate grayscale atlas. This is not a per-channel PBR bake or export: ordinary source strokes remain bounded 2D source images, and general source-layer/channel blending is still deferred. Native opacity and masks reveal lower surface passes. Material exports and geometric bakes are not relabelled as composited decal maps. Native glass/wax transmission now samples ordered lower linear lit history, including stamps, without converting optical recipes to alpha. UV optical sampling stays at the same chart pixel to avoid cross-chart refraction/mip bleeding. This is a lit appearance view, not a nested-volume refraction solver or exported channel atlas.

## Portability and bounds

- `alloy.texture.v1`, the local project key and existing source/stamp formats remain unchanged. New fields are optional, preserving legacy projects without part assignments or UV components.
- Part targets accept only the current teapot and its four existing regions. Native snapshots keep known material fields, bounded finite values, validated palettes/patterns and at most 200KB; executable/foreign fields and external texture URLs are not retained.
- Existing limits remain: 64 layers, eight folder levels, 1MB project, 80-step history, 64 decals and 16 embedded stamp snapshots. No large mesh data or generated GPU buffers are serialized into history.
- Save project JSON / local recovery / undo / redo preserve assignments and UV placements. This pass does not add Texture mesh importing or UDIM-file loading.

## Verification — 10 October 2026

```sh
npm run test:parts-uv
npm run test:stamps
npm run test:texture
npm run test:layout
npm run build
npm run build:githack
```

- **22 part/UV checks passed in one uninterrupted run**: 14 model + eight browser cases. These verify geometry/UV0 preservation, real topology/UDIMs, barycentric anchors, chart/gutter boundaries, real 3D clipping, declarative snapshot validation across all 127 presets, scope/limits/locks/history, native library dragging, and actual mask pixel changes.
- The lid-only browser check compares real framebuffers: ceramic changes the sampled **lid** pixels while sampled **body, handle and spout** pixels remain **byte-identical**. The native drag test checks unchanged scene dimensions; the source-UV mode rejects ambiguous placement.
- UV move/rotate/scale, single undo/redo, uncommitted local-storage state, Escape / pointer-cancel / blur restoration, component locks, reload, one retained WebGL canvas, independent zoom and 390px phone actions are covered.
- **All 100 existing Stamp/Texture checks passed in one compatibility run**, including the prior Stamp hover/G/R/S fix, 3D decal handles, masks/channels, folders/native row drags, gradients, imported/painted sources, history and phone layout.
- Final production verification reran all **22 part/UV cases** alongside shared layouts/runtime: **29 passed, three dev-only cases skipped**, 8.3 minutes. The four layout cases cover all five studios at 1440/1024/780px and phone docks; runtime cases cover deduplicated dependency configuration, repeated cached reloads/workspace switches and embedded iframe switching. The three skipped tests specifically replay development optimizer modules; the production preview has no such modules.
- Visual review caught thumbnail-sized mobile tiles. Phone UV mode now reserves real canvas space, uses adaptive framing/compact readouts and a fitting toolbar. The 390px test now asserts canvas height **>180px**, initial UV scale **>60px per tile**, contained controls and successful part-scoped placement. Final desktop/lid/UDIM/phone screenshots were inspected after this correction.
- **Four focused standalone/offline cases passed**, 2.2 minutes: part-native material + UV decal/mask/save/3D retention, rich-text Stamp/decal/masks, shared Material generator/gradient tools, and overlay/folder/imported-source/colour-mask behavior. All HTTP asset requests were blocked; each case finished with no external-resource requests or browser errors.
- **Production and standalone builds passed.** Production output is `index-BqFh5U-m.js` / `index-oYD8Yawa.css`. `site/index.html` is the rebuilt, fully embedded 3,814,378-byte standalone file; SHA-256 `07439947e9ffbb674f3eb71d824f52f0dbc6e76d3a8f08080d34358f5a3af631`. Preview binding/host allowlist and no-store headers are retained. Audited baking algorithms and deliverables are unchanged.

Verification logs (ignored QA artifacts): `.playwright/parts-uv/final-layout-runtime.log`, `.playwright/parts-uv/compatibility.log`, and `.playwright/parts-uv/offline-final.log`. The earlier 100-case compatibility run precedes the final responsive UV-only framing/toolbar polish; the final 22-case run and all layout/offline checks include that polish.
