# 🧩 `Engine/SimulationSpace/FluidSpace/`

> **TL;DR**: Pure TOML serializer & deserializer for FluidSceneDocument (.fluid.toml & legacy Y-up .gasscene.toml conversion) (19 source/header/shader files under `Engine/SimulationSpace/FluidSpace/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `FluidCodec.cpp` | Pure TOML codec using tomlpp (no JSON): handles native +Z-Up .fluid.toml & legacy Y-Up .gasscene.toml import |
| `FluidCodec.h` | Pure TOML serializer & deserializer for FluidSceneDocument (.fluid.toml & legacy Y-up .gasscene.toml conversion) |
| `FluidDescriptors.h` | Authoritative +Z-Up descriptors for Eulerian Fire/Smoke, Continuous World Drift Grid, ForceFields, Particles & GI Coupling |
| `FluidForceCoupler.cpp` | Samples ForceFieldAccumulator & FieldLatticeCascade into Fluid velocity and emits RadialBurst impulses on detonation |
| `FluidForceCoupler.h` | Bidirectional bridge between SimulationSpace::ForceFields (ForceFieldAccumulator / FieldLatticeCascade) and FluidSpace |
| `FluidGiLightExtractor.cpp` | Clusters high-temperature blackbody cells across 2x2x2 octants into bounded GI emissive light proxies |
| `FluidGiLightExtractor.h` | Reduces hot combustion voxels into dynamic blackbody GI emissive point/sphere light proxies for OpenPBR, DDGI & PathTrace |
| `FluidGridBuffers.cpp` | Allocation, indexing, and trilinear sampling for Hero Pyro Grid & Continuous World Drift Smoke Grid |
| `FluidGridBuffers.h` | Dual-tier 3D Eulerian fluid voxel buffers (Hero Pyro Grid + Sliding World Smoke Drift Grid) for CPU & Vulkan GPU compute |
| `FluidObstacleVoxelizer.cpp` | Voxelizes GI SignedDistanceVolume + WideBoundingTree + 5 analytical colliders into +Z-Up fluid solid masks |
| `FluidObstacleVoxelizer.h` | Voxelizes GI SignedDistanceVolume, WideBoundingTree & analytical FluidObstacle primitives into solid fluid collision masks |
| `FluidParticleCoupler.cpp` | Fluid-driven Ember/Ash advection in ParticleSlots & ballistic Shrapnel smoke/fire injection into Fluid grids |
| `FluidParticleCoupler.h` | Bidirectional ParticleSpace <-> FluidSpace bridge: Fluid-advected Ember/Ash ParticleSlots & ballistic shrapnel fluid splats |
| `FluidPresets.cpp` | Built-in Fire/Smoke/Explosion presets & 6 physical/stylized blackbody palettes |
| `FluidPresets.h` | Built-in Fire/Smoke/Explosion presets & blackbody color palette evaluator (ported from Experimental/FluidEditor) |
| `FluidSimulationSequence.cpp` | Fixed-step +Z-Up 3D Eulerian Fire/Smoke solver with adaptive headroom, sliding World Drift Grid, ForceFields, Particles & GI coupling |
| `FluidSimulationSequence.h` | Fixed-step 3D Eulerian Fire/Smoke solver + Dual-tier World Drift Grid + ForceFields/Particle/GI coupling (CPU & Vulkan Compute) |
| `FluidWorldGridPartition.cpp` | Implements adaptive explosion ceiling growth & sliding toroidal World Smoke Drift Grid |
| `FluidWorldGridPartition.h` | Adaptive uncapped explosion headroom + sliding toroidal World Smoke Drift Grid (prevents top-wall flattening & side-wall clipping) |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::SimulationSpace::FluidSpace`
- **Primary Types**: `FluidCodec`, `FluidColorScheme`, `FluidObstacleType`, `FluidRenderChannel`, `FluidAtlasMinimapField`, `FluidDebrisCategory`, `FluidInteractionTool`, `FluidDomainSettings`, `FluidSourceSettings`, `FluidBlastSpecification`, `FluidCombustionSettings`, `FluidObstacleSettings`, `FluidShadingSettings`, `FluidParticleCouplingSettings`, `FluidGiCouplingSettings`, `FluidEmissiveLightProxy`, `FluidSceneCameraBasis`, `FluidSceneNames` (+16 more)

## Usage

```cpp
#include "Engine/SimulationSpace/FluidSpace/FluidCodec.h"
#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"
#include "Engine/SimulationSpace/FluidSpace/FluidForceCoupler.h"

using namespace Frontier::SimulationSpace::FluidSpace;
FluidCodec Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
