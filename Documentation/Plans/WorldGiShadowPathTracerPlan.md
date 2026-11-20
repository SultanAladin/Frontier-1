# World GI, Direct Shadows & Deterministic Path Tracer Architecture Plan

> **TL;DR**: Synced `SultanAladin/Frontier@arena/8391e994-frontier` (`daed305`) onto `arena/9154d753-frontier-1`, stripped all leftover runtime GI/SDF artifacts from `Engine/` (`GlobalIllumination/` → `WorldRendering/`, `RayAcousticFieldReading`), and locked a **100% real-triangle-surface** rendering architecture (**zero 3D Voxel SDF**, **zero analytical capsule/box/sphere proxies**). Frontier standardizes on **2 denoiser-free GTX GI renderers** (`StokesContourGiSequence` + `SurfelCascadeGiSequence`), **6 real-triangle direct solar shadow modes** (`ContactPcss`, `PcssPlaneAware`, `ShadowRayMarch`, `PcfBilinear`, `WideTreeRayQuery`, `SurfelDiskShadow`), and **1 unified deterministic Wavefront Path Tracer** (`DeterministicPathTraceSequence` across GTX `BVH4`/`CWBVH8` and RTX `VK_KHR_ray_query`).

**Branch:** `arena/9154d753-frontier-1` · **Date:** `2026-11-20` · **Status:** 🏁 Base Synced & Cleaned (`e61e5e4`) · Plan Ready for Phase 1–4 Implementation

---

## 1. Architectural Locks & Empirical Shootout Decision Tables

> **Polarity Legend**: **Accuracy / Determinism / Real-Geometry Fidelity**: 🔴 = highest/best · 🚩 = medium · ✔️ = lowest. **Runtime Cost / Memory Overhead**: ✔️ = lowest/best · 🚩 = medium · 🔴 = highest/worst.

### 1.1 Verdict on 3D Voxel SDF & Analytical Primitives: **NO to Both (100% Real Triangle Surfaces)**

| Geometry & Occlusion Representation | Rank | Shadow RMSE (`536cfe1`) | Lit HDR RMSE (`536cfe1`) | Thin-Shell / Character Fidelity | Dynamic Character Cost | Runtime VRAM Overhead | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **100% Real Triangle Surfaces** (`PolygonNetwork` + `SurfelOcclusionField` + `SpatialSplitBoundingTree` / `VK_KHR_ray_query`) | 🏆 🥇 | **`0.2973`** ✔️ | **`0.1340`–`0.1814`** 🔴 | 🔴 Exact silhouettes, `4.5 cm` car trim, wheel spokes, cloth & fingers | ✔️ `< 0.20 ms` (GPU barycentric surfel refresh + BVH/BLAS refit) | ✔️ `~2–6 MiB` compact triangle + surfel buffers | 🟢 **Selected Everywhere** |
| **Analytical Capsule / Box / Sphere Proxies** (`BoneCapsule`, `ClosedBox`) | 🥈 | `0.2973`–`0.2983` 🚩 | `0.1340` 🚩 | ✔️ Misses fingers, facial contours, draped cloth & non-capsule props | ✔️ `0.08 ms` CPU skeleton update, requires manual rig authoring | ✔️ `< 64 KiB` | 🔴 **Rejected** (real character & prop triangles used directly) |
| **Runtime 3D Voxel SDF / Clipmaps** (`DistanceFieldSpace`, `6–13 cm` voxels) | 🥉 | `0.3068`–`0.4170` 🔴 | `0.1968`–`0.2464` 🔴 | ✔️ Light-leak holes on thin car bodies, AABB cascade seams & ringing | 🔴 `97.9–600 ms` dirty-brick / local-asset voxel rebake | 🔴 `32–96 MiB` 3D float/SH grids | 🔴 **Rejected** (keep `GpuSdfBaker` offline `.fvol` only) |

- 💡 **Why 3D Voxel SDF is permanently retired from runtime rendering**: Across both `arena/8391e994-frontier` (`2d6f15e`, `536cfe1`) and `arena/39c6910b-frontier-1` (`460cfc8`, `d5bbb81`), real-triangle contour integration and stratified surfel disks beat every 3D voxel clipmap in shadow RMSE (`0.2973` vs `0.3068` / `0.4170`), emissive GI RMSE (`0.2238` / `0.2239` vs `0.2259` / `0.2402`), and lit HDR RMSE (`0.1340` / `0.1814` vs `0.2464`), while eliminating `98–600 ms` dynamic voxel rebakes and AABB boundary discontinuities.
- 📝 **Offline Baking Boundary**: `Engine/GeometricRaster/Baking/GpuSdfBaker.{h,cpp}` and `Engine/Shaders/Baking/{BakeVolumeSdfCompute,ExtractSdfSectionCompute}.slang` remain strictly isolated inside `BakingWorkspace` for offline `.fvol` asset export (`BakingGPUValidation`). Zero world rendering, shadow, GI, or acoustic units depend on SDF volumes.

---

### 1.2 GTX Diffuse & Radiant GI Selection (2 Best Denoiser-Free Renderers)

| GI Architecture Candidate | Rank | GI RMSE vs 128-SPP NEE (`2d6f15e`) | Lit HDR RMSE (`536cfe1`) | Shootout sRGB / HDR RMSE (`460cfc8`) | Per-Pixel Lookup Cost (GTX 1080p) | Dynamic Character & Vehicle Response | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **`StokesContourGiSequence`** *(Exact Spherical-Polygon Stokes Contour 1st Bounce + `DiffuseSurfacePatch` 2nd Bounce + Real-Triangle `SurfelOcclusionField` + `0.28 m` BVH4 Contact)* | 🏆 🥇 | **`0.2238`** 🔴 | **`0.1340`** 🔴 | **`0.1331` / `0.2194`** 🔴 | 🚩 `~1.1–1.8 ms` (clustered Stokes contour + patch transfer) | 🔴 Zero lag on moving vehicle lamps & deformed character triangles | 🟢 **GTX GI Renderer #1** (`WorldRenderEngine::StokesContourGi`) |
| **`SurfelCascadeGiSequence`** *(Barycentric Real-Triangle `RadiantSurfelDisk` Multi-Bounce Radiosity + Analytic Dual-Cascade `OctahedralIrradianceCascade` `8×8` + `16×16` Chebyshev Depth)* | 🥈 | **`0.2239`** (`0.2269` cascade) 🔴 | **`0.1814`** 🔴 | `0.1657` / `0.2263` 🚩 | ✔️ **`~0.35–0.55 ms` `O(1)`** trilinear octahedral lookup | 🔴 `< 0.05 ms` barycentric surfel follow on skinned character triangles | 🟢 **GTX GI Renderer #2** (`WorldRenderEngine::SurfelCascadeGi`) |
| **L1 Continuous Voxel Field Clipmap** (`ContinuousFieldEmissiveGi`) | 🥉 | `0.2259` 🚩 | `0.2464` ✔️ | `0.1669` / `0.2254` 🚩 | 🚩 `~1.9 ms` cone / trilinear 3D grid | ✔️ Voxel swimming & `98 ms` dirty-brick rebake | 🔴 **Rejected** |
| **Legacy Voxel Clipmap / Legacy Irradiance Lattice** | 🏅 | `0.2402` / `0.5361` ✔️ | `0.1968` ✔️ | — | 🔴 Heavy 3D dispatch + banding | ✔️ Severe light leaks & plane banding | 🔴 **Retired (`63db120`)** |

---

### 1.3 Direct Solar Shadow Modes (`SolarShadowAlgorithm` — 6 Real-Triangle Modes, Zero SDF)

| Enum Value | Shadow Mode | Real-Triangle Mechanism | GTX 1080p Cost | Penumbra & Contact Quality | Role in Engine |
| :---: | :--- | :--- | :---: | :---: | :--- |
| `0` | **`ContactPcss`** 🏆 🥇 | `1024²` `3.91 mm/tx` Fine + `15.6 mm/tx` Coarse `Depth32f` triangle raster cascades + `0.24 m` `WideBoundingTree` triangle contact ray + `PlaneAwareShadowFilter` | ✔️ `~0.65 ms` | 🔴 Sub-millimeter tyre/foot contact + smooth physical solar penumbra | 🟢 **Default GTX Direct Shadow** |
| `1` | **`PcssPlaneAware`** 🥈 | 2-Cascade Vogel blocker search + continuous distance-proportional penumbra + 3-step `PlaneAwareShadowFilter` | ✔️ `~0.50 ms` | 🔴 Clean contact-hardening penumbra across multi-light scenes | 🟢 Fast Multi-Light GTX Tier |
| `2` | **`ShadowRayMarch`** | Reverse-quadratic `Depth32f` shadow-cascade raymarch (SMRT) over rasterized real triangles | 🚩 `~1.35 ms` | 🚩 Crisp silhouette hardening without BVH traversal | 🟢 Depth-Only Silhouette Mode |
| `3` | **`PcfBilinear`** | `3×3` tent-weighted bilinear orthographic `Depth32f` cascade filter | ✔️ `~0.28 ms` | ✔️ Fixed kernel sharpness, ultra-low GPU budget | 🟢 Mobile / Low-Power Tier |
| `4` | **`WideTreeRayQuery`** 🏆 🥇 | Deterministic Vogel solar-disc rays against `SpatialSplitBoundingTree` (GTX) or `VK_KHR_ray_query` BLAS/TLAS (RTX) + `PlaneAwareShadowFilter` | 🚩 `~2.8 ms` GTX / ✔️ `< 0.8 ms` RTX | 🔴 Exact real-triangle ground-truth solar penumbra | 🟢 **Default RTX Direct Shadow** |
| `5` | **`SurfelDiskShadow`** *(New)* 🥈 | Area-stratified `OccluderSurfelDisk` clusters built from real world & deformed character triangles + `0.28 m` `WideBoundingTree` contact ray + `SoftMin` solar-cone clearance | ✔️ `~0.85 ms` | 🔴 Noise-free analytic soft area penumbra (`0.2973` RMSE, zero denoiser) | 🟢 **Zero-Cascade Analytic Soft Shadow** |

---

### 1.4 Unified Deterministic Path Tracer (`DeterministicPathTraceSequence` — GTX + RTX)

| Path Tracer Execution Architecture | Rank | Cross-Batch Determinism (`Determinism.json`) | Triangle Tests / Ray (`AuditReport`) | SM75 Occupancy & Spill | Hardware Scaling (GTX + RTX) | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **6-Step Lock-Free Wavefront Compute** (`WavefrontPathTraceCompute.slang`) + **`SpatialSplitBoundingTree` (`BVH4`/`CWBVH8` GTX)** + **`VK_KHR_ray_query` (`RayQueryKHR` BLAS/TLAS RTX)** | 🏆 🥇 | 🔴 **Bit-Exact** (`1` digest across batch `1,2,4,8,32`; `0 / 147,456` Sobol diffs) | ✔️ **`-27.0%`** via Stich SBVH + YKL DP 4/8-wide collapse (`98.6%` slot fill) | ✔️ **`50%–100%`** (`32–56` VGPRs/step, zero local-memory spill) | 🔴 **`2.2×–3.8×`** GTX SM75 · **`3.0×–8.0×`** RTX RT cores | 🟢 **Selected Unified Path Tracer** |
| **Monolithic Megakernel Compute** | 🥈 | 🔴 Bit-Exact | ✔️ `-27.0%` with SBVH | 🔴 `12.5%–25%` (`130–160` VGPRs, `>1 KiB` `TransportStack` spill, `8.9×–14.1×` tail stall) | 🚩 Moderate | 🔴 **Rejected** |
| **`VK_KHR_ray_tracing_pipeline` (SBT Callable Recursion)** | 🥉 | 🚩 Driver-dependent hit ordering unless manually sorted | ✔️ Hardware RT only | 🚩 SBT indirection + continuation stack overhead | ✔️ Unavailable on GTX Turing / Android non-RT | 🔴 **Rejected** (inline `VK_KHR_ray_query` runs inside identical Wavefront steps) |

---

## 2. Canonical Folder Structure & 7-Gate Naming Audit

All directories, filenames, C++20 types, and `.slang` entry points adhere strictly to `Documentation/FolderStructure.md`, `Documentation/BannedWordsVocabulary.md`, and `Documentation/AgenticInstructions/SKILL-Naming.md` (zero `And`, zero `Mesh`, zero `Probe`, zero `Cache`, zero `Emitter`, zero `Node`, zero `Pass`, zero `Pipeline`, zero `Stage`, zero `Kernel`, zero `Map`, zero `Frame` in new identifiers).

```text
Frontier-1/
├── Documentation/
│   ├── FolderStructure.md                                      // Updated: WorldRendering/ replaces GlobalIllumination/
│   └── Plans/
│       ├── README.md                                           // Index updated with WorldGiShadowPathTracerPlan.md
│       ├── WorldGiShadowPathTracerPlan.md                      // This architectural & phased implementation plan
│       └── MarkdownStudioViewer.html                           // Rebuilt self-contained Markdown Studio bundle
│
├── Engine/
│   ├── SpatialAcceleration/                                    // Real-triangle acceleration & dynamic refit
│   │   ├── ClusterPyramid.{h,cpp}                              // Existing: 128-triangle Morton QEM LOD pyramid
│   │   ├── TriangleVisibilitySpace.h                           // Existing: RaySegmentQuery & RayTriangleHitResult
│   │   ├── WideBoundingTree.{h,cpp}                            // Existing: 128 B 4-Wide SAH BVH4
│   │   └── SpatialSplitBoundingTree.{h,cpp}                    // [NEW] Stich SBVH + YKL DP BVH4/CWBVH8 + AlignDeformedSurfaceBounds refit
│   │
│   ├── GeometricRaster/
│   │   ├── Baking/
│   │   │   └── GpuSdfBaker.{h,cpp}                             // Existing: Strictly isolated to offline BakingWorkspace .fvol export
│   │   │
│   │   ├── DirectLighting/                                     // 6-Mode Real-Triangle Direct Solar Shadows + LTC Area Lights
│   │   │   ├── LinearCosineLightIntegration.{h,cpp}            // Existing: Clipped-polygon LTC + 2-scale chassis AO
│   │   │   ├── SolarShadowSequence.{h,cpp}                     // [EXTEND] Add Mode 5 (SurfelDiskShadow) alongside Modes 0..4
│   │   │   └── SurfelOcclusionField.{h,cpp}                    // [NEW] Area-stratified real-triangle OccluderSurfelDisk grid + barycentric character refresh
│   │   │
│   │   └── WorldRendering/                                     // Unified 5-Engine World Renderer (MatCap, Direct, 2 GTX GI, 1 GTX/RTX PT)
│   │       ├── ShowcaseGalleryScene.{h,cpp}                    // [EXTEND] Register static studio, ControlVehicle & deforming character PolygonNetworks
│   │       ├── SpecularReflectionSequence.{h,cpp}              // Existing: WideBoundingTree VNDF specular & Abbe dielectric refraction
│   │       ├── DeterministicQuadrature.h                       // [NEW] Shared C++20 Owen-scrambled Sobol (0,2) + Dupuy-Benyoub Spherical-Cap VNDF
│   │       ├── StokesContourGiSequence.{h,cpp}                 // [NEW] GTX GI #1: Real-triangle Stokes contour 1st bounce + DiffuseSurfacePatch 2nd bounce
│   │       ├── SurfelCascadeGiSequence.{h,cpp}                 // [NEW] GTX GI #2: Barycentric RadiantSurfelDisk radiosity + Dual OctahedralIrradianceCascade
│   │       ├── DeterministicPathTraceSequence.{h,cpp}          // [NEW] Unified GTX (SBVH4/CWBVH8) + RTX (VK_KHR_ray_query) 6-step Wavefront Path Tracer
│   │       ├── TieredWorldRenderSequence.{h,cpp}               // [EXTEND] Wire WorldRenderEngine 0..4 on CPU reference / validation path
│   │       └── VulkanWorldRenderSequence.{h,cpp}               // [EXTEND] Dispatch GPU .slang shaders for all 6 shadows + 2 GTX GI + Wavefront PT
│   │
│   └── Shaders/
│       ├── DirectLighting/
│       │   ├── WideTreeRayQuery.slang                          // Existing: Shared BVH4 & VK_KHR_ray_query intersection routines
│       │   ├── SolarFrustumShadow.slang                        // Existing: Deterministic Vogel solar-disc trace + chassis AO
│       │   ├── PlaneAwareShadowFilter.slang                    // Existing: Coplanar blocker dilation + 3-step à-trous edge-stopping filter
│       │   └── SurfelDiskShadow.slang                          // [NEW] Mode 5 GPU soft solar cone vs real-triangle OccluderSurfelDisk + 0.28 m BVH4 contact
│       │
│       ├── GeometricRaster/
│       │   └── WorldViewportRadiometricCompute.slang           // [EXTEND] Integrate Engine modes 2 (StokesContourGi) & 3 (SurfelCascadeGi) + Shadow mode 5
│       │
│       └── WorldRendering/
│           ├── SpecularGlassRaytrace.slang                     // Existing: Coherent VNDF specular & Abbe dielectric glass compute unit
│           ├── DeterministicQuadrature.slang                   // [NEW] 5-op bit-parallel GF(2) Sobol (0,2) + Laine-Karras Owen scramble + Spherical-Cap VNDF
│           ├── StokesContourGiCompute.slang                    // [NEW] GPU real-triangle Stokes contour irradiance + 2-bounce DiffuseSurfacePatch transfer
│           ├── SurfelCascadeGiCompute.slang                    // [NEW] GPU barycentric surfel radiosity + Dual OctahedralIrradianceCascade Chebyshev lookup
│           ├── WavefrontPathTraceCompute.slang                 // [NEW] 6-step lock-free Wavefront PT (GTX SBVH4/CWBVH8 + RTX VK_KHR_ray_query)
│           └── EdgeStoppingDisplayStack.slang                  // [NEW] Optional deterministic feature-guided display reconstruction stack
│
└── Exhibits/
    ├── WorldGiShadowPathTraceValidation.cpp                    // [NEW] End-to-end CTest validating 6 shadows, 2 GTX GI, GTX/RTX PT determinism & RMSE
    └── Images/
        └── WorldGiShadowPathTrace/                             // Deterministic .png visual comparison sheets + metrics JSON
```

### 2.1 Seven-Gate Naming Compliance Cross-Check

| Forbidden / Risk Concept (`BannedWordsVocabulary.md`) | Compliant Frontier Replacement Used in Plan & Code |
| :--- | :--- |
| `GlobalIllumination` (stale non-GI folder) · `*And*` conjunctions | `WorldRendering/` · `WorldGiShadowPathTracerPlan.md` · `WorldGiShadowPathTraceValidation.cpp` |
| `Mesh` (`SurfelMeshShadow`, `CharacterMesh`) | `PolygonNetwork` · `TriangleSurface` · `DeformedSurfaceSlice` · `SurfelDiskShadow` |
| `Probe` / `Cache` (`DdgiProbe`, `IrradianceCache`, `RadianceCache`) | `OctahedralIrradianceCascade` · `OctahedralDirectionCell` · `SurfelCascadeGiSequence` |
| `Emitter` / `Emit` (`EmissiveEmitter`, `EmitRay`) | `RadiantTriangleSource` · `RadiantSurfelDisk` · `AppendContinuationRay` |
| `Node` / `Parent` / `Child` (`BvhNode`, `ChildIndex`) | `WideTreeEntry` · `CompressedWideEntry8` · `EnclosingIndex` · `FirstNestedIndex` |
| `Pass` / `Stage` / `Kernel` / `Pipeline` / `Composite` | `…Sequence` · `…Compute` · `…Entry` · `EdgeStoppingDisplayStack` |
| `SdfAcousticFieldReading` (leftover AudioSpace struct) | `RayAcousticFieldReading` *(already remediated & verified in `e61e5e4`)* |

---

## 3. 100% Real-Triangle Pipeline for Static Worlds, Rigid Vehicles & Skinned Characters

Every rendering unit consumes real triangles (`PolygonNetwork` / `GpuShadedTriangle96`) without analytical proxy shapes or voxelized distance fields:

```text
Static Studio / World PolygonNetwork ──► Stich SBVH + YKL DP Collapse ──► Static BLAS / SpatialSplitBoundingTree
Rigid Assets (ControlVehicle 25,068△) ──► RigidClusterInstance Transform ─┤
Skinned / Deforming Character (Real △) ──► GPU Skinning / Vertex Stream ──┼─► Per-Tick Fast Refit (< 0.20 ms total):
                                                                          │    1. GTX: AlignDeformedSurfaceBounds() bottom-up BVH4 AABB refit
                                                                          │    2. RTX: VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR BLAS + TLAS
                                                                          │    3. Shadow Cascades 0..1: Rasterize deformed character △ into Depth32f
                                                                          └─►  4. Surfels: Barycentric (ClusterSlot, TriIdx, UV) Position/Normal align
```

### 3.1 Core GPU-Aligned Real-Triangle Data Layouts (`16 B` / `64 B` Aligned)

| Struct (`C++20` & `.slang` Mirror) | Size | Exact Fields & Real-Triangle Mechanism |
| :--- | :---: | :--- |
| `DeformedSurfaceSlice` | `32 B` | `FirstTriangleIndex`, `TriangleCount`, `FirstBvhEntryIndex`, `BvhEntryCount`, `ClusterInstanceOrdinal`, `MaterialOffset`, `SurfaceRevision`, `Reserved`. Tracks live skinned character triangle ranges inside `SceneTriangles` for sub-`0.15 ms` bottom-up AABB refit (`AlignDeformedSurfaceBounds`) without rebuilding static SBVH trees. |
| `OccluderSurfelDisk` | `32 B` | `float4 PositionRadius` (`xyz` world centroid, `w` area-equivalent disk radius `sqrt(Area / pi) * 1.12`), `float4 NormalClusterTri` (`xyz` unit triangle/cluster normal, `w` packed `(ClusterSlot, RepresentativeTriOrdinal)`). Built directly from real scene triangles; dynamic character entries refresh every tick from deformed vertex positions. |
| `RadiantTriangleSource` | `64 B` | `float4 VertexA_Area`, `float4 VertexB_ClusterId`, `float4 VertexC_MaterialSlot`, `float4 Normal_TwoSided`, `float4 RadiantExitanceRgb_Power` (derived via `EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight)` on real emissive triangles such as the `42` `ControlVehicle` headlamps/taillamps and architectural light strips). |
| `RadiantSurfelDisk` | `64 B` | `float4 PositionRadius`, `float4 NormalArea`, `float4 AlbedoSelfRadiance`, `uint4 AnchorClusterTriUv` (`x = ClusterSlot`, `y = TriangleOrdinal`, `z = PackedHalf2BarycentricUv`, `w = MaterialSlot`). Because each surfel is anchored by `(ClusterSlot, TriangleOrdinal, BarycentricUV)` to a real triangle, rigid vehicle motion and character skinning move surfels exact-on-surface in `O(1)` per surfel with zero surface swimming or rebaking. |
| `OctahedralDirectionCell` | `512 B` | Per lattice point inside `OctahedralIrradianceCascade`: `8×8` `RGB9E5` (`uint32_t`) octahedral irradiance texels (`256 B`) + `16×16` `R8G8_UNORM` radial depth / squared-depth Chebyshev moments (`256 B` → `512 B` total per lattice point; Dual Cascade `24×24×12` fine + `24×24×12` coarse = **`6.75 MiB` total VRAM**). |

---

## 4. Detailed Subsystem Specifications (`WorldRenderEngine` `0..4`)

`Engine/GeometricRaster/WorldRendering/TieredWorldRenderSequence.h` expands `WorldRenderEngine` cleanly from 2 modes to all 5 unified modes while preserving full backward compatibility for existing `MatCap` (`0`) and `OpenPbrDirect` (`1`) callers:

```cpp
enum class WorldRenderEngine : uint8_t
{
    MatCap                 = 0u, // [-] - Studio MatCap Normal/Roughness Shading (Zero GI, Zero Shadow Cascades)
    OpenPbrDirect          = 1u, // [-] - OpenPBR Direct + LTC Area Lights + 6-Mode Shadow Lab + Specular/Glass (Zero GI)
    StokesContourGi        = 2u, // [-] - GTX GI #1: Exact Real-Triangle Stokes Contour + 2-Bounce Surface Patches (Zero Denoiser)
    SurfelCascadeGi        = 3u, // [-] - GTX GI #2: Barycentric Real-Triangle Surfel Radiosity + Dual Octahedral Cascade (Zero Denoiser)
    DeterministicPathTrace = 4u, // [-] - Unified GTX (SBVH4/CWBVH8) + RTX (VK_KHR_ray_query) 6-Step Wavefront Path Tracer
};
```

### 4.1 Direct Lighting & Mode 5 `SurfelDiskShadow` (`Engine/GeometricRaster/DirectLighting/`)
- **Modes `0..4` (`SolarShadowSequence`)**: Kept intact and verified (`ContactPcss`, `PcssPlaneAware`, `ShadowRayMarch`, `PcfBilinear`, `WideTreeRayQuery`), rasterizing all real static, vehicle, and skinned character triangles into `FineContactClip` (`4.0 m` span, `3.91 mm/tx`) and `CoarseContactClip` (`16.0 m` span, `15.6 mm/tx`).
- **Mode `5` (`SolarShadowAlgorithm::SurfelDiskShadow`, `SurfelOcclusionField.{h,cpp}` + `SurfelDiskShadow.slang`)**:
  1. **Build & Dynamic Refresh**: Clusters real non-boundary triangles into `OccluderSurfelDisk` entries binned into a flat spatial cell hash/grid (`0.28 m` cells) for `O(1)` ray-slab cell traversal, plus persistent barycentric character triangle groups updated in-place when character vertices deform.
  2. **Contact + Soft Cone Evaluation**: Traces a `0.28 m` `WideBoundingTree` real-triangle contact ray (`QueryRayOccluded`) for razor-sharp foot/tyre/finger contact, then marches only the `3×3` solar-ray cell beam through `SurfelOcclusionField` using `SoftMin` signed disk clearance `(DistToRayAxis - DiskRadius) / max(0.02, Travel * TanSunRadius)` with smooth receiver-plane fade. Zero spatial denoiser required.

### 4.2 GTX GI Renderer #1: `StokesContourGiSequence` (`StokesContourGiCompute.slang`)
- **1st Bounce (Exact Analytic Radiant-Triangle Stokes Contour)**:
  - Extracts all real emissive triangles (`RadiantTriangleSource`) and groups spatially adjacent co-planar/near-planar triangles into hierarchical `RadiantClusterSummary` bounds (`<= 64` clusters for distant evaluation, exact per-triangle contour within `2.5 × ClusterRadius`).
  - Evaluates the closed-form Lambert–Stokes spherical-polygon contour integral $\Phi(\mathbf{p}, \mathbf{n}) = \frac{1}{2\pi} \sum_{e} \|\mathbf{u}_e \times \mathbf{u}_{e+1}\|^{-1} \arccos(\mathbf{u}_e \cdot \mathbf{u}_{e+1}) ((\mathbf{u}_e \times \mathbf{u}_{e+1}) \cdot \mathbf{n})$ clipped above the receiver tangent horizon — **zero Monte Carlo noise, zero point-light singularity, exact polygon solid angle**.
- **2nd Bounce (`DiffuseSurfacePatch` Indirect Colour Bleeding)**:
  - Samples `256–512` area-stratified `DiffuseSurfacePatch` elements across real scene & character triangles, evaluates their direct solar + 1st-bounce radiant illumination once per tick on GPU (`< 0.12 ms`), and integrates horizon-clipped solid-angle form-factors for saturated crimson/cobalt wall & vehicle body colour bleeding onto characters and studio floors.
- **Visibility Gating**: `0.28 m` `WideBoundingTree` triangle contact ray + `SurfelOcclusionField` ray-beam transmittance (`0` SDF lookups).

### 4.3 GTX GI Renderer #2: `SurfelCascadeGiSequence` (`SurfelCascadeGiCompute.slang`)
- **Barycentric Real-Triangle Surfel Cloud (`1,024–2,048` `RadiantSurfelDisk` Entries)**:
  - Generated directly from `PolygonNetwork` triangle areas (`ControlVehicle`, studio architecture, props, and character surfaces) via deterministic Sobol low-discrepancy triangle stratification.
  - Multi-bounce radiosity transport (`2` persistent bounce iterations per tick over `64` deterministic surfel-pair / `WideBoundingTree` visibility rays per surfel) converges smooth multi-bounce colour bleeding across dynamic scenes.
- **Analytic Dual-Cascade `OctahedralIrradianceCascade` (`O(1)` Per-Pixel Lookup)**:
  - Fine cascade (`0.45 m` spacing around camera/focus) + Coarse cascade (`1.80 m` spacing across world) populated directly from the solved `RadiantSurfelDisk` cloud + `RadiantTriangleSource` list.
  - Per-pixel evaluation performs normal-biased trilinear cascade interpolation weighted by **Chebyshev radial variance** ($P(r > d) \le \sigma^2 / (\sigma^2 + (r - \mu)^2)$) and smooth cascade-border blending — **eliminating backface light leaks, dark wall blotches, and plane banding without a spatial/temporal denoiser**.

### 4.4 Unified Deterministic Path Tracer: `DeterministicPathTraceSequence` (`WavefrontPathTraceCompute.slang`)
- **Shared Quadrature (`DeterministicQuadrature.{h,slang}`)**:
  - Exact 5-op bit-parallel GF(2) Pascal zeta `SobolSecondDimension` + Laine–Karras `NestedDigitPermutation` (`Sobol02Sample`, `0 / 147,456` host/device mismatches) + Dupuy–Benyoub (2023) `SampleSphericalCapVndf` GGX importance sampler + screen-space blue-noise toroidal rank shift. Zero PRNGs, zero Russian roulette.
- **Real-Triangle Acceleration (`SpatialSplitBoundingTree.{h,cpp}` + `VK_KHR_ray_query`)**:
  - **GTX Software Compute Path**: Stich et al. (2009) SBVH spatial splits ($\alpha = 10^{-5}$) on high-overlap `ControlVehicle` / architectural triangles (`-27%` triangle intersections) collapsed via Ylitie–Karras–Laine (2017) dynamic programming into `98.6%`-full 4-wide (`WideTreeEntry`, `128 B`) and optional quantized 8-wide (`CompressedWideEntry8`, `80 B`) trees, plus two-level rigid/skinned instance transforms (`AlignDeformedSurfaceBounds` for character triangles).
  - **RTX Hardware Ray-Query Path**: Inline `VK_KHR_ray_query` (`RayQueryKHR`) inside the exact same compute entry points. Each mesh builds a BLAS splitting **Geometry 0 (`Opaque` triangles, `VK_GEOMETRY_OPAQUE_BIT_KHR`)** from **Geometry 1 (`ThinDielectric` glass triangles, non-opaque)** so hardware RT cores terminate opaque shadow rays in fixed-function silicon while reporting windshield/lamp glass sheets for analytic `ThinSheetTransmittance`. Skinned character BLASes execute fast `VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR` refits per tick.
- **6-Step Lock-Free Wavefront Execution (`WavefrontPathTraceCompute.slang`, Single `vkQueueSubmit`)**:
  1. `GeneratePrimaryRaysEntry`: Coherent `8×8` tile camera/thin-lens Sobol `(0, 2)` ray generation (or primary G-Buffer reuse at `SampleIndex == 0` when `ApertureRadius == 0`).
  2. `TraceClosestSurfaceEntry`: Dense queue closest-hit traversal (`SpatialSplitBoundingTree` on GTX, `RayQueryKHR` on RTX) writing compact `32 B` hit records (`InstanceOrdinal`, `PrimitiveOrdinal`, `HitDistance`, packed `BarycentricUV`).
  3. `ScatterSurfaceLobesEntry`: Reconstructs exact vertex-interpolated position & normals, evaluates full `10`-lobe `OpenPbrSurfaceScattering` (`Coat`, `Specular`, `Diffuse`, Abbe-dispersed dielectric glass, `BounceSlabEntry` secondary tail), enqueues deterministic NEE shadow rays (`SolarDirectionalSource` + `RadiantTriangleSource` light-tree importance cut), and pushes deterministic lobe continuation rays.
  4. `TraceShadowTransmittanceEntry`: Zero-division opaque any-hit rejection + deterministic `4`-entry distance-sorted `ThinDielectric` glass transmittance product (guaranteeing bit-exact cross-driver RTX/GTX parity).
  5. `CompactContinuationQueuesEntry`: Subgroup ballot prefix-sum compaction (`WavePrefixCountBits`) feeding bounce `b + 1` with `~1.0×` warp utilization.
  6. `AccumulateDeterministicSampleEntry`: Strictly ordered `SampleIndex = 0, 1, ...` progressive linear HDR accumulation + optional `EdgeStoppingDisplayStack.slang` (`1`-SPP live camera orbit preview) + ACES gamut display encoding.

---

## 5. Phased Implementation Roadmap & Verification Gates

| Phase | Deliverables (`C++20` + `.slang`) | Verification Gate |
| :---: | :--- | :--- |
| **Phase 1** | **`SurfelOcclusionField` + Shadow Mode 5 (`SurfelDiskShadow`)**: Implement `Engine/GeometricRaster/DirectLighting/SurfelOcclusionField.{h,cpp}`, `Engine/Shaders/DirectLighting/SurfelDiskShadow.slang`, and `SolarShadowAlgorithm::SurfelDiskShadow = 5u` in `SolarShadowSequence.{h,cpp}` & `VulkanWorldRenderSequence.{h,cpp}` over real static, `ControlVehicle`, and deformed character triangles. | `SolarShadowValidation` + `SurfelDiskShadow` RMSE `< 0.30` vs 128-SPP Vogel BVH4 reference, `< 0.20 ms` character triangle surfel refresh, zero denoiser. |
| **Phase 2** | **2 Best GTX GI Renderers (`StokesContourGiSequence` + `SurfelCascadeGiSequence`)**: Implement `DeterministicQuadrature.{h,slang}`, `StokesContourGiSequence.{h,cpp}`, `StokesContourGiCompute.slang`, `SurfelCascadeGiSequence.{h,cpp}`, and `SurfelCascadeGiCompute.slang`; wire `WorldRenderEngine::StokesContourGi` (`2`) & `SurfelCascadeGi` (`3`) into `TieredWorldRenderSequence` and `VulkanWorldRenderSequence`. | Both GTX GI renderers beat `0.23` GI RMSE & `0.19` Lit HDR RMSE on `ControlVehicle` + studio + deformed character mesh with zero denoiser and zero SDF volumes. |
| **Phase 3** | **SBVH + Unified GTX/RTX Wavefront Deterministic Path Tracer**: Implement `SpatialSplitBoundingTree.{h,cpp}` (Stich SBVH + YKL DP collapse + `AlignDeformedSurfaceBounds`), `DeterministicPathTraceSequence.{h,cpp}`, `WavefrontPathTraceCompute.slang` (GTX compute + RTX `VK_KHR_ray_query` specialization), and `EdgeStoppingDisplayStack.slang` (`WorldRenderEngine::DeterministicPathTrace = 4u`). | `0` host/device Sobol mismatches; `1` identical FNV-1a/SHA-256 image digest across batch sizes `1, 4, 16` on real-triangle `ControlVehicle` + character scene; `-20%+` fewer triangle intersections vs baseline BVH4. |
| **Phase 4** | **Viewport Wiring, Multi-Profile Build & Visual Exhibit**: Expose `WorldRenderEngine` (`0..4`) and `SolarShadowAlgorithm` (`0..5`) selectors in `ViewportPanel` / `ShowcaseGalleryScene`; add `Exhibits/WorldGiShadowPathTraceValidation.cpp` generating comparison sheets under `Exhibits/Images/WorldGiShadowPathTrace/`; verify `Editor`, `Development`, and `Shipping` builds. | All CTests green across `Editor`, `Development`/`Shipping` compile with `0` `Engine/Editor/` leakage, deterministic `.png` sheets + JSON metrics committed & pushed to GitHub. |
