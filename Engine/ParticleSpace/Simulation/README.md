# 🧩 `Engine/ParticleSpace/Simulation/`

> **TL;DR**: Real-time telemetry & physics health collector for ParticleSpace rigs (live count, speed, kinetic energy, AABB & species counters) (6 source/header/shader files under `Engine/ParticleSpace/Simulation/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ParticleMetricsCollector.cpp` | Implementation of ParticleMetricsCollector for CPU & Vulkan GPU reduction telemetry |
| `ParticleMetricsCollector.h` | Real-time telemetry & physics health collector for ParticleSpace rigs (live count, speed, kinetic energy, AABB & species counters) |
| `ParticleSoftwareExecutor.cpp` | Implementation of modular CPU particle executor dispatching active dynamics bitmask, ForceFieldAccumulator & ColourSpace tinting |
| `ParticleSoftwareExecutor.h` | Modular CPU particle simulation executor dispatching all 16 dynamics specs, ForceFieldAccumulator coupling & 20-vertex strand ribbons |
| `ParticleStrandRenderer.cpp` | Perspective Z-buffered 3D rasterizer for dynamic strands (hollow chain links, slender leafy vines, seamless quad-mesh flags/sails & beads) |
| `ParticleStrandRenderer.h` | Perspective Z-buffered 3D software rasterizer for dynamic strands (cables/chains, leafy vines, woven flags/sails, glossy beaded curtains & props) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::ParticleSpace`, `Frontier::SimulationSpace::PhysicsSpace`
- **Primary Types**: `ParticleSimulationMetrics`, `ParticleMetricsCollector`, `ParticleStepInput`, `ResolvedCamera`, `PixelCanvas`, `ScreenVert`, `StrandCameraSpec`, `DynamicStrandRigView`

## Usage

```cpp
#include "Engine/ParticleSpace/Simulation/ParticleMetricsCollector.h"
#include "Engine/ParticleSpace/Simulation/ParticleSoftwareExecutor.h"
#include "Engine/ParticleSpace/Simulation/ParticleStrandRenderer.h"

using namespace Frontier::ParticleSpace;
ParticleSimulationMetrics Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
