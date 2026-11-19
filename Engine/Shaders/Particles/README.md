# 🧩 `Engine/Shaders/Particles/`

> **TL;DR**: Vulkan 1.3 particle advection compute kernel importing ColourRampSampling & ForceFieldEvaluationShared (zero duplicated physics/colour) (15 source/header/shader files under `Engine/Shaders/Particles/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ParticleAdvectCompute.slang` | Vulkan 1.3 particle advection compute kernel importing ColourRampSampling & ForceFieldEvaluationShared (zero duplicated physics/colour) |
| `ParticleBuffers.slang` | Shared Vulkan 1.3 GPU particle buffers bit-exact with Engine/ParticleSpace/ParticleBuffers.h (80B Slot, 208B Global, 256B Rig) |
| `ParticleForceCoupling.slang` | Dedicated Vulkan 1.3 compute pass coupling ForceFieldEvaluationShared 19 ForceModel laws & ambient wind onto live ParticleSlots |
| `ParticleLensRasterization.slang` | Vulkan 1.3 optical gravitational-lens Einstein ring & thermal heat-shimmer screen distortion carrier |
| `ParticleLineArcRasterization.slang` | Vulkan 1.3 glowing dielectric lightning arcs, plasma beams, & silk spiderweb tension strands with radial core corona falloff |
| `ParticleOverlayRasterization.slang` | Vulkan 1.3 procedural tactical overlay carrier: radar sweep arm, concentric range rings, compass notches & acoustic sonar pulses |
| `ParticleReductionCompute.slang` | Vulkan 1.3 GPU telemetry reduction compute kernel aggregating live count, fixed-point AABB bounds & kinetic energy per rig |
| `ParticleReleaseCompute.slang` | Vulkan 1.3 GPU deterministic PCG particle release compute kernel sampling analytic origins or pre-baked ParticleAnchorSampleGpu buffers |
| `ParticleRibbonRaster.slang` | Vulkan 1.3 continuous camera-facing ribbon/strand vertex & fragment shaders consuming StrandVertexGpu history chains |
| `ParticleSolidRasterization.slang` | Vulkan 1.3 PBR-shaded instanced 3D solid particle carrier (coins, cubes, glass shards, snowflakes, crystal beads, Voronoi ice/basalt) |
| `ParticleSpatialGridCompute.slang` | Vulkan 1.3 GPU spatial hash grid builder for O(N) Boids, SPH Fluid, Granular DEM, & Molecular Lennard-Jones neighbor lookups |
| `ParticleSpriteCutouts.slang` | Pure .slang library implementing all 20 procedural SpriteCutoutCategory SDF cutouts (0..19) & BokehApertureCategory optics |
| `ParticleSpriteRaster.slang` | Vulkan 1.3 instanced camera-aligned billboard vertex & fragment shaders importing ParticleBuffers & ParticleSpriteCutouts |
| `ParticleSurfaceBackingRasterization.slang` | Vulkan 1.3 glossy obsidian / brushed metallic 3D substrate shader for MoGraph Effector Halftone & Ferrofluid liquid basins |
| `ParticleWingedRasterization.slang` | Vulkan 1.3 articulated dual-wing flapping carrier for birds, starlings, butterflies, and sinuous swimming fish schools |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `ParticleAdvectCompute.slang` | `ParticleAdvectComputeEntry` (`compute`) | `ParticleAdvectCompute.*.spv` |
| `ParticleBuffers.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ParticleForceCoupling.slang` | `ParticleForceCouplingEntry` (`compute`) | `ParticleForceCoupling.*.spv` |
| `ParticleLensRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ParticleLineArcRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ParticleOverlayRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ParticleReductionCompute.slang` | `ParticleReductionComputeEntry` (`compute`) | `ParticleReductionCompute.*.spv` |
| `ParticleReleaseCompute.slang` | `ParticleReleaseComputeEntry` (`compute`) | `ParticleReleaseCompute.*.spv` |
| `ParticleRibbonRaster.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ParticleSolidRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ParticleSpatialGridCompute.slang` | `ParticleSpatialGridComputeEntry` (`compute`) | `ParticleSpatialGridCompute.*.spv` |
| `ParticleSpriteCutouts.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ParticleSpriteRaster.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ParticleSurfaceBackingRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ParticleWingedRasterization.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
