# 🧩 `Engine/GeometrySpace/Parametric/Containment/`

> **TL;DR**: Trimmed face domains in (𝑢, 𝑣): SP-curve loops, exact 2D winding number, point-in-face, interior point location (4 source/header/shader files under `Engine/GeometrySpace/Parametric/Containment/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `FaceDomain.cpp` | Trimmed face domains in (𝑢, 𝑣): SP-curve loops, exact 2D winding number, point-in-face, interior point location |
| `FaceDomain.h` | Trimmed face domains in (𝑢, 𝑣): SP-curve loops, exact 2D winding number, point-in-face, interior point location |
| `WindingNumber.cpp` | Generalized winding number of trimmed analytic faces: point-in-solid with precision-aware boundary detection |
| `WindingNumber.h` | Generalized winding number of trimmed analytic faces: point-in-solid with precision-aware boundary detection |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `PointMembership`, `DomainSegment`, `DomainBoundaryCurve`, `FaceDomain`, `FacePositionClass`, `WindingSettings`, `WindingTally`, `ContainmentMethod`, `SolidPointClass`, `BodyContainment`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Containment/FaceDomain.h"
#include "Engine/GeometrySpace/Parametric/Containment/WindingNumber.h"

using namespace Frontier::GeometrySpace::Parametric;
PointMembership Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
