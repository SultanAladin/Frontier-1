# 🧩 `Engine/GeometrySpace/Parametric/Intersection/`

> **TL;DR**: Univariate Bernstein polynomials on [0, 1]: arithmetic, subdivision, and root isolation by Bézier clipping (12 source/header/shader files under `Engine/GeometrySpace/Parametric/Intersection/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BernsteinPolynomial.cpp` | Bernstein arithmetic (de Casteljau, elevation, products) and Bézier-clipping root isolation |
| `BernsteinPolynomial.h` | Univariate Bernstein polynomials on [0, 1]: arithmetic, subdivision, and root isolation by Bézier clipping |
| `CurveSurfaceIntersection.cpp` | Bézier clipping candidates, signed-distance Newton polishing, tolerant tangency merge |
| `CurveSurfaceIntersection.h` | Curve ∩ surface for lines/arcs against the analytic surfaces: all crossings, tangencies and coincident stretches |
| `ImplicitSurface.cpp` | Signed distances, rational Bézier conversion of lines and arcs, homogeneous implicit composition |
| `ImplicitSurface.h` | Implicit view of the analytic surfaces: exact signed distance + normal, and implicit equations composed with curves |
| `IntersectionCurve.cpp` | Exact intersection curves: Hermite guide, normal-plane Newton onto both surfaces, exact derivative of the map |
| `IntersectionCurve.h` | Exact intersection curves of two surfaces: a marched chart as the guide, Newton onto both surfaces per evaluation |
| `IsoParameterCurve.cpp` | Iso-parameter lines and circles per surface type |
| `IsoParameterCurve.h` | Exact iso-parameter curves of the analytic surfaces (every one is a line or a circle) |
| `SurfaceIntersection.cpp` | Analytic dispatch (meridians, cross-sections, plane–plane) and hybrid lattice / normal-cone / marching intersection |
| `SurfaceIntersection.h` | Surface ∩ surface over parameter boxes: exact analytic branches where they exist, hybrid lattice + marching elsewhere |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `BernsteinPolynomial`, `BernsteinSplit`, `RootIsolation`, `CrossingCategory`, `CurveSurfaceCrossing`, `CoincidentInterval`, `CurveSurfaceOutcome`, `CurveSurfaceSettings`, `SurfaceDistance`, `RationalSegment`, `BallProfileForm`, `BallBlendProfile`, `TracedIntersection`, `IsoDirection`, `IsoParameterCurve`, `ParameterBox`, `IntersectionPolicy`, `SurfaceIntersectionSettings` (+8 more)

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Intersection/BernsteinPolynomial.h"
#include "Engine/GeometrySpace/Parametric/Intersection/CurveSurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Intersection/ImplicitSurface.h"

using namespace Frontier::GeometrySpace::Parametric;
BernsteinPolynomial Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
