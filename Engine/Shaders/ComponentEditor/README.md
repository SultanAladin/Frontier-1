# 🧩 `Engine/Shaders/ComponentEditor/`

> **TL;DR**: Compiled geometry shader supporting OpenPbrDirect (Zero GI) plus all 7 Component Editor inspection channels & MRT PickID (6 source/header/shader files under `Engine/Shaders/ComponentEditor/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ComponentChannelInspection.slang` | Compiled geometry shader supporting OpenPbrDirect (Zero GI) plus all 7 Component Editor inspection channels & MRT PickID |
| `ComponentConstraintVisual.slang` | Layer-1 X-ray constraint hardware, Hinge/Ball limit range fans, Spin comet ribbons, and Dock pulsing iris shader |
| `ComponentDotFloor.slang` | Infinite metric (1.0 m) & quarter-meter (0.25 m) ground dot floor fading around CameraRig focus point with X/Z axis tinting |
| `ComponentEditorShared.slang` | Shared uniforms, OpenPBR-direct (Zero GI) BRDF, ACES Filmic tonemapping, and selection halo helpers for Component Editor viewports |
| `ComponentFieldHull.slang` | Fresnel rim, soft box face borders, rising world-Y scan band, and analytical hull shell shader (no wireframe line cages) |
| `ComponentLightVolume.slang` | Additive SpotBeam & Camera frustum volumetric beam shader attenuating smoothly along +X beam axis |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `ComponentChannelInspection.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ComponentConstraintVisual.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ComponentDotFloor.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ComponentEditorShared.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ComponentFieldHull.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `ComponentLightVolume.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
