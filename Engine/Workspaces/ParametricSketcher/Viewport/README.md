# 🧩 `Engine/Workspaces/ParametricSketcher/Viewport/`

> **TL;DR**: Sketcher viewport camera: the shared Engine/Render/OrbitCamera plus scene framing and box selection of edges (14 source/header/shader files under `Engine/Workspaces/ParametricSketcher/Viewport/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `SketcherCamera.cpp` | Sketcher viewport camera: scene framing and box selection of edges over Engine/Render/OrbitCamera |
| `SketcherCamera.h` | Sketcher viewport camera: the shared Engine/Render/OrbitCamera plus scene framing and box selection of edges |
| `SketcherGizmoSpace.cpp` | The gizmo's handle triangles (Slate GizmoPRO look), their pick and the drag maths. |
| `SketcherGizmoSpace.h` | GizmoSpace, the world the transform gizmo lives in, one for every workspace (Sketcher, the Frontier editor and the |
| `SketcherOrbitCamera.cpp` | Shared Z-up orbit camera (see header) |
| `SketcherOrbitCamera.h` | Shared Z-up orbit camera: orbit / pan / dolly, view presets, fit-to-bounds, perspective or orthographic Vulkan clip |
| `SketcherPortableImage.cpp` | PNG output of rendered frames and identity maps (stb_image_write), plus a stable FNV-1a digest of pixel arrays |
| `SketcherPortableImage.h` | PNG output of rendered frames and identity maps (stb_image_write), plus a stable FNV-1a digest of pixel arrays |
| `SketcherRasterization.cpp` | Viewport rasterization: offscreen colour + R32 identity + depth, surface / edge / grid programs, probe and readback |
| `SketcherRasterization.h` | Viewport rasterization: offscreen colour + R32 identity + depth, surface / edge / grid programs, probe and readback |
| `SketcherSnapping.cpp` | Snap catalogue (exact carriers of edges, sketches, SpaceCurves, faces) and the pointer search over it |
| `SketcherSnapping.h` | Object snapping (Fusion / Plasticity point picker): endpoints, midpoints, centres, intersections, tangent and |
| `SketcherSurfaceTracing.cpp` | SketcherSurfaceTracing implementation |
| `SketcherSurfaceTracing.h` | Surface hits are exact carrier evaluations, seeded by the visible tessellation. |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Workspaces::Sketcher`, `Frontier`
- **Primary Types**: `GizmoBuilder`, `GizmoHandle`, `GizmoVertex`, `GizmoPose`, `GizmoGeometry`, `OrbitViewPreset`, `OrbitCamera`, `OrbitCameraBasis`, `OrbitRay`, `RenderProgramRecipe`, `SketcherProjectionBlock`, `SketcherGridBlock`, `SketcherImagePlaneBlock`, `SketcherImagePlaneQuad`, `SketcherViewportStyle`, `SketcherRasterPane`, `SketcherRasterization`, `CurvePoint` (+12 more)

## Usage

```cpp
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherCamera.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherGizmoSpace.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherOrbitCamera.h"

using namespace Frontier::Workspaces::Sketcher;
GizmoBuilder Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
