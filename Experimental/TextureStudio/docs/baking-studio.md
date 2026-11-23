# Baking Studio — map-only mesh baking

## Shared layout

Material, Pattern, Texture and Baking use the newer ProjectZero/Texture Studio theme. **Libraries, outliners, layer stacks and bake-source/map selection are on the left; the viewport is in the middle; inspectors are on the right.** Desktop and tablet preserve this order. Phone layouts use an upper viewport and accessible dock tabs or lower panels.

- `/?studio=material`: material library → viewport → material inspector.
- `/?studio=pattern`: pattern library/layers → vector editor → vector/surface inspector. Pen, Curve, Text, safe SVG, snapping, history, Apply and exports remain intact.
- `/?studio=texture`: layer stack → teapot → layer inspector. This is still layer-metadata setup, not brush painting or compositing.
- `/?studio=baking` (alias `?studio=bake`): low/high source meshes and map tiles → 3D/2D preview → bake/map inspector.

**The mesh-baking workspace has no node editor, node library, node-inspector tab or hidden default node graph.** Map outputs are evaluated directly from the selected tiles and settings. The old graph evaluator remains a tested, optional internal API for compatibility, but the studio does not use it.

## Workflow

1. Pick the built-in Teapot / Cube / Sphere study or import low UV targets and high sources.
2. Select the low objects to bake. Each is an independent texture set. Optional name matching recognizes `_low`/`_high`, `_lp`/`_hp` and equivalent separators.
3. **Click map tiles on the left. Green means enabled.** Essential, Masks, Normals and All maps presets are available; Clear maps disables every output and the bake button until a map is enabled.
4. The right inspector controls resolution, padding, tangent-normal convention and projection. Clicking a tile also shows its description and applicable parameters: bevel radius/strength/samples, dust up axis/falloff, wire width or mask strength.
5. Click **Bake mesh maps** / **Bake all selected meshes**. A CPU BVH worker reports real sets, texels, hits and misses. Cancel terminates the worker. Switching workspaces also stops an active job.
6. Inspect the PNGs in 2D or the tangent/bevel normal on the low mesh, then save individual maps or the complete ZIP. After a bake, tiles show the actual map previews instead of illustrative swatches.

Nine familiar maps are enabled by default; additional outputs are opt-in. Existing stored selections are recovered. An old node recipe's **settings** can be loaded, but its custom node effects are not applied silently.

## Supported imports and UV contract

- Low: OBJ, self-contained static GLB/glTF, PLY. UV0 is required.
- High: the same formats, plus STL.
- Geometry transforms are baked into scene-space positions and normals. Original low-object transforms are retained separately for object-space normals.
- External glTF buffers/images, compressed glTF, skinned/animated meshes and unrealized instances are rejected or removed by the geometry-only import contract; high material textures are not loaded.
- Imported low UVs must be non-overlapping in one 0–1 tile. Overlapping, missing and out-of-tile UVs fail visibly. No automatic unwrap or UDIM is implied.
- The built-in teapot packs its 32 patches into unique charts. The cube has six UV islands but is one source object. Built-in sphere pole UVs are kept inside the unit tile; imported UVs are never silently clamped.
- The viewport high overlay displays the first high part; the worker projects against all relevant high parts.

## 27 working outputs

| Family                | Maps                                                                                                                       | Contract                                                                                                                                                                                                               |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Normals (7)           | Tangent normal, world normal, object normal; bent tangent, bent world, bent object normal; bevel normal                    | World uses imported scene axes. Object uses the **low target's original local axes**, not an alias of world space. Bent normals average unoccluded hemisphere directions. Bevel is geometry-derived normal averaging.  |
| Surface (8)           | Ambient occlusion, secondary AO, curvature, convexity, cavity/concavity, thickness, projection height, vector displacement | Secondary AO has a separate distance. Curvature is signed local normal variation; convexity and cavity isolate opposite signs. Vector displacement retains the full high-minus-low XYZ offset, not just scalar height. |
| Coordinates / IDs (5) | Position, object ID, UV island ID, UV coordinates, UV wireframe                                                            | Object IDs identify source objects; UV-island IDs identify connected low UV charts. The manifest contains both palettes. Wireframe includes low triangle edges with a pixel-width setting.                             |
| Masks (6)             | UV coverage, alpha/hit mask, bevel mask, dust mask, dirt mask, edge-wear mask                                              | Coverage describes low UV occupancy; alpha/hit mask describes whether high projection succeeded. They are not interchangeable. Dust/dirt/edge wear are derived masks, not simulated weathering.                        |
| Color (1)             | Vertex color                                                                                                               | High vertex colors are interpolated and encoded as sRGB; missing vertex color produces white. This is not high-material/albedo texture transfer.                                                                       |

### Encodings and limits of interpretation

- **Tangent normals:** RGB encodes −1…1 as 0…1. OpenGL +Y / DirectX −Y flips green for tangent, bent-tangent and bevel-normal outputs only. Object/world vector normals are not altered by that setting. Tangents use low-triangle UV derivatives, not certified MikkTSpace interchange.
- **Height:** `distance = (sample − 0.5) × 2 × max(front, back)` in source scene units. Vector displacement uses that range independently on XYZ in world axes. Neither is a multires sculpt solver.
- **Position:** XYZ normalized by the high-source world bounding box; a zero-size axis encodes 0.5.
- **Curvature:** 0.5 is flat; convex is above 0.5, concave below. Convexity/cavity strength is bounded by the mask-strength control. This estimate is not an exact differential-curvature solver.
- **Thickness:** inward opposite-surface distance divided by thickness range. The ray searches beyond that display range, so farther surfaces saturate white instead of becoming false holes. White means thicker, black means thin/no hit. This differs from references that invert thickness. Open meshes can have no inward hit.
- **Object IDs:** source-object index + 1, encoded as little-endian 24-bit RGB; zero is reserved for a projection miss. Name-matched sets retain the original high-source identity. These are not material/group IDs.
- **UV-island IDs:** deterministic, distinct chart colors; `chartPalette` records their 8-bit RGB values. Do not color-manage ID maps as albedo.
- **Dust:** upward-facing world normal, chosen up axis and slope falloff, combined with occlusion. **Dirt:** occlusion plus cavity. **Edge wear:** convexity weighted by exposure. These are editable mask starting points, not actual particles, grime textures or abrasion simulations.
- **Bevel:** local high-normal averaging with radius, samples and strength. It changes shading only, not geometry, silhouettes or topology, and is not exact Blender Cycles Bevel. Zero radius/strength is a bypass. The bevel mask isolates the angular change. Radius must span the mesh detail and working texel density.
- **Wireframe:** at low resolution, very dense triangles can fill the entire visible chart with wire; reduce width or increase resolution.
- **Misses:** counted and exposed in alpha/hit mask. Derived dust/dirt/bevel masks do not invent high-surface data on misses. Neutral low normals/zero displacement are fallbacks, not successful high projections.
- **Padding:** extends RGB outside low coverage, while PNG alpha remains zero there. UV coverage and alpha/hit masks are not padded.

PNG channels are 8-bit. All mesh data/IDs/masks remain linear; only vertex color uses sRGB conversion. The ZIP manifest includes selected settings, map descriptions/space/convention, bounds, sources, palettes, chart count, degenerate triangles, coverage, hits and misses. The map-only manifest has `workflow: "mesh-maps"` and no graph document.

## Research: mesh maps versus material / lighting passes

There is no finite list of every custom shader output, so this catalogue was checked against the standard families in these official references:

- Marmoset **Map Types**: tangent/object and bent normals, height/position/curvature/convexity/cavity/thickness, AO/secondary AO, object/material/group/UV IDs, wireframe and alpha, material outputs, and lighting passes. [3](https://docs.marmoset.co/docs/map-types/)
- Adobe **Bake Mesh Maps**: normal/world normal, ID, AO, curvature, position and thickness; directional normals are also useful for dust masks. [2](https://helpx.adobe.com/in/substance-3d-painter/using/baking.html)
- Blender **Render Baking**: combined, AO, shadow, position, normal, UV, roughness, emission, environment, diffuse/glossy/transmission, and multires normal/displacement/vector displacement. [1](https://docs.blender.org/manual/en/latest/render/cycles/baking.html)

The following require data/evaluation beyond this geometry-only worker and are **not fake enabled tiles**:

| Other family                 | Types reviewed                                                                                    | Missing prerequisite                                                                                          |
| ---------------------------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| Material channels            | Albedo, roughness/gloss, metalness, specular, emission, material opacity, transferred textures    | High-material/shader and texture evaluation. Vertex color is not substituted for material albedo.             |
| Material / grouping identity | Material ID, group/collection ID                                                                  | Preserved material slots or source grouping metadata. Object and UV-island IDs are not substituted for these. |
| Lighting passes              | Combined/complete lighting, diffuse, specular/glossy, indirect, transmission, shadow, environment | A scene lighting/material integrator. AO is geometric visibility, not a complete lightmap.                    |

An expandable note below the map grid lists these families and their requirements. This is not full Substance/Toolbag/Blender parity.

## Material patch mode

The existing **Material patch** mode is separate: it exports six channels from the current procedural material over a flat repeat. It is not a high-to-low mesh bake, does not transfer imported high materials, and does not capture all view-dependent finishes, transmission, scattering or iridescence. The material inspector/export controls remain on the right.

## Recipes, recovery and budgets

- New recipe schema: `alloy.baking-recipe.v2`, settings and source names only; no node graph and no embedded geometry. Mesh ZIP manifest remains `alloy.mesh-bake.v1` with explicit workflow metadata.
- Legacy `alloy.baking-recipe.v1` files load their settings with a visible note that node effects are not used. Before auto-migrating an old browser draft, its original JSON is archived at `alloy-baking-legacy-recipe-v1-backup`. If that backup cannot be written, automatic replacement is blocked to preserve the original.
- Map toggles, presets and settings have per-session undo/redo. An empty map selection is valid in the UI/recipe but cannot launch a worker.
- Meshes stay in the current session. Recipes are not model backups; reimport sources after reload or in another session.
- Per-file import: 30 MB. At most 16 objects, one million combined high triangles, and 150,000 low triangles per UV target.
- Resolution: 64–1024. Working-field budget: 320 MB; batch output buffers: 160 MB. **All 27 maps at 1024 exceed the field budget:** choose 512 or fewer maps. Oversized jobs fail before allocating the fields.
- No FBX/USD, compressed glTF decoder, animation baking, custom cage mesh, UDIM, geometric bevel, material texture transfer, EXR/16-bit maps, painting or full tool parity is claimed.

## Verification

```sh
npm run build
npm run test:baking
npm run test:texture
npm run test:layout
npm run test:runtime
npm run test:standalone
```

Tests cover every map's real evaluator, UV chart palettes, object/world/tangent frames and nonuniform transforms, bent direction under an occluder, separate AO distances, dust direction, signed-curvature masks, hit/coverage distinction, bevel/no-topology/zero bypass, recipe migration/backup, budgets, green toggle tiles, actual 27-map worker/PNG/ZIP output, name-matched batches, cancellation, desktop/tablet right-dock order and phone tabs. Legacy graph API tests are retained, but no graph UI is exposed. Offline tests exercise the embedded worker and portable application without external runtime assets.

## Audited projection and volume SDF

Ray projection selects the closest eligible hit to the original low surface along the bounded normal ray, rejecting high attribute normals facing away from the low target. This avoids another teapot lid/handle surface being mistaken for the target merely because it is closer to the cage origin. A bounded oriented nearest fallback is available for ray misses; `backfacesRejected` and `nearestFallbacks` are recorded. All closest-point results are explicitly distance-checked, including root-leaf BVHs. Imported mirrored transforms use attribute-normal orientation, not triangle winding alone.

AO uses cosine-weighted low-discrepancy hemisphere samples, with rotation derived from world position rather than UV texel indices, reducing chart-to-chart sampling discontinuities. Bevel averaging stays in the low reference hemisphere. Curvature remains a bounded signed local normal-gradient estimate, not a certified measured curvature solver.

A separate **Volume SDF** mode uses full Float32 Euclidean point-to-triangle distances and explicitly chosen sign handling. It is not another 2D texture channel. Strict signs require closed consistently oriented edge-manifold geometry; the standard teapot's 384 boundary edges require explicitly approximate or unsigned output. See [volume format and the 512 quality audit](volume-sdf-and-bake-audit.md).
