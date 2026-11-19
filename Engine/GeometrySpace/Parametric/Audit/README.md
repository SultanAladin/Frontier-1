# 🧩 `Engine/GeometrySpace/Parametric/Audit/`

> **TL;DR**: Exact enclosed volume and parametric face area by Green's theorem over SP-curves (no tessellation) (8 source/header/shader files under `Engine/GeometrySpace/Parametric/Audit/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `EnclosedVolume.cpp` | Exact enclosed volume and parametric face area by Green's theorem over SP-curves (no tessellation) |
| `EnclosedVolume.h` | Exact enclosed volume and parametric face area by Green's theorem over SP-curves (no tessellation) |
| `IntersectionAudit.cpp` | Residual, box, completeness, termination, count and length checks for intersection outcomes |
| `IntersectionAudit.h` | Audit of a surface–surface intersection against the surfaces themselves and an independent dense reference lattice |
| `ModelAudit.cpp` | Tolerance-aware topological and geometric audit of a body — the gate every operation result must pass |
| `ModelAudit.h` | Tolerance-aware topological and geometric audit of a body — the gate every operation result must pass |
| `TessellationAudit.cpp` | Independent audit of a body tessellation: closure, Euler characteristic, deviation, orientation, volume, conformity |
| `TessellationAudit.h` | Independent audit of a body tessellation: closure, Euler characteristic, deviation, orientation, volume, conformity |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `SolidVolumeMeasure`, `IntersectionAuditReport`, `AuditScope`, `AuditTally`, `AuditFinding`, `AuditReport`, `TessellationAuditReport`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"
#include "Engine/GeometrySpace/Parametric/Audit/IntersectionAudit.h"
#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"

using namespace Frontier::GeometrySpace::Parametric;
SolidVolumeMeasure Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
