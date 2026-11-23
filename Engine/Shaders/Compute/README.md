# 🧩 `Engine/Shaders/Compute/`

> **TL;DR**: Shared `.slang` compute shader staging directory (active compute kernels reside in `Atmosphere/`, `DirectLighting/`, `GeometricRaster/`, `MaterialSpace/`, `Visibility/`, and domain directories).

---

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir Build --output-on-failure`).
