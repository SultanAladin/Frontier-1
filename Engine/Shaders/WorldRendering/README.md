# 🧩 `Engine/Shaders/WorldRendering/`

> **TL;DR**: Coherent VNDF specular reflection & Abbe-dispersed dielectric glass refraction compute unit (1 `.slang` shader under `Engine/Shaders/WorldRendering/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `SpecularGlassRaytrace.slang` | Coherent VNDF Specular Reflection & Abbe-Dispersed Dielectric Glass Refraction Compute Unit |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `SpecularGlassRaytrace.slang` | `SpecularRaytraceComputeEntry` (`compute`) | `SpecularGlassRaytrace.*.spv` |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
