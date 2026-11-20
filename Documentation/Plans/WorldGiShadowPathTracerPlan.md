# Real-mesh world GI, shadows and deterministic path tracing — review plan

**Status:** Proposed; implementation and selection are gated on review and measurements.

**Baseline:** `SultanAladin/Frontier` commit `daed3058c274694020c6e34e4f4f2e6b5d8bd9e6`, restored as a snapshot on this session's `Frontier-1` branch.

**Comparison sources:** `Frontier` commits `2d6f15e`, `536cfe1`, `63db120`; `Frontier-1` branch `arena/39c6910b-frontier-1` at `d5bbb81`.

## Decision in plain language

Render actual imported triangle meshes, including animated character triangles. Do not replace characters or vehicles with analytical capsule, sphere or box occluders. A surfel is an *approximation sampled from a real surface*, not a substitute source mesh: keep it only as an optional GI/shadow cache if its leakage and update costs pass tests. Analytical integration of light emitted by a real triangle is still allowed; it does not make scene geometry an analytic primitive.

**Proposed GI candidates for GTX:** (1) Stokes-contour emissive triangle integration plus diffuse bounce patches; (2) mesh-derived surfel radiosity plus an irradiance cache. They are candidates, not proven winners on GTX. Both need SDF-dependent visibility and cache operations replaced with triangle-based visibility before comparison; their old measurements will not automatically carry over.

**Proposed shadow finalists:** (1) Contact PCSS for a fast shadow-map path; (2) deterministic triangle BVH solar-disc rays for a quality/reference path. Compare these alongside the other existing modes and an experimental mesh-derived surfel method. The finalists are provisional: final selection depends on real animated scenes, GPU time, memory and quality. Either finalist can run on GTX using raster or software BVH traversal; RTX hardware ray query is an optional acceleration path when supported.

**One path tracer:** a single material/lighting/integrator contract with a software BVH compute backend (GTX and other Vulkan compute hardware) and, where supported, a Vulkan hardware-ray-query backend (RTX). The existing WebGPU path tracer is a reference/prototype, not an already integrated Vulkan path tracer.

**SDF answer:** a 3D SDF volume is not architecturally required for these proposed triangle-visibility approaches, and should not be reintroduced as a required runtime dependency. That is a design choice, not a proof that SDF is inferior in every scene. Keep the separate existing baking SDF functionality unless its owners approve removal. The historic GI candidates themselves **do use SDF visibility**; a mesh-based replacement is new work and must be revalidated.

## What exists and what was recovered

- The baseline originally contained neither `Exhibits/CleanShadowAndGiCandidates.h` nor `Exhibits/CleanEmissiveShadowValidation.cpp`, nor their GI companion files. They do exist in `Frontier` history at `536cfe1` (GI companions introduced in `2d6f15e`) and were removed in `63db120`. Inspect with `git show 536cfe1:Exhibits/CleanShadowAndGiCandidates.h` and `git show 536cfe1:Exhibits/CleanEmissiveShadowValidation.cpp`; do not blindly copy them into the baseline. They include the removed `Engine/DistanceFieldSpace/VoxelSdfClipmap.h`, removed `DeterministicIntegrators.h`, capsule character geometry, and scene-specific assumptions. The user-supplied source is useful as a design reference, not a buildable drop-in implementation.
- The restored `Engine/GeometricRaster/DirectLighting/SolarShadowSequence.{h,cpp}` declares **five** modes: `ContactPcss`, `PcssPlaneAware`, `ShadowRayMarch`, `PcfBilinear`, `WideTreeRayQuery`. `Experimental/ShadowAlgorithmLab` exposes seven choices: these five categories plus legacy hybrid SDF and baked mesh SDF. The comparison branch `d5bbb81` declares **eight** enum values, adding three SDF variants. The previous answer's claim of six existing zero-SDF engine modes was inaccurate. A mesh-surfel mode would be a **new sixth non-SDF candidate**, not an existing result.
- The baseline already removed runtime distance-field GI/shadow dependencies from `Engine/` at `63db120`; a text search found no `DistanceFieldSpace`, `VoxelSdfClipmap`, `DynamicLocalSdfInstance` or `DeterministicIntegrators` references there. This does not mean all SDF code is obsolete: `Engine/GeometricRaster/Baking/GpuSdfBaker.{h,cpp}` and baking shaders have separate offline uses and are retained. Do not delete the direct-rendering files in the `GlobalIllumination` directory merely because of their directory name.
- `Experimental/DeterministicPathTracer/` contains the WebGPU/JavaScript prototype and `Documentation/Plans/DeterministicPathTracerAuditReport.md` and `DeterministicPathTracerVulkanOptimizationPlan.md` describe past results and proposed Vulkan work. `d5bbb81` additionally has `Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h`; it is not in the restored baseline. No cross-branch migration has yet been performed.

## Historic results — useful clues, not a final selection

The historical `536cfe1` JSON reports 560×315, zero denoiser and one studio/vehicle setup. The GI reference was a 128-sample emissive NEE comparison. It reported normal-GI RMSE `0.2238` for Stokes, `0.2239` for surfels and `0.2259` for the continuous field. The small differences may not generalize. These are mainly emissive-GI candidates, not a demonstration of all-light, multi-bounce production GI.

The separate shadow summary reports visibility RMSE against a 128-sample disc reference: hybrid/capsule `0.298272`, surfel/capsule `0.297254`, clipmap/capsule `0.306751`, previous SDF baseline `0.417013`. Its measured 560×315 CPU times were approximately `617`, `806`, `319`, and `71` ms respectively; these are **not GTX frame times**. The surfel result used bone capsules and a short BVH contact query, not real skinned-mesh shadows, and the paired lit-image comparisons also used different GI methods. It does not establish the fastest or best real-mesh method.

The historic pose-B check has a material bug: `RenderShadowField` captures pose-A `GBufA` even after pose-B `GBufB` is created; pose-B shadows are evaluated at pose-A surface points. A changed image does not verify pose-B pixels. The prototype also uses studio-specific exclusion regions and hard-coded object handling. Do not present its passing checks as validation of a real character renderer. Rebuild fair tests before accepting any claimed ranking.

## Proposed architecture and data flow

### Shared scene and animation

1. Consume actual indexed triangles, per-instance transforms, material/emission IDs, normals/UVs and alpha/transmission semantics from the engine scene. Obtain or implement a character animation/skinning path; the old capsule rig is not a skinning system. Define ownership and a frame-versioned deformed-vertex buffer shared by visibility, shadows, GI and tracing.
2. Update skinned vertex positions before building/refitting acceleration structures and rendering shadow maps; synchronize GPU stages with explicit resource barriers. Handle topology changes, non-uniform transforms, animated emissive materials, mesh removal and instance identity. Double-buffer resources as necessary to avoid reading a partially updated frame.
3. Start with the existing `WideBoundingTree` CPU interface and existing GPU traversal as a correctness baseline; measure whether refit exists and is adequate. Implement software BLAS/TLAS refit or rebuild only after verifying current interfaces, with rebuild thresholds for degraded bounds. Hardware BLAS update requires supported Vulkan features and an acceleration structure initially built with the relevant update flag; topology changes may require rebuilding. Budget memory and synchronization explicitly.
4. Surface samples may be grouped into surfels for a cache, with position, normal, area, material, emissive radiance and mesh/triangle identity. Preserve or regenerate samples coherently on deformation and motion; invalidate on material/topology/lighting changes. Disks and finite form factors approximate transport: they can leak through thin geometry or miss small features, so compare them to triangle rays and allow a safe fallback.

### GI #1 — triangle contour + bounce patches

Extract emissive triangles from real meshes. Use the old clipped spherical-polygon form factor as a starting point for unoccluded diffuse integration, with robust clipping, normal orientation, near-field and degenerate-triangle handling. Evaluate visibility with deterministic triangle BVH samples, not the old SDF cone. Partial occlusion is not exactly represented by multiplying one analytic form factor by a single visibility estimate; quality depends on sampling and must be measured. Mesh-derived diffuse patches may approximate subsequent bounces; establish energy conservation and update policy. Scale to many emissive triangles using a measured light hierarchy or sampling strategy instead of assuming per-pixel full scans are affordable. Add sun/sky, dynamic lights, material response and bounce limits explicitly: the old emissive-only metric is insufficient to claim general GI.

### GI #2 — mesh surfels + irradiance cache

Generate area-weighted surfels from real emissive and diffuse triangles; use deterministic sampling with stable IDs. Compute radiance exchange with triangle visibility, aggregate into a bounded, spatially indexed irradiance cache, and interpolate at receivers with normal/depth or visibility-aware weights. A fixed-size cache lookup can be bounded, but generation, updates, visibility and memory are not free or automatically O(1). Tune cache spacing and update budgets against leaks, temporally stale light, moving characters, thin walls and small emissive sources. The old DDGI/surfel implementation also used hybrid SDF cluster visibility: replacing it changes the algorithm and requires new measurements.

### Direct shadow comparison

Benchmark `ContactPcss`, `PcssPlaneAware`, `ShadowRayMarch`, `PcfBilinear`, and `WideTreeRayQuery` on the same real meshes. First compare raw visibility without denoising; separately report the effect and cost of any spatial/temporal filter already used by a mode. Contact PCSS is the initial GTX speed candidate; BVH multi-ray visibility is the initial quality candidate. Calibrate bias and shadow-map resolution fairly. If added, mesh-derived surfel occlusion is an experimental sixth candidate, with triangle contact rays and controlled per-frame updates. It is approximate, not an exact substitute for triangles, and the historical capsule score cannot be reused for it. Keep legacy SDF modes only in an isolated research comparison if needed, not as a runtime dependency.

### One deterministic path tracer

Use a shared sample schedule, scene/material representation, integrator, bounce limits, light sampling, alpha/transmission semantics and accumulation policy across backends. Port and validate prototype quadrature and BSDF behavior incrementally, retaining a CPU/compute correctness reference. Software GPU traversal uses mesh BVH; optionally use hardware inline ray query only after querying Vulkan extensions and features (`VK_KHR_ray_query`, `VK_KHR_acceleration_structure` and their dependencies). Select software fallback when unavailable; do not assume every GTX supports RT cores or that every RTX exposes the required Vulkan features.

Guarantee repeatable seeds, stable ordering and reproducible output within a specified device/driver/build and scene. Parallel queues and floating-point summation must not silently change accumulation order; avoid claiming bit-identical results across GPU vendors, traversal backends, compilers or drivers. Detect stack/queue overflow and report failures rather than dropping paths. Wavefront dispatch, BVH formats and atomic choices are optimization options only after the simple path passes correctness tests. Denoising is optional display reconstruction, off for raw reference and algorithm comparisons; report it separately.

## Proposed folder structure (new files are illustrative, not implemented)

```text
Engine/
  GeometrySpace/Network/PolygonNetwork.*                [existing scene mesh]
  SpatialAcceleration/
    WideBoundingTree.{h,cpp}                            [existing CPU BVH]
    MeshAcceleration.{h,cpp}                            [proposed shared mesh updates]
  GeometricRaster/
    DirectLighting/SolarShadowSequence.{h,cpp}           [existing; compare modes]
    GlobalIllumination/
      TieredWorldRenderSequence.{h,cpp}                 [existing integration]
      VulkanWorldRenderSequence.{h,cpp}                 [existing GPU integration]
      MeshGiScene.{h,cpp}                               [proposed shared scene/cache]
      StokesContourGiSequence.{h,cpp}                   [proposed GTX GI 1]
      SurfelCascadeGiSequence.{h,cpp}                   [proposed GTX GI 2]
      DeterministicPathTraceSequence.{h,cpp}            [proposed shared tracer]
  Shaders/
    DirectLighting/                                     [existing shadow shaders]
    GlobalIllumination/
      StokesContourGiCompute.slang                      [proposed]
      SurfelCascadeGiCompute.slang                      [proposed]
      DeterministicPathTraceCompute.slang               [proposed software backend]
      DeterministicPathTraceRayQuery.slang              [proposed optional backend]
Exhibits/
  WorldGiShadowPathTraceValidation.cpp                  [proposed correctness suite]
Experimental/
  ShadowAlgorithmLab/                                   [existing comparison lab]
  DeterministicPathTracer/                              [existing WebGPU prototype]
Documentation/Plans/WorldGiShadowPathTracerPlan.md      [this plan]
```

Use established project naming and build conventions when implementing; exact files/interfaces may change after auditing real scene ownership and shader bindings. The offline SDF baker remains in `Engine/GeometricRaster/Baking/` and `Engine/Shaders/Baking/`.

## Implementation gates — do not claim completion before each passes

1. **Baseline and reference:** verify a clean build and existing tests; audit prototype dependencies and license/provenance before porting. Provide a deterministic CPU triangle reference and high-sample reference images for each scene and frame. Record configuration, seed, source commit, assets and hardware. Agree target GTX model(s), resolution, quality and frame/memory budgets before defining success.
2. **Real-mesh scene contract:** import static and rigid meshes plus an actual animated character; test two visibly different poses and movement. Verify skinning, bounds, normal/material IDs, ray hits, update ordering, and removal. A fixed-world floor receiver test and a separate visible-pixel test must use the correct frame's geometry and camera buffers. No capsule stand-ins.
3. **Shadows:** compare all existing five plus optional mesh-surfel candidate against high-sample disc-ray reference with equal geometry and sun. Record raw per-frame errors, shadow-edge/contact error, thin features, temporal stability, CPU/GPU update and render cost, memory and full settings. Choose two only after quality and GTX budgets pass.
4. **Two GI prototypes:** first build triangle-visible Stokes and surfel paths with emissive sources and controlled diffuse bounce, then cover sun, sky, moving lights, color bleed, occlusion and material behavior. Compare identical frames against a high-sample path-traced reference with matching bounce limits and emission. Record linear HDR error and perceptual/tone-mapped images separately; inspect thin walls, near emitters and disocclusion. Include cold-build and per-frame update time, VRAM, full-frame GPU time and temporal stability on target hardware. If a candidate misses targets, redesign or replace it rather than preserve a historic ranking.
5. **Tracer:** implement/test the common integrator on software traversal first; add optional hardware traversal afterward. Compare hit distance/identity, material visibility and HDR radiance within declared tolerances across backends; validate fixed-device repeatability across batch sizes. Test accumulation reset on camera, light, mesh, material and pose changes; test overflow, resize, missing extensions and device loss. Benchmark actual GTX and RTX hardware; SwiftShader and estimates are not substitutes.
6. **Integration and release:** wire independent GI, shadow and tracer selectors; keep no-denoiser reference mode; document limitations and capability fallback. Run build, shader validation and scene regression tests on supported platforms. Publish reproducible measurements, costs, failure cases and images, then ask for final algorithm approval.

## Review questions

- Which GTX model(s), RTX model(s), resolution and desired frame-rate/memory budget define success?
- Which imported animated character/vehicle assets and representative interior/exterior scenes can be used for validation?
- Should the two GI modes target **emissive + sun/sky diffuse GI** initially, with specular/reflections kept in the existing separate path?
- Is the optional mesh-surfel shadow candidate worth building, or should we select among the five existing non-SDF modes first?
