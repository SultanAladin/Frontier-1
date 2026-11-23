# 2D shape editor → material viewer

This editor continues the material and pattern studio from `unassignedinbox/Slate`, branch `arena/835eeb10-slate`, source commit `a6df214c54bf51963846fc685794548199e2f010`. The existing material recipes, pattern catalogue, textile construction tools and baker remain available.

## Quick workflow

1. Open **Pattern studio**, or visit `/?studio=pattern&pattern=blank&material=natural-cotton` on the running app.
2. Choose **New** for a blank repeat tile. New/preset/import operations are undoable.
3. Pick a shape tool and **drag on the canvas**, rather than only inserting a fixed-size motif. Set fill, stroke and finish in the inspector. New shapes inherit the last selected vector style; drawing defaults can be changed without recoloring existing shapes.
4. Build a motif from several shapes. Shift-click or drag a selection marquee; group, align, distribute, duplicate or use **Repeat selection** to create a centered grid.
5. Inspect the wrapped edges and the **Seamless repeat preview**. Choose straight, half-drop or mirror repeat under **Material & repeat**. Motifs crossing a repeating edge are wrapped, not destructively cropped in the document.
6. Use **3D material** to inspect the same vector document with the actual material shader. Choose a substrate, preview mesh and lighting. Motif finishes control roughness, metalness and signed relief, not just color.
7. Click **Apply to material ↗**. The editor closes and the composed material is displayed in the main viewer. Reopening Pattern studio edits that material's embedded document. **Save as preset** persists the whole material in the local library; **Export material** provides its JSON, self-contained Three.js module and six-channel bake tools.

## Tools

| Tool               | Shortcut | Behavior                                                                                                                                             |
| ------------------ | -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| Select             | V        | Click, Shift-click, marquee; move one shape or a whole selection.                                                                                    |
| Edit points        | A        | Edit native path anchors and Bézier control handles. Double-click a native path to enter this mode. Convert basic primitives in the inspector first. |
| Rectangle          | R        | Drag to size; adjustable corner radius, expressed in the local 0–100 shape box.                                                                      |
| Ellipse            | O        | Drag an ellipse; Shift constrains it to a circle.                                                                                                    |
| Line               | L        | Open vector line; Shift snaps the angle to 45° increments.                                                                                           |
| Triangle / Diamond | T / D    | Drag-to-size primitives.                                                                                                                             |
| Polygon / Star     | G / S    | Editable side/point count; stars also have an adjustable inner radius.                                                                               |
| Pen                | P        | Precision Bézier pen: click corners, drag handles, edit any draft point, continue endpoints and insert points on segments.                           |
| Curve              | U        | Click-through automatic curvature, with adjustable tension and Alt-click corner points.                                                              |
| Freehand           | B        | Drag to sketch a simplified, editable vector path.                                                                                                   |
| Arc                | C        | Drag an elliptical arc; edit start/sweep angles and choose open, chord or pie closure.                                                               |
| Text               | X        | Click to place editable multiline text; control typography and convert to outlines.                                                                  |
| Eyedropper         | I        | Sample fill/stroke styling from a native vector, including locked references; Shift samples stroke only.                                             |
| Hand               | H        | Drag to pan. Space-drag or the middle mouse button pans temporarily.                                                                                 |

Selection transforms have **eight resize handles**, an additional rotation handle, and editable numeric properties. Opposite edges/corners stay anchored during normal resizing; Alt resizes from the center. Shift (or **Constrain proportions**) keeps the aspect ratio. Shift snaps rotation to 15° increments. Nonuniform resizing of a multi-selection containing rotated shapes is constrained to uniform scaling, rather than silently introducing an unrepresentable shear.

In point-edit mode, select a point to access **Smooth point**, **Corner point**, **Add point** and **Remove point**. Adding a point subdivides a Bézier segment without changing its curve. Dragging an anchor carries its handles with it; Alt-dragging a handle breaks symmetry. Point edits refit the shape box without changing world-space geometry.

Independent fill and stroke, with round/butt/square caps and round/miter/bevel joins, are supported on native vectors. New vector strokes have a document-space width: stretching a shape does not crush a line's stroke. Canvas zoom, exported SVG, PNG and the material maps retain consistent coverage. Existing textile paths retain their original stroke semantics.

## Precision pen & curve workbench

The **Path workbench** appears at the top of the inspector for Pen, Curve and Edit points. It does not cover or shrink the artboard.

- **Pen (P):** click to place corners; drag to set incoming/outgoing tangents. **Shift** constrains a segment/handle to 45° increments. Hold **Alt** during a placement drag to keep the incoming tangent fixed while redirecting the outgoing one. Hold **Space** during placement to reposition the anchor and its handles. Ctrl/⌘-drag an existing endpoint edits it instead of continuing it.
- **Curve (U):** click anchors to interpolate a centripetal, automatically smoothed Bézier contour. Unevenly spaced points and coincident anchors are handled safely. **Curve tension** scales the tangents from 0–100%; Alt-click places or converts a sharp corner. Double-click an existing point toggles corner/smooth (automatic in Curve) without finishing the whole path.
- Stroke and fill controls are directly in the Path workbench and apply to both drafts and completed paths; zero width explicitly disables the stroke.
- Draft anchors and control handles are directly editable **before** the path is committed. A dashed live preview shows the next segment or closing contour. Enter or **Finish path** finishes an open contour; the Pen also finishes on a double-click at its last/new anchor. Curve double-click converts an anchor instead. Click the first anchor or **Close path** to close it.
- **Point types:** Corner permits independent tangents; Smooth aligns tangents while preserving unequal lengths; Symmetric aligns them with equal lengths; Automatic regenerates tangents when neighbouring anchors move. **Corner point** collapses handles to a sharp point; choosing Corner in the selector only breaks their coupling.
- Select a point to type its **document-space X/Y**, incoming/outgoing handle **length** and **angle**. Angles follow SVG coordinates: 0° right, 90° down. These controls also work on rotated/flipped/nonuniformly sized layers. Shift-click or marquee-select points; Ctrl/⌘ A selects all points in the active path, and arrow keys move the selected anchors as a rigid set.
- **Insert without distortion:** Pen/Curve click a segment, or Alt-click in Edit points. The closest curve position is found and the Bézier is subdivided exactly, not replaced with an approximate midpoint. **Add point** also inserts on the selected segment or after the selected anchor.
- **Bend:** in Edit points, drag a segment to bend it while keeping both endpoints fixed. Ctrl/⌘-drag provides the same segment editing from Pen/Curve. This deliberately makes the segment's two tangents independent.
- **Continue:** select an open path and choose **Continue end/start**, or click its endpoint in Pen/Curve. Existing curve geometry is preserved, including rotated/flipped source paths. The draft replaces the original only when finished; Cancel leaves the original intact. **New path** starts a separate contour, even on an existing endpoint.
- **Topology:** open/close or reverse a contour; split at an interior anchor; select two open paths and **Join paths** (Ctrl/⌘ J). Join picks the closest endpoints, merges coincident anchors and retains the first path's style. Capacity/point limits are checked atomically.
- **Undo while drawing:** Ctrl/⌘ Z / Shift Z (or the header buttons) undo/redo draft placements, point edits and topology, restoring the active point too. Once finished, one document undo restores/removes the complete path.
- Save, SVG/PNG export, 3D preview and Apply finish a valid pending path before proceeding. A one-anchor draft cannot be exported accidentally. Changing to another shape tool finishes an existing valid path; Hand/Edit points can be used without discarding the draft. Pointer cancellation or focus loss rolls back only the current gesture.
- Pending paths with at least two anchors are saved locally after 700 ms of inactivity. Recovery restores them as editable open paths and selects the latest open contour; use Continue to resume drawing. The original source is retained if an extension is canceled or its source is changed/protected elsewhere.

## Stroke width, snapping and SVG tools

- **Curve/Pen stroke width:** choose **Curve**, **Pen** or **Edit points**. The **Stroke & fill** section is directly below the Path workbench heading. It edits the active draft or selected path, with separate stroke/fill colors, caps and joins. Width is in document units; **0 means no stroke**, including on an unfinished path. New open paths receive a visible default stroke. Completed shapes also retain the ordinary inspector appearance fields.
- **Target snapping:** the magnet enables anchors/endpoints, object centers and alignment guides. **Snap targets** independently enables **Anchors & centers**, **Curves & edges** and **Object alignment**. Curves/edges are opt-in; they project onto the actual Bézier rather than a bounding-box approximation. Visible locked artwork may be used as a reference; hidden artwork is excluded. Moving anchors/adjacent segments are excluded from their own targets, and multi-point moves remain rigid.
- **Grid and angles:** grid spacing remains independent of the magnet. The **Angle snapping** menu offers 15°, 30°, 45° and 90° for pen segments and tangents; Shift temporarily constrains to 45°. Object/curve targets never pull a constrained tangent off its angle. The on-canvas marker identifies what was snapped. Snapping applies to path placement, point/tangent editing, shapes, lines, arcs, resize and selection movement; freehand strokes remain freehand.
- **More path tools:** under **Path operations**, select 2–16 unlocked, closed, filled vectors and choose **Union**, **Difference**, **Intersection** or **Exclude**. These are Bézier operations, not flattened polygons. Difference subtracts upper selected layers from the bottom selected shape and uses the bottom shape's styling. Compound holes are retained; stroke outlines, embedded SVGs and open paths are not expanded implicitly. Empty/over-detailed results leave originals unchanged, and one Undo restores the selection.
- **Arc (C):** draw an ellipse-sized arc, then edit **Arc start angle**, signed **Arc sweep angle** and **Arc closure** in the inspector. Convert to an editable path to adjust individual points.
- **Eyedropper (I):** select the target shape(s), choose Eyedropper and click a native source. Fill and stroke styling is applied to unlocked selected vectors and future drawing defaults; Shift samples stroke only. Embedded images/SVG retain their own colors and are not style-sampled.

## Editable text

Choose **Text (X)**, enter content in the **Text workbench**, then click the artboard. Clicking existing text or double-clicking it with Select reopens its content. Text supports multiple lines, DM Sans/Space Grotesk, regular/semibold weights, font size, line height, tracking and left/center/right alignment. Normal fill, stroke, transformations and material assignments also apply.

The bundled fonts support Latin glyphs; unsupported characters show a missing-glyph warning. Editable text metadata is saved alongside a vector outline. **SVG, PNG, offline and material exports render that outline**, so there are no runtime font downloads or system-font substitutions. Resizing/rotation remains intact when editing content. **Convert text to outlines** removes text metadata and exposes path source/boolean operations; Undo restores editable text. Compound multi-letter outlines stay compound rather than losing holes or letters.

## Paste, embed and edit SVG markup

Use the toolbar's **Paste SVG source** button (also under Draw & import). Paste a complete SVG into the code panel; its preview uses **only validated source**. **Validate SVG** checks the draft, **Embed SVG** adds it, and **Edit SVG source** reopens an embedded layer. Applying a valid edit replaces only its source—position, size, rotation and material assignments stay intact. Cancel or invalid source leaves the document unchanged. Ctrl/⌘ Enter applies the source; Escape cancels.

Supported static markup includes basic shapes/paths, groups, gradients, clips, masks, patterns, local `defs`/`use`/`symbol` references, text/tspans and embedded PNG/JPEG/WebP data images. Presentation-only inline styles are expanded to SVG attributes. Source SVG text uses system fonts; use the Text tool for font-independent outlined typography.

SVGs must be self-contained. JavaScript, event handlers, animation, foreign HTML, stylesheet blocks, external fonts/resources, entity declarations, missing IDs and circular/excessive references are rejected. Limits: 500 KB of markup, 5,000 elements, nesting ≤128 and expanded references ≤20,000. Definitions/references are namespaced when embedded/exported so separate layers do not clash. This is an artwork source editor, **not an executable script runner**.

## Navigation and precision

- Mouse wheel zooms at the cursor; use the zoom menu/buttons for exact levels. **0** or **Fit** fits the tile. Camera changes do not enter document undo history.
- The rulers show document units, including negative/off-tile positions while panning.
- Grid visibility and grid snapping are independent. Snapping supports 8, 16, 32 and 64 units; grouped moves preserve internal spacing instead of snapping each object separately.
- **Smart alignment guides** snap selection edges/centers to other visible shapes and the tile. Alt bypasses smart guides while moving.
- Touch drawing uses pointer capture; two fingers pan/zoom. Escape, pointer cancellation, capture loss and window blur roll back uncommitted transform gestures.

## Layers and arrangement

The pinned layer panel remains available while scrolling through presets. Its top row is frontmost. Drag rows to reorder; the inspector and `[` / `]` move the selected objects one step backward/forward, with Shift moving to back/front.

- Click an eye checkbox to hide/show a layer; hidden layers remain in JSON but are omitted from SVG and material maps.
- Click a lock checkbox to lock/unlock a layer. Locks prevent direct transforms, painting, deletion and point editing. Global generator/preset changes deliberately rebuild their generated layers and remain undoable.
- Shift-click canvas objects adds/removes selection. In the layer list, Shift selects a range; Ctrl/⌘ toggles selection. Alt selects an individual member of a group.
- Ctrl/⌘ A selects all visible, unlocked layers; Ctrl/⌘ G groups; Ctrl/⌘ Shift G ungroups. Groups are flat, not nested scene graphs.
- Alignment uses the collective selection bounds; a single object/group aligns to the tile. Distribution makes equal gaps between at least three objects/groups.
- Ctrl/⌘ D duplicates; Ctrl/⌘ C / X / V use an in-editor clipboard. Pasted/duplicated groups have new group identities. No clipboard permission is required. This clipboard does not persist across editor sessions and is not the operating-system clipboard.
- **Repeat selection** replaces selected source shapes with a centered row/column array, with independent X/Y spacing. Each grouped instance preserves its motif layout. Undo restores the original selection. The existing radial and reflected-copy tools are also retained.

## Undo, persistence and exports

**Ctrl/⌘ Z** undoes; **Ctrl/⌘ Shift Z** or Ctrl/⌘ Y redoes. History retains up to 80 edits and their selections. Pointer transforms are one undo step, not one per pointer move. Rapid changes to the same numeric/name property are coalesced. New commits clear redo; canceled/no-op gestures do not.

Committed edits are debounced into a browser-local draft. Opening a different/blank pattern offers **Restore draft** or **Dismiss**. Editing a new document replaces the last local draft; recovery is not a versioned cloud backup. If storage is blocked/full, the status explicitly asks you to save a document. Save portable JSON for reliable project transfer, especially with embedded images.

- **Save document / Ctrl/⌘ S**: `alloy.pattern.v1` JSON, preserving editable nodes, groups, visibility, locks, finishes and embedded imports. It can be reopened using **Open document**.
- **Export seamless/border/rug SVG**: a standalone vector tile/supertile appropriate to the repeat axes and layout. Selection boxes, grids, guides and preview-only textile shadows are excluded.
- **Export PNG**: 512, 1024, 2048 or 4096 pixels on the longest side. Half-drop and mirrored patterns export their full repeat supertile; physical design aspect and transparency are retained.
- **Apply to material**: uses the same normalized document and base resolver as the live 3D preview. It is not a screenshot or an unrelated texture approximation. The renderer samples generated color, material-parameter and finish maps at finite resolution.

Finishing a pending pen/curve path is automatic before saving, exporting, previewing in 3D or applying. Individual vector and image sources remain embedded in the material preset/module. Neither exporting nor applying uploads user documents.

## Formats and limits

- The existing portable format limits a document to **64 layers / 12 MB**. Editable paths have 2–400 nodes; freehand input is simplified. Raw SVG path text is limited to 100,000 characters and finite numbers, with explicit errors rather than silent truncation. Arrays and duplicates reject over-capacity operations without changing the existing document.
- Coordinates are bounded to −512…1024, shape sizes to 1…1024, and material properties to the original renderer's physical ranges. The authoring tile remains 512 units; the existing finite/one-axis and physical-aspect documents still load.
- Point conversion supports basic primitives and **single-contour SVG paths**, including relative coordinates, cubic/quadratic/smooth commands and elliptical arcs. Arcs are converted into cubic approximations. Compound paths and grouped SVGs retain their complete source and are transformed as objects; they are not silently flattened or truncated into a lossy contour.
- SVG import uses the original safe subset: no scripts, external URLs, HTML, linked fonts, animation or arbitrary CSS. Simple 100-unit SVG paths with compatible solid paints become native vectors; gradients, per-paint alpha, percentage opacity or oversized strokes retain their complete SVG source. Raster imports must be embedded PNG/JPEG/WebP, with the existing 4 MB input limit.
- No text/font authoring, boolean geometry operations, nested groups, collaboration, geometry export, UV unwrapping or arbitrary shader-node graph editor is implied. Pattern slots and baked maps retain the material studio's documented rendering limitations.

## Development

```sh
npm ci
npm run dev
npm run build
npm run test:editor
npm run build:githack
```

`test:editor` covers vector schema/geometry, pointer/keyboard behavior, undo/cancellation, grouping, locks, arrays, recovery, export formats and the existing pattern-to-material workflow. The full library/material regression suite remains `npm test`. Use `PLAYWRIGHT_EXECUTABLE_PATH` for an installed Chromium if needed. `site/index.html` is the self-contained build, not a dev-server shell; `npm run test:standalone` exercises it with external asset requests blocked.
