# 🧩 `Engine/ParticleSpace/`

> **TL;DR**: 16-byte-aligned GPU & CPU particle simulation buffer layouts (80B ParticleSlot, 16B StrandVertex, 32B Anchor, Uniforms) (10 source/header/shader files under `Engine/ParticleSpace/`).

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Dynamics/`](Dynamics/README.md) | Modular divergence-free 3D curl noise & constrained manifold dynamics (free 3D, radial, sphere shell, torus, vortex funnel) |
| [`Origins/`](Origins/README.md) | Deterministic PRNG & analytic/structural release-origin samplers (Point, Sphere, Disc, Box, Lattice, Electrode, Web, Cloth, Flora) |
| [`Presets/`](Presets/README.md) | Authoritative C++ repository of all 102 Frontier Particle Studio presets (93 active + 9 quarantined across 15 VFX categories) |
| [`Simulation/`](Simulation/README.md) | Real-time telemetry & physics health collector for ParticleSpace rigs (live count, speed, kinetic energy, AABB & species counters) |
| [`Sprites/`](Sprites/README.md) | Deterministic multi-channel procedural sprite, crystal, wing, and dissolve-mask synthesizer shared by ParticleSpace and Pattern Studio |
| [`VisualCarriers/`](VisualCarriers/README.md) | Modular visual render-geometry carrier types (all 7 carriers + all 20 procedural sprite cutouts + ColourSpace OpticalBlendEquation) |

## Files

| File | Purpose |
| :--- | :--- |
| `ParticleBuffers.h` | 16-byte-aligned GPU & CPU particle simulation buffer layouts (80B ParticleSlot, 16B StrandVertex, 32B Anchor, Uniforms) |
| `ParticleDocument.cpp` | Implementation of ParticleDocument helpers and Studio Default scene seeding |
| `ParticleDocument.h` | Authoritative in-memory ParticleDocument holding ParticleRigs, linked Vector Field ForceSources, ambient wind, colliders & clock |
| `ParticleRig.cpp` | Implementation of ParticleRig GPU uniform packing and Studio Default rig factory |
| `ParticleRig.h` | Single authored particle rig composing ReleaseOrigin + VisualCarrier + ColourSpace::ColourSourceProfile + ParticleTintDriver + Dynamics |
| `ParticleSimulationSession.cpp` | Implementation of ParticleSimulationSession delegating simulation to modular ParticleSoftwareExecutor + ForceFieldAccumulator + ColourAssetRegister |
| `ParticleSimulationSession.h` | Designated ParticleSpace lifecycle participant coupling ForceFieldAccumulator, ColourAssetRegister, anchor caches & deterministic stepping |
| `ParticleTintDriver.cpp` | Implementation of ParticleTintDriver routing all 15 particle scalar channels directly through Frontier::ColourSpace::SampleColourSource |
| `ParticleTintDriver.h` | Particle-only scalar driver inputs (all 15 HTML COLOR_BY_MODES) routed directly into Engine/ColourSpace/ColourRampSampler |
| `ParticleVectorMath.h` | Shared single-source 3D vector helpers for ParticleSpace dynamics & strand rendering backed by Frontier::Mathematics::Axis3D |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::ParticleSpace`, `Frontier::SimulationSpace::PhysicsSpace`
- **Primary Types**: `AmbientWindConfiguration`, `SimulationClockConfiguration`, `ParticleDocument`, `ParticleKinematicsSpec`, `ParticleRig`, `ParticleTelemetrySnapshot`, `ParticleSimulationSession`, `ChannelDescriptor`, `ParticleScalarChannel`, `ParticleTintInput`, `ParticleTintDriverSpec`

## Usage

```cpp
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleRig.h"

using namespace Frontier::ParticleSpace;
AmbientWindConfiguration Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
