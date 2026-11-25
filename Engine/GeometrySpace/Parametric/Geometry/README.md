# 🧩 `Engine/GeometrySpace/Parametric/Geometry/`

> **TL;DR**: Analytic 3D curves (line, circle, ellipse), exact intersection curves and 2D parameter-space curves (SP-curves on faces) with exact inversion (13 source/header/shader files under `Engine/GeometrySpace/Parametric/Geometry/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CurveGeometry.cpp` | Analytic 3D curves (line, circle, ellipse) and 2D parameter-space curves (SP-curves on faces) with exact inversion |
| `CurveGeometry.h` | Analytic 3D curves (line, circle, ellipse), exact intersection curves and 2D parameter-space curves (SP-curves on faces) with exact inversion |
| `NurbsCarrier.cpp` | Rational B-spline curves and surfaces as first-class carriers: exact rational derivatives, inversion, iso-curves, affine images |
| `NurbsCarrier.h` | Rational B-spline curves and surfaces as first-class carriers: exact rational derivatives, inversion, iso-curves, affine images |
| `ParallelCarrier.cpp` | Exact offset (parallel surface) of a rational B-spline surface: evaluation, normals, inversion, curvature, fold margin |
| `ParallelCarrier.h` | Exact offset (parallel surface) of a rational B-spline surface: P + d·n̂ with Weingarten derivatives, inversion, fold test |
| `ProjectedCurve.cpp` | SP-curves for section curves: exact line / ellipse when the chart image is affine, exact projection otherwise |
| `ProjectedCurve.h` | SP-curves for section curves: exact line / ellipse when the chart image is affine, exact projection otherwise |
| `RollingBallCarrier.cpp` | Procedural rolling-ball carrier and its contact rails, evaluated from an exact spine (traced, elliptical or any curve) |
| `RollingBallCarrier.h` | Procedural rolling-ball carrier and its contact rails, evaluated from an exact spine (traced, elliptical or any curve) |
| `SurfaceGeometry.cpp` | Analytic surfaces (plane, cylinder, cone, sphere, torus): evaluation, closed-form normals, closed-form inversion; dispatch to procedural and NURBS carriers |
| `SurfaceGeometry.h` | Analytic surfaces (plane, cylinder, cone, sphere, torus), the procedural rolling-ball carrier and NURBS: evaluation, normals, inversion |
| `VectorAlgebra.h` | Double-precision 2D/3D vector algebra and orthonormal frames for exact geometry |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `StraightLine`, `CircularArc`, `EllipticalArc`, `IntersectionCurve`, `ContactCurve`, `NurbsCurve`, `ConicForm`, `CurveSample`, `ParameterLine`, `ParameterEllipse`, `ParameterProjection`, `ParameterSample`, `NurbsCurveDefinition`, `NurbsSurfaceDefinition`, `NurbsCurveDerivatives`, `NurbsSurfaceDerivatives`, `NurbsDomain`, `NurbsBezierPiece` (+23 more)

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"

using namespace Frontier::GeometrySpace::Parametric;
StraightLine Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
