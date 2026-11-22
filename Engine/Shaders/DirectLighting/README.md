# 🧩 `Engine/Shaders/DirectLighting/`

> **TL;DR**: Plane-aware shadow denoiser, Vogel solar frustum raytracer, `GtxLowEnd`/`HighEnd` hybrid cone shadow & GPU `BVH4` bottom-up refit compute shaders, and shared 128 B / 64 B quantized `BVH4` ray query library (5 `.slang` files under `Engine/Shaders/DirectLighting/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `HybridConeShadowCompute.slang` | Order 2 soft-shadow compute shader (`GtxLowEnd` local `C0` SDF + `ArticulatedCapsuleRig` cone trace vs `HighEnd` 64 B `BVH4` Vogel ray trace) |
| `PlaneAwareShadowFilter.slang` | Coplanar blocker-dilated continuous-sigma plane-aware à-trous shadow denoiser preserving sharp 0 px contact edges |
| `RefitWideBoundingTreeCompute.slang` | Sub-0.12 ms bottom-up GPU `CompressedWideTree` (`BVH4`) refit compute shader for dynamic/skinned surfaces without tree rebuild |
| `SolarFrustumShadow.slang` | Deterministic Vogel solar disc raytracer + 2-scale (`0.25 m` / `2.2 m`) chassis ambient occlusion over 4-Wide `BVH4` |
| `WideTreeRayQuery.slang` | Shared 128 B & 64 B 8-bit quantized 4-Wide `BVH4` ray-box & Möller-Trumbore intersection routines |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `HybridConeShadowCompute.slang` | `ComputeEntry` (`compute`) | `HybridConeShadowCompute.comp.spv` |
| `PlaneAwareShadowFilter.slang` | `PlaneFilterComputeEntry` (`compute`) | `PlaneAwareShadowFilter.comp.spv` |
| `RefitWideBoundingTreeCompute.slang` | `ComputeEntry` (`compute`) | `RefitWideBoundingTreeCompute.comp.spv` |
| `SolarFrustumShadow.slang` | `SolarShadowComputeEntry` (`compute`) | `SolarFrustumShadow.comp.spv` |
| `WideTreeRayQuery.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |

## Build & Verification

- Compiled automatically at build time by `CMakeLists.txt` (`FrontierShaders` target) into `${CMAKE_BINARY_DIR}/CompiledShaders/`.
