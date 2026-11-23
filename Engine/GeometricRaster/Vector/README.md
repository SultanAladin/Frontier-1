# 🧩 `Engine/GeometricRaster/Vector/`

> **TL;DR**: Pure Vulkan 1.3 + .slang VectorGraphics GPU rasterizer driven by ThorVG symbolic DOM/typography paths + Planar2D TransformGizmo + R32G32B32A32_UINT picking. (4 source/header/shader files under `Engine/GeometricRaster/Vector/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `VectorCanvasRasterizer.cpp` | Pure Vulkan 1.3 + .slang VectorGraphics GPU rasterizer (Modifier Compute -> 2x SSAA Stencil-Cover -> OpenPBR Stack -> Guide + Planar2D Gizmo MRT). |
| `VectorCanvasRasterizer.h` | Pure Vulkan 1.3 + .slang VectorGraphics GPU rasterizer driven by ThorVG symbolic DOM/typography paths + Planar2D TransformGizmo + R32G32B32A32_UINT picking. |
| `VulkanVectorRenderMethod.cpp` | Implementation of ThorVG 1.0.0 symbolic SVG + TTF RenderMethod frontend producing GPU stencil-cover fans & stroke triangles for Vulkan .slang. |
| `VulkanVectorRenderMethod.h` | Symbolic ThorVG 1.0.0 frontend (SVG DOM + TTF/OTF typography + cubic path flattening) implementing tvg::RenderMethod for Vulkan .slang GPU rasterization. |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `VectorRasterPushConstants`, `VectorCanvasLayoutMetrics`, `VectorGpuEvaluationMetrics`, `VectorCanvasRasterizer`, `SymbolicPreparedShape`, `SymbolicGroupEntry`, `VectorGradientPreset`, `VectorStrokeCapStyle`, `VectorStrokeJoinStyle`, `VectorVertex2D`, `VectorColorStop`, `VectorCubicKnot`, `VectorCubicSubpath`, `ExtractedVectorShape`, `ExtractedSvgLayer`, `ThorVGVectorFrontend`

## Usage

```cpp
#include "Engine/GeometricRaster/Vector/VectorCanvasRasterizer.h"
#include "Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.h"

using namespace Frontier::GeometricRaster;
VectorRasterPushConstants Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
