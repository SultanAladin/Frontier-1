# 🧩 `Engine/GeometrySpace/Parametric/Diagnostics/`

> **TL;DR**: Development trace of operations: what began, what it decided, how long it took (FRONTIER_DEVELOPMENT_TRACE builds) (2 source/header/shader files under `Engine/GeometrySpace/Parametric/Diagnostics/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `OperationTrace.cpp` | Development trace of operations: what began, what it decided, how long it took (FRONTIER_DEVELOPMENT_TRACE builds) |
| `OperationTrace.h` | Development trace of operations: what began, what it decided, how long it took (FRONTIER_DEVELOPMENT_TRACE builds) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `OperationTraceScope`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"

using namespace Frontier::GeometrySpace::Parametric;
OperationTraceScope Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
