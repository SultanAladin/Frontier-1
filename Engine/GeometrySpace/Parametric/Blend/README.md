# 🧩 `Engine/GeometrySpace/Parametric/Blend/`

> **TL;DR**: Blend specimens: bodies, edge selections, closed-form volume changes and membership oracles (validation + viewer) (8 source/header/shader files under `Engine/GeometrySpace/Parametric/Blend/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BlendRepository.cpp` | Blend specimens: bodies, edge selections, closed-form volume changes and membership oracles (validation + viewer) |
| `BlendRepository.h` | Blend specimens: bodies, edge selections, closed-form volume changes and membership oracles (validation + viewer) |
| `EdgeBlend.cpp` | Rolling-ball fillets and chamfers on edge sets: vertex resolution (caps, mitres, ball corners) and direct rebuild |
| `EdgeBlend.h` | Rolling-ball fillets and chamfers on edge sets: vertex resolution (caps, mitres, ball corners) and direct rebuild |
| `FairPatch.cpp` | Energy-fair rational quads: homogeneous Bézier algebra, Boehm refinement, Coons start and the fairness solve |
| `FairPatch.h` | Energy-fair rational quads on exact rational Bézier rims: G0 boundary rows, G1 rows towards rim normal fields |
| `RollingBallGeometry.cpp` | Rolling-ball geometry of one edge: spine = offset ∩ offset, contact curves, exact blend carrier, conic sections |
| `RollingBallGeometry.h` | Rolling-ball geometry of one edge: spine = offset ∩ offset, contact curves, exact blend carrier, conic sections |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `BlendSpecimen`, `VertexBlendCategory`, `BlendFaceOrigin`, `BlendFaceHistory`, `BlendStatistics`, `BlendOutcome`, `Homogeneous`, `LeastSquares`, `RationalBezier`, `FairSide`, `FairQuadOutcome`, `BlendProfile`, `BlendSettings`, `EdgeBlendSheet`, `EdgeBlendSheetOutcome`, `SupportOverride`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Blend/BlendRepository.h"
#include "Engine/GeometrySpace/Parametric/Blend/EdgeBlend.h"
#include "Engine/GeometrySpace/Parametric/Blend/FairPatch.h"

using namespace Frontier::GeometrySpace::Parametric;
BlendSpecimen Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
