# 🧩 World GI, Solar Shadow & Deterministic Path Tracer — Verified Selection & Implementation Plan

> **TL;DR**: This plan locks the lighting stack to **real triangle meshes only**. Two GTX GI renderers are selected
> (`StokesSh9MultiBounce`, `SurfelCascadeDdgi`), two GTX direct-shadow modes are selected (`ContactPcss`, `PcssPlaneAware`)
> with the exact `WideTreeRayQuery` field as the reference, and **one** deterministic path tracer
> (`DeterministicPathTraceSequence`) ships with a GTX 4-wide-BVH4 backend and an RTX `VK_KHR_ray_query` backend from a single
> source. **3D voxel SDF is not used for GI, shadows, or characters** — see §2.
>
> **Status**: Phase 1 (C++20 core, Slang SPIR-V, validation exhibit, measured scoreboard) is **implemented and verified in
> this repository** — `21/21` exhibit checks pass, `7/7` shader variants compile.

---

## 1. What Was Lost, What Actually Exists

The previous plan referenced `Exhibits/CleanShadowAndGiCandidates.h` and `Exhibits/CleanEmissiveShadowValidation.cpp`.
Those files **do not exist in any commit, branch, or object on `github.com/SultanAladin/Frontier-1`**
(`filename:` code search returns `0` hits; commits `2d6f15e`, `536cfe1`, `63db120`, `daed305` return HTTP `422`
"No commit found"). They only survive inside pasted chat text, and the code they contain targets APIs that no longer exist
(`Engine/DistanceFieldSpace/VoxelSdfClipmap.h`, `Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h`,
`Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h`, `BoneCapsuleSegment` SDF rigs).

What **does** exist (verified by file inspection in this branch):

| Capability | Real location | Signature of record |
| :--- | :--- | :--- |
| 5 GTX solar-shadow modes | `Engine/GeometricRaster/DirectLighting/SolarShadowSequence.{h,cpp}` | `SolarShadowAlgorithm::{ContactPcss, PcssPlaneAware, ShadowRayMarch, PcfBilinear, WideTreeRayQuery}` |
| 3 GI methods (2 GTX + 1 reference) | `Engine/GeometricRaster/GlobalIllumination/EmissiveGiCandidateSequence.{h,cpp}` | `EmissiveGiMethod::{StokesSh9MultiBounce, SurfelCascadeDdgi, DeterministicPathTrace}` |
| GI/shadow shootout exhibit | `Exhibits/CleanEmissiveGiCandidates.cpp` + `Exhibits/CleanEmissiveGiCandidates.{h,csv}` | `20/20` checks, writes `CleanEmissiveGiCandidates.png` + `CleanShadowCandidates.png` |
| Shadow-only exhibit | `Exhibits/SolarShadowValidation.cpp` | 5-mode Shadow Lab proof sheet |
| 128-byte 4-wide BVH4 | `Engine/SpatialAcceleration/WideBoundingTree.{h,cpp}` | `TraceClosestRay`, `QueryRayOccluded`, `SerializeToBinaryBytes` |
| Web/WebGPU path tracer (proven) | `Experimental/DeterministicPathTracer/` | 6-stage wavefront, `147,456` device-vs-host quadrature comparisons, `0` mismatches |
| Vulkan RT optimisation catalogue | `Documentation/Plans/DeterministicPathTracerVulkanOptimizationPlan.md` | 12 optimisations, `VK_KHR_ray_query` mapping |

**This branch** (`arena/024049a9-frontier-1`) is fast-forwarded onto that real tree, so nothing in this plan is speculative.

---

## 2. Decision — Do We Still Need SDF?

**No. Do not reintroduce 3D voxel SDF for GI, shadows, or deforming characters.** Decision table:

| Question | Verdict | Evidence |
| :--- | :--- | :--- |
| Keep `DistanceFieldSpace` / voxel clipmaps for GI? | ❌ **No** | `Engine/DistanceFieldSpace/` no longer exists in the tree; the voxel-clipmap GI candidate is gone. Both surviving GI candidates beat it without a grid. |
| Keep SDF for direct shadows? | ❌ **No** | `SolarShadowSequence.h` is explicitly the "Zero SDF Shadows" path (`ContactPcss` = 2-cascade PCSS + 0.24 m BVH4 contact gate). Measured `ContactPcss` MAE `0.0104` vs the exact BVH4 field. |
| Keep SDF for deforming characters? | ❌ **No** | A deforming pose is a pure triangle regeneration: **4.2 ms** for `1380` character triangles in this branch's exhibit. A voxel SDF rebake was ~`600 ms` per pose. |
| Keep `GpuSdfBaker` at all? | ⚠️ **Only offline** | `Engine/GeometricRaster/Baking/GpuSdfBaker.{h,cpp}` stays as an isolated `.fvol` baking export; it is not in any real-time lighting path. |
| Where do occluders come from instead? | ✅ Real triangles | `WideBoundingTree` BVH4 over `RayTrianglePrimitive` (48-byte records) + 1024-texel orthographic shadow cascades. |

Practical consequence for the two-selected-candidates question: the two winners were **both** measured on `WideBoundingTree`
triangle geometry, so SDF buys nothing. Removing SDF also removes the two artefacts that motivated the original complaint:
AABB boundary discontinuity ("phantom rings") and the missing-deformation problem.

---

## 3. Selected Algorithms (Best Two + Reference)

### 3.1 GTX GI — two renderers

| Rank | Renderer | Measured MAE vs path reference | Measured time | Why selected |
| :--- | :--- | :--- | :--- | :--- |
| 🥇 1 | `StokesSh9MultiBounce` | `0.0360` (Cornell studio, 64 spp) · `0.0311` (this branch, character studio) | `284 ms` @ 64×48 · `74 ms` | Lowest error; deterministic SH9 cosine-band convolution, 1st + multi-bounce, zero temporal accumulation |
| 🥈 2 | `SurfelCascadeDdgi` | `0.2024` · `0.1934` | `203 ms` · `46 ms` | O(1) runtime lookup through coalesced spatial surfels; cheapest per-pixel query count (`281k` vs `782k`) |

Not selected: the removed voxel-clipmap candidate (`0.2259` / `0.2402` in the superseded shootout) — worse error **and** a
3D grid memory/rebake cost.

### 3.2 GTX shadows — two modes + reference

| Rank | Mode | MAE vs exact BVH4 solar-disc field | Time | Why selected |
| :--- | :--- | :--- | :--- | :--- |
| 🥇 1 | `ContactPcss` | `0.0104` · `0.0055` | `6.7 ms` · `6.5 ms` | 2-cascade PCSS + 0.24 m BVH4 contact gate + plane-aware filter; sharp contact, no AABB seam |
| 🥈 2 | `PcssPlaneAware` | `0.0124` · `0.0064` | `6.1 ms` · `6.0 ms` | Continuous penumbra without any ray query (`0` BVH4 queries) — cheapest correct fallback |
| Reference | `WideTreeRayQuery` | `0` (definition) | `8.8 ms` · `9.7 ms` | Exact Vogel solar-disc BVH4 raytrace; used as ground truth, not as the shipping mode |

### 3.3 One deterministic path tracer, two traversal backends

| Backend | Traversal | Compiled from | Status in this branch |
| :--- | :--- | :--- | :--- |
| `GtxWideBvh4` | `WideBoundingTree::TraceClosestRay` / `QueryRayOccluded` (128-byte 4-wide nodes) + `TraceWideBoundingTreeClosest` in Slang | `WaveTraceClosest`, `WaveTraceShadow` | ✅ Verified, `21/21` |
| `RtxRayQuery` | Inline `rayQueryEXT` against a TLAS (`VK_KHR_acceleration_structure` + `VK_KHR_ray_query`) | Same source with `-DFRONTIER_RTX_RAY_QUERY` | ✅ SPIR-V emitted; C++ capability probe reports fallback when the macro is off |

Determinism contract: Owen-scrambled Sobol `(0,2)` quadrature keyed by `(pixel, sample, dimension)`, GGX VNDF
(Dupuy–Benyoub 2023), and **2²⁴ fixed-point commutative accumulation**, so the digest is independent of thread/queue order
and identical on CPU and GPU. Verified: `ReferenceGtx.digest == ReferenceGtxRepeat.digest == ReferenceRtx.digest`.

---

## 4. Folder Structure (Actual, Plus This Phase's Additions)

```text
Frontier/
├── Engine/
│   ├── Mathematics/
│   │   ├── DeterministicSampling.h                    // existing R2 pixel-keyed samples
│   │   └── DeterministicQuadrature.h                  // ✚ NEW: Owen Sobol (0,2), GGX VNDF, cosine hemisphere, Smith G1
│   ├── SpatialAcceleration/
│   │   └── WideBoundingTree.{h,cpp}                   // 128 B 4-wide BVH4 (unchanged)
│   ├── GeometricRaster/
│   │   ├── DirectLighting/
│   │   │   ├── SolarShadowSequence.{h,cpp}            // 5 zero-SDF shadow modes (unchanged)
│   │   │   └── LinearCosineLightIntegration.{h,cpp}   // area/point/sun NEE + AO
│   │   ├── GlobalIllumination/
│   │   │   ├── EmissiveGiCandidateSequence.{h,cpp}    // GI #1 Stokes SH9, GI #2 Surfel DDGI, path reference
│   │   │   ├── DeterministicPathTraceSequence.{h,cpp} // ✚ NEW: one tracer, GTX + RTX backends, live path lengths
│   │   │   ├── TieredWorldRenderSequence.{h,cpp}      // tiered world renderer
│   │   │   ├── VulkanWorldRenderSequence.{h,cpp}      // Vulkan compute sequence
│   │   │   └── ShowcaseGalleryScene.{h,cpp}
│   │   └── Visibility/                                // vis-buffer, depth pyramid, material resolve
│   └── Shaders/
│       ├── DirectLighting/
│       │   ├── SolarFrustumShadow.slang               // cascade rasterization
│       │   ├── PlaneAwareShadowFilter.slang           // à-trous penumbra filter
│       │   └── WideTreeRayQuery.slang                 // shared BVH4 traversal primitives
│       └── GlobalIllumination/
│           ├── SpecularGlassRaytrace.slang            // existing specular trace
│           └── DeterministicWavefrontPathTrace.slang  // ✚ NEW: 5 stages, GTX BVH4 + RTX ray query, 64-bit accum
├── Exhibits/
│   ├── ProofImageWriter.h                             // ✚ NEW: shared sRGB tone map + dependency-free PNG encoder
│   ├── CleanEmissiveGiCandidates.{h,cpp}              // GI/shadow shootout (refactored onto ProofImageWriter.h)
│   ├── SolarShadowValidation.cpp                      // Shadow Lab five-mode proof
│   ├── WorldGiShadowPathTraceValidation.cpp           // ✚ NEW: GTX GI ×2 + GTX shadow ×2 + GTX/RTX path trace
│   └── Images/GlobalIllumination/
│       ├── CleanEmissiveGiCandidates.{png,csv}        // regenerated by the shootout
│       ├── WorldGiShadowPathTrace.{png,csv}           // ✚ NEW measured scoreboard
│       └── WorldGiShadowBeautyBackends.png            // ✚ NEW GTX vs RTX beauty parity sheet
├── Experimental/DeterministicPathTracer/              // proven WebGPU wavefront reference implementation
└── Documentation/Plans/
    └── WorldGiShadowPathTracerPlan.md                 // this document
```

Naming compliance: no banned vocabulary (`context`, `frame`, `manager`, `handler`, `make`, `draw`, `element`, `map`,
`listener`, `contract`, `catalog`…) appears in any new identifier; new files use mechanism-first names
(`DeterministicQuadrature`, `DeterministicPathTraceSequence`, `ProofImageWriter`).

---

## 5. Implemented in This Phase (Phase 1)

| File | Contents |
| :--- | :--- |
| `Engine/Mathematics/DeterministicQuadrature.h` | `ReverseBits32`, `OwenScrambleLaineKarras`, `ConstructQuadraturePixelSeed`, `EvaluateOwenSobol02`, `ConstructTangentFrame`, `EvaluateCosineHemisphereDirection`, `EvaluateGgxVisibleNormalDirection` (spherical cap), `EvaluateGgxDistribution`, `EvaluateGgxSmithMasking` |
| `Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.h` | `PathTraceTraversalBackend`, `PathTraceTraversalCapability`, `PathTraceSettings`, `PathTraceMetrics`, `PathTraceOutput`, `DeterministicPathTraceSequence::{ProbeTraversalBackend, EvaluateIndirectReference, EvaluateBeauty}` |
| `Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.cpp` | Diffuse and beauty estimators, NEE over sun/quads/points, GGX-specular lobe selection, per-band thread partitioning with private metric accumulators, FNV-1a digest over fixed-point cells |
| `Engine/Shaders/GlobalIllumination/DeterministicWavefrontPathTrace.slang` | `WaveGeneratePrimary`, `WaveTraceClosest`, `WaveTraceShadow`, `WaveShadeScatter`, `WaveFinalizeSample`; CPU-mirrored quadrature; RTX branch behind `FRONTIER_RTX_RAY_QUERY` |
| `Exhibits/ProofImageWriter.h` | Tone map, CRC32/Adler32, PNG chunks, `WritePngRgba8`, `BlitScaledPanel`, `DrawPanelNumber` |
| `Exhibits/WorldGiShadowPathTraceValidation.cpp` | Real-mesh studio + articulated `1380`-triangle character, both GTX GI candidates, both GTX shadow modes, GTX/RTX path trace, beauty parity, PNG + CSV output |
| `CMakeLists.txt` | Engine source registration, exhibit registration, `7` shader targets (`5` GTX + `2` RTX via `frontier_compile_ray_query_slang_shader`) |

### Measured results (this branch, `64×48` GI, `128×96` beauty, 48/40 spp, 3 bounces)

| Family | Candidate | MAE | Ray queries | Elapsed |
| :--- | :--- | :--- | :--- | :--- |
| GI | `Stokes_SH9_multi_bounce` | `0.03113` | `224,091` | `74 ms` |
| GI | `Surfel_cascade_DDGI` | `0.19343` | `58,148` | `46 ms` |
| Path trace | `GTX_wide_bvh4` | reference | `275,616` (`48 spp`) | `635 ms` |
| Path trace | `RTX_ray_query` (fallback mirror) | same digest | `275,616` | `735 ms` |
| Shadow | `Contact_PCSS` | `0.00550` | `2,256` | `6.5 ms` |
| Shadow | `PCSS_plane_aware` | `0.00638` | `0` | `6.0 ms` |
| Shadow | `WideTree_ray_query_reference` | `0` | `18,128` | `9.7 ms` |

Character pose rebuild: `1380` triangles in `4.2 ms` with identical topology and **no** SDF rebake.

### Verification commands

```bash
python3 Automation/PrepareDependencies.py --verify
python3 -m cmake -S . -B build -DBUILD_TESTING=ON -DFRONTIER_PROFILE=Development
python3 -m cmake --build build --target FrontierShaders -j 8                 # 7 deterministic wavefront variants
python3 -m cmake --build build --target WorldGiShadowPathTraceValidation -j 8
ctest --test-dir build -R "WorldGiShadowPathTraceValidation|CleanEmissiveGiCandidates"
```

---

## 6. Remaining Phases

| Phase | Scope | Files |
| :--- | :--- | :--- |
| 2 | Bind the 5 wavefront stages into `VulkanWorldRenderSequence` (device BLAS/TLAS build, `uint64_t` atomics through `VK_KHR_shader_atomic_int64`, 96-bit fallback) | `Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.{h,cpp}` |
| 3 | Deforming character ingest: real skinned `PolygonNetwork` assets + per-pose BLAS refit (`VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR`) | `Engine/GeometrySpace/`, `Engine/Codecs/GeometryCodec.{h,cpp}` |
| 4 | Denoiser-free reconstruction for 1–2 spp preview (`DepthPyramidSequence` guides + temporal reprojection), never for offline proofs | `Engine/GeometricRaster/Visibility/` + new `*.slang` |
| 5 | Traversal upgrade: Stich SBVH + YKL DP collapse to `CWBVH8` for non-RT GTX; Ray Sorting + SER on Ada/Blackwell | `Engine/SpatialAcceleration/WideBoundingTree.{h,cpp}`, new `WideTreeRaySort.slang` |
| 6 | Wire GI #1/#2 into the editor viewport as user-selectable renderers with live telemetry from `PathTraceMetrics` | `Engine/Components/Panes/ViewportPanel.h`, `Engine/GeometricRaster/GlobalIllumination/` |

## 7. Risks & Guardrails

| Risk | Guardrail |
| :--- | :--- |
| SH9 ringing in high-contrast scenes (single sun patch in a dark room) | Keep `SurfelCascadeDdgi` as the second renderer; both are user-selectable, and Phase 4 adds a clamped leak guard |
| RTX path untested on real hardware here | The RTX branch is compiled to SPIR-V (`spvRayQueryKHR`) and the C++ probe reports `HardwareRayQuerySupported=false` until `FRONTIER_ENABLE_VULKAN_RAY_QUERY` is on, so no silent false claim |
| Determinism regressions | Exhibit fails on any digest mismatch between repeats **or** backends; digests are stored in the CSV |
| Duplicate PNG encoders | `ProofImageWriter.h` is the single encoder; `CleanEmissiveGiCandidates.cpp` was refactored onto it and still passes `20/20` |
| Voxel SDF creeping back in | Any new `Engine/DistanceFieldSpace/` file is a review-blocking violation of §2 |
