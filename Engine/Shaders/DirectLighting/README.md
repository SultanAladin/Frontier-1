# 🧩 `Engine/Shaders/DirectLighting/`

> **TL;DR**: GTX Tier 1 Calibrated 2-Cascade (3.9 mm / 15.6 mm) Orthographic Contact Clip + 3D Signed Distance Volume Cone Penumbra (Zero VSM!) (4 source/header/shader files under `Engine/Shaders/DirectLighting/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ContactClipShadow.slang` | GTX Tier 1 Calibrated 2-Cascade (3.9 mm / 15.6 mm) Orthographic Contact Clip + 3D Signed Distance Volume Cone Penumbra (Zero VSM!) |
| `PlaneAwareShadowFilter.slang` | Coplanar Blocker-Dilated Continuous-Sigma Plane-Aware A-Trous Shadow Denoiser preserving razor-sharp 0 px contact edges |
| `SolarFrustumShadow.slang` | Deterministic Vogel Solar Disc Raytracer + 2-Scale (0.25 m / 2.2 m) Physical Chassis Ambient Occlusion over 4-Wide BVH4 |
| `WideTreeRayQuery.slang` | Shared 128-byte 4-Wide BVH4 ray-box & Moller-Trumbore intersection routines for GTX compute & RTX software/hybrid fallback |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `ContactClipShadow.slang` | `ContactClipComputeEntry` (`compute`) | `ContactClipShadow.*.spv` |
| `PlaneAwareShadowFilter.slang` | `PlaneFilterComputeEntry` (`compute`) | `PlaneAwareShadowFilter.*.spv` |
| `SolarFrustumShadow.slang` | `SolarShadowComputeEntry` (`compute`) | `SolarFrustumShadow.*.spv` |
| `WideTreeRayQuery.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
