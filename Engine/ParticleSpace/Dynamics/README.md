# 🧩 `Engine/ParticleSpace/Dynamics/`

> **TL;DR**: Modular divergence-free 3D curl noise & constrained manifold dynamics (free 3D, radial, sphere shell, torus, vortex funnel) (21 source/header/shader files under `Engine/ParticleSpace/Dynamics/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CurlManifoldDynamics.cpp` | Implementation of divergence-free 3D curl noise & manifold projection solvers (modes 1..5) |
| `CurlManifoldDynamics.h` | Modular divergence-free 3D curl noise & constrained manifold dynamics (free 3D, radial, sphere shell, torus, vortex funnel) |
| `DynamicStrandSolver.cpp` | Implementation of exact 20-node HTML Particle Studio strand solvers (cables/chains, vines+leaves, flags, ship sails, beaded curtains) |
| `DynamicStrandSolver.h` | Authentic 20-node catenary cables/chains, hanging leafy vines, woven flags/ship sails & beaded doorway curtain physics solver |
| `EffectorGridSolver.cpp` | Exact C++ MoGraph & Effector Grid solver (Kinetic Hex Torus Matrix, Obsidian Control Vehicle Halftone & Tiles, Height Cubes, Placeable 3D Effectors) |
| `EffectorGridSolver.h` | Exact C++ MoGraph & Effector Grid solver (Kinetic Hex Torus Matrix, Obsidian Control Vehicle Halftone & Tiles, Height Cubes, Placeable 3D Effectors) |
| `FluidGranularDynamics.cpp` | Implementation of SPH Fluid, DEM Granular collisions, Elastic PBD Strands, Thermal/Fireworks, Dielectric Arcs, Field Matrix & Flora |
| `FluidGranularDynamics.h` | Modular SPH Fluid, DEM Granular collisions, Elastic PBD Strands/Cloth, Thermal Pyrotechnics, Dielectric Arcs, Field Matrix & Flora |
| `MolecularReactionDynamics.cpp` | Implementation of spatial-hashed Lennard-Jones 12-6 molecular kinetics, Langevin thermostat & A + B <-> C reaction kinetics |
| `MolecularReactionDynamics.h` | Modular Lennard-Jones 12-6 molecular dynamics, Langevin thermostat & reversible A + B <-> C chemical reaction kinetics |
| `OrbitalElectromagneticDynamics.cpp` | Implementation of Keplerian gravity, Boris electromagnetic pusher, Chladni standing waves & 9 RK4 chaotic attractors |
| `OrbitalElectromagneticDynamics.h` | Modular orbital gravity, exact Boris Lorentz/dipole electromagnetism, Chladni standing waves & 9 chaotic RK4 strange attractors |
| `SimulationDynamicsTypes.h` | Composable typed particle physics & simulation dynamics specifications (zero monolithic switches, directly reuses ForceFields) |
| `SpecialEffectsDynamics.cpp` | Implementation of Ferrofluid, Fracture, Shallow-Water Splashes, Transition Derez, Shield Barrier, Spiderweb & MoGraph Effectors |
| `SpecialEffectsDynamics.h` | Modular specialized physics solvers: Ferrofluid spikes, Ice/Seismic Fracture, Shallow-Water Splashes, Derez, Shield Barrier, Web & Effectors |
| `SplineEnsembleSolver.cpp` | Exact 1:1 C++20 implementation of HTML Particle Studio Curl/Bokeh Stream Ribbons, Spiderweb Catenary Splines, and Dielectric Arcs |
| `SplineEnsembleSolver.h` | 1:1 C++20 spline & filament solvers: Curl Stream Ribbons, Bokeh Object Pour, Catenary Spiderwebs, and Dielectric Lightning Arcs |
| `SwarmFlockingDynamics.cpp` | Implementation of spatial-hashed Reynolds Boids, multi-species Particle Life, & Physarum chemotaxis dynamics |
| `SwarmFlockingDynamics.h` | Modular Boids 3D flocking, multi-species asymmetric attraction/repulsion (SpeciesLife), & Physarum morphogenetic chemotaxis |
| `VehicleDissolveSolver.cpp` | Controlled Integrator (ControlVehicle) 16^3 lattice Derez, Reverse-Rez, Dissolve-to-Nothing, and Dissolve-to-Butterfly implementation |
| `VehicleDissolveSolver.h` | Controlled Integrator (ControlVehicle) 16^3 lattice Derez, Reverse-Rez, Dissolve-to-Nothing, and Dissolve-to-Butterfly solver + Z-buffered two-way GI renderer |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::ParticleSpace`
- **Primary Types**: `DynStrandRest`, `BeadRest`, `HardwareCylinderSegment`, `HardwareSphereAccent`, `HardwareRingAccent`, `StrandRiggingHardware`, `EvaluatedAnchorPoint`, `EffectorVolumeShape`, `EffectorDriverCategory`, `ShieldBarrierForm`, `BallisticDynamicsSpec`, `ForceFieldCouplingSpec`, `CurlManifoldSpec`, `StrandConstraintSpec`, `BotanicalSwaySpec`, `OrbitalGravitySpec`, `ElectromagneticSpec`, `HarmonicWaveSpec` (+42 more)

## Usage

```cpp
#include "Engine/ParticleSpace/Dynamics/CurlManifoldDynamics.h"
#include "Engine/ParticleSpace/Dynamics/DynamicStrandSolver.h"
#include "Engine/ParticleSpace/Dynamics/EffectorGridSolver.h"

using namespace Frontier::ParticleSpace;
DynStrandRest Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
