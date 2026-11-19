# 🧩 `Engine/Shaders/Compute/`

> **TL;DR**: Runtime & Editor Vulkan 1.3 Compute shader evaluating cascaded 3D velocity + curl lattice voxels from ForceSourceShaderEntry SSBO (2 source/header/shader files under `Engine/Shaders/Compute/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `FieldLatticeCompute.slang` | Runtime & Editor Vulkan 1.3 Compute shader evaluating cascaded 3D velocity + curl lattice voxels from ForceSourceShaderEntry SSBO |
| `ForceFieldEvaluationShared.slang` | Shared +Z Up Slang GPU analytical evaluation of 19 ForceModel laws, reach bounds, CurlNoise, FlowStream, and spline curves |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `FieldLatticeCompute.slang` | `ComputeFieldLatticeVoxel` (`compute`) | `FieldLatticeCompute.*.spv` |
| `ForceFieldEvaluationShared.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
