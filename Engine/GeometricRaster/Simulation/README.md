# 🧩 `Engine/GeometricRaster/Simulation/`

> **TL;DR**: Editor-only Vulkan 1.3 dynamic-rendering overlay sequence executing all 6 .slang ForceField rasterization pipelines + MRT PickID (2 source/header/shader files under `Engine/GeometricRaster/Simulation/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `VectorFieldOverlaySequence.cpp` | Editor-only Vulkan 1.3 dynamic-rendering overlay sequence executing all 6 .slang ForceField rasterization pipelines + MRT PickID |
| `VectorFieldOverlaySequence.h` | Editor-only Vulkan 1.3 dynamic-rendering overlay sequence executing all 6 .slang ForceField rasterization pipelines + MRT PickID |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Editor::Simulation`, `Frontier::GeometricRaster`
- **Primary Types**: `RenderInstancePushConstantsGpu`, `LitBeaconVertex`, `PositionNormalVertex`, `Float3`, `GpuBufferAllocation`, `PaneTargetResources`, `VectorFieldOverlaySequence`, `OverlayPickCategory`, `OverlayCameraBasis`, `OverlayPickResult`, `OverlayRenderRequest`, `OverlayRenderOutput`

## Usage

```cpp
#include "Engine/GeometricRaster/Simulation/VectorFieldOverlaySequence.h"

using namespace Frontier::Editor::Simulation;
RenderInstancePushConstantsGpu Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
