# 🧩 `Engine/Components/Notch/`

> **TL;DR**: Top-edge trapezoidal pull-down notch ("Frontier") with elastic carry, axis lock, and flick physics (2 source/header/shader files under `Engine/Components/Notch/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `PanelExpansionTab.cpp` | Top-edge trapezoidal pull-down notch implementation matching Frontier-OLD 1:1. |
| `PanelExpansionTab.h` | Top-edge trapezoidal pull-down notch ("Frontier") with elastic carry, axis lock, and flick physics |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`
- **Primary Types**: `PanelExpansionTab`

## Usage

```cpp
#include "Engine/Components/Notch/PanelExpansionTab.h"

using namespace Frontier;
PanelExpansionTab Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
