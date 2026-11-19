# 🧩 `Engine/Constants/`

> **TL;DR**: Shared compile-time engine capacities, 64-byte container alignment, and 215 MiB / 750 MiB revision memory ceilings (4 source/header/shader files under `Engine/Constants/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `EngineConstants.h` | Shared compile-time engine capacities, 64-byte container alignment, and 215 MiB / 750 MiB revision memory ceilings |
| `MathConstants.h` | Shared compile-time mathematical constants, angular conversion factors, and IEEE-754 comparison tolerances |
| `PhysicsConstants.h` | Shared compile-time physical constants, reference medium densities, and contact friction limits |
| `TimeConstants.h` | Shared compile-time timestep durations, unit conversions, priority tick slice guards, and watchdog thresholds |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Constants`

## Usage

```cpp
#include "Engine/Constants/EngineConstants.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Constants/PhysicsConstants.h"

using namespace Frontier::Constants;
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
