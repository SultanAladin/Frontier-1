# SolidArc Native Blender `.blend` Import & Precision CAD Retopology Report

**Scope**: Native C++20 `.blend` binary SDNA decoding, Blender 5.x modifier stack evaluation, zero-CPU-reupload Vulkan 1.3 GPU reference surface rendering, O(log N) BVH precision curve tracing, and SolidArc Outliner/Inspector/File-Dialog UX integration for automotive vehicle retopology.

**Date**: 2026-11-23  
**Repository**: `SultanAladin/Frontier-1` (`arena/fa8c2344-frontier-1`)  
**Validated Vehicle Assets**: `Content/Geometry/Vehicles/Blends/{Egoist,Liger,Quicksilver}.blend`

---

## 1. Architecture Overview

SolidArc now imports native Blender `.blend` files directly inside `FrontierEngine` with zero external runtime dependencies (no Blender executable, no Python interpreter, no Assimp).

| Layer | Files | Responsibility |
| :--- | :--- | :--- |
| **Pinned ZSTD Decompression** | `Automation/Dependencies.json`, `CMakeLists.txt` (`FrontierZstdDecompress`) | Pinned `facebook/zstd` `v1.5.7` (SHA-256 verified, 12 pure-C decompression files, `ZSTD_DISABLE_ASM=1`) for multi-frame ZSTD `.blend` streams; bundled `stb_image` inflate handles legacy GZIP `.blend`; uncompressed streams read directly. |
| **Native `.blend` SDNA Decoder & Modifier Evaluator** | `Engine/Workspaces/ParametricSketcher/Document/SketcherBlendDecoder.{h,cpp}` | Parses both legacy 12-byte (`<= 4.x`) and Blender 5.0+ extended (`BLENDER17-01v0502` + 32-byte `LargeBHead8`) headers, resolves SDNA schemas dynamically, enforces **per-ID local `DATA` pointer scoping** (matching Blender's `readfile.cc`), reads Blender 5.x `AttributeStorage` + hybrid `CustomData` layers (`CD_MDEFORMVERT`, `CD_MVERT_SKIN`), resolves parent transforms & collection visibility, and evaluates dependency-ordered modifier stacks (`Mirror`, `Subsurf` Catmull-Clark with semi-sharp creases, `Solidify`, `Shrinkwrap`, `Mask`, `Array`, `Bevel`, `Skin`, `AutoSmooth`). |
| **Immutable GPU Reference Surface & BVH** | `Engine/Workspaces/ParametricSketcher/Document/SketcherReferenceSurface.{h,cpp}` | Stores evaluated triangles and feature crease segments inside immutable `std::shared_ptr<const SketcherReferenceGeometry>` for O(1) undo/redo clones in `SketcherDocument`, paired with a flat median-split AABB BVH supporting O(log N) Möller-Trumbore ray hits (`IntersectReferenceSurfaces`) and Ericson closest-point projection (`ProjectPointOntoReferenceSurfaces`). |
| **Vulkan 1.3 GPU Rendering** | `Engine/Shaders/Sketcher/SketcherReferenceSurface.slang`, `Engine/Workspaces/ParametricSketcher/Viewport/SketcherRasterization.{h,cpp}` | Uploads vertex corners & crease segments once via staging transfer to `DEVICE_LOCAL` VRAM (`UploadReferenceSurfaces`), updates display controls via uniforms with zero re-upload (`SyncReferenceDisplay`), and draws opaque studio/MatCap/Zebra shading, depth-prepassed ghosted translucency, and anti-aliased screen-space crease lines. |
| **Precision CAD Retopology Snapping** | `Engine/Workspaces/ParametricSketcher/Viewport/SketcherSnapping.cpp` | Competes `IntersectReferenceSurfaces` BVH ray hits against B-rep hits inside `FindSketcherSnap`, automatically enabling `SnapSketcherTrace` and `ProjectSketcherTrace` to project and fit Class-A NURBS curves directly onto imported Blender vehicles. |
| **SolidArc UX & Exchange Integration** | `SketcherDocument.{h,cpp}`, `SketcherExchange.{h,cpp}`, `SketcherFileDialog.{h,cpp}`, `SketcherOutliner.{h,cpp}`, `SketcherInterface.{h,cpp}`, `SketcherWorkspaceHost.cpp` | Registers `SketcherExchangeFormat::Blend` (`.blend`), extends OS file pickers with `BrowseSketcherImportFile`, adds Outliner `REF` category/rows with emerald ink, adds Inspector controls (Visibility, Opacity/Ghost, Creases, Scale mm/cm/m, MatCap, Frame, Remove), and adds direct `Browse…` + 3 vehicle example shortcuts (`Egoist`, `Liger`, `Quicksilver`) in the Import dialog. |

---

## 2. Blender 5.2 Binary Layout Discovery

Visual inspection of the first GPU validation pass revealed a critical Blender 5.0+ persistence behavior: when Blender serializes `AttributeStorage`, `Attribute`, `AttributeArray`, and `ModifierData` blocks as `DATA` records following each ID block (`ME`, `OB`, `GR`, `SC`), temporary allocator addresses are reused across separate ID blocks (`46` colliding `OldAddr` keys in `Egoist.blend`).

Implementing Blender's authentic **two-tier block lookup** (`LocalPayloadsById[OwnerId]` checked first, then `IdBlockByAddr`, then global fallback) recovered 100% of non-empty vehicle surfaces across all three assets and matched Blender's own evaluated triangle counts within `< 0.3%`.

---

## 3. Validation Results

All 5 SolidArc standalone validation binaries and the `FrontierSketcher` desktop binary built and passed with **0 failures** under Linux headless SwiftShader Vulkan (`Build/Linux`):

| Target | Result | Highlights |
| :--- | :--- | :--- |
| `FrontierSketcher` | Built & Linked | Standalone SolidArc desktop executable with ZSTD + 4 new SPIR-V shaders |
| `SketcherKernelValidation` | **Passed=4, Failed=0** | Core B-rep & NURBS kernel unchanged |
| `SketcherDocumentValidation` | **Passed=7, Failed=0** | O(1) `ReferenceSurfaces` undo/redo clone stability |
| `SketcherViewportValidation` | **Passed=8, Failed=0** | `NonZeroIds=67511`, zero regression across all 16 Vulkan pipelines |
| `SketcherWorkspaceValidation` | **Passed=6, Failed=0** | `Tabs=5 Faces=9 Edges=18`, Outliner & Inspector UI clean |
| `SketcherBlendImportValidation` | **Passed=31, Failed=0** | All 3 Google Drive vehicles decoded, rendered on GPU, traced & lofted |

### 3.1 Per-Vehicle Decode & GPU Metrics

| Vehicle Asset | Blender Version | Visible Parts | Evaluated Triangles | Feature Creases | Active Modifiers | Decode + BVH Time | GPU Studio Render (1280x720) |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `Egoist.blend` (1.6 MB) | `5.02` | `44` | `240,068` | `25,051` | `82` | `510 ms` | `681,566 px` · `778 ms` |
| `Liger.blend` (504 KB) | `5.02` | `18` | `269,590` | `15,236` | `36` | `333 ms` | `719,926 px` · `559 ms` |
| `Quicksilver.blend` (602 KB) | `5.02` | `32` | `923,533` | `41,829` | `61` | `2,275 ms` | `676,415 px` · `2,136 ms` |

### 3.2 Precision CAD Retopology Verification (`Egoist`)

- **BVH Ray Snapping (`SnapSketcherTrace`)**: exact surface hits across 240,068 evaluated triangles.
- **NURBS Curve Fitting (`ProjectSketcherTrace`)**:
  - Styling Profile #1: `6 controls -> 5 poles`, worst surface deviation **`0.1118 mm`**
  - Styling Profile #2: `6 controls -> 8 poles`, worst surface deviation **`0.0308 mm`** (`30.8 µm`)
- **Parametric CAD Surface Rebuild (`LoftSketcherCurves`)**: successfully lofted a Class-A NURBS windshield/deck panel directly across the two traced profiles, rendered cleanly over translucent Ghost `Egoist`.

---

## 4. Visual GPU Exhibits

All five 1280x720 headless Vulkan renders are checked into `Exhibits/Images/`:

1. `Exhibits/Images/SolidArc_BlendImport_Egoist_Studio.png` — `Egoist` studio clay/metallic shading with feature creases (`240,068` triangles).
2. `Exhibits/Images/SolidArc_BlendImport_Liger_Studio.png` — `Liger` studio shading with sharkfin, wing, and bilateral mirror symmetry (`269,590` triangles).
3. `Exhibits/Images/SolidArc_BlendImport_Quicksilver_Studio.png` — `Quicksilver` carbon matte body with decoded Blender magenta splitter & blue vent materials (`923,533` triangles).
4. `Exhibits/Images/SolidArc_BlendImport_Egoist_Zebra.png` — Real-time GPU Class-A automotive Zebra reflection stripe check across Catmull-Clark subdivided body panels.
5. `Exhibits/Images/SolidArc_BlendImport_Egoist_RetopologyTrace.png` — Translucent Ghost `Egoist` overlaid with the two sub-millimeter traced NURBS styling curves and the rebuilt lofted SolidArc CAD surface panel.
