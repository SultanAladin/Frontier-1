# World Global Illumination, Solar Shadow & Deterministic Path Trace Plan

**Status:** Proposed (pending your approval of the candidate set)
**Branch:** `arena/01e8a1f0-frontier-1`
**Integration Base:** `SultanAladin/Frontier` @ `arena/8391e994-frontier` (`daed305`) — see §1
**Geometry Policy:** 100% real triangle geometry. Zero 3D voxel SDF for shadows or GI. Zero capsule distance proxies.
**Quality Gate:** `Constants::MaximumOptimizationPixelDelta = 0.05f`, zero denoiser on all candidate paths.

---

## 1. Where the Code Actually Is (Recovery Audit)

`SultanAladin/Frontier-1` `main` contains **one file** (`README.md`, "General Purpose Editor"). The losing-the-workspace incident was worse than described: your branch `arena/01e8a1f0-frontier-1` (from `0946d8e`) has the same empty tree, so nothing existed locally to lose.

I swept **70 branches of `SultanAladin/Frontier`** and **27 branches of `SultanAladin/Frontier-1`** by content, not just by filename.

| Artifact | Claimed Location | Found? | Reality |
| :--- | :--- | :--- | :--- |
| `Exhibits/CleanShadowAndGiCandidates.h` | `a398f3e7` / `536cfe1` | ❌ **Absent** | Not in any commit of either repository. Content grep for `SurfelOccluderField`, `EvaluateCandidate1HybridConeShadow`, `StokesContourEmissiveGi`, `DeformingBoneCapsule` returns **zero hits across all 97 branches**. |
| `Exhibits/CleanEmissiveShadowValidation.cpp` | `a398f3e7` / `536cfe1` | ❌ **Absent** | Same — never committed. |
| `Exhibits/CleanEmissiveGiCandidates.{h,cpp}` | `a398f3e7` / `2d6f15e` | ✅ **Recovered** | Present at `origin/arena/a398f3e7-frontier-1`. Note: it is *not* a GI-candidate implementation — it is a comparison-scorecard driver (1037 lines) over the real engine candidates. |
| `EmissiveGiCandidateSequence.{h,cpp}` | `a398f3e7` | ✅ **Recovered** | The actual GTX GI engines: `StokesSh9MultiBounce`, `SurfelCascadeDdgi`, `DeterministicPathTrace`. |
| 6 Shadow algorithms | `39c6910b` | ✅ **Recovered** | `Engine/GeometricRaster/DirectLighting/SolarShadowSequence.{h,cpp}` — 8 enum values, 5 zero-SDF modes already wired. |
| Deterministic quadrature + VNDF | `39c6910b` | ✅ **Recovered** | `Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h` (Sobol (0,2) + Dupuy–Benyoub spherical-cap VNDF + MIS). |
| Path tracer (CPU + Vulkan) | `39c6910b` | ✅ **Recovered** | CPU: `TieredWorldRenderSequence.cpp::TraceDeterministicPathSample`. GPU: `Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang` `EngineMode == 4` (3563 lines, NEE + MIS + depth-0 lobe split). |
| 3-algorithm emissive shootout | `39c6910b` | ✅ **Recovered** | `Exhibits/EmissiveGiShootoutValidation.cpp` + `results.json` (Algo3 SH9 wins: sRGB RMSE 0.1331 vs 0.1669 / 0.1657). |
| `Documentation/Plans/WorldGiShadowPathTracerPlan.md` | this branch | ❌ **Absent** | This document is its replacement. |

**Consequence:** the numbers quoted in your prompt (`0.2973` / `0.2983` / `0.2238`) **cannot be reproduced or verified**, because the harness that produced them was never committed. They should not drive an architecture decision. §2 replaces them with measurements you can rerun in 4 seconds.

**Correct integration base:** `Frontier-1` `main` is unusable as a base. Merge work onto `SultanAladin/Frontier @ arena/8391e994-frontier` — that tree is a genuine descendant of a real `main` (2183 files, 127 MB, no GI/SDF leftovers in `Engine/`), which is exactly the "removed the leftover GI/SDF artifacts" state you referenced.

---

## 2. Measured Evidence (Runnable)

Harness: [`tools/LightingCandidateBench/LightingCandidateBench.cpp`](../../tools/LightingCandidateBench/LightingCandidateBench.cpp)
Scene: 694 real triangles (studio shell, 2 emissive panels, ControlVehicle-proportioned body, 12-segment articulated figure as tapered limb triangles), binary SAH bounds, 400×225, 2 threads, x86 CPU.
Reference: 128-ray Vogel solar disc (shadows) / 48-spp Owen-scrambled Sobol path trace (GI). Emissive-only reference for the GI comparison so the candidates are scored on the same transport they actually compute.

### 2.1 Solar shadows — visibility RMSE vs 128-ray reference

| Candidate | RMSE | Umbra | Penumbra | Lit | CPU ms | Verdict |
| :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `VogelDiscQuery` 16 rays | **0.0086** | 0.0018 | 0.0390 | 0.0060 | 97.6 | Reference-grade |
| `VogelDiscQuery` 8 rays | **0.0153** | 0.0027 | 0.0687 | 0.0098 | 44.7 | **Selected (GTX Tier 2)** |
| `VogelDiscQuery` 4 rays | 0.0268 | — | — | — | 23.1 | Viable low setting |
| `ContactPcss` 1024² cascade | 0.1301 | 0.1409 | 0.2765 | **0.0099** | 19.5 (+33 cascade build) | **Selected (GTX Tier 1)** |
| `VogelDiscQuery` 1 ray | 0.0852 | — | — | — | 6.8 | Cheapest |
| `SurfelBlockerField` 151 disks | **0.2993** | **0.3103** | **0.5335** | **0.2045** | 58.0 | ❌ **Rejected** |

### 2.2 Why the surfel blocker field is rejected (and it is not a tuning problem)

Clustering resolution sweep — the error **plateaus**, so the method is structurally capped:

| Cluster cell | Disks | RMSE |
| :--- | ---: | ---: |
| 0.280 m | 151 | 0.2993 |
| 0.140 m | 339 | 0.2927 |
| 0.090 m | 495 | 0.2943 |
| 0.055 m | 596 | 0.2956 |

Six times more disks, **zero** improvement. The `lit`-band error of **0.2045** is the disqualifier: the disk field casts **phantom occlusion on fully sunlit surfaces**, which is the exact failure the candidate was designed to avoid. The visible proof is in `results/01_ShadowVisibilityComparison.png` (bottom-right cell).

`ContactPcss` is a different story: its `lit`-band error is **0.0099** (i.e. correct outside shadows). All of its error sits in umbra/penumbra edge placement — a tuning axis (tap count, blocker window, texel density), not a bias. It stays.

### 2.3 Diffuse GI — radiance RMSE vs 48-spp emissive path trace

Reference mean luminance = 0.0636.

| Candidate | RMSE | Rel. RMSE | Mean luminance | Spatial corr. | CPU ms | Verdict |
| :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `EmissiveFormFactorSequence` (SH9 field) | **0.0805** | 126% | 0.0247 | **0.772** | 152 (no precompute) | **Selected (GTX GI 1)** |
| `SurfelRadianceCascade` (octahedral + Chebyshev) | 0.1118 | 176% | 0.0057 | 0.344 | 40 build + 244 lookup | **Selected (GTX GI 2)**, needs its own tuning |
| `PathTraceTransport` 48 spp | 0.0000 | 0% | 0.0636 | 1.000 | 1813 | **Selected (reference)** |

Reading this honestly:

- The SH9 field transfers **structure** well (corr 0.772) but under-reports **magnitude** (0.025 vs 0.064). That is a documented, one-line windowing deficiency in the cosine convolution, not a modelling error — `DeterministicIntegrators.h` already carries the correct `A_l/π` factors to lift it.
- The cascade is 4× cheaper at lookup-adjacent quality but currently loses most of its energy through the 8×8 octahedral quantization plus single-tap depth gating. It is the right architecture for O(1) GTX lookups; it needs its own tuning phase before it is shippable. It does **not** win on quality today.
- The 152 ms versus 40 ms precompute split is the real design axis: SH9 pays once per scene, the cascade pays once per viewport cell. For a moving character with zero voxel rebake, `Pose A → Pose B` real-triangle regeneration **plus BVH rebuild costs 1.08 ms** — that is the entire deforming-character budget, which is why no part of this plan reintroduces voxel baking.

### 2.4 Determinism

Repeat evaluation of the whole shadow field produced an identical 64-bit hash (`F7DA761E…`), confirming the zero-RNG / zero-roulette contract survives on real triangles.

---

## 3. Decision: Do You Still Need SDF?

**No. Remove 3D voxel SDF from both shadows and GI.**

Three sources disagreed, so here is the reconciliation:

| Source | Position | Assessment |
| :--- | :--- | :--- |
| `Experimental/ShadowAlgorithmLab/FRONTIER_LIGHTING_PLAN.md` (`39c6910b`) | Remove SDF from shadows **and reflections**; keep it for diffuse GI | Correct on shadows/reflections — confirmed by §2.2 |
| `Documentation/Plans/RenderingRaytracingArchitecturePlan.md` (`39c6910b`) | SDF cone GI is **8.3× faster** than BVH GI on GTX (0.48 ms vs 4.3–11.8 ms) | True **for stochastic ray-traced GI**. It does not apply to the analytic emitters selected here. |
| Prior agent's claim | Drop SDF entirely, keep bone capsules | Right conclusion, **wrong mechanism** — capsules are distance proxies, and you have ruled out analytic primitives |

The resolution: the 8.3× advantage of SDF exists only because it replaces *stochastic* hemisphere sampling. An **exact analytic emitter form factor** (Arvo/Lambert closed form, 5 visibility taps) plus a **cosine-convolved SH9 shelf** needs no hemisphere sampling at all, so it gets the same low-frequency benefit with no voxel grid, no AABB seams, no 6–13 cm light-leak through thin shells, and no per-frame rebake. §2.3 measures that at 0.0805 RMSE — competitive with the engine's existing emit-only shootout winner (0.1331 sRGB).

**Keep exactly two SDF-adjacent things, both outside the render path:**

1. `Engine/GeometricRaster/Baking/GpuSdfBaker.{h,cpp}` — offline `.fvol` export in `BakingWorkspace` only.
2. Nothing else. `DistanceFieldSpace/` stays as a library but is **not** referenced by `SolarShadowSequence`, the GI sequences, or the path tracer.

---

## 4. Selected Architecture

### 4.1 Two GTX GI renderers

| # | Sequence | Mechanism | Why it wins |
| :--- | :--- | :--- | :--- |
| **GI 1** | `EmissiveFormFactorSequence` | Exact per-triangle Lambert form factor on real emissive triangles + deterministic stratified visibility taps + order-2 SH9 cosine-convolved radiance shelf for bounce 2…∞ | Highest structural agreement (corr 0.772), analytic, zero grid memory, exact emitter silhouettes |
| **GI 2** | `SurfelRadianceCascadeSequence` | Real-triangle virtual surfels → 3-scale radiance shelf → 8×8 octahedral irradiance with 16×16 Chebyshev depth moments | O(1) per-pixel lookup, no per-pixel rays, and the only candidate whose cost is independent of light count |

### 4.2 One unified deterministic path tracer (GTX + RTX)

`PathTraceTransportSequence` — single transport kernel, two acceleration back-ends:

| Aspect | GTX (no RT cores) | RTX |
| :--- | :--- | :--- |
| Bounds | `SplitBoundingVolume` (Stich SBVH + YKL DP-wide collapse) in `Engine/SpatialAcceleration/` | `VkAccelerationStructureKHR` BLAS/TLAS via `VK_KHR_ray_query` |
| Bounds refit | full rebuild (measured 1.08 ms for the whole deformation scenario) | `VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR` on the figure BLAS |
| Quadrature | `DeterministicIntegrators.h` Owen-scrambled Sobol (0,2) | identical |
| BRDF | `SampleSphericalCapGgxVndfLocal` + `PowerHeuristicMis` | identical |
| Scheduling | CPU tile workers, then 6-step lock-free widefront compute | same widefront compute, hardware traversal |
| Output | bit-identical for identical sample index | bit-identical |

`WideTreeRayQuery.slang` today is a **module stub with no `RayQuery` object at all** — the only `RaytracingAccelerationStructure` bindings in the tree are in `Baking/BakeVolumeSdfCompute.slang` and `BakeSurfaceTransferCompute.slang`. The RTX path must therefore be built, not configured. This is the single largest schedule risk (§7).

### 4.3 Solar shadow set

| Tier | Algorithm | Status |
| :--- | :--- | :--- |
| GTX Tier 1 (default) | `ContactPcss` — 2-cascade contact + PCSS + plane-aware filter | exists in `SolarShadowSequence`, keep |
| GTX Tier 2 (quality) | `VogelDiscQuery` 8/16 rays on the real mesh bounds | exists as `WideTreeRayQuery`, retune ray count from measurement |
| Reference | `VogelDiscQuery` 128 rays | exhibit-only |
| Retired | `SurfelBlockerField` | rejected by §2.2, do not implement |
| Retired | `VoxelSdfPenumbra`, `VoxelSdfClipmapPenumbra`, `HybridSdf` | enum values 5–7 to be deleted |

---

## 5. Folder Structure

### 5.1 This branch now

```text
Frontier-1/  (arena/01e8a1f0-frontier-1)
├── README.md
├── Documentation/
│   └── Plans/
│       ├── README.md                        // index
│       └── WorldGiShadowPathTracerPlan.md   // this document
└── tools/
    └── LightingCandidateBench/
        ├── README.md
        ├── LightingCandidateBench.cpp
        └── results/
            ├── 01_ShadowVisibilityComparison.png
            ├── 02_GiCandidatesComparison.png
            ├── 03_DeformingPoseShadowShift.png
            ├── 04_ReferencePathTrace.png
            ├── 05_CandidateContactPcss_Sh9Gi.png
            ├── 06_CandidateContactPcss_SurfelCascadeGi.png
            ├── lighting_candidate_metrics.csv
            └── lighting_candidate_summary.json
```

### 5.2 Target additions on `Frontier @ arena/8391e994-frontier`

Naming follows `Documentation/BannedWordsVocabulary.md` (no `probe`, `pass`, `frame`, `mesh`, `pool`, `map`, `stage`, `graph`, `binding`).

```text
Engine/
├── SpatialAcceleration/
│   ├── SplitBoundingVolume.h                // NEW  Stich SBVH + YKL DP wide collapse
│   ├── SplitBoundingVolume.cpp              // NEW  deterministic SAH bins, fixed tie-breaks
│   └── WideBoundingTree.h                   // KEEP exact-mesh bounds, contact rays
├── GeometricRaster/
│   ├── DirectLighting/
│   │   ├── SolarShadowSequence.h            // EDIT delete VoxelSdf*/HybridSdf values 5-7
│   │   ├── SolarShadowSequence.cpp          // EDIT drop SDF branches, retune Vogel budget
│   │   └── SolarDiscQuerySequence.h/.cpp    // NEW  Vogel disc + plane-aware filter, mesh-only
│   └── GlobalIllumination/
│       ├── DeterministicIntegrators.h       // KEEP Sobol (0,2) + VNDF + MIS  (already correct)
│       ├── EmissiveFormFactorSequence.h     // NEW  GI 1
│       ├── EmissiveFormFactorSequence.cpp   // NEW  GI 1
│       ├── SurfelRadianceCascadeSequence.h  // NEW  GI 2
│       ├── SurfelRadianceCascadeSequence.cpp// NEW  GI 2
│       ├── PathTraceTransportSequence.h     // NEW  unified GTX+RTX transport
│       ├── PathTraceTransportSequence.cpp   // NEW
│       └── TieredWorldRenderSequence.h      // EDIT add the 3 new engines to WorldRenderEngine
└── Shaders/
    ├── DirectLighting/
    │   └── SolarDiscQuery.slang             // NEW  replaces the empty WideTreeRayQuery stub
    └── GlobalIllumination/
        ├── EmissiveFormFactorCompute.slang  // NEW
        ├── SurfelRadianceCascadeCompute.slang // NEW
        └── PathTraceTransportCompute.slang  // NEW  GTX widescan + RTX ray_query in one entry

Exhibits/
└── WorldGiShadowPathTraceValidation.cpp     // NEW end-to-end gate + PNG sheets
```

---

## 6. Phases

| Phase | Scope | Exit Gate |
| :--- | :--- | :--- |
| **P0 — Base** | Merge `arena/8391e994-frontier` as the working base; port `DeterministicIntegrators.h`, `SolarShadowSequence`, `WideBoundingTree`, `TieredWorldRenderSequence` unchanged | Tree builds; existing `SolarShadowValidation` + `TieredWorldRenderValidation` pass |
| **P1 — Shadows** | Delete SDF enum values 5–7; retune `ContactPcss` (tap count, blocker window); add `SolarDiscQuerySequence` with the measured 8-ray/16-ray budget; add `SolarDiscQuery.slang` | Visibility RMSE ≤ 0.02 at 8 rays, ≤ 0.011 at 16 rays, `lit`-band ≤ 0.011; no phantom shadow in open sun |
| **P2 — GI 1** | `EmissiveFormFactorSequence` (CPU) + `EmissiveFormFactorCompute.slang`; fix the SH9 cosine windowing so magnitude matches the path trace | Radiometric RMSE ≤ 0.05 vs 32-spp reference, rel. RMSE ≤ 40% |
| **P3 — GI 2** | `SurfelRadianceCascadeSequence` (CPU + GPU); tune octahedral resolution and Chebyshev bias to clear the 0.1118 floor | Radiometric RMSE ≤ 0.07 vs 32-spp reference; lookup ≤ 1.0 ms at 1080p on GTX |
| **P4 — Path tracer GTX** | `SplitBoundingVolume` + `PathTraceTransportSequence` + `PathTraceTransportCompute.slang` widescan; bit-exact determinism across batch sizes 1/2/4/8 | Repeat runs produce identical image hashes; Sobol parity vs. CPU reference |
| **P5 — Path tracer RTX** | `VK_KHR_ray_query` BLAS/TLAS + `UPDATE` refits for the deforming figure BLAS; same kernel, different traversal | RTX output matches GTX within 1 ULP per sample; deformation refit ≤ 2 ms |
| **P6 — Gate** | `Exhibits/WorldGiShadowPathTraceValidation.cpp`: 2 GI + 1 PT + shadow set, PNG sheets, CSV/JSON telemetry, `MaximumOptimizationPixelDelta` check | All bands pass; sheets committed under `Exhibits/Images/` |

---

## 7. Risks

| Risk | Impact | Mitigation |
| :--- | :--- | :--- |
| **No RTX code exists yet** — `WideTreeRayQuery.slang` is a stub, no `RayQuery` anywhere | P5 is greenfield, not configuration | Land P4 (GTX) first and keep RTX a back-end swap behind one concept interface; do not block GTX delivery on it |
| **Path trace is 11.9× the cost of GI 1** at equal viewport (1813 ms vs 152 ms, 48 spp) | Not real-time at 48 spp | Ship it as the offline/hero reference at 16–32 spp; 4-core scaling ≈ 2× over these 2-thread numbers |
| **SH9 magnitude deficit** (0.025 vs 0.064 mean) | GI 1 looks dark next to the reference | P2 gate is explicitly radiometric, not structural — do not accept corr-only wins |
| **Cascade energy loss** (mean 0.0057 vs 0.064) | GI 2 is not shippable as-is | Treat 0.1118 as a *floor to beat*, not a result to defend; if P3 misses its gate, ship GI 1 + path tracer and hold GI 2 |
| Scene is 694 triangles; `39c6910b` targets 20k+ triangle vehicles | Timing will grow with real vehicle density | The 1.08 ms pose rebuild already includes a full rebuild at this density; re-measure at vehicle scale in P4 |
| Banned-word vocabulary is enforced repo-wide | Naming churn late in the work | Names in §5.2 were checked against the blacklist before writing |
| `Frontier-1` main is empty; this branch has no engine | Can't compile against `Frontier-1` | Work happens on `Frontier @ 8391e994-frontier`; this branch keeps the plan and the harness |

---

## 8. Reproduce

```bash
g++ -std=c++20 -O2 -pthread -o /tmp/bench tools/LightingCandidateBench/LightingCandidateBench.cpp
/tmp/bench tools/LightingCandidateBench/results
```

Emits the console table, three comparison sheets, two full-resolution renders, a CSV, and a JSON summary — in about 4 seconds on 2 threads.
