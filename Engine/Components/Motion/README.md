# 🧩 `Engine/Components/Motion/`

> **TL;DR**: Unified 3-detent (Closed / Half / Full) touch-like drag settle physics & elastic clamping shared by pull-down/pull-up drawers (3 source/header/shader files under `Engine/Components/Motion/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `DrawerDetentGesture.h` | Unified 3-detent (Closed / Half / Full) touch-like drag settle physics & elastic clamping shared by pull-down/pull-up drawers |
| `SpringIntegrator.cpp` | Substep-clamped semi-implicit Euler spring-damper integration |
| `SpringIntegrator.h` | Substep-clamped spring-damper integrator for UI trays, sheets, and smooth transitions |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`
- **Primary Types**: `DrawerDetentStop`, `SpringAxis`, `SpringIntegrator`

## Usage

```cpp
#include "Engine/Components/Motion/DrawerDetentGesture.h"
#include "Engine/Components/Motion/SpringIntegrator.h"

using namespace Frontier;
DrawerDetentStop Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
