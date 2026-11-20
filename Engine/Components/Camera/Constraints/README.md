# 🧩 `Engine/Components/Camera/Constraints/`

> **TL;DR**: Spatial 3D AABB volume clamp and 2D UDIM planar canvas center constraint (3 source/header/shader files under `Engine/Components/Camera/Constraints/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BoundsConstraint.h` | Spatial 3D AABB volume clamp and 2D UDIM planar canvas center constraint |
| `CollisionAvoidance.h` | Analytic swept-sphere ray pullback from Pivot toward DesiredEye preventing camera occlusion inside scene obstacles |
| `PitchConstraint.h` | Symmetric pole-guard elevation constraint preventing right-handed camera basis gimbal flip at +/- 90 degrees |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Components::Constraints`

## Usage

```cpp
#include "Engine/Components/Camera/Constraints/BoundsConstraint.h"
#include "Engine/Components/Camera/Constraints/CollisionAvoidance.h"
#include "Engine/Components/Camera/Constraints/PitchConstraint.h"

using namespace Frontier::Components::Constraints;
// Entry functions: ClampSpatialPositionToBounds, ClampPlanarCenterToBounds
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
