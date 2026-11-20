# 🧩 `Engine/Components/ControlCentre/`

> **TL;DR**: Top-notch pull-down Control Centre, Appearance/Input/Notifications/Workspace settings, toast overlay, and modal dialogues (2 source/header/shader files under `Engine/Components/ControlCentre/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ControlCentrePanel.cpp` | Top-notch pull-down Control Centre, Appearance/Input/Notifications/Workspace settings, toast overlay, and modal dialogues |
| `ControlCentrePanel.h` | Top-notch pull-down Control Centre, Appearance/Input/Notifications/Workspace settings, toast overlay, and modal dialogues |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`
- **Primary Types**: `ControlCentrePage`, `AppearanceSectionTab`, `ControlCentrePanel`

## Usage

```cpp
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"

using namespace Frontier;
ControlCentrePage Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
