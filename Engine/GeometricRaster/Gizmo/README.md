# 🧩 `Engine/GeometricRaster/Gizmo/`

> **TL;DR**: Modular 3D & 2D Slate GizmoPRO manipulator reusable across UV, Geometry, Material, Vector & Simulation workspaces (4 source/header/shader files under `Engine/GeometricRaster/Gizmo/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `TransformGizmo.cpp` | Implementation of Slate GizmoPRO 3D & 2D procedural geometry builder and skew-line / ray-plane / Ctrl-snap drag solver |
| `TransformGizmo.h` | Modular 3D & 2D Slate GizmoPRO manipulator reusable across UV, Geometry, Material, Vector & Simulation workspaces |
| `WorldGizmoOverlayRasterization.cpp` | Implementation of WorldGizmoOverlayRasterization executing GizmoRasterization.slang on Vulkan 1.2/1.3 over TieredWorldRenderSequence |
| `WorldGizmoOverlayRasterization.h` | Vulkan GPU .slang GizmoRasterization unit overlaying 3D Slate TransformGizmo onto TieredWorldRenderSequence RGBA8 buffers + GPU PickID |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Editor`, `Frontier::GeometricRaster`
- **Primary Types**: `GizmoDimensionMode`, `GizmoHandleFilter`, `GizmoHandleOrdinal`, `ViewportRay3D`, `GizmoInteractionDelta`, `TransformGizmo`, `WorldGizmoOverlayRasterization`

## Usage

```cpp
#include "Engine/GeometricRaster/Gizmo/TransformGizmo.h"
#include "Engine/GeometricRaster/Gizmo/WorldGizmoOverlayRasterization.h"

using namespace Frontier::Editor;
GizmoDimensionMode Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
