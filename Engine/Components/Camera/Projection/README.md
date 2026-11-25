# 🧩 `Engine/Components/Camera/Projection/`

> **TL;DR**: Aspect-locked parallel 3D & 2D planar orthographic projection transforms (3 source/header/shader files under `Engine/Components/Camera/Projection/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `OrthographicProjection.h` | Aspect-locked parallel 3D & 2D planar orthographic projection transforms |
| `PerspectiveProjection.h` | Physical thin-lens perspective optics: sensor gate + focal length -> FOV, entrance pupil, hyperfocal & Vulkan perspective clip |
| `ViewTransform.h` | Unified camera View-Projection clip evaluation and viewport screen-to-world picking ray construction |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Components::Projection`
- **Primary Types**: `SpatialPickingRay`

## Usage

```cpp
#include "Engine/Components/Camera/Projection/OrthographicProjection.h"
#include "Engine/Components/Camera/Projection/PerspectiveProjection.h"
#include "Engine/Components/Camera/Projection/ViewTransform.h"

using namespace Frontier::Components::Projection;
SpatialPickingRay Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
