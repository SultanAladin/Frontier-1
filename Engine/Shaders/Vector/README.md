# 🧩 `Engine/Shaders/Vector/`

> **TL;DR**: Resolves 2x SSAA SurfacePreviewImage + StackedPick into FinalColorImage (RGBA8) & FinalPickImage (RGBA32UI), plus GPU vector guides/knots/pins/marquee. (7 source/header/shader files under `Engine/Shaders/Vector/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `VectorGuideOverlay.slang` | Resolves 2x SSAA SurfacePreviewImage + StackedPick into FinalColorImage (RGBA8) & FinalPickImage (RGBA32UI), plus GPU vector guides/knots/pins/marquee. |
| `VectorLayerStackCompute.slang` | Compute blending of per-layer RGBA16F + Pick + procedural Mask + BevelEdgeWear into StackedColor & 4x RGBA16F OpenPBR surface packs. |
| `VectorModifierExpand.slang` | GPU Compute expansion of all 8 non-destructive layer modifiers into VectorInstanceEntry[] + atomic summary KPI metrics. |
| `VectorPbrSurfaceCompute.slang` | Evaluates VectorSVG, Seamless2x2, StackedPbrTile (OpenPBR direct+IBL+height normals), or any of 22 diagnostic PBR channels into SurfacePreviewImage. |
| `VectorShapeCover.slang` | Stencil-cover & stroke fragment rasterizer writing RGBA16F LayerCoverColor + RGBA32UI LayerPickTarget (Solid/Linear/Radial/12-Pin PointField). |
| `VectorSharedLayouts.slang` | Shared GPU structures and procedural math for Frontier's Vulkan VectorGraphics & OpenPBR rasterization shaders. |
| `VectorStencilFill.slang` | Stencil winding/even-odd triangle fan rasterizer supporting per-instance transform, domain warp, and seamless tile wrapping. |

## Shader Entry Points & Compilation

| Shader | Entry Points | Compiled SPIR-V Outputs |
| :--- | :--- | :--- |
| `VectorGuideOverlay.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `VectorLayerStackCompute.slang` | `ComputeEntry` (`compute`) | `VectorLayerStackCompute.*.spv` |
| `VectorModifierExpand.slang` | `ComputeEntry` (`compute`) | `VectorModifierExpand.*.spv` |
| `VectorPbrSurfaceCompute.slang` | `ComputeEntry` (`compute`) | `VectorPbrSurfaceCompute.*.spv` |
| `VectorShapeCover.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `VectorSharedLayouts.slang` | Shared `.slang` include library | Included by downstream `.slang` targets |
| `VectorStencilFill.slang` | `FragmentEntry` (`fragment`) | `VectorStencilFill.*.spv` |

```cmake
# Shaders in this directory are compiled automatically at build time by CMakeLists.txt
# into ${CMAKE_BINARY_DIR}/CompiledShaders/ via ExternalPackages/VulkanCompute/Slang.
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
