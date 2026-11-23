# 🧩 `Engine/Components/Camera/Kinematics/`

> **TL;DR**: Step-rate-independent exponential smoothing 1 - exp(-lambda * dt) and logarithmic zoom interpolation (2 source/header/shader files under `Engine/Components/Camera/Kinematics/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `InterpolationCurves.h` | Step-rate-independent exponential smoothing 1 - exp(-lambda * dt) and logarithmic zoom interpolation |
| `SpringDamper.h` | Exact analytical critically-damped second-order spring-damper integration for FollowTarget camera pursuit |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Components::Kinematics`

## Usage

```cpp
#include "Engine/Components/Camera/Kinematics/InterpolationCurves.h"
#include "Engine/Components/Camera/Kinematics/SpringDamper.h"

using namespace Frontier::Components::Kinematics;
// Entry functions: EvaluateExponentialBlendFactor, InterpolateScalarSmooth, InterpolateAxisSmooth, InterpolatePlanarSmooth
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
