# Real-Mesh GI, Shadow & Path-Tracing Plan

**Status:** Review plan — implementation is intentionally not started until the algorithm choices and validation gates are approved.
**Scope:** Two GTX real-time GI candidates, six mesh-based shadow candidates (five existing zero-SDF modes plus one surfel candidate), and one deterministic path tracer with GTX and RTX execution backends.
**Geometry constraint:** Render and test real triangle meshes, including animated/skinned characters. Do not substitute spheres, boxes, capsules, or other analytic proxy geometry for scene objects.

## Executive recommendation

| Decision | Recommendation |
|---|---|
| Two GTX GI candidates | Implement and compare **triangle-emitter Stokes/contour GI** and **mesh-surfel cascade GI**. Both derive emitters/receivers from actual scene triangles. Keep the existing SH9 emissive field and DDGI cache as baselines, not presumed winners. |
| Two preferred shadow candidates | Benchmark **BVH solar-disc ray query** and **mesh-surfel blocker field + short BVH contact query** against the same reference. Do not declare a winner until the validation scene and quality/performance results are recorded. |
| Other shadow modes | Retain Contact PCSS, plane-aware PCSS, SMRT, PCF, and wide-tree ray query as comparison modes. These are the five currently implemented zero-SDF shadow choices in the audited source. |
| Path tracer | One transport/integrator design and consistent scene/material contract. GTX uses portable software wide-BVH traversal; RTX may use hardware ray queries when supported, with software BVH as the fallback. |
| SDF | **Do not use a 3D voxel SDF in the selected shadow or GI paths.** Keep SDF functionality isolated for unrelated/baking use and, temporarily, as a clearly labeled legacy comparison if needed. |
| Character deformation | Render the actual deformed/skinned triangles and update/refit their acceleration structure. No bone-capsule occluders or proxy limbs. |

“Analytic” integration over a real triangle emitter is not analytic proxy geometry: the triangle remains the actual source mesh. This distinction permits exact/closed-form triangle-light calculations without replacing authored geometry with primitives.

## Repository check and evidence boundaries

The active checkout initially contained only `README.md` at `0946d8e` (`main`); it did not contain the two candidate files named in the request. I inspected the available source snapshot `origin/arena/39c6910b-frontier-1` at `d5bbb81` without switching this working branch. Neither `Exhibits/CleanShadowAndGiCandidates.h`, `Exhibits/CleanEmissiveShadowValidation.cpp`, nor `Documentation/Plans/WorldGiShadowPathTracerPlan.md` exists in that snapshot or its reachable history. The earlier hashes mentioned in conversation (`2d6f15e`, `536cfe1`, `63db120`, `daed305`) are not present in the objects available to this checkout. Therefore, results previously attributed to those files (including RMSE numbers) are not independently verified here and must not be used as acceptance claims.

Relevant code actually present in the inspected source snapshot:

- `Engine/GeometricRaster/DirectLighting/SolarShadowSequence.{h,cpp}` contains the shadow lab. Its enum currently has five non-SDF modes (`ContactPcss`, `PcssPlaneAware`, `ShadowRayMarch`, `PcfBilinear`, `WideTreeRayQuery`) and three legacy SDF modes (`VoxelSdfPenumbra`, `VoxelSdfClipmapPenumbra`, `HybridSdf`). Thus the historic phrase “six shadow algorithms” does not exactly describe this snapshot; the sixth plan candidate is the proposed mesh-surfel method.
- `Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.{h,cpp}` contains DDGI, weighted path-replay/SDF GI routes, and an emissive triangle SH9 field. These are useful existing baselines, but the Stokes-contour and surfel-cascade candidate headers/validation mentioned in the request are absent.
- `Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h` contains shared deterministic quadrature helpers. `Experimental/DeterministicPathTracer/` contains a WebGPU/WGSL reference implementation and validation artifacts; that is not, by itself, proof that a native C++/Slang Vulkan engine path tracer is already integrated.
- The source snapshot includes `Engine/Shaders/DirectLighting/` and `Engine/Shaders/GlobalIllumination/WeightedPathReplay.slang`. Proposed new shader paths below are new work, not existing files.

## Algorithm choices and comparison protocol

### GTX GI: two real-mesh candidates

**GI A — Stokes/contour triangle-emitter integration**

- Extract emissive triangles, material emission, area, orientation, and instance transform from the scene's real mesh/BVH data.
- Evaluate first-bounce diffuse transport with a triangle-boundary/Stokes contour formulation; visibility must query real scene triangles (BVH/contact path), not an SDF or proxy geometry.
- Evaluate deforming characters from the current deformed triangle set; refresh/refit acceleration data when they move.
- If adding a second diffuse bounce, make the receiver patch/triangle sampling and budget explicit; do not silently claim full multi-bounce transport when only a first bounce is calculated.

**GI B — mesh-surfel cascade**

- Build a deterministic, area-weighted sample set from emissive and selected diffuse mesh triangles. Surfel locations/normals/radiance are samples *of* real mesh surfaces, not replacement scene geometry.
- Accumulate irradiance into a documented cascade/cache representation and use visibility/depth tests against the real-mesh BVH to control leaks.
- Specify dynamic-surface invalidation/refit and cache update budgets. Report memory and update cost separately from the per-pixel lookup cost.

**Existing baselines:** the current mesh-emitter SH9 field, DDGI cache, and converged path-trace reference. Keep the same scene, materials, camera, lighting, exposure, and display transform across candidates. The earlier claimed near-tie between Stokes and surfel candidates should be treated as a hypothesis until reproduced from code on this branch.

### Shadows: six-mode lab

Run all six against a common reference and same real-mesh scene:

1. Contact PCSS.
2. Plane-aware PCSS.
3. Shadow-map ray march (SMRT).
4. Bilinear PCF.
5. Wide-tree solar-disc ray query (high-quality mesh reference candidate).
6. **Proposed mesh-surfel blocker field + short-range BVH contact ray** (with surfels extracted from static and updated animated mesh triangles).

The primary correctness reference should be deterministic solar-disc sampling over the actual triangle BVH at a stated sample count. Compare visibility RMSE, penumbra/contact behavior, temporal stability under mesh deformation, per-frame time, memory, and failure cases. A short contact ray only improves local contact; it cannot be described as a full visibility solution. The surfel candidate must be validated for missed thin geometry and stale dynamic surfels.

### Deterministic path tracer: shared design, two execution backends

- Reuse one transport definition: primary/secondary triangle hits, material evaluation, emissive-triangle next-event estimation, MIS, deterministic sample sequence, progressive accumulation, and explicit bounce/sample limits.
- **GTX:** software wide-BVH traversal and compute shader path; keep work queues and memory traffic suited to non-RT hardware. Avoid promising real-time rates before profiling target GTX hardware.
- **RTX:** use hardware ray queries/acceleration structures when the selected Vulkan device exposes the required features. Provide software BVH fallback and a capability-based switch; do not duplicate shading semantics between backends.
- Validate deterministic repeatability for a fixed scene/configuration, convergence against a high-sample reference, emissive visibility, glass/material behavior, animated-mesh updates, and accumulation reset on camera/scene/material changes.
- Denoising is an optional display/reconstruction stage, not part of the ground-truth transport result. Preserve raw accumulation for comparisons.

## Proposed folder structure

```text
Engine/
├── SpatialAcceleration/
│   └── WideBoundingTree.{h,cpp}                 # existing real-triangle acceleration; refit/update API if needed
├── GeometricRaster/
│   ├── DirectLighting/
│   │   ├── SolarShadowSequence.{h,cpp}           # existing five zero-SDF modes; add sixth mode behind an explicit enum
│   │   ├── MeshSurfelShadowSequence.{h,cpp}      # proposed triangle-derived blocker samples / visibility
│   │   └── README.md                             # update the existing subsystem documentation
│   └── GlobalIllumination/
│       ├── StokesContourEmissiveGiSequence.{h,cpp} # proposed mesh-emitter GI candidate A
│       ├── SurfelCascadeEmissiveGiSequence.{h,cpp} # proposed mesh-sample GI candidate B
│       ├── DeterministicIntegrators.h             # existing shared sampling helpers; extend only if needed
│       ├── DeterministicPathTraceSequence.{h,cpp} # proposed common path-trace orchestration
│       └── README.md                             # update subsystem documentation
└── Shaders/
    ├── DirectLighting/
    │   └── MeshSurfelShadow.slang                # proposed, if GPU implementation is selected
    └── GlobalIllumination/
        ├── StokesContourEmissiveGi.slang          # proposed
        ├── SurfelCascadeEmissiveGi.slang          # proposed
        ├── DeterministicPathTrace.slang            # proposed shared shading/transport kernels
        ├── PathTraceAccumulation.slang             # proposed progressive accumulation
        └── PathTraceReconstruction.slang           # optional display-only reconstruction

Exhibits/
├── RealMeshGiShadowPathTraceValidation.cpp        # proposed single reproducible validation entry point
└── Images/RealMeshGiShadowPathTrace/              # generated proof images/metrics (avoid committing bulky output unless policy requires)

Documentation/Plans/
├── WorldGiShadowPathTracerPlan.md                 # this plan
└── README.md                                      # plan index
```

Actual names and placement must be checked against the live branch's naming rules and CMake source registration before implementation. Keep the validation harness and reference outputs separate from engine runtime code.

## Phased implementation and acceptance gates

1. **Recover/confirm source baseline:** bring the intended project sources onto the active branch through the repository's normal sync workflow; verify which existing implementation commit is authoritative. Do not invent missing files from the pasted excerpt as if they already existed.
2. **Mesh validation fixture:** construct a scene from real triangle assets, including an animated/skinned character. Prove that moving/deforming vertices update the visible geometry and BVH; include thin occluders, contact, emissive mesh, and two-bounce receiver arrangements.
3. **Reference and metrics:** implement deterministic BVH solar-disc and emissive NEE reference renders; fix sample counts, randomization/determinism contract, resolution, exposure, and metrics before candidate tuning.
4. **Shadow lab:** port/retain the five existing non-SDF methods, implement the triangle-derived surfel candidate, and run quantitative + visual comparisons. Select top two only after pass/fail thresholds and measured latency are reviewed.
5. **GTX GI candidates:** implement Stokes and surfel cascades as separate selectable engines; compare both against the existing SH9/DDGI baselines and high-sample reference. Record first-bounce and multi-bounce results separately.
6. **Unified path tracer:** implement shared CPU/reference and Slang transport behavior, then software-BVH GTX and hardware-query RTX backends. Validate image parity/convergence and device fallback before UI integration.
7. **Integration:** wire selected modes to world-render configuration and UI only after standalone validation; keep legacy SDF modes unavailable to the chosen path and clearly labeled if temporarily retained for A/B.

**Acceptance:** no analytic proxy geometry; no SDF sampling in selected shadow/GI candidates; no stale animated-mesh acceleration/surfel data; consistent energy units and exposure; repeatable test configuration; every claimed quality/performance number emitted by the checked-in harness and associated with the exact source revision/hardware.

## Decision requested before implementation

Please review the six shadow modes, the two GTX GI candidates, and the proposed shared path-tracer backends. In particular, confirm whether triangle-contour integration over actual emissive mesh triangles meets the “real meshes, no analytic primitives” requirement. No candidate is called “best” until the comparison suite runs on the recovered, authoritative source baseline.
