# ReSTIR GI realtime architecture plan

Date: 2026-08-25

Scope: planning only. This is the proposed architecture for scalable realtime ReSTIR GI in Slate, targeting games first while keeping the renderer useful for CAD/visualisation scenes. No implementation is included in this plan.

## Research conclusions

1. ReSTIR should be integrated as a reservoir framework, not as one monolithic renderer. Direct lighting, diffuse GI, broader path reuse and denoising need separate passes sharing the same scene, motion and history infrastructure.
2. Capability detection must decide the backend. GTX 1060-class cards can be a baseline target, but they should not be assumed to expose Vulkan ray tracing extensions. Some GTX cards received DXR support through drivers, but hardware RT cores are absent and Vulkan support must be tested extension-by-extension at runtime.
3. The portable baseline for GTX is compute-shader ray traversal over a Slate-owned triangle BVH, scaled down aggressively. RTX and other hardware ray tracing devices should use Vulkan acceleration structures and ray queries or ray pipelines.
4. The first production target should be ReSTIR DI plus ReSTIR GI for one-bounce diffuse or mid-roughness indirect lighting. ReSTIR PT is the later high tier for broader multi-bounce glossy/specular transport.
5. Motion vectors, history validation, disocclusion handling, denoising and temporal accumulation are mandatory architecture pieces, not polish. ReSTIR without stable motion and denoising will flicker in games.

## Source trail

- ReSTIR DI: Bitterli et al. 2020, spatiotemporal reservoir resampling for many dynamic lights.
- ReSTIR GI: Ouyang et al. 2021, path resampling for realtime path tracing, reporting large MSE reductions at one sample per pixel.
- World-space reservoir reuse: Boisse 2021, hash-grid reservoir reuse for path vertices beyond primary image-space reuse.
- GRIS / ReSTIR PT: Lin et al. 2022, generalized resampled importance sampling for path tracing.
- RTXDI: NVIDIA SDK path containing ReSTIR DI, ReSTIR GI and ReSTIR PT shader/host building blocks.
- NRD: NVIDIA realtime denoisers; REBLUR/RELAX/SIGMA families show the required signal separation, motion-vector and history inputs.
- Later research to track before implementation of the high tier: Suffix ReSTIR, Area ReSTIR, reservoir splatting, ReSTIR BDPT, ReSTIR PG and ReSTIR PT Enhanced.

## Capability tiers

### Tier A: GTX 1060+ compute baseline

Purpose: broadest playable mode.

Backend:
- Raster G-buffer plus compute passes.
- Slate software triangle BVH traversal in compute shaders.
- Half or quarter resolution GI, checkerboardable.
- One ray budget class: 1 primary GI candidate ray per shaded pixel at reduced resolution, with optional direct-light visibility rays.

Recommended features:
- ReSTIR DI for many lights where possible.
- ReSTIR GI one-bounce diffuse indirect lighting.
- Screen-space candidates as a fallback when software BVH cost spikes.
- Temporal accumulation with strict rejection.
- Spatial denoiser tuned for low ray count.

Avoid in this tier initially:
- Multi-bounce ReSTIR PT.
- Glossy caustics.
- Full-resolution diffuse plus specular GI.
- Dynamic per-frame full BVH rebuild for heavy scenes.

### Tier B: RTX / hardware ray query path

Purpose: main realtime GI path for modern devices.

Backend:
- Vulkan `VK_KHR_acceleration_structure` plus `VK_KHR_ray_query` when available.
- Ray queries from compute shaders so reservoirs, ray traversal and denoising stay in a compute-oriented frame graph.
- Hardware BLAS/TLAS for triangles and instances.

Recommended features:
- ReSTIR DI and ReSTIR GI at half resolution, scaling to full resolution on faster GPUs.
- Two temporal reservoirs per pixel for ping-pong history.
- Optional separate diffuse and specular reservoirs once material roughness support is mature.
- Denoiser-ready diffuse radiance, specular radiance, hit distance, normals, roughness, depth and motion vectors.

### Tier C: RTX high path / ray pipeline path

Purpose: scalable graphics and high-end game/visualisation mode.

Backend:
- Vulkan `VK_KHR_ray_tracing_pipeline` where available.
- Shader binding table for ray generation, miss and hit groups.
- Optional device features such as Shader Execution Reordering if a later backend exposes it cleanly.

Recommended features:
- ReSTIR PT after Tier B is stable.
- Multi-bounce indirect path reservoirs.
- Better glossy transport and thin emissive geometry handling.
- Higher quality mode using more initial candidates and larger spatial reuse radius.

## Frame graph shape

1. G-buffer pass
   - Outputs depth, normal, albedo/base colour, material parameters, roughness/metallic, emissive, object id and primitive id.
   - Writes current clip-space and previous clip-space positions for motion.

2. Motion vector pass
   - Camera motion from previous/current matrices.
   - Object motion from previous/current instance transforms.
   - Skinned/deforming geometry later requires previous vertex positions or deformation data.

3. Acceleration structure update
   - Tier A: update or refit software BVH nodes for changed triangle ranges.
   - Tier B/C: build/refit BLAS for changed geometry and rebuild/update TLAS for instances.
   - Track dirty ranges to avoid rebuilding the world every frame.

4. Initial candidate generation
   - Direct lighting candidates from light reservoirs or alias tables.
   - GI candidates from one stochastic path vertex or one indirect bounce per shaded pixel.
   - Store candidate radiance, hit position, normal, direction, material summary and target function value.

5. Temporal resampling
   - Reproject pixel using motion vectors.
   - Validate depth, normal, roughness/material and object identity.
   - Combine current candidate with previous reservoir when compatible.
   - Clamp history confidence on disocclusion, camera cuts and rapidly changing light.

6. Spatial resampling
   - Sample neighbors using blue-noise rotated patterns.
   - Reuse only compatible reservoirs: similar normal, plane distance/depth, roughness bucket and material response.
   - Use a small radius on GTX; larger and multiple rounds on RTX.

7. Visibility and shading resolve
   - Trace visibility to chosen direct light or GI path connection.
   - Evaluate BSDF, geometry term and emission/incident radiance.
   - Output noisy diffuse/specular GI, hit distance and confidence.

8. Denoising and temporal accumulation
   - Separate diffuse and specular signals.
   - Use hit distance, normals, roughness and motion vectors.
   - Anti-firefly clamp before temporal accumulation.
   - Feed final resolved lighting into upscaler/TAA.

## Core data structures

### Triangle scene data

- `TriangleVertexSoa`: positions, normals, tangents, UVs, previous positions where needed.
- `TriangleIndexRange`: mesh-local ranges for triangle batches.
- `MaterialSummary`: compact shader-facing material fields; no full authoring layer stack on the ray path.
- `InstanceRecord`: current transform, previous transform, material override index, geometry range, visibility mask.

### Software BVH for Tier A

- Two-level design even without hardware RT:
  - Bottom tree per static mesh or mesh chunk.
  - Top tree over instances.
- Node format:
  - Quantized AABB bounds where possible.
  - Child indices or triangle range.
  - Axis/order metadata for stackless or small-stack traversal.
- Build strategy:
  - CPU SAH builder for static content at load/import time.
  - GPU LBVH or refit path later for dynamic content.
  - Keep refit cheap by tracking changed ranges.

### Hardware acceleration structures for Tier B/C

- BLAS per mesh or mesh chunk.
- TLAS per scene view.
- Compaction when stable.
- Refits for transform-only animation; rebuild for topology changes.
- Separate flags for opaque, alpha-tested and two-sided geometry.

### Reservoirs

Direct lighting reservoir:
- Light index or emissive triangle reference.
- Sample point/UV on light.
- Target value.
- Weight sum.
- Candidate count.
- Visibility confidence/age.

GI reservoir:
- Path vertex position, normal, incoming/outgoing direction.
- Radiance or throughput summary.
- Material roughness bucket.
- Target value and PDF summary.
- Weight sum and candidate count.
- Age and validation stamp.

History buffers:
- Ping-pong reservoirs.
- Previous depth/normal/material/object id.
- Previous moments or variance.
- Previous radiance for accumulation.

World-space reservoir cache, later phase:
- Hashed grid keyed by quantized world cell plus normal cone bucket.
- Stores compact reservoir lists for reuse at secondary vertices.
- Useful after screen-space ReSTIR GI is stable.

## Denoising plan

Minimum internal denoiser:
- Temporal accumulation with disocclusion tests.
- À-trous or bilateral spatial filter with depth/normal/roughness edge stops.
- Separate diffuse and specular histories.
- Firefly rejection using luminance percentile or variance clamp.

Production target:
- Keep the data model compatible with NRD-style inputs: diffuse/specular radiance, hit distance, roughness, normals, depth, motion vectors and non-jittered matrices.
- Vendor SDK integration can remain optional; Slate should retain a built-in fallback denoiser for non-NVIDIA and minimal builds.

## Scalable quality presets

Low / GTX 1060:
- Quarter or checkerboard half resolution GI.
- One initial GI candidate.
- One temporal pass, one spatial pass.
- Diffuse GI only.
- Strict history clamp.

Medium / GTX 1660 or lower RTX:
- Half resolution GI.
- One to two initial candidates.
- One temporal pass, two spatial passes.
- Diffuse GI plus limited rough specular.

High / RTX:
- Half to full resolution GI.
- ReSTIR DI plus ReSTIR GI.
- Larger neighbor pattern and better denoiser settings.
- Optional ray-query direct visibility for chosen samples.

Ultra / high RTX:
- ReSTIR PT experimental path.
- More candidates and larger temporal confidence where stable.
- Optional world-space reservoir cache.
- Better glossy transport and multi-bounce path reuse.

## Implementation phases recommended for Slate

1. Capability and frame graph foundation
   - Add `RayTracingCapabilitySet` to the Vulkan device edge.
   - Detect acceleration structure, ray query and ray pipeline support.
   - Add render graph resources for motion vectors, previous frame data and history ping-pong.

2. Triangle scene and BVH foundation
   - Extract scene triangles into GPU-friendly buffers.
   - Add software BVH build/load path for Tier A.
   - Add hardware BLAS/TLAS path for Tier B/C.

3. ReSTIR DI first
   - Implement light/emissive triangle sampling.
   - Add direct lighting reservoirs.
   - Validate temporal and spatial reuse on moving camera/lights.

4. ReSTIR GI Tier A/B
   - Add indirect path candidate generation.
   - Add GI reservoirs and history validation.
   - Resolve diffuse GI and feed denoiser.

5. Denoiser and temporal stability hardening
   - Motion vector validation.
   - Disocclusion tests.
   - Firefly clamp and variance tracking.
   - Internal fallback denoiser.

6. RTX high path
   - Add ray pipeline option only after ray-query path is stable.
   - Add ReSTIR PT experiments.
   - Investigate world-space reservoir cache and ReSTIR PT Enhanced improvements.

## Acceptance criteria for the first implementation pass

- The renderer runs on a device without Vulkan ray tracing extensions using the compute/software BVH fallback.
- The renderer uses Vulkan hardware ray tracing when the extension set is present.
- One scene can switch between low, medium and high ReSTIR GI presets.
- Motion vectors and previous-frame validation are visible in diagnostics.
- Camera motion and object motion do not smear history across disocclusions.
- Denoised one-bounce diffuse GI is stable enough for game camera movement.
- Unsupported capabilities refuse cleanly or downgrade to the next preset; no fake ray tracing path is presented as hardware RT.
