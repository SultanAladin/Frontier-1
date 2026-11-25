# Open-World SDF Terrain System — Multi-Repo Synthesis & Architecture

## 1. Baseline Selection Verdict

Across all candidate implementations inspected:

1. **`unassignedinbox/Frontier-OLD@arena/68467568-frontier` (`5e896e57`) — Selected Primary Base ("Main Editor")**:
   - Directly implements the target architecture: **large-scale worlds authored and simulated as small, manageable sub-worlds**.
   - Infinite sparse `256×256` (`1 km × 1 km`) RLE-compressed 2D world masks (`WorldMask2D.js`) paired with independent, toggleable `TerrainRegion` bounds boxes (`TerrainLayerStack.js`), Thin-Slab SDF wafer geometry (`-6.5 m` floor with zero deep-ocean space waste), 3D XYZ gizmo splines, 6-process physical continuum erosion (`ErosionPhysicsEngine.js`) supporting **Solo 1-by-1** OR **Shared-Weather Group** seasonal cycles, 23 geological presets (`ErosionPresets.js`), 25 Gaea data maps + 9 stamps + 8 SatMap ramps (`GaeaTexturingAndStamps.js`), and the companion `SdfSculptEditor` (`RGBA16F` 3D clay + 24 CSG primitives + Dual Surface Nets + `.sdfspace`).
2. **`unassignedinbox/Frontier-OLD@arena/fe92aaa5-frontier` (`d6a791d1`) — Sibling Hydrological Branch**:
   - Contributed **Priority-Flood closed-basin depression filling** for upland hydrostatic lakes, dedicated translucent `WATERBODY_VS/FS` 3D lake/river rendering, and **upstream structural hash invalidation** (`ComputeRegionBaseSignature`) so modifying base layers after baking erosion flags stale erosion cleanly.
3. **`eosclient0001-rgb/Frontier@arena/e59ae44d-frontier` (`318aa288`) & `arena/2b0a3e2b-frontier` (`2827db9f`) — SDF Raymarch & Fast Delta Prototypes**:
   - Contributed **scissored dirty-subrect `O(brush area)` sculpt evaluation** over cached pre-sculpt elevation prefixes, **unlimited 3D volumetric cave & tunnel bore carving**, **automatic dominant painted 2D mask adoption** when boxing new regions, and **debounced `IndexedDB` + `localStorage` workspace autosave**.
4. **`unassignedinbox/Frontier-OLD@arena/bacd63a6-frontier` & `eosclient0001-rgb/Frontier@arena/b89469dc-frontier`**:
   - Older Vite-bundled single-atlas / single-tile rock studios; superseded for open-world multi-region authoring by `68467568`.

---

## 2. Bugs Fixed & Upgrades Integrated into `Experimental/TerrainWorldEditor`

### A. Large-World $\to$ Manageable Sub-World Partitioning & Seamless Borders (`WorldMask2D.js`, `TerrainLayerStack.js`)
- **Fixed Interior Partition Seam Collapse Bug**: Previously, `ComputeRegionMaskSdf2D` treated any `100%`-land interior tile (`InsideCount === Count`) as having coastlines along its own bounding-box border (`SdfMeters = -Math.min(Dx, Dz)`), collapsing interior sub-world borders to sea level (`0 m`). It now evaluates Meijster's exact 2D Euclidean Distance Transform over a **world-continuous padded collar (`>= 480 m`)** outside `Region.bounds` and caches `(coverage, sdf2D, hasLand)` keyed by mask revision + bounds.
- **Locked World-Space Noise Wavelength (`worldFrequencySpan`)**: Prevents fractal wavelength jumps when adjacent sub-world tiles have different widths/depths.
- **1-Click Continent Grid Partitioner (`partitionMaskIntoSubRegions`)**: Splits any large continent/archipelago mask into a clean grid of small `TerrainRegion` sub-worlds (`600 m .. 2600 m`), skipping empty ocean cells and cloning the procedural/spline stack with `seamLock: true`.
- **Seamless Cross-Tile Border Stitching (`StitchAdjacentRegionBorders`)**: Reconciles shared East–West and North–South borders across touching sub-world tiles using $C^1$ Hermite boundary blending, fusing shared seams to `< 2 cm` even after **solo 1-by-1 sculpting and erosion** on individual sub-worlds.
- **Automatic Dominant Mask Adoption (`findDominantMaskInBounds`)**: New region bounds boxes automatically bind to whichever visible 2D mask contains painted land inside the drawn rectangle.

### B. True 3D Volumetric SDF Caves, Smooth Overhangs & Mountain Tunnel Bores (`TerrainLayerStack.js`)
- Removed the `96`-operation truncation cap on `Region.sdf3DOps` and replaced un-booleaned sphere placeholders with a **3D Dual-Surface-Nets volumetric brick extractor** (`BuildVolumetric3DBricks` + `AppendVolumetric3DBricksMesh`).
- Suppresses overlapping 2.5D top quads inside each snapped `[Gx0..Gx1] × [Gz0..Gz1]` volumetric brick footprint while preserving Thin-Slab undersides and perimeter skirts, opening clean mountainside portals into hollow `'cave'` chambers, smooth-blended (`smin`) `'overhang'` arches, and flat-floored arched `'tunnel'` 3D spline bores.

### C. Cached Coast SDF & Scissored Dirty-Rect Sculpting (`TerrainLayerStack.js`)
- Caches `preSculptHeights` right below the active interactive sculpt layer so `ApplyTerrainSculptDab` updates `Rt.heights` strictly inside the touched brush sub-rectangle `[X0..X1, Z0..Z1]` (`Region.dirty = false`) in sub-millisecond `O(brush area)` time, plus adds the `'restore'` brush to recover base procedural relief smoothly.

### D. Priority-Flood Hydrostatic Lakes/Rivers & Stale-Erosion Tracking (`TerrainLayerStack.js`, `ErosionPhysicsEngine.js`, `TerrainOceanViewport.js`)
- `ComputeHydrostaticWaterBodies` runs $O(N \log N)$ Priority-Flood depression filling seeded from ocean/boundary outlets to extract exact hydrostatic spill surfaces for upland closed basins (caldera lakes, glacial tarns) alongside channeled trunk rivers, rendered with translucent Fresnel + sun-glint `WATERBODY_VS/FS` shaders.
- `ComputeRegionBaseSignature` hashes upstream non-erosion layers & mask state so editing base mountains or splines after an erosion bake flags `erosionStale = true` and surfaces a 1-click clean re-bake action in the Simulation tab.

### E. Persistent Workspace Autosave (`TerrainWorldEditor.js`)
- Debounced `IndexedDB` (with `localStorage` fallback) autosave preserves multi-tile worlds, sparse RLE 2D masks, layer stacks, sculpt rasters, and 3D volumetric ops across reloads, while `3 Demo Masks ↗` (or `?example`) resets cleanly to the starter archipelago.

---

## 3. Validation

- `node Experimental/TerrainWorldEditor/TerrainWorldValidation.mjs` — **19/19** suites passing.
- `node Experimental/SdfSculptEditor/SdfSculptValidation.mjs` — **9/9** suites passing.
