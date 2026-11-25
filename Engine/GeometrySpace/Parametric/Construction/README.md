# 🧩 `Engine/GeometrySpace/Parametric/Construction/`

> **TL;DR**: Direct assembly of a solid body from a blueprint of vertices, edges with carriers and faces with SP-curved loops (8 source/header/shader files under `Engine/GeometrySpace/Parametric/Construction/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BodyArrangement.cpp` | Direct assembly of a solid body from a blueprint of vertices, edges with carriers and faces with SP-curved loops |
| `BodyArrangement.h` | Direct assembly of a solid body from a blueprint of vertices, edges with carriers and faces with SP-curved loops |
| `BodyModifiers.cpp` | Non-destructive CAD modifier kernels: multi-instance assembly, linear/circular/curve/surface arrays, and certified 3D curve bend |
| `BodyModifiers.h` | Non-destructive CAD modifier kernels: multi-instance assembly, linear/circular/curve/surface arrays, and certified 3D curve bend |
| `BodyTransform.cpp` | Transformed copies of a body — carriers mapped per kind, SP-curves re-fitted, loops reversed by mirrors |
| `BodyTransform.h` | Transformed copies of a body — move, rotate, mirror, scale and arrays (G2), exact on every carrier kind it accepts |
| `PrimitiveConstruction.cpp` | Solid primitives assembled purely from Euler edits, then given exact analytic geometry and SP-curves |
| `PrimitiveConstruction.h` | Solid primitives assembled purely from Euler edits, then given exact analytic geometry and SP-curves |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `BlueprintVertex`, `BlueprintEdge`, `BlueprintUse`, `BlueprintFace`, `BodyBlueprint`, `ArrangementOutcome`, `ModifierBodyOutcome`, `AffineMapping`, `TransformOutcome`, `ApproximationOutcome`, `PrismArrangement`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Construction/BodyArrangement.h"
#include "Engine/GeometrySpace/Parametric/Construction/BodyModifiers.h"
#include "Engine/GeometrySpace/Parametric/Construction/BodyTransform.h"

using namespace Frontier::GeometrySpace::Parametric;
BlueprintVertex Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
