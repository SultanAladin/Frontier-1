# 🧩 `Engine/GeometrySpace/Parametric/Boolean/`

> **TL;DR**: Boolean scenes: argument pairs with closed-form volumes, shared by BooleanValidation and the inspection viewer (4 source/header/shader files under `Engine/GeometrySpace/Parametric/Boolean/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BooleanRepository.cpp` | Boolean scenes: argument pairs with closed-form volumes, shared by BooleanValidation and the inspection viewer |
| `BooleanRepository.h` | Boolean scenes: argument pairs with closed-form volumes, shared by BooleanValidation and the inspection viewer |
| `GeneralFuse.cpp` | General Fuse Booleans: interferences → pave blocks → split faces → GWN classification → selection → assembly |
| `GeneralFuse.h` | General Fuse Booleans: interferences → pave blocks → split faces → GWN classification → selection → assembly |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `BooleanExpectation`, `BooleanScene`, `BooleanOperation`, `FragmentCondition`, `GeneralFuseSettings`, `GeneralFuseStatistics`, `FuseVertex`, `FuseEdge`, `FuseUse`, `FuseFragment`, `GeneralFuseOutcome`, `FaceHistory`, `EdgeHistory`, `BooleanOutcome`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Boolean/BooleanRepository.h"
#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"

using namespace Frontier::GeometrySpace::Parametric;
BooleanExpectation Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
