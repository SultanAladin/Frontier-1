# Frontier · UV Workspace (`Project-Zero` HTML UI)

De-cluttered 2D + 3D UV Editing, Conformal Unwrapping, Multi-UDIM (`1001+`), Importance-Based Packing, and **Per-Face Multi-MatCap + Fast Lit (EON + Blinn-Phong + PCF Directional Shadows)** workspace styled in the exact **Project-Zero Editor UI** visual language.

## Key Capabilities

0. **Project-Zero UI Architecture & Per-Face Multi-MatCap + Fast Lit 3D Shading (`Engine/UVViewport3D.js`)**
   - **De-cluttered Project-Zero 3-Column Dock**: Full-height `Outliner | Viewport | Inspector` starting at `y = 0` with unrounded trapezoidal tabs, S-curve `"Frontier"` pull-down document notch, inverted `"Sample Assets"` bottom handle, draggable column splitters, `22px`-rounded property cards, `26px` capsule sliders, and radial-glow quick-action tiles.
   - **Per-Face Multi-MatCap (`0..7` Simultaneous Studio MatCaps on One Surface)**: Assign any of 8 studio MatCaps (`Sculpt Clay`, `Satin Pearl`, `Red Sculpt Wax`, `Celadon Jade`, `Brushed Gold`, `Anodized Cobalt`, `Obsidian Rim`, `Solar Carpaint`) to any single face, multi-face selection, or UV chart via `0.02 ms` GPU `uFaceStateTex` B-channel updates (with Undo/Redo support and right-click quick strip).
   - **Fast Lit Renderer**: Portsmouth, Kutz & Hill (2024) **Energy-Preserving Oren-Nayar (EON)** rough diffuse + energy-conserving normalized **Blinn-Phong** specular + `2048×2048` orthographic directional **9-tap PCF shadows** + ground contact shadow receiver.

1. **Importance-Based Space Allocation Packer (`Engine/ImportancePackingSolver.js`)**
   - Select any subset of UV islands (e.g. 5 hero islands) and assign a target UV space share (e.g. `70%`).
   - Select another subset (e.g. 10 secondary islands) and assign `10%`.
   - Remaining unassigned islands automatically calculate and fill the remaining space (`20%`), or greedily expand into unoccupied tile pockets (`Fill Pockets` mode).
   - Rasterized exact-shape concave occupancy masks (`Uint8Array` row-interval checks + circular margin dilation) allow smaller islands to nest cleanly inside concavities and empty pockets with zero overlaps.

2. **Complete Blender UV Editing Toolset & Hotkeys**
   - **Selection Modes (`1`–`4`)**: Vertex, Edge, Face, Island + synchronized 2D↔3D picking & highlighting + Outliner tree selection.
   - **Blender Edge, Boundary & Seam Tools (`2` Edge Mode / `K` Seam Brush / `Ctrl+E` Menu)**:
     - **3D & 2D Edge Picking & Hover Highlights**: Direct edge click, `Shift+Click` additive/toggle, `Alt+Click` / Double-Click **Edge Loop**, `Ctrl+Alt+Click` **Edge Ring**, `Ctrl+Click` **Shortest Edge Path** (geodesic 3D & 2D), `Ctrl++` / `Ctrl+-` **Grow / Shrink Selection**.
     - **Sharp & Boundary Selection**: `Select Sharp Edges` (configurable dihedral angle threshold °), `Select Boundary Loop` (face-selection perimeter / open mesh rims / island borders), and `Select All Seam Edges`.
     - **Seam Marking & Healing**: Dedicated **Seam Brush (`K`)** (click single edge, `Alt+Click` whole seam loop in 3D or 2D), `Mark Seam`, `Clear Seam`, `Clear Seam & Weld`, `Mark Boundaries as Seams`, `Mark Sharp Edges as Seams`, `Seams from Islands`, `Clear All Seams`, and automatic seam-healing across cleared seams on `Unwrap (U)`.
   - **Modal Transforms (`G` / `R` / `S`)**: Live drag, `X`/`U` and `Y`/`V` axis locks, direct numeric typing (`R 4 5 Enter`), pivot modes (`.`: Median, Bounds, 2D Cursor, Individual), snapping (`Shift+Tab`: Off, Grid, Pixel, Vertex), and Proportional Editing (`O` + scroll wheel radius).
   - **Selection Tools**: Box Select (`B`), Circle Select (`C`), Select All (`A`), Deselect (`Alt+A`), Invert (`Ctrl+I`), Select Linked (`L`).
   - **Unwrap & Topology Solvers (`Engine/UnwrapSolver.js`, `Engine/TopologySurface.js`)**: Sparse PCG LSCM Conformal Unwrap (`U`) honoring seams & pins (`P` / `Alt+P`), Smart UV Project, Cube / Cylinder / Sphere / View / Reset projections, Follow Active Quads, Flip-guarded Minimize Stretch (`Ctrl+V`), Average Island Scale (`Ctrl+A`), Stitch (`V`), Weld/Merge (`M`), Split Island (`Y`), Mirror (`Ctrl+M` + `X`/`Y`), Align / Straighten / Rectify Quads, and full Undo/Redo (`Ctrl+Z` / `Ctrl+Shift+Z`).

3. **Full UDIM Tiles & Multi-Resolution Workflow (`1001+`)**
   - Mari/Blender UDIM coordinate mapping (`1001 + u + v*10`) with visual 4×3 UDIM Matrix Inspector.
   - Per-UDIM texture resolutions (`512px`–`8K`), custom image backdrops, per-UDIM checker/grid styles, moving selected islands between tiles, cross-UDIM mixed-resolution texel density equalization, per-UDIM packing, and Auto-UDIM overflow spillover.

4. **Synchronized 2D UV Canvas & 3D WebGL2 Viewport**
   - Real-time Three.js mesh preview (`Vendor/three.esm.min.js`) with **UV Checker**, **Flat Shaded**, **Matcap Clay**, **Matcap Satin**, **Importance**, **Texel Density**, and **UV Stretch** shading (`Z` shortcut or 3D toolbar pills), plus bold 3D selection overlays (golden face highlight, thick 3D edge tubes, active edge highlight, round vertex markers, wireframe, and seam tubes).
   - Wavefront `.OBJ` import & export (preserving multi-UDIM UVs), SVG layout wireframe export, and Importance Budget JSON export.

## Running Locally & Validation

```bash
# Run headless verification suite (Node v22+):
node Experimental/UVEditor/UVValidation.mjs

# Launch live preview server on 0.0.0.0:8085:
python3 Experimental/UVEditor/Serve.py 8085
```
