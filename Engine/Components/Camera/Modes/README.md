# 🧩 `Engine/Components/Camera/Modes/`

> **TL;DR**: Eye-anchored world-up-aligned first-person navigation translating on the horizontal ground plane (6 source/header/shader files under `Engine/Components/Camera/Modes/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `FirstPersonMode.h` | Eye-anchored world-up-aligned first-person navigation translating on the horizontal ground plane |
| `FlyMode.h` | Free 6-DOF spatial flight camera translating along Forward/Right/Up axes with smoothed look & position |
| `FollowTargetMode.h` | Critically-damped spring pursuit tracking a moving target anchor with collision-guarded chase offset |
| `OrbitMode.h` | Pivot-centered right-handed spherical turntable navigation with exponential angular/pivot/log-distance smoothing |
| `Pan2DMode.h` | Smoothed 2D orthographic UDIM canvas pan and logarithmic zoom kinematics |
| `SplineRailMode.h` | Smooth Catmull-Rom spline rail traversal with forward tangent orientation or explicit look-at pivot |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Components::Modes`

## Usage

```cpp
#include "Engine/Components/Camera/Modes/FirstPersonMode.h"
#include "Engine/Components/Camera/Modes/FlyMode.h"
#include "Engine/Components/Camera/Modes/FollowTargetMode.h"

using namespace Frontier::Components::Modes;
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
