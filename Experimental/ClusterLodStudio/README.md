# Cluster LOD Studio (`Experimental/ClusterLodStudio`)

> A Nanite-style cluster hierarchy with view-dependent LOD selection, built from scratch in plain JavaScript and
> rasterised with WebGL2. No dependencies, no build step. UI follows `Experimental/ProjectZeroEditor`
> (Outliner · Viewport · Inspector docks, trapezoid tabs, top notch, bottom drawer).

![Cluster LOD Studio](Images/ClusterLodStudioOverview.png)

## Run

```bash
python3 Experimental/ClusterLodStudio/Serve.py 8093      # then open http://localhost:8093/
node Experimental/ClusterLodStudio/Scripts/CheckClusterHierarchy.mjs
```

URL parameters for repeatable captures: `?preset=vase|braid|torus|knot|asteroid|planet|terrain&budget=200000&side=5&mode=levels&error=1&wire&drawer`.

## Geometry Editor

`Open Geometry Editor` in the Inspector's GEOMETRY section (or the editor's own controls) transforms the current hero
source before the cluster hierarchy is built: scale (uniform or per axis), centre (keep, origin or floor), mirror X/Y/Z,
rotate in degrees, and flip normals. The preview runs on the main thread from an untransformed copy of the source, so
nothing rebuilds until **Apply and rebuild hero**. A mirror reverses triangle winding so normals stay outward. The
import area lists OBJ, glTF, GLB, STL, PLY and FBX, but the file reader is not connected in this build.

Planned Hero Workspace and SDF Baker work is tracked in `Documentation/Plans/ClusterLodStudioHeroWorkspace.md`.

## Hero Workspace

The Outliner's tree is the hero's workspace list: **Metadata**, **Source**, **Geometry**, **Icon**, **UV**, **SDF**
(closed surfaces only) and **Cluster LOD**. Selecting a part scrolls the right-hand Inspector to its section, which holds
the controls for that part, in this order: METADATA, SOURCE, ICON, UV LAYOUT, GEOMETRY, then CLUSTER LOD (preview, LOD
selection, hierarchy levels, build). UV and Geometry also open their overlays, Icon turns on the icon camera, and SDF
takes over the centre pane. Cluster LOD is the default and keeps the viewport.

- **Metadata**: name, category, tags and info, edited in the Inspector and saved in this browser per source. The name replaces the hero title.
- **SDF**: bakes a signed distance field on a worker at 32–64 cubed. The narrow band is 2 voxels, and the sign comes from
  crossing parity along +X, so open meshes get an approximate sign. The Slice view shows one Z plane. The Raymarch view
  sphere-traces the field on the CPU. The iso offset is a percentage of hero size. Preview only: the field does not feed
  the renderer.
- **Cluster preview** (Inspector): one LOD level of the built hierarchy, drawn in 3D with one colour per cluster. The level
  slider picks the level, and the triangle and cluster counts match the hierarchy exactly.

## Icons and the UV Workspace

- **Icon camera** (`I`, or the **Icon** tool): a square camera box opens over the viewport at a default three-quarter
  angle, fitted to the centre instance. Orbit, zoom and pan as usual; the box is the exact crop of the saved PNG. `P` or
  **Capture** renders that one instance on a transparent background at 256, 512 or 1024 px, shaded or as the live view.
  The result shows in the Inspector, can be downloaded or copied, and **Save as icon** stores a 256 px copy in this
  browser. Saved icons replace the Outliner thumbnail for their source.
- **UV Workspace** (`U`, or **Open UV Workspace** in the Inspector's UV LAYOUT section): a full-screen UV editor for the
  current geometry, including imported OBJ models. Islands come from a planar unwrap computed in the browser: triangles
  are flood-filled across shared edges while their normals stay within 60° of the seed, each island is projected onto
  its average plane, and islands smaller than 24 triangles merge into the neighbour they share the most edges with when
  that neighbour's plane is within 75°. Click selects an island, Shift-click selects a range, Ctrl/Cmd-click adds or
  removes one, and `A` selects all. Drag selected islands to move them as a group; `R` rotates, `M` mirrors, `P` packs all
  at one texel density, `Esc` closes. Export writes OBJ with one texture coordinate per triangle corner (round-trips
  through Import OBJ) or an SVG layout sheet with seams in amber.

The chart unwrap is a projection, not a least-squares conformal unwrap: charts that bend far from their plane are
foreshortened, which the chart list reports as *projection fit*. The 3D view does not sample these UVs yet.

## How it works

| Step | File | What happens |
| :--- | :--- | :--- |
| Source | `Engine/SourceGeometry.js` | Welded procedural assets (thin-walled fluted vase with a real interior, (3,7) braided knot, carved torus, trefoil knot, asteroid, planet, open terrain) or an imported OBJ (positions + faces, welded by position). |
| Cluster | `Engine/ClusterHierarchy.js` | Triangles are split into clusters of ≤ 128 by median bisection of centroids. |
| Group | `Engine/ClusterHierarchy.js` | ~4 neighbouring clusters are grouped by greedily maximising shared border edges. |
| Simplify | `Engine/QuadricSimplification.js` | The group's outer border is locked; the interior is reduced to ~50 % by quadric edge collapse onto existing vertices. |
| Recurse | `Engine/ClusterHierarchy.js` | The result is split into new clusters (next level) until one cluster remains or simplification stalls. |
| Select | `Engine/ClusterSelection.js` | Per frame, per instance: walk the DAG from the roots; construct a cluster when its own projected error ≤ threshold and its parent's is above it. Frustum culling (hierarchical) and normal-cone culling run in the same walk. |
| Rasterise | `Engine/ViewportRasterization.js` | One static corner stream for all levels; selected ranges go out through `WEBGL_multi_draw` (range loop fallback). Positions are pulled from a float texture, so every level shares one vertex set. |

Why cuts are crack-free: every group stores one `(error, sphere)` pair that is the *own* error of the clusters built
from it and the *parent* error of the clusters it consumed, so both sides of a group border always decide the same way;
border vertices are locked, so any mix of levels meets exactly. Error is forced monotone and spheres nested up the DAG.

## Controls

| Input | Effect |
| :--- | :--- |
| Drag · Right-drag / Shift-drag · Wheel / pinch | Orbit · Pan · Zoom |
| `W A S D` `Q E` (+ Shift) | Fly |
| Click · Double-click | Pick a cluster into the Inspector · Orbit around it |
| `1`–`6` | Shaded · Clusters · Groups · LOD level · Triangles · Error |
| `[` `]` | Pixel error threshold |
| `L` | Freeze LOD + culling at the current camera, then fly out to inspect the cut and the frustum |
| `Z` `B` `G` `C` | Wireframe · Picked bounds · Grid · Cone culling |
| `F` · `Esc` | Fit everything · Clear pick (Esc also leaves the icon camera) |
| `I` · `P` | Icon camera · Capture icon |
| `U` | UV Workspace (island layout) |

Inspector: icon capture and saved icons, the static UV layout, pixel error, forced level, culling toggles, instance grid, build parameters (cluster size, group size,
keep ratio, source density), and for the picked cluster its level, triangle count, own/parent error in units and
pixels, and the sentence explaining why it was chosen. The bottom **Hierarchy** drawer charts stored vs. constructed
triangles per level and frame time.

## Validation

`Scripts/CheckClusterHierarchy.mjs` builds every preset and asserts: cluster size limit, monotone error, nested
spheres, every level watertight, and for 60 random cameras per asset that the DAG walk equals a brute-force oracle and
the resulting cut is watertight (no open edges, no edges with 3+ triangles). It also covers OBJ welding and a deep
hierarchy with small clusters. For the UV layout it checks that charts partition the triangles, every UV lies in the
unit square, packed boxes do not overlap, every chart has one texel density, rotate + mirror keep chart area, a cube
unwraps to six flat charts, and the OBJ and SVG exports carry the expected counts.

## Limits (stated plainly)

- Error is a quadric RMS-distance estimate in object units, not a certified Hausdorff bound.
- Simplification is position-only: no UV / normal seams, no attribute error. Coarse levels reuse source vertex normals.
- UV charts are projection-based and not optimised for distortion; the 3D view does not show them. Many 1–10 triangle
  charts appear on strongly curved parts, and they are packed as thin slivers.
- Icons are captured from the browser's WebGL2 canvas, so their quality follows the GPU; saved icons are capped at 256 px
  to fit browser storage.
- Clusters come from spatial bisection, not graph partitioning (METIS), so a cluster can be disconnected on thin or
  folded shapes. Correctness does not depend on it; cluster quality does.
- No occlusion culling (HZB), no streaming, no software / compute rasteriser, no visibility buffer. Selection runs on
  the CPU; WebGPU is not used.
- Picking supports up to 254 instances (the grid tops out at 15 × 15).

Fonts: DM Sans, SIL Open Font License (`Fonts/OFL.txt`).
