# Frontier Baking, Hardware Telemetry, Cluster LOD, Icon Capture & TextureStudio UI Plan

> **TL;DR:** Branches `unassignedinbox/Frontier` (`arena/429d7dc7-frontier` @ `967b92c`) and `c7egoist/Frontier` (`arena/36d6f048-frontier` @ `34f384a`) are cleanly merged onto `arena/6eeea758-frontier` (`0` conflicts), all 4 JS validation suites pass, and the `And` ban is enforced in `CLAUDE.md` (§2.7). **Confirmed locks:** `TextureBaker` + `SdfBaker` + `GeometryRayAccelerator` (§3), `Engine/Configuration/HardwareProfile` + `ResourceBudgetRegister` (§4), and sparse `8×8×8` narrow-band SDF bricks `Mip0` + conservative `Mip1` + world `Clip0..2` + brick eviction (§6.3). **New decision in §7:** Create ONE dedicated **`GeometryCompiler` Workspace** (built on **`Experimental/ProjectZeroEditor/`** UI, hosted in existing reserved slot `Engine/Workspaces/Geometry/`) for **SDF + Cluster LOD (Nanite-like + Glass-Safe) + Dual-Contour SDF-to-Mesh + Fracture + Asset Import + Icon Capture**, keep **`Texturing → Baking`** focused on 2D UV PBR maps, and keep headless C++ solvers inside canonical engine folders (never `GeometryProcessing`, since `Process`/`Processor` is barred).

**Status:** 🏁 Confirmed Core Architecture + §7 Workspace Decision Ready (Zero C++ / JS / Slang Code Edits This Turn) · **Date:** `2026-11-14`  
**Branch:** `arena/6eeea758-frontier` · **Governance Updated:** `CLAUDE.md` (§2.7), `Documentation/AgenticInstructions/SKILL-Naming.md` (Gate ⑦), `Documentation/BannedWordsVocabulary.md`

---

## 0. 🏁 Confirmed Architectural Locks

| Decision Domain | Confirmed Choice | Status |
| :--- | :--- | :---: |
| **Baker Split (§3)** | **🏆 🥇 Option A** — `TextureBaker.{h,cpp}` + `SdfBaker.{h,cpp}` sharing one `GeometryRayAccelerator.{h,cpp}` | 🟢 Confirmed |
| **Hardware & Budgets (§4)** | **🏆 🥇 Option A** — Dedicated `Engine/Configuration/HardwareProfile.{h,cpp}` + `ResourceBudgetRegister.{h,cpp}` | 🟢 Confirmed |
| **SDF Multi-Scale (§6.3)** | **🏆 🥇 Option A** — Sparse `8×8×8` narrow-band bricks (`Mip0`) + conservative `Mip1` + world `Clip0..2` + VRAM brick eviction | 🟢 Confirmed |
| **Workspace & Folder Ownership (§7)** | **Pending your pick:** Standalone `GeometryCompiler` (`ProjectZeroEditor` UI) vs `Texturing` vs Engine Core layout (see §7 decision tables) | 🚧 Open Question |

---

## 1. 🏁 Merge & Baseline Verification Ledger

| Source Repository & Branch | Exact Pinned Commit | Merge Result | Validation Suite | Result |
| :--- | :--- | :---: | :--- | :---: |
| `unassignedinbox/Frontier` (`arena/429d7dc7-frontier`) | `967b92c` *(Particle + ForceFields + 4-Tab VectorField Studio + Streamlines/Heatmap/Bokeh)* | 🟢 Fast-forward | `WorldMirrorValidation.mjs` · `VectorFieldValidation.mjs` · `CheckForceFields.mjs` | 🟢 `14/14` · `18/18` · `65/65` |
| `c7egoist/Frontier` (`arena/36d6f048-frontier`) | `34f384a` *(Pure-GPU Baking Studio: GTX BVH4 / RTX RayQuery + 3D Volume SDF)* | 🟢 Clean `ort` merge (`0` conflicts) | `Experimental/TextureStudio/TextureStudioValidation.mjs` | 🟢 `27/27` suites |

---

## 2. 🏷️ Naming Governance (`And` Ban) & `34f384a` Remediation Map

`CLAUDE.md` §2.7, `SKILL-Naming.md` Gate ⑦, and `BannedWordsVocabulary.md` now strictly ban the conjunction `And` inside filenames, types, functions, fields, shaders, and plans. When C++ refactoring starts, these exact `34f384a` symbols will be renamed:

| Current `34f384a` Symbol / File | Violation | Approved Refactor Target |
| :--- | :--- | :--- |
| `GpuTextureAndSdfBaker.{h,cpp}` | 🔴 Conjunction `And` + conflated surface/volume monolith (`1,552` LOC) | `TextureBaker.{h,cpp}` + `SdfBaker.{h,cpp}` |
| `StudyGeometryBuilder.{h,cpp}` | 🚩 Vague name + conflates presets, duplicate OBJ/STL parser, and GPU packing | `SpecimenGeometryLibrary.{h,cpp}` + `GeometryInterchange.{h,cpp}` + `SurfaceBakeInputPacker.{h,cpp}` |
| `StudyGeometryKind` | 🔴 Banned `Kind` | `SpecimenCategory` |
| `BakeAccelerationBackend` / `ExecutedBackend` | 🔴 Banned `Backend` | `BakeAccelerationTier` / `ExecutedTier` |
| `BuildAndUploadStudy` | 🔴 Conjunction `And` | `UploadGeometryPair` |
| `CachedSurfaceChannelsRgba8` / `CachedSectionRgba8` | 🔴 Banned `Cache` | `ReadbackChannelsRgba8` / `ReadbackSectionRgba8` |
| `CameraEyeAndMode`, `OverlayAndFlags`, `ViewportSizeAndTime`, `BoundsMinAndPad`, `BoundsMaxAndPad` | 🔴 Conjunction `And` + banned `Flags` | `CameraEyeMode`, `OverlayDirectives`, `ViewportExtentTime`, `BoundsMinExtent`, `BoundsMaxExtent` |
| `BakerTargetGroup::VolumeAndDistance` / `MasksAndIds` | 🔴 Conjunction `And` | `VolumetricDistance` / `RegionIdentifiers` |
| `"Baking Quick Menu"` / `"Compute Backend"` (UI labels) | 🔴 Banned `Menu` & `Backend` | `"Baking Quick Strip"` / `"Ray Execution Tier"` |

---

## 3. 🏆 Baker Architecture Decision: Split `TextureBaker` + `SdfBaker` vs Unified `GpuTextureBaker` *(🟢 Confirmed — Option A)*

> **Legend:** Reuse / Separation / VRAM Efficiency: **🔴 = Highest (Best)** · File Bloat / Coupling Cost: **✔️ = Lowest (Best)** · Quality: **⭐** (more = better).

| Option | Engine Reuse Without UV Overhead | VRAM Isolation | Compile / File Maintainability | Naming Compliance | Rating |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **🏆 🥇 Option A — Two Separate Bakers (`TextureBaker` + `SdfBaker`) Sharing One `GeometryRayAccelerator`** | 🔴 **GI, Particles, Fracture & Asset Import call `SdfBaker` alone (`0` UV/2D targets)** | 🔴 **Allocates only 2D arrays OR 3D voxels on demand** | ✔️ **Two clean `~550–700` LOC files** | ✔️ **Zero `And`, single responsibility** | ⭐⭐⭐⭐⭐ |
| **🥈 Option B — Unified `GpuTextureBaker` Containing Both Surface Maps & 3D SDF** | 🚩 **SDF-only callers drag in UV G-buffer rasterizers & 17-channel arrays** | 🚩 **Mixed surface/volume descriptor layouts & lifecycle** | 🔴 **`1,550+` LOC monolith** | ✔️ **Zero `And`, but misleading name (`Texture` hides 3D SDF)** | ⭐⭐⭐ |

**Recommendation — Adopt 🏆 🥇 Option A (`TextureBaker` + `SdfBaker` + shared `GeometryRayAccelerator`):**
- **Zero Duplicate Acceleration Builds:** `GeometryRayAccelerator.{h,cpp}` uploads a `PolygonNetwork` once into `WideBoundingTree` (GTX `BVH4`) + optional `VkAccelerationStructureKHR` (RTX `BLAS`/`TLAS`). Both `TextureBaker` and `SdfBaker` borrow a `const GeometryRayAccelerator&` at zero extra VRAM.
- **Independent Engine Reuse:**
  - `TextureBaker` requires a paired Low-UV `PolygonNetwork` + High `GeometryRayAccelerator` and runs `BakeLowUvRasterization` → `BakeSurfaceTransferCompute` → `BakeChannelResolveCompute` → `BakeDilationCompute`.
  - `SdfBaker` requires **only** a `GeometryRayAccelerator` (no low-poly target, no UVs, no gutter dilation) and outputs either dense `.fvol` grids (Baking Studio) or sparse `8×8×8` narrow-band bricks (`Engine/DistanceFieldSpace/SignedDistanceVolume`) directly consumable by World GI, Particles, Fracture, and the future Asset Import Editor.

### Reusable Headless Baking Core Across Frontier

```text
                      ┌───────────────────────────────────────────────────────────┐
                      │   Engine/GeometricRaster/RayAcceleration/                 │
                      │   GeometryRayAccelerator (GTX BVH4 + Optional RTX TLAS)   │
                      └─────────────┬───────────────────────────────┬─────────────┘
                                    │                               │
              ┌─────────────────────▼───────────┐     ┌─────────────▼──────────────────────┐
              │ Engine/GeometricRaster/Baking/  │     │ Engine/GeometricRaster/Baking/     │
              │ TextureBaker (17 UV PBR Maps)   │     │ SdfBaker (3D Dense & Sparse Bricks)│
              └──────────┬──────────┬───────────┘     └──┬──────────┬──────────┬───────────┘
                         │          │                    │          │          │
                         ▼          ▼                    ▼          ▼          ▼
                  Baking Studio  Texture Paint     Baking Studio  World GI  Particles / Fracture
                  (Workspace)    (Live Maps)       (Slice View)   (Clip0-2) (SDF Collisions)
                                                                       ▲
         ┌─────────────────────────────────────────────────────────────┴───────────────────┐
         │ Future: GeometryCompileWorkspaceHost + GeometrySpecimenRasterizer + ClusterLod  │
         │ (Import -> Repair -> UV -> Material -> SdfBaker -> ClusterLodBaker -> Icon)     │
         └─────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. 🔍 Hardware Capability Inventory & Starvation-Safe Budgets (`Engine/Configuration/`) *(🟢 Confirmed — Option A)*

`HardwareRayQueryInterchange` should **not** live under `Baking/`. Hardware probing is currently fragmented across 3 places (`VulkanInterchange` for ray-query, `AssetStreamThread::DetectStorageHardwareTier` for SSD/HDD, and `EngineConfiguration::DeriveOptimalChannelLanes` for CPU threads), and **VRAM / System RAM budgets are not probed at all**.

> **Legend:** Cross-Domain Access / Starvation Safety: **🔴 = Highest (Best)** · Layering Violation / Coupling: **✔️ = Lowest (Best)** · Quality: **⭐** (more = better).

| Placement Option | Access by Particles / GI / Streaming / Baking | Clean Layer Separation | Live Dynamic VRAM/RAM Refresh | Rating |
| :--- | :---: | :---: | :---: | :---: |
| **🏆 🥇 Option A — Dedicated `Engine/Configuration/HardwareProfile.{h,cpp}` + `ResourceBudgetRegister.{h,cpp}`** | 🔴 **Every domain reads one canonical hardware snapshot & budget guard** | ✔️ **Zero circular dependency on Baking or UI** | 🔴 **Polls `VK_EXT_memory_budget` + OS RAM dynamically** | ⭐⭐⭐⭐⭐ |
| **🥈 Option B — Fold Directly Into `EngineConfiguration.{h,cpp}`** | 🚩 **Mixes user launch flags (`--workspace=UV`) with live OS/Vulkan telemetry** | 🚩 **Bloats startup config header with Vulkan/Win32/sysfs hooks** | 🚩 **Static startup snapshot only** | ⭐⭐⭐ |
| **🥉 Option C — Keep Under `GeometricRaster/Baking/` or `VulkanInterchange`** | 🚩 **Particles & Streaming forced to depend on Baking or raw Vulkan internals** | 🔴 **Wrong domain ownership for RAM & SSD/HDD** | 🚩 **Misses CPU/RAM/Disk** | ⭐⭐ |

### 4.1 `HardwareProfile` — Unified Cross-Platform Probe (`Windows · Linux · Android`)

| Capability Domain | Probed Metrics | Cross-Platform Mechanism |
| :--- | :--- | :--- |
| **GPU Ray Tier** | `SoftwareFallback` (SwiftShader) · `ComputeBvh4` (GTX) · `InlineRayQuery` (`VK_KHR_ray_query`) · `DedicatedRayTracing` (`VK_KHR_ray_tracing_pipeline` + SER) · Subgroup width · Scratch alignment | Queried once via `VulkanInterchange` physical device features2/properties2 and written into `HardwareProfile`. |
| **GPU VRAM Budget** | `DeviceLocalTotalBytes`, `DeviceLocalBudgetBytes`, `DeviceLocalUsageBytes`, `UnifiedMemoryArchitecture` (iGPU/Android) | `VK_EXT_memory_budget` (`VkPhysicalDeviceMemoryBudgetPropertiesEXT`); fallback to `VkPhysicalDeviceMemoryProperties` × `0.80` safety ceiling + Win32 `IDXGIAdapter3::QueryVideoMemoryInfo`. |
| **System RAM** | `PhysicalRamTotalBytes`, `PhysicalRamAvailableBytes` | Win32 `GlobalMemoryStatusEx` · Linux `/proc/meminfo` (`MemTotal` / `MemAvailable`) · Android `sysconf` + `/proc/meminfo`. |
| **Storage Media Tier** | `NvmeFlash` · `SataFlash` · `RotationalDisk` (promoted out of `AssetStreamThread::DetectStorageHardwareTier`) + per-mount lookup (`ProjectRoot` vs `EngineContentRoot` can sit on different drives!) | Linux `/sys/dev/block/<maj>:<min>/queue/rotational` (+ `nvme*` transport check) · Win32 `IOCTL_STORAGE_QUERY_PROPERTY` (`StorageDeviceSeekPenaltyProperty`) · Android flash default. |
| **CPU & SIMD Tier** | Physical/logical core count · `Scalar` / `Sse42` / `Avx2` / `NeonArm64` | `std::thread::hardware_concurrency` + compile/CPUID/auxval feature checks. |

### 4.2 `ResourceBudgetRegister` — Starvation Guards (Preventing Particle / Bake OOM Crashes)

- **Hard Global Reserve:** Never allocate beyond `85%` of `DeviceLocalBudgetBytes` (or `60%` of shared RAM on unified-memory Android/iGPU devices); reserve `15%` headroom for swapchain, OS compositor, and driver scratch.
- **Per-Domain Soft/Hard Quotas (`BudgetDomain`):** `WorldRender`, `Particles`, `BakingScratch`, `SdfVolumes`, `ClusterStreaming`, `TextureResident`, `RevisionHistory`.
- **Anti-Starvation Enforcement Rules:**
  1. **Particles (`SimulationSpace::Particles`):** Before spawning high-capacity presets (`100k–500k` particles + 20-node trails = `~40–200 MiB` SSBO), `ParticleSimulationHost` queries `ResourceBudgetRegister::RequestAllocation(BudgetDomain::Particles, Bytes)`. If over ceiling, it gracefully scales particle capacity/trail count with an Inspector telemetry badge instead of starving or triggering `VK_ERROR_OUT_OF_DEVICE_MEMORY`.
  2. **Transient Baking Protection:** `TextureBaker` (`2048² × 17` RGBA8 ≈ `272 MiB` ping-pong = `544 MiB`) and `SdfBaker` (`128³` FP32 = `8 MiB`, `256³` = `64 MiB`) automatically switch to **tiled/chunked channel dispatch** or clamp resolution when `BakingScratch` would encroach on active `Particles` or `WorldRender` VRAM.
  3. **Storage-Aware Streaming Budgets:** On `RotationalDisk` (HDD), `ClusterStreaming` and `ThumbnailSheetRepository` increase RAM read-ahead cache (`2 MiB` C-SCAN coalescing, `0` backward seeks) while on `NvmeFlash` they keep RAM footprint tight with direct `4 KiB` scatter reads.

---

## 5. 🧩 3D Object Icon Baking & Future Dedicated Asset Import Editor Plan

### 5.1 Current Gap Audit (`C++` vs HTML `FrontierWorldMirror`)

| Asset Category | HTML Reference (`Experimental/`) | Current C++ (`Engine/`) | Gap Status |
| :--- | :--- | :--- | :---: |
| **Materials** | `MaterialIconBaker.js` (transparent PBR shader ball) | `Engine/MaterialSpace/Baking/MaterialSpecimenRasterizer.{h,cpp}` + MatCap slots | 🟢 Present |
| **Colours / Vectors / Textures / UV** | Direct swatch / SVG / tile rendering | `ThumbnailSheetRepository.cpp` + `VectorCanvasRasterizer` | 🟢 Present |
| **3D Objects (`ContentCategory::Geometry`)** | `FrontierWorldMirror/ModelThumbnailRenderer.js` (`RenderAnalyticalThumbnailRgba`: 3-point OpenPBR raytracer, **100% transparent `Alpha=0` background**, tight AABB auto-fit, `FitSilhouetteRgba`) | `ThumbnailSheetRepository.cpp:135-160` draws a flat **3-face 2D hexagon stub** | 🔴 **Missing — Only category without real icon baking** |

### 5.2 Phase 1 (Immediate C++ Port): Auto-Bake Missing 3D Object Icons
1. **New Unit — `Engine/GeometricRaster/Baking/GeometrySpecimenRasterizer.{h,cpp}`** *(mirrors existing `MaterialSpecimenRasterizer.{h,cpp}`)*:
   - Accepts any `PolygonNetwork` (plus optional bound `SurfaceMaterial` slots & per-face colours) or analytic prop descriptor.
   - Computes tight Right-Hand `+Z Up` bounding extent, positions a calibrated `45 mm` 3/4 studio camera (`ViewOffsetDir = [-0.54, -0.76, 0.37]`, auto-fit distance `1.18 * Radius / tan(Fov/2)`), and renders with 3-point neutral studio lighting onto a **100% transparent background (`Alpha = 0`)**.
   - Runs premultiplied-alpha silhouette tight-crop resampling (`Margin = 6%`, matching `ModelThumbnailRenderer.js::FitSilhouetteRgba`) into exact `128×128` RGBA8 (`ThumbnailSheetRepository::TileDimensionPixels`).
   - **Dual Execution Path:** Vulkan GPU offscreen rasterization when `VulkanInterchange` is ready, with deterministic CPU `WideBoundingTree` raycast fallback for headless `Exhibits/` tests and zero-GPU CI (`0` excuses per `CLAUDE.md` §2.5).
2. **Automatic Lazy Bake Trigger in `ThumbnailSheetRepository`:**
   - When registering or indexing any `ContentCategory::Geometry` entry (`.GeometrySpace` or imported `.obj`/`.gltf`/`.stl`), check whether a valid pre-baked `128×128` icon already exists in `ThumbnailSheet.ImageSpace` or inside the `.GeometrySpace` binary icon section.
   - **If missing → auto-bake once** asynchronously on `WorkspaceThread` / `ExecutionChannels`, stamp into `ThumbnailSheetRepository::UpdateEntryThumbnailRgba(...)`, and persist to `ThumbnailSheet.ImageSpace` so future sessions load in `< 2 ms` with zero re-baking.

### 5.3 Phase 2 (Saved Future Plan): Dedicated 3D Asset Import Editor *(Unified With §7 `GeometryCompiler`)*

When the user imports a 3D asset (drag-drop or `Import` button), instead of immediately writing a raw `.GeometrySpace` after quarantine, Frontier opens **`GeometryCompileWorkspaceHost`** (`Engine/Workspaces/Geometry/` — see §7!) in Import mode:

```text
[OS Drop / Import .obj .gltf .glb .stl]
        │
        ▼  Step 1: Quarantine (Saved/ImportIntake/) + GeometryInterchange Parse
┌──────────────────────────────────────────────────────────────────────────────────┐
│                        ASSET IMPORT EDITOR (Live 3D Stage)                       │
│  [1] Geometry Repair   — Unit scale -> meters, RHR +Z Up, watertight weld, N/T   │
│  [2] UV Authoring      — Keep existing UVs OR auto-Seam + LSCM + UDIM Pack       │
│  [3] Material Binding  — Map source slots to 10-lobe MaterialSpace presets       │
│  [4] SDF Bake (opt.)   — Calls SdfBaker -> sparse SignedDistanceVolume section   │
│  [5] Cluster LOD (opt.)— Calls ClusterLodBaker (UV-locked + Glass-safe pyramid)  │
│  [6] Fracture (opt.)   — Pre-partition Voronoi/convex shards + interior UVs      │
│  [7] Final Icon Shot   — User can orbit camera or accept Auto-Fit -> captures    │
│                          transparent 128x128 RGBA8 icon via SpecimenRasterizer   │
└───────────────────────────────────────┬──────────────────────────────────────────┘
                                        ▼  Click [Finalize Import]
   Writes unified .GeometrySpace (Network + UVs + Materials + SDF + Clusters +
   Fracture + Pre-baked 128x128 Icon) -> Registers directly in ContentBrowser!
```

---

## 6. 🚧 Deconstructing `StudyGeometryBuilder`, Cluster LOD (Nanite-Like + Glass-Safe) & SDF LOD Verdict

### 6.1 Replacing `StudyGeometryBuilder.{h,cpp}` (Zero Duplication, Clear Ownership)

| Current Responsibility Inside `StudyGeometryBuilder.{h,cpp}` | Defect | Target Replacement |
| :--- | :--- | :--- |
| Procedural preview shapes (`UtahTeapot`, `ShaderBall`, `ChamferBox`, `UvSphere`, `TorusKnot`) | Trapped inside `Baking/`; Material, Paint & UV cannot reuse | **`Engine/GeometrySpace/Primitives/SpecimenGeometryLibrary.{h,cpp}`** (`SpecimenCategory` enum) |
| Custom `.OBJ` / `.STL` file parsing (`ImportPolygonNetworkFromFile`) | 🔴 Duplicates `Engine/Interchange/GeometryInterchange.{h,cpp}` (`883+` LOC OBJ/glTF2 loader) | **Delete duplicate parser**; add `.STL` reader to **`Engine/Interchange/GeometryInterchange.{h,cpp}`** |
| Packing `LowNetwork` + `HighNetwork` into `GpuBakeTriangle` / `GpuLowSurfaceVertex` | Conflated with geometry generation | **`Engine/GeometricRaster/Baking/SurfaceBakeInputPacker.{h,cpp}`** |

### 6.2 Future Nanite-Like Cluster & Hierarchical LOD Baker (`ClusterLodBaker` — Plan Only, Prototype in HTML First!)

Frontier already has a clean foundational `Engine/SpatialAcceleration/ClusterPyramid.{h,cpp}` (`128`-triangle Morton clusters, `128 B` `ClusterHeaderEntry`, basic seam-locked QEM) and a tiny educational parametric demo (`Experimental/FrontierEditor/cluster-lod.html`). We have **not** built a full HTML WebGPU Nanite-like demo or C++ `ClusterLodBaker` yet; here is the staged blueprint:

1. **Multi-Source High-Poly Intake (2 High-Poly Models + SDF-to-Mesh Extraction):**
   - Supports importing **2 high-poly models** (e.g. a raw sculpted/scanned high-poly `PolygonNetwork` **plus** an implicit/volumetric SDF that must be extracted into a watertight surface first).
   - **SDF → Watertight `PolygonNetwork` Extraction (`Engine/DistanceFieldSpace/DualContourSurfaceExtractor.{h,cpp}`):** Uses **Manifold Dual Contouring with Quadratic Error Function (`QEF`) vertex placement** from analytical SDF gradients $\nabla \phi$ (never naive Marching Cubes, which rounds off mechanical bevels and creates skinny sliver triangles). Feeds directly into `Engine/Workspaces/UV/` for automatic seam + LSCM unwrap if needed, then into `ClusterLodBaker` and/or `TextureBaker`.
2. **UV-Aware, Seam-Retaining & Crack-Free Cluster DAG Construction:**
   - **Group-Interior Simplification (Crack-Free DAG):** Group `4–8` spatially adjacent `128`-triangle clusters via graph partitioning (`METIS`-style edge-cut minimization), **lock outer group boundary vertices**, simplify shared interior edges down to `50%`, then re-partition into `2–4` coarser clusters. Guarantees `0` cracks across any monotonic LOD cut (`FinerSelfError <= AllowedError < CoarserThresholdError`).
   - **9D Attribute-Aware QEM (`XYZ + UV + Normal`):** Upgrade `ClusterPyramid.cpp`'s 3D geometric quadric (`SymmetricQuadric10`) to a **coupled geometric + UV + normal quadric** so interior edge collapses never shear textures or warp specular highlights inside UV charts.
   - **Paired Seam Collapse & UDIM Isolation:** Vertices on `EnableSeamCut` / UV chart discontinuities may collapse *only* along straight colinear seam segments with synchronized half-edge targets on both sides of the seam (preserving exact UV border shape); clusters **never** cross UDIM tile boundaries or `MaterialSlot` boundaries.
3. **Glass & Refractive Geometry Preservation ("We Are Not Unreal — We Fix It"):**

   Why Unreal's Nanite broke glass/translucency: (a) `64-bit` Visibility Buffer (`atomicMax64`) holds only 1 closest opaque depth hit, breaking multi-layer refraction and double-sided glass; (b) unconstrained geometric QEM collapses thin curved panes (windshields, headlamp lenses, bottles), amplifying tiny normal errors $10\times$ through Snell's law and causing front/back walls to self-intersect (ruining Beer-Lambert absorption).

   **Frontier's 4-Part Glass-Safe Cluster Guarantee:**

   | Protection Layer | Exact Mechanism in `ClusterLodBaker` & Renderer | Result |
   | :--- | :--- | :--- |
   | **① Optical Material Tagging** | Tag every `ClusterHeaderEntry` with `SurfaceOpticalCategory` (`OpaqueSolid` · `ThinWalledGlass` · `SolidRefractive` · `AlphaCutout` · `SubsurfaceVolume`) derived from `CompactMaterialRegister::ActiveLobeMask` (`DielectricTransmission`). Never mix glass and opaque faces inside one `128`-triangle cluster. | Zero VisBuffer corruption; clean dispatch separation. |
   | **② Minimum-Thickness Wall Guard** | For `SolidRefractive` volumes (bottles, headlights, thick panes), compute local opposing-wall thickness via short inward ray test; **reject any QEM collapse** that shrinks wall thickness below `t_min` or flips front/back separation. | Zero self-intersecting glass walls or black Beer-Lambert artifacts. |
   | **③ `8×–16×` Curvature & Normal Error Weight** | Multiply QEM normal/curvature deviation cost by `8×–16×` on glass clusters (enforcing angular error `Δθ < 0.25°` and pinning curved optical silhouettes — generalizing `glassSafe` from `cluster-lod-core.js`). | Smooth continuous car windshields & optical reflections at every LOD. |
   | **④ Dedicated Glass Render Path** | Route `OpaqueSolid` clusters to `64-bit` `VisibilityClusterRasterization`; route `ThinWalledGlass` & `SolidRefractive` clusters automatically to **`DielectricTransmission.h` + `WideBoundingTree` / `VK_KHR_ray_query` deterministic Snell refraction + Abbe dispersion + exact exit-face trace**. | Physically accurate multi-bounce refractive glass alongside Nanite-scale opaque worlds. |

4. **HTML Validation Gate First:** Before touching C++ `ClusterPyramid`, build **`Experimental/GeometryCompiler/`** (WebGPU, styled with **`Experimental/ProjectZeroEditor/`** UI — see §7!) proving: (a) SDF Dual-Contour extraction → `128`-tri DAG, (b) live UV checkerboard & seam-integrity delta metric, and (c) split-screen **Naive Decimated Glass vs Frontier Glass-Safe Cluster** visual comparison!

### 6.3 Technical Verdict: Do SDFs Require an LOD System, or Is It Unnecessary? *(🟢 Confirmed — Option A)*

> **Legend:** Quality / Bandwidth Efficiency / Memory Savings: **🔴 = Highest (Best)** · Authoring & Storage Cost: **✔️ = Lowest (Best)** · Rating: **⭐** (more = better).

| SDF Strategy | Raymarch / Cone Anti-Aliasing | VRAM Footprint | Authoring & Bake Complexity | Verdict | Rating |
| :--- | :---: | :---: | :---: | :--- | :---: |
| **🏆 🥇 Option A — Sparse `8×8×8` Narrow-Band Bricks (`Mip0`) + Single Conservative `Mip1` Coarse Grid + World Clipmaps (`Clip0..2`) + Brick Streaming Eviction** | 🔴 **Zero cone aliasing + exact narrow-band near surface** | 🔴 **`~12.5%` overhead for `Mip1`; far props evict `Mip0` bricks under VRAM pressure** | ✔️ **Single bake pass + `2×` min-filter downsample** | **CONFIRMED — Best across GI, Particles, Fracture & Streaming** | ⭐⭐⭐⭐⭐ |
| **🥈 Option B — Single-Resolution `Mip0` Only (No Mips, No Clipmaps)** | 🚩 **Wide GI/shadow cones undersample & thrash 3D texture cache at distance** | 🚩 **Wastes VRAM keeping fine bricks resident for distant objects** | ✔️ **Simplest** | Acceptable **only** for offline Baking Studio / Physics collisions | ⭐⭐⭐ |
| **🥉 Option C — Discrete Mesh-Style Per-Object SDF LODs (`LOD0..LOD4` Copies)** | 🚩 **Popping artifacts when swapping distance fields** | 🔴 **`1.5×–2×` duplicate volume storage + redundant bakes** | 🔴 **High complexity with zero geometric benefit** | **UNNECESSARY — Reject** | ⭐⭐ |

**Direct Answer:** **Discrete mesh-style per-object SDF LODs (`LOD0..LOD4`) are UNNECESSARY.** SDFs are volumetric scalar fields, not triangle soups — they already achieve multi-scale efficiency via:
1. **Per-Asset Local SDFs (`SignedDistanceVolume`):** Keep **1 sparse `8×8×8` narrow-band brick grid (`Mip0`)** plus **1 tiny conservative `Mip1` coarse grid** (`1/8` voxel count, using `min` pooling so sphere-tracing never over-steps!). Particles, Fracture, and Dual Contouring read exact `Mip0`; distant soft shadows / VRAM-constrained scenes gracefully drop `Mip0` bricks and fall back to `Mip1` / `CoarseBrickCenterDistance` via `ResourceBudgetRegister`.
2. **World-Scale Global SDF (`TieredWorldRenderSequence`):** Already uses **3 camera-centered world clipmap cascades (`Clip0`, `Clip1`, `Clip2`)** — distant geometry stamps automatically at coarser world spacing without per-object SDF LOD chains.

---

## 7. 🏆 Workspace Boundary & Engine Folder Ownership (`Texturing` vs Standalone `GeometryCompiler` vs Engine Core)

Separating **user-facing Workspace UI** from **headless C++ Engine Solvers** solves all three parts of your question cleanly — and naturally unifies **SDF + Cluster LOD (Nanite-like) + Asset Import Editor** into one cohesive tool.

### 7.1 Decision Table A — User-Facing Workspace Boundary (SDF + Cluster LOD vs `Texturing → Baking`)

> **Legend:** Domain Clarity / UX Coherence / Asset-Import Synergy: **🔴 = Highest (Best)** · UI Clutter / Coupling: **✔️ = Lowest (Best)** · Quality: **⭐** (more = better).

| Option | UX & Mental Model Clarity | Synergy With Future 3D Asset Import Editor | `ContentBrowser` Hard Category Mask Integrity | UI Clutter Cost | Rating |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **🏆 🥇 Option A — New Standalone `GeometryCompiler` Workspace (`ProjectZeroEditor` UI) for SDF + Cluster LOD (Nanite-Like + Glass-Safe) + Dual-Contour + Fracture + Import; Keep `Texturing → Baking` for 2D UV Surface Maps** | 🔴 **3D volumetric/geometric compilation stays separate from 2D UV texture baking** | 🔴 **This standalone workspace *IS* the Phase-2 Asset Import Editor (zero duplicate UI)** | 🔴 **`Geometry`/`Volumetrics`/`Materials` mask stays pure** | ✔️ **Clean focused cards on both sides** | ⭐⭐⭐⭐⭐ |
| **🥈 Option B — Fold SDF + Cluster LOD + Glass Guard + Fracture Into `Texturing → BakingWorkspace`** | 🚩 **Texture artists wade through Nanite cluster DAGs, Dual Contouring & fracture** | 🚩 **Asset import forced to launch Texture Studio** | 🔴 **Breaks `Texturing` workspace category boundaries** | 🔴 **Overloaded Franken-UI** | ⭐⭐ |
| **🥉 Option C — Split Into 3 Separate Workspaces (`TextureBaking` + `SdfWorkspace` + `ClusterLodWorkspace`)** | 🚩 **User must jump across 2–3 apps to prep one imported 3D asset** | 🔴 **Fractures unified import-to-icon capture flow** | 🚩 **Extra top-bar tab sprawl** | 🚩 **Duplicate 3D viewport chrome** | ⭐⭐⭐ |

### 7.2 Decision Table B — C++ Engine Folder Ownership (`Engine/GeometryProcessing` Audit vs Canonical Layout)

⚠️ **Naming Governance Alert:** `Process` is a **barred verb** in `SKILL-Naming.md` §4 and `Processor` is **hard-banned** in `BannedWordsVocabulary.md` §1.2 — avoid naming any folder `Engine/GeometryProcessing/`. Meanwhile **`Compile`** is an explicitly approved Frontier verb (`SKILL-Naming.md` §4) and **`Engine/Workspaces/Geometry/`** is **already reserved** in `Documentation/FolderStructure.md`!

> **Legend:** Reuse Across GI/Particles/Shipping / Governance Compliance: **🔴 = Highest (Best)** · Folder Duplication / Build Coupling: **✔️ = Lowest (Best)** · Quality: **⭐** (more = better).

| Option | Headless Reuse (GI, Particles, Fracture, Runtime) | Zero Duplication With Existing `FolderStructure.md` | Governance Compliance (`SKILL-Naming` + `And` Ban) | Rating |
| :--- | :---: | :---: | :---: | :---: |
| **🏆 🥇 Option A — Headless Solvers in Existing Canonical Folders + Standalone UI Host in Reserved `Engine/Workspaces/Geometry/`** | 🔴 **100% headless; `Development`/`Shipping` link solvers with zero Editor/UI code** | ✔️ **Uses existing `GeometrySpace`, `SpatialAcceleration`, `DistanceFieldSpace`, `GeometricRaster/Baking`, `Workspaces/Geometry`** | 🔴 **Zero banned words (`Compile` approved)** | ⭐⭐⭐⭐⭐ |
| **🥈 Option B — New Top-Level `Engine/GeometryProcessing/` Folder** | 🚩 **Duplicates `GeometrySpace`, `SpatialAcceleration` & `DistanceFieldSpace`** | 🔴 **Splits `ClusterPyramid` & `SignedDistanceVolume` across 2 roots** | 🔴 **Violates `Process`/`Processor` ban** | ⭐⭐ |
| **🥉 Option C — Put SDF & Cluster LOD Workspace Inside `Engine/Workspaces/Texturing/`** | 🚩 **Forces Geometry/SDF authoring to depend on Texturing/Paint stack** | 🚩 **Leaves `Workspaces/Geometry/` empty while bloating `Texturing/`** | 🚩 **Wrong domain ownership** | ⭐⭐ |

### 7.3 Decision Table C — What Should Happen to `Volume SDF` Inside `TextureStudio → Baking`?

> **Legend:** Artist Convenience / Simplicity: **🔴 = Highest (Best)** · Feature Duplication: **✔️ = Lowest (Best)** · Quality: **⭐** (more = better).

| Sub-Option | 2D Texture Artist Workflow | Duplication With `GeometryCompiler` | Rating |
| :--- | :---: | :---: | :---: |
| **🏆 🥇 Sub-Option A1 — Keep 17 2D UV Surface Maps in `Texturing → Baking` + Add `"Open in GeometryCompiler"` Handoff *(optional 1-click `"Quick SDF"` trigger delegating to shared headless `SdfBaker`)* ** | 🔴 **Zero friction when a texture artist needs a quick SDF mask or wants to jump to full 3D SDF/Cluster view** | ✔️ **Zero duplicated UI panels — full 3D slice, Dual-Contour, Cluster LOD, Glass Guard & Fracture live exclusively in `GeometryCompiler`** | ⭐⭐⭐⭐⭐ |
| **🥈 Sub-Option A2 — Remove `Volume SDF` Completely From `Texturing → Baking`** | 🚩 **100% pure 2D UV texture studio; requires switching workspace for any SDF check** | ✔️ **Strictest zero overlap** | ⭐⭐⭐⭐ |

### 7.4 Unified Architecture & `ProjectZeroEditor` UI Prototype Plan

```text
   HTML PROTOTYPES (Experimental/)                   NATIVE C++20 WORKSPACES (Engine/Workspaces/)
   ┌──────────────────────────────────────────┐      ┌──────────────────────────────────────────┐
   │ Experimental/TextureStudio/              │ ───► │ Engine/Workspaces/Texturing/             │
   │  Material · Pattern · Stamp · Paint ·    │      │  Material · Pattern · Stamp · Paint ·    │
   │  Texture Baking (17 2D UV PBR maps)      │      │  BakingWorkspaceHost (17 2D UV maps)     │
   └──────────────────────────────────────────┘      └────────────────────┬─────────────────────┘
                                                                          │
   ┌──────────────────────────────────────────┐      ┌────────────────────┼─────────────────────┐
   │ NEW: Experimental/GeometryCompiler/      │ ───► │ Engine/Workspaces/Geometry/              │
   │  (Built with ProjectZeroEditor UI!)      │      │  GeometryCompileWorkspaceHost            │
   │  • Outliner: Parts + Optical Material Tag│      │  • Standalone Workspace (ProjectBrowser) │
   │  • 3D GPU Viewport: Clusters, LOD Cut,   │      │  • Auto-opens as 3D Asset Import Editor  │
   │    UV Seams, Glass Guard, SDF Iso/Slices │      │  • SDF + Dual-Contour + Cluster LOD +    │
   │  • ProjectZero Inspector Cards + Notch   │      │    Glass Guard + Fracture + 128px Icon   │
   └──────────────────────────────────────────┘      └────────────────────┬─────────────────────┘
                                                                          │
                                  BOTH WORKSPACES + WORLD GI + PARTICLES  │  CALL SHARED HEADLESS CORE
                                                                          ▼
   ┌────────────────────────────────────────────────────────────────────────────────────────────┐
   │ HEADLESS C++20 ENGINE CORE (Zero UI · Linked in Editor, Development & Shipping)            │
   │  • Engine/Configuration/           HardwareProfile · ResourceBudgetRegister                │
   │  • Engine/GeometricRaster/RayAcceleration/  GeometryRayAccelerator (GTX BVH4 + RTX TLAS)   │
   │  • Engine/GeometricRaster/Baking/  TextureBaker · SdfBaker · SurfaceBakeInputPacker ·      │
   │                                    GeometrySpecimenRasterizer                              │
   │  • Engine/DistanceFieldSpace/      SignedDistanceVolume (Mip0+Mip1) ·                      │
   │                                    DualContourSurfaceExtractor                             │
   │  • Engine/SpatialAcceleration/     ClusterPyramid · ClusterLodBaker · WideBoundingTree     │
   │  • Engine/GeometrySpace/Primitives/ SpecimenGeometryLibrary                                │
   │  • Engine/Interchange/             GeometryInterchange (.obj · .gltf · .glb · .stl)        │
   └────────────────────────────────────────────────────────────────────────────────────────────┘
```

**Recommendation:** Adopt **🏆 🥇 Option A (`GeometryCompiler` standalone workspace using `ProjectZeroEditor` UI in `Engine/Workspaces/Geometry/`) + 🏆 🥇 Canonical Headless Folders + 🏆 🥇 Sub-Option A1** (or pick **A2** if you prefer zero SDF toggle inside `Texturing → Baking`). Confirm your pick on **§7.1–§7.3** (plus any remaining questions) and we are cleared for **Step 1**!

---

## 8. 🔍 Experimental `TextureStudio` Validation & Pre-C++ UI Coherence Audit

`node Experimental/TextureStudio/TextureStudioValidation.mjs` ran cleanly across **all `27/27` validation suites** (`Material`, `Pattern`, `Stamp`, `Texture Paint`, `Baking`, `UV Bridge`, `AssetBrowser`, `ColourCodex`, `Text Tool`). However, side-by-side inspection of all 5 HTML workspaces (`Experimental/TextureStudio/`) and the `34f384a` C++ Baking screenshots (`Exhibits/Images/Texturing/09..11-*.png`, `FrontierBaking_DesktopExecutable.png`) surfaced **6 concrete UI coherence gaps** to fix in `Experimental/TextureStudio/` and align **before** porting `Material` and `Texture Paint` to C++:

| # | Severity | Area & Exact Location | Finding / Incoherence | Planned Pre-Port Remediation |
| :---: | :---: | :--- | :--- | :--- |
| **1** | 🔴 **Bug** | `Experimental/TextureStudio/Serve.py:14` | Root redirect points to `/Experimental%20Remastered/TextureStudio/` (**HTTP 404** after folder rename in `2031175`). | Fix redirect to `/Experimental/TextureStudio/` and add live smoke test so browser preview opens on first click. |
| **2** | 🚩 **UI Drift** | `BakingWorkspace.js:157` vs `ViewportHeaderKit.js` | `Pattern`, `Stamp`, and `Texture Paint` share the unified 2-tier AlloyStudio header (`docBar` + `toolsBar`: renameable canvas title, editable Resolution combo, View dropdown, Undo/Redo, Save menu, primary CTAs). **`BakingWorkspace.js` bypasses `ViewportHeaderKit.js`** — uses a custom 1-row toolbar lacking renameable study title, header resolution combo, Undo/Redo, and Save dropdown; its GPU pill clips in C++ (`09-BakingWorkspaceMain.png`: `GTX BVH4 · SwiftShader Device (Su...`). | Adopt `ViewportHeaderKit` (2-tier header) in `BakingWorkspace.js` + concise tier pill (`RTX RayQuery` / `GTX BVH4` / `CPU`) and mirror in C++ `BakingViewportPane`. |
| **3** | 🚩 **UI Drift** | `AssetBrowser.js:76` (`MESH_PRESETS`) vs `BakingEngine.js:33` (`BAKE_STUDY_SHAPES`) | Preset IDs and names diverge (`chamfer`/`helmet` in AssetBrowser vs `box`/`sphere` in BakingEngine), and `Meshes` cards lack real 3D transparent thumbnails while `Materials` have `MaterialIconBaker.js`. | Unify both onto a single shared specimen list and hook `Meshes` cards to real transparent 3D icon baking (`ModelThumbnailRenderer` in HTML / `GeometrySpecimenRasterizer` in C++). |
| **4** | 🚩 **Naming** | `TextureStudio` + `34f384a` C++ (`BakingInspectorPane`, `BakingViewportPane`) | Banned terms leaked from HTML into C++ UI & code: `"Compute Backend"`, `"Baking Quick Menu"`, `"Low Mesh" / "High Mesh"`, `"18 MAPS"`, and `ArtworkStack*` filenames in HTML. | Scrub UI copy (`Ray Execution Tier`, `Baking Quick Strip`, `Low Target / High Source`, `18 Targets`) and apply §2 C++ rename ledger. |
| **5** | 🚩 **Parity** | `ContentBrowserPanel` (`FrontierBaking_DesktopExecutable.png`) vs `AssetBrowser.js` | C++ ContentBrowser top workspace strip lists only `[ Scene  UV  Material  Simulation  Audio ]` — missing `Baking`, `Pattern`, `Stamp`, and `TexturePaint` workspace masks. | Add `Baking` / `TexturePaint` / `Pattern` / `Stamp` / `GeometryCompiler` entries to ContentBrowser workspace selector strip & `WorkspaceCapabilityRegister`. |
| **6** | 🚩 **Parity** | Coordinate Basis & Theme Support (`TextureStudio.css` vs `ThemeRepository`) | HTML `TextureStudio` is `+Y Up` (Three.js) and locked to Dark theme (`#0b0b0b`); C++ Frontier enforces strict **Right-Hand `+Z Up`** and 8 live themes (`ThemeRepository`). `MaterialWorkspace.js` uses a 2-column Look-Dev dock (`.studio-grid-2col`) vs 3-column dock in the other 4 studios. | Document explicit `+Y -> +Z` transform adapters for study meshes/normals, verify all C++ `TexturingPaneAppearance` colors resolve from `ThemeRepository`, and lock `MaterialWorkspaceHost`'s 2-column dock spec before C++ port. |

---

## 9. 🏁 Recommended Execution Sequence (Pending §7 Sign-Off)

| Order | Milestone | Scope (Zero Code Until Approved) | Validation Gate |
| :---: | :--- | :--- | :--- |
| **Step 1** | **Fix `TextureStudio` HTML Coherence** | Fix `Serve.py` 404, unify `BakingWorkspace.js` onto `ViewportHeaderKit`, sync `MESH_PRESETS` ↔ `BAKE_STUDY_SHAPES`, scrub UI labels, verify live browser preview across all 5 tabs. | `TextureStudioValidation.mjs` (`27/27+`) + live visual check across all 5 workspaces |
| **Step 2** | **C++ `HardwareProfile` + `ResourceBudgetRegister`** | Create `Engine/Configuration/HardwareProfile.{h,cpp}` + `ResourceBudgetRegister.{h,cpp}`; promote SSD/HDD probe from `AssetStreamThread`; wire `VK_EXT_memory_budget` + OS RAM + RTX/GTX tier into `VulkanInterchange`. | New `Exhibits/HardwareProfileValidation.cpp` + starvation-guard unit checks |
| **Step 3** | **Refactor `34f384a` Baking Core (Zero `And`, Zero Banned Words)** | Split `GpuTextureAndSdfBaker` → `TextureBaker` + `SdfBaker` + shared `GeometryRayAccelerator`; replace `StudyGeometryBuilder` with `SpecimenGeometryLibrary` + `GeometryInterchange` (`.STL`) + `SurfaceBakeInputPacker`; scrub UI labels & wire `ResourceBudgetRegister`. | `BakingGPUValidation` + `BakingWorkspaceUIValidation` + refreshed `.png` exhibits |
| **Step 4** | **C++ 3D Object Icon Auto-Baking (Phase 1)** | Implement `GeometrySpecimenRasterizer.{h,cpp}` (transparent 3-point OpenPBR `128×128` auto-fit) and wire auto-bake fallback into `ThumbnailSheetRepository` for `ContentCategory::Geometry`. | `ContentBrowserValidation` + side-by-side HTML vs C++ `.png` proof |
| **Step 5** | **HTML Prototype: `Experimental/GeometryCompiler/` (`ProjectZeroEditor` UI + WebGPU)** | Build standalone Project Zero UI studio prototyping: Dual-Contour SDF-to-Mesh + Sparse `Mip0`/`Mip1` SDF + 9D UV/seam-locked `128`-tri Cluster LOD DAG + side-by-side Naive vs Glass-Safe refractive comparison + `128×128` Icon Capture before C++ port. | Automated watertight / UV-seam / glass-thickness validation + visual screenshots |
| **Step 6** | **C++ `ClusterLodBaker` & `Engine/Workspaces/Geometry/` (`GeometryCompiler` + Asset Import Editor)** | Port validated `ClusterLodBaker` + `DualContourSurfaceExtractor` + sparse SDF `Mip1`, then wire `GeometryCompileWorkspaceHost` (both standalone workspace & auto-opened 3D Asset Import Editor writing unified `.GeometrySpace`). | `GeometryCodec` round-trip + `ClusterLodValidation` + `GeometryCompileValidation` `.png` proofs |
