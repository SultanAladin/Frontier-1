# 🧩 `Engine/GeometricRaster/Compute/`

> **TL;DR**: Modular Vulkan .slang compute pipeline registry, descriptor encoder, and timeline transfer readback ring shared across UV, Geometry, Texture, Material & World rendering (4 source/header files under `Engine/GeometricRaster/Compute/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ComputeDispatchEncoder.cpp` | Implementation of ComputeDispatchEncoder managing .slang SPIR-V compute pipelines, descriptor sets, and UAV barriers |
| `ComputeDispatchEncoder.h` | Modular Vulkan .slang compute pipeline registry and descriptor encoder shared across UV, Geometry, Texture, Material & World rendering |
| `TimelineTransferRegister.cpp` | Implementation of TimelineTransferRegister encoding zero-stall GPU buffer/image transfers into 64B-aligned host-cached staging slots |
| `TimelineTransferRegister.h` | Reusable triple-buffered HOST_VISIBLE \| HOST_CACHED GPU->CPU readback ring synchronized via Vulkan Timeline Semaphores |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `ComputeSlotDescriptor`, `RegisteredComputeProgram`, `ComputeDispatchEncoder`, `ReadbackTicket`, `TimelineTransferRegister`

## Usage

```cpp
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/Compute/TimelineTransferRegister.h"

using namespace Frontier::GeometricRaster;
ComputeSlotDescriptor Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
