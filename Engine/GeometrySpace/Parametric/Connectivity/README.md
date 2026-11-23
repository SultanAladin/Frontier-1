# 🧩 `Engine/GeometrySpace/Parametric/Connectivity/`

> **TL;DR**: Euler edits — the only topology mutations; each preserves V − E + 2F − L = 2(S − G) (5 source/header/shader files under `Engine/GeometrySpace/Parametric/Connectivity/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `EulerEdits.cpp` | Euler edits — the only topology mutations; each preserves V − E + 2F − L = 2(S − G) |
| `EulerEdits.h` | Euler edits — the only topology mutations; each preserves V − E + 2F − L = 2(S − G) |
| `ModelPartition.cpp` | B-rep topology (Body → Region → SolidShell → Face → Loop → Fin → Edge → Vertex) with attached exact geometry |
| `ModelPartition.h` | B-rep topology (Body → Region → SolidShell → Face → Loop → Fin → Edge → Vertex) with attached exact geometry |
| `SlotRegister.h` | Dense slot store with generation-checked tokens (TokenIssuer / TokenAuthentication) and a vacancy table |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `EditOutcome`, `LoopCorner`, `AcornOutcome`, `EdgeVertexOutcome`, `EdgeFaceOutcome`, `EdgeRingOutcome`, `BodyCategory`, `RegionMaterial`, `Orientation`, `BodyProfile`, `RegionProfile`, `SolidHullProfile`, `FaceProfile`, `LoopProfile`, `FinProfile`, `EdgeProfile`, `VertexProfile`, `BodyInventory` (+3 more)

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Connectivity/EulerEdits.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/SlotRegister.h"

using namespace Frontier::GeometrySpace::Parametric;
EditOutcome Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
