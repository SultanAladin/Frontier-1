# 🧩 `Engine/GeometrySpace/Parametric/Arrangement/`

> **TL;DR**: Assembly checks (priority 11, A1): mass properties of several bodies with densities, clearance and interference (4 source/header/shader files under `Engine/GeometrySpace/Parametric/Arrangement/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ArrangementAnalysis.cpp` | Assembly checks (priority 11, A1): mass properties of several bodies with densities, clearance and interference |
| `ArrangementAnalysis.h` | Assembly checks (priority 11, A1): mass properties of several bodies with densities, clearance and interference |
| `JointSolver.cpp` | Assembly joints (priority 11, A1): joint connectors read exactly off body geometry and the placement each joint kind imposes |
| `JointSolver.h` | Assembly joints (priority 11, A1): joint connectors read exactly off body geometry and the placement each joint kind imposes |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `InterferenceCondition`, `ArrangementBody`, `ArrangementMassProperties`, `BodyClearance`, `InterferenceMeasure`, `JointCategory`, `JointConnector`, `JointMotion`, `JointRange`, `JointPlacement`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Arrangement/ArrangementAnalysis.h"
#include "Engine/GeometrySpace/Parametric/Arrangement/JointSolver.h"

using namespace Frontier::GeometrySpace::Parametric;
InterferenceCondition Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
