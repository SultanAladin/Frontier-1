# 🧩 `Engine/GeometrySpace/Parametric/Inspection/`

> **TL;DR**: Blend result → JSON: shown body geometry, face provenance, spines / contacts and the closed-form volume check (19 source/header/shader files under `Engine/GeometrySpace/Parametric/Inspection/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BlendEncoding.cpp` | Blend result → JSON: shown body geometry, face provenance, spines / contacts and the closed-form volume check |
| `BlendEncoding.h` | Blend result → JSON: shown body geometry, face provenance, spines / contacts and the closed-form volume check |
| `BooleanEncoding.cpp` | General Fuse + Boolean inspection as JSON: result body, history, ghost arguments, all four operations, identities |
| `BooleanEncoding.h` | General Fuse + Boolean inspection as JSON: result body, history, ghost arguments, all four operations, identities |
| `FaceOperationEncoding.cpp` | Face-operation result → JSON: shown body geometry, applied steps, face provenance and the closed-form volume check |
| `FaceOperationEncoding.h` | Face-operation result → JSON: shown body geometry, applied steps, face provenance and the closed-form volume check |
| `FeatureEncoding.cpp` | Sketch and feature-tree results → JSON: solved sketches, diagnosis, step reports, persistent names, body geometry |
| `FeatureEncoding.h` | Sketch and feature-tree results → JSON: solved sketches, diagnosis, step reports, persistent names, body geometry |
| `InspectionEncoding.cpp` | One-line JSON description of a body for the HTML inspection viewport (topology tree, exact samples, audit) |
| `InspectionEncoding.h` | One-line JSON description of a body for the HTML inspection viewport (topology tree, exact samples, audit) |
| `IntersectionEncoding.cpp` | Hatch patches, branch polylines, special points, metrics, audit and a scene tree as JSON |
| `IntersectionEncoding.h` | JSON encoding of an intersection scene for the HTML viewer (same schema family as model specimens, Kind = Intersection) |
| `IntersectionRepository.cpp` | Intersection scenes: analytic sections, classic singular curves (Steinmetz, Viviani, Villarceau), small loops |
| `IntersectionRepository.h` | Named surface–surface intersection scenes with independent expectations (counts, special points, exact length) |
| `JsonWriter.h` | Minimal streaming JSON writer shared by the inspection encoders |
| `SpecimenRepository.cpp` | Named specimen bodies with independently derived expectations (counts, genus, analytic volume) for the gates |
| `SpecimenRepository.h` | Named specimen bodies with independently derived expectations (counts, genus, analytic volume) for the gates |
| `TessellationEncoding.cpp` | PolygonNetworkStructure → JSON: geometry for the shaded view, per-face refinement records and the N1–N7 gate verdicts |
| `TessellationEncoding.h` | One-line JSON description of a PolygonNetworkStructure for the viewer's shaded mode (triangles, per-face records, N-gates) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `BlendInspection`, `BooleanInspectionResult`, `BooleanInspectionTiming`, `FaceOperationInspection`, `FeatureInspection`, `InspectionTiming`, `IntersectionExpectation`, `IntersectionScene`, `JsonWriter`, `SpecimenParameters`, `SpecimenExpectation`, `SpecimenOutcome`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Inspection/BlendEncoding.h"
#include "Engine/GeometrySpace/Parametric/Inspection/BooleanEncoding.h"
#include "Engine/GeometrySpace/Parametric/Inspection/FaceOperationEncoding.h"

using namespace Frontier::GeometrySpace::Parametric;
BlendInspection Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
