# 🧩 `Engine/Shaders/Colour/`

> **TL;DR**: Shared pure .slang GPU colour ramp LUT sampling, wrap/curve math, Oklab/LinearSrgb conversions, Kelvin blackbody & 3D lattice noise (1 source/header/shader files under `Engine/Shaders/Colour/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ColourRampSampling.slang` | Shared pure .slang GPU colour ramp LUT sampling, wrap/curve math, Oklab/LinearSrgb conversions, Kelvin blackbody & 3D lattice noise |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `ColourRampSampling.slang` | `SampleColourRampLutComputeEntry` (`compute`) | `ColourRampSampling.*.spv` |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
