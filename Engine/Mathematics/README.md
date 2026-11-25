# 🧩 `Engine/Mathematics/`

> **TL;DR**: Axis3D, Axis2D, zero-overhead Scalar3D/Scalar2D aliases, dedicated UVAxes, SpatialTransform, and 16B-aligned .slang GPU layout structs (3 source/header/shader files under `Engine/Mathematics/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `AxisCoordinates.h` | Axis3D, Axis2D, zero-overhead Scalar3D/Scalar2D aliases, dedicated UVAxes, SpatialTransform, and 16B-aligned .slang GPU layout structs |
| `CoordinateSpace.h` | Configurable Right-Handed World Basis (RightHandedZUp default / RightHandedYUp), det=+1 boundary conversion & isolated Vulkan NDC clip |
| `LinearColors.h` | Dedicated scene-referred Linear Rec.709 / sRGB tristimulus struct preserving explicit .R, .G, .B radiometric channels |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Mathematics`, `CoordinateBasisDetail`
- **Primary Types**: `Axis3D`, `Axis2D`, `UVAxes`, `SpatialTransform`, `WorldCoordinateBasis`, `LinearRGB`

## Usage

```cpp
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/CoordinateSpace.h"
#include "Engine/Mathematics/LinearColors.h"

using namespace Frontier::Mathematics;
Axis3D Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
