# Mesh lighting: two GTX GI modes and one deterministic path tracer

**Decision for review:** use the same indexed world, vehicle and post-skinning character triangles for all three renderers. Do not allocate a runtime voxel distance field or replace limbs with capsules. BVH boxes only accelerate triangle intersections; surfels sample triangle area for indirect lighting, not character silhouettes. Leave the independent offline SDF baker alone.

## What is recoverable

The source snapshot is `SultanAladin/Frontier@daed305`. Neither requested shadow exhibit exists there. [536cfe1](https://github.com/SultanAladin/Frontier/commit/536cfe1) contains `Exhibits/CleanShadowAndGiCandidates.h` and `Exhibits/CleanEmissiveShadowValidation.cpp`; [2d6f15e](https://github.com/SultanAladin/Frontier/commit/2d6f15e) contains their GI counterparts. Use these as algorithm references, not drop-in headers: their scene bridge samples SDF assets. The shadow benchmark also builds capsule geometry.

The archived GI summary at 560×315 reports normal-GI RMSE of 0.2238 (Stokes), 0.2239 (surfels), 0.2259 (voxel field). The surfel probe lookup scored 0.2269, distinct from the direct surfel result. These are one-scene quality measurements, not GTX frame times. The historical surfel/capsule shadow scored 0.2973 against a 128-sample disc reference versus 0.4170 for the SDF baseline. This does **not** measure a skinned triangle shadow.

## Implementation order and proposed folders

```
Engine/SpatialAcceleration/                     shared triangle BVH and pose refit
Engine/GeometricRaster/DirectLighting/          SolarShadowSequence (existing)
Engine/GeometricRaster/GlobalIllumination/      StokesContourGiSequence.{h,cpp}
                                               SurfelCascadeGiSequence.{h,cpp}
                                               DeterministicPathTraceSequence.{h,cpp}
Engine/Shaders/GlobalIllumination/              matching Slang compute passes
Experimental/{ShadowAlgorithmLab,DeterministicPathTracer}/   existing comparison labs
Exhibits/WorldGiShadowPathTraceValidation.cpp   proposed independent reference and metrics
```

1. **Geometry and direct shadows first.** Share material IDs, transforms, indices and current skinned vertices. Refit bounds after skinning; rebuild on topology changes. Feed the same pose to shadow maps and BVH. The synced enum has five non-SDF modes; `Frontier-1@d5bbb81` has eight entries including three SDF variants. Retain the five for comparison. Shortlist ContactPcss (cost) and WideTreeRayQuery (geometry accuracy); do not declare winners until timed. If a sixth non-SDF experiment is needed, test triangle-derived surfel blockers with exact triangle contact rays against thin features before promoting it.
2. **GTX quality GI: Stokes contour.** Extract oriented emissive triangles; integrate their visible solid angle at each receiver. Clip against the receiver horizon and sample visibility with bounded rays to the light, not just a short contact test. Accumulate diffuse second-bounce patches separately. Compare unoccluded emitters first, then occlusion by moving fingers and distant walls.
3. **GTX fast GI: surfel cascade.** Attach area-weighted emissive and diffuse samples to triangle IDs and barycentric coordinates. Refresh positions, normals and areas after deformation. Inject radiance into two irradiance-probe cascades with directional depth moments; blend cascades and reject probes behind walls. Budget dirty probe updates separately from fixed-cost receiver lookup. Invalidate old and new bounds when lights or characters move.
4. **One path tracer, two traversal backends.** Port sampling and material evaluation from the deterministic lab once. Use pixel/sample/bounce-indexed Sobol dimensions independent of queue ordering; share emission, NEE, MIS and termination. Start with software BVH4 closest-hit/any-hit compute on GTX. Select Vulkan ray queries on feature-capable devices, update deforming BLAS and instance TLAS, otherwise fall back to software. Compare hit IDs, distances and fixed-seed radiance before optimizing wavefront queues. Determinism means repeatability on each backend, not bit identity across GPUs.
5. **Promotion gate.** Render identical poses, cameras, exposure and materials without denoising. Use a real skinned character crossing the vehicle headlights, a thin occluder, and an empty sunlit receiver. Trace each pose's own primary buffer. Record isolated solar visibility RMSE, linear diffuse GI RMSE, contact leaks, motion ghosting, GPU build/refit and shading milliseconds and peak memory. Record resolution, GPU, driver and samples. Publish per-mode results; keep CPU historical numbers separate. Commit and push each working milestone.
