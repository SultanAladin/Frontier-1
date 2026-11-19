# 🧩 `Engine/SimulationSpace/PhysicsSpace/`

> **TL;DR**: Broadphase collision channels, raycast/overlap queries, and 80-byte GPU/CPU RigidColliderProxy particle collision solver (14 source/header/shader files under `Engine/SimulationSpace/PhysicsSpace/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BroadphaseSolver.cpp` | Analytical Sphere, Oriented Box (OBB), and Oriented +Z Capsule collision resolution for RigidColliderProxy & ParticleSpace |
| `BroadphaseSolver.h` | Broadphase collision channels, raycast/overlap queries, and 80-byte GPU/CPU RigidColliderProxy particle collision solver |
| `CollisionDetection.cpp` | Implementation of narrow-phase ContactImpulseJournal tracking peak impulses and impact energy per simulation step |
| `CollisionDetection.h` | Narrow-phase contact impulse journal capturing peak impact impulses & energies for rigid bodies and fracture triggers |
| `ConstraintSolver.cpp` | Strain-bond rupture ratio evaluation utilities for two-body physics constraints |
| `ConstraintSolver.h` | Two-body joint & breakable strain-bond constraint descriptors, generation-checked tokens, and rupture telemetry |
| `FractureSimulationRunner.cpp` | JoltPhysics v5.6.0 rigid-body fracture simulation runner: ConvexHull shards, BreakableBond strain rupture, and ground interaction |
| `FractureSimulationRunner.h` | JoltPhysics v5.6.0 rigid-body fracture simulation runner: ConvexHull shards, BreakableBond strain rupture, and RHR +Z Up ground contacts |
| `PhysicsExtension.cpp` | JoltPhysics v5.6.0 Pimpl runtime implementing rigid dynamics, breakable strain bonds, ForceFields forces & ParticleSpace proxies |
| `PhysicsExtension.h` | Deterministic JoltPhysics v5.6.0 lifecycle participant satisfying SuspendableParticipant, ForceFields coupling & ParticleSpace colliders |
| `RigidBodyDynamics.cpp` | Analytic volume, effective mass, principal inertia, and quaternion rotation utilities for rigid bodies |
| `RigidBodyDynamics.h` | Rigid-body shape descriptors, motion categories, generation-checked tokens, and inertial tensor evaluators (RHR +Z Up) |
| `VehicleDynamicsSolver.cpp` | Implementation of VehicleDynamicsSolver TorqueCurve spline evaluation, 6-speed shifting & aero downforce telemetry |
| `VehicleDynamicsSolver.h` | Vehicle powertrain, 4-law TorqueCurve interpolation, 6-speed gearbox, aero downforce & constraint-based Jolt rig driver |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::SimulationSpace::PhysicsSpace`, `BroadPhaseLayers`
- **Primary Types**: `CollisionLayerChannel`, `RayCastQuery`, `RayCastHitReading`, `SphereOverlapQuery`, `ParticleCollisionFeedback`, `BroadphaseSolver`, `ContactImpulseReading`, `CollisionDetection`, `ConstraintCategory`, `ConstraintToken`, `ConstraintDescriptor`, `ConstraintReading`, `ConstraintSolver`, `FractureCollisionShapeMode`, `FractureSimulationParameters`, `FractureBodyTransformPose`, `FractureBondCondition`, `FractureSimulationTelemetry` (+15 more)

## Usage

```cpp
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/CollisionDetection.h"
#include "Engine/SimulationSpace/PhysicsSpace/ConstraintSolver.h"

using namespace Frontier::SimulationSpace::PhysicsSpace;
CollisionLayerChannel Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
