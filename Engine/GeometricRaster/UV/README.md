# 🧩 `Engine/GeometricRaster/UV/`

> **TL;DR**: Vulkan .slang 3D Surface/MatCap + 2D UDIM UVTile + 3D/2D TransformGizmo MRT rasterizer with GPU Selection & Stretch compute (2 source/header/shader files under `Engine/GeometricRaster/UV/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `UVWorkspaceRasterizer.cpp` | Implementation of UVWorkspaceRasterizer driving Vulkan .slang MRT pipelines, O(1) GPU selection, and 3D/2D GPU picking |
| `UVWorkspaceRasterizer.h` | Vulkan .slang 3D Surface/MatCap + 2D UDIM UVTile + 3D/2D TransformGizmo MRT rasterizer with GPU Selection & Stretch compute |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `SurfaceDisplayMode`, `UVCanvasDisplayMode`, `GPUSelectionOperation`, `CameraOrbitRig3D`, `Viewport3DRenderOptions`, `UVBackdropPreset`, `Canvas2DRenderOptions`, `GPUUVDistortionSummary`, `UVWorkspaceRasterizer`

## Usage

```cpp
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"

using namespace Frontier::GeometricRaster;
SurfaceDisplayMode Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
