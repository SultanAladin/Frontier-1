# 🧩 `Engine/GeometricRaster/Compute/`

> **TL;DR**: Modular Vulkan .slang compute pipeline registry and descriptor encoder shared across UV, Geometry, Texture, Material & Simulation (8 source/header/shader files under `Engine/GeometricRaster/Compute/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ComputeDispatchEncoder.cpp` | Implementation of ComputeDispatchEncoder managing .slang SPIR-V compute pipelines, descriptor sets, and UAV barriers |
| `ComputeDispatchEncoder.h` | Modular Vulkan .slang compute pipeline registry and descriptor encoder shared across UV, Geometry, Texture, Material & Simulation |
| `FieldLatticeVulkanExecutor.cpp` | Runtime Vulkan 1.3 Compute executor dispatching FieldLatticeCompute.spv (hardware ICD or headless SwiftShader fallback) |
| `FieldLatticeVulkanExecutor.h` | Runtime Vulkan 1.3 Compute executor dispatching FieldLatticeCompute.spv (hardware ICD or headless SwiftShader fallback) |
| `ParticleVulkanExecutor.cpp` | Implementation of ParticleVulkanExecutor: 21-module SPIR-V 1.3 verifier & real Vulkan 1.3 / SwiftShader compute dispatcher |
| `ParticleVulkanExecutor.h` | Real Vulkan 1.3 SPIR-V compute & raster pipeline executor (SwiftShader/hardware GPU dispatch + deterministic CPU bridge) |
| `TimelineTransferRegister.cpp` | Implementation of TimelineTransferRegister encoding zero-stall GPU buffer/image transfers into 64B-aligned host-cached staging slots |
| `TimelineTransferRegister.h` | Reusable triple-buffered HOST_VISIBLE \| HOST_CACHED GPU->CPU readback ring synchronized via Vulkan Timeline Semaphores |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`, `Frontier::SimulationSpace::ForceFields`, `Frontier::ParticleSpace`
- **Primary Types**: `ComputeSlotDescriptor`, `RegisteredComputeProgram`, `ComputeDispatchEncoder`, `LatticePushConstantBlock`, `FieldLatticeVulkanExecutor`, `HostMappedBuffer`, `ParticleVulkanExecutor`, `ParticleShaderArtifacts`, `ParticleVulkanTelemetry`, `ReadbackTicket`, `TimelineTransferRegister`

## Usage

```cpp
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometricRaster/Compute/FieldLatticeVulkanExecutor.h"
#include "Engine/GeometricRaster/Compute/ParticleVulkanExecutor.h"

using namespace Frontier::GeometricRaster;
ComputeSlotDescriptor Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
