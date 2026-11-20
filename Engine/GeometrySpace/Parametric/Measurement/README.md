# 🧩 `Engine/GeometrySpace/Parametric/Measurement/`

> **TL;DR**: Exact body and face measures: volume, surface area, centroid and inertia tensor by Green / divergence integrals (6 source/header/shader files under `Engine/GeometrySpace/Parametric/Measurement/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `MassProperties.cpp` | Exact body and face measures: volume, surface area, centroid and inertia tensor by Green / divergence integrals |
| `MassProperties.h` | Exact body and face measures: volume, surface area, centroid and inertia tensor by Green / divergence integrals |
| `MinimumDistance.cpp` | Minimum distance between B-rep entities (vertex, edge, face) with the two closest points |
| `MinimumDistance.h` | Minimum distance between B-rep entities (vertex, edge, face) with the two closest points |
| `PlaneSection.cpp` | Exact plane section of a solid: the cut region's area, perimeter, centroid, second moments and outline |
| `PlaneSection.h` | Exact plane section of a solid: the cut region's area, perimeter, centroid, second moments and outline |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `BodyMassProperties`, `FaceAreaMeasure`, `DistanceMeasure`, `PlaneSectionMeasure`, `HalfSpaceCut`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Measurement/MassProperties.h"
#include "Engine/GeometrySpace/Parametric/Measurement/MinimumDistance.h"
#include "Engine/GeometrySpace/Parametric/Measurement/PlaneSection.h"

using namespace Frontier::GeometrySpace::Parametric;
BodyMassProperties Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
