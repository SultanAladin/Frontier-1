# 🧩 `Engine/Components/Camera/Diagnostics/`

> **TL;DR**: Non-Shipping normalized 2D viewport composition overlay segments (Rule of Thirds, Broadcast Safe Gate, Optical Reticle) (3 source/header/shader files under `Engine/Components/Camera/Diagnostics/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CompositionOverlay.h` | Non-Shipping normalized 2D viewport composition overlay segments (Rule of Thirds, Broadcast Safe Gate, Optical Reticle) |
| `FocusPlaneIndicator.h` | Non-Shipping world-space thin-lens focus plane quad and near/far acceptable sharpness plane markers |
| `FrustumVisualizer.h` | Non-Shipping 8-corner / 12-segment world-space camera frustum outline generator |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Components::Diagnostics`
- **Primary Types**: `FocusPlaneQuad`, `FrustumOutlineGeometry`

## Usage

```cpp
#include "Engine/Components/Camera/Diagnostics/CompositionOverlay.h"
#include "Engine/Components/Camera/Diagnostics/FocusPlaneIndicator.h"
#include "Engine/Components/Camera/Diagnostics/FrustumVisualizer.h"

using namespace Frontier::Components::Diagnostics;
FocusPlaneQuad Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
