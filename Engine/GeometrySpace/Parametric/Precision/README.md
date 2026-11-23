# 🧩 `Engine/GeometrySpace/Parametric/Precision/`

> **TL;DR**: Floating-point-filtered exact orientation predicates (Shewchuk expansion arithmetic) for combinatorial decisions (3 source/header/shader files under `Engine/GeometrySpace/Parametric/Precision/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ExactPredicates.cpp` | Floating-point-filtered exact orientation predicates (Shewchuk expansion arithmetic) for combinatorial decisions |
| `ExactPredicates.h` | Floating-point-filtered exact orientation predicates (Shewchuk expansion arithmetic) for combinatorial decisions |
| `PrecisionSpecification.h` | Session precision and tolerant-modelling rules (exact by default, local precision per edge and vertex) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `SignResolution`, `SignOutcome`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Precision/ExactPredicates.h"
#include "Engine/GeometrySpace/Parametric/Precision/PrecisionSpecification.h"

using namespace Frontier::GeometrySpace::Parametric;
SignResolution Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
