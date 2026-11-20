# 🧩 `Engine/GeometrySpace/Parametric/Surfacing/`

> **TL;DR**: Certified NURBS approximation of procedural carriers (rolling ball, parallel offset, traced curves) — option A of the (16 source/header/shader files under `Engine/GeometrySpace/Parametric/Surfacing/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CarrierApproximation.cpp` | Certified cubic B-spline stand-ins for procedural carriers: Greville interpolation, triple knots at C¹ breaks |
| `CarrierApproximation.h` | Certified NURBS approximation of procedural carriers (rolling ball, parallel offset, traced curves) — option A of the |
| `CurveFitting.cpp` | Space-curve fitting: centripetal parameters, faired least squares, foot-point correction, knot refinement |
| `CurveFitting.h` | Space-curve fitting: ordered noisy points → a fair clamped B-spline within a tolerance (P&T ch. 9), corners split |
| `CurveNetwork.cpp` | Curve networks: crossing search, 2-colour family sorting, monotone reparametrisation + KKT refit, Gordon S₁ + S₂ − T |
| `CurveNetwork.h` | Curve networks (vehicle plan V5): family sorting, crossing search, compatible reparametrisation, Gordon / Coons surface |
| `LinearSystem.cpp` | Dense square solves: Doolittle LU with partial pivoting and a relative pivot floor |
| `LinearSystem.h` | Dense square solves for fitting and interpolation: LU with partial pivoting, several right-hand sides at once |
| `PanelJoining.cpp` | Panel sewing: corner merging, point-for-point boundary pairing, rim loops, plane caps, outward side by signed volume |
| `PanelJoining.h` | Panels → body (vehicle plan V7): sewing of coincident boundaries, mirror copies, planar caps, sheet or solid |
| `SectionLoft.cpp` | Loft through sections: knot merging, chord-averaged 𝑣ₖ, homogeneous interpolation, sheet assembly with iso-line charts |
| `SectionLoft.h` | Loft (skinning) through section curves: compatible sections → interpolation across them → one exact NURBS sheet face |
| `SurfaceAlgebra.cpp` | Degree elevation by Greville collocation, knot unions, pole rows / columns, skinning with end-derivative conditions |
| `SurfaceAlgebra.h` | Exact B-spline algebra for surfacing: degree elevation, knot union, pole rows / columns, skinning with end derivatives |
| `SurfaceDistanceCurve.cpp` | SurfaceDistanceCurve implementation |
| `SurfaceDistanceCurve.h` | Intrinsic distance on a connected triangle surface, not ambient XYZ distance. |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `ApproximationSettings`, `SurfaceApproximation`, `CurveApproximation`, `CurveFitSettings`, `CurveFitOutcome`, `NetworkSettings`, `NetworkCrossing`, `NetworkSurfaceOutcome`, `NetworkSheetOutcome`, `ClosedLoopEdge`, `ClosedCurveLoop`, `ClosedLoopSurfaceOutcome`, `DenseLinearArray`, `SurfacePanel`, `PanelJoiningSettings`, `PanelJoiningOutcome`, `LoftContinuity`, `LoftSettings` (+5 more)

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Surfacing/CarrierApproximation.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CurveFitting.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CurveNetwork.h"

using namespace Frontier::GeometrySpace::Parametric;
ApproximationSettings Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
