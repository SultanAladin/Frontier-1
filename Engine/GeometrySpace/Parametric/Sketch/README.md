# 🧩 `Engine/GeometrySpace/Parametric/Sketch/`

> **TL;DR**: Interactive sketch constraining: SolveSpace constraint verbs on picked points / curves, dimension measuring, point drag (22 source/header/shader files under `Engine/GeometrySpace/Parametric/Sketch/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `SketchConstraining.cpp` | Interactive sketch constraining: verb + picks → constraint, dimension measuring, names, pinned point drag |
| `SketchConstraining.h` | Interactive sketch constraining: SolveSpace constraint verbs on picked points / curves, dimension measuring, point drag |
| `SketchCorner.cpp` | Sketch corner fillet / chamfer: setbacks along the two lines, a tangent arc or a cut line, the corner kept as a virtual sharp |
| `SketchCorner.h` | Sketch corner fillet / chamfer (Plasticity fillet-curve): two lines meeting at a point get a tangent arc or a cut line |
| `SketchEditing.cpp` | Sketch editing (Plasticity O, J): chain offsets re-joined at carrier intersections, sketch merging with point welding |
| `SketchEditing.h` | Sketch editing (Plasticity O offset-curve, J join-curves): exact offsets of line / arc / circle chains, sketch merging |
| `SketchProfile.cpp` | Closed profile loops from a solved sketch: chaining, nesting, orientation (outer CCW, holes CW), exact area |
| `SketchProfile.h` | Closed profile loops from a solved sketch: chaining, nesting, orientation (outer CCW, holes CW), exact area |
| `SketchRailDeform.cpp` | Flow along curve: shape axis box, rail chain with an arc-length table, mapped samples refitted as cubic splines; |
| `SketchRailDeform.h` | Flow along curve (Rhino Flow, Blender curve deform): a 2D shape bent so its long axis runs along a rail curve; Wrap: a |
| `SketchRegion.cpp` | Closed regions of overlapping sketch curves: arrangement by crossings, faces traced left-handed, the face under a point |
| `SketchRegion.h` | Closed regions of overlapping sketch curves (Plasticity fill): the planar arrangement, the face under the pointer |
| `SketchRepository.cpp` | Sketch corpus: under-, well-, over-constrained and singular sketches with hand-derived expectations |
| `SketchRepository.h` | Sketch corpus: under-, well-, over-constrained and singular sketches with hand-derived expectations |
| `SketchSection.cpp` | A solved sketch as one exact NURBS section curve (lines, polylines, arcs, slots, circles, splines) for Loft |
| `SketchSection.h` | A solved sketch as one exact NURBS section curve (lines, polylines, arcs, slots, circles, splines) for Loft |
| `SketchSolver.cpp` | 2D sketch model and numerical constraint solver: minimum-norm Gauss–Newton on a rank-revealing QR, DOF = n − rank |
| `SketchSolver.h` | 2D sketch model and numerical constraint solver: minimum-norm Gauss–Newton on a rank-revealing QR, DOF = n − rank |
| `SketchSpline.cpp` | Freeform sketch curves (Hermite, Bezier, NURBS, cubic spline) resolved to exact polynomial clamped B-splines |
| `SketchSpline.h` | Freeform sketch curves (Hermite, Bezier, NURBS, cubic spline) resolved to exact polynomial clamped B-splines |
| `SketchTrim.cpp` | Curve trim (Plasticity T): crossings by polyline seeds + Newton, then the picked piece comes off the sketch curve |
| `SketchTrim.h` | Curve trim (Plasticity T): the piece of a sketch curve under the pointer, between its neighbouring crossings, is removed; |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `SketchPickCategory`, `SketchConstraintVerb`, `SketchPick`, `SketchConstraintProposal`, `SketchDragOutcome`, `SketchCornerShape`, `SketchCornerOutcome`, `SketchOffsetOutcome`, `SketchPlacement`, `SketchJoinOutcome`, `ProfileSegmentCategory`, `ProfileSegment`, `ProfileLoop`, `SketchProfile`, `SketchWrapCategory`, `SketchRailDeformSettings`, `SketchRailDeformOutcome`, `SketchWrapSettings` (+25 more)

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Sketch/SketchConstraining.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchCorner.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchEditing.h"

using namespace Frontier::GeometrySpace::Parametric;
SketchPickCategory Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
