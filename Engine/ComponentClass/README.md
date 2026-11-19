# 🧩 `Engine/ComponentClass/`

> **TL;DR**: Top-level ComponentClass asset encapsulating title, simulation role, revision, baked RGBA thumbnail & ComponentInstanceStorage (12 source/header/shader files under `Engine/ComponentClass/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ComponentClassDefinition.cpp` | Implementation of ComponentClassDefinition revision tracking and role labels |
| `ComponentClassDefinition.h` | Top-level ComponentClass asset encapsulating title, simulation role, revision, baked RGBA thumbnail & ComponentInstanceStorage |
| `ComponentClassToken.h` | Generation-checked identity handles for ComponentClass assets, typed component instances, and world placements |
| `ComponentClassValidator.cpp` | Implementation of ComponentClass structural checks (assets, Dock key tags, wheel bindings, torque curve monotonicity) |
| `ComponentClassValidator.h` | Structural and physics validation diagnostics for ComponentClass definitions prior to compilation or world spawning |
| `ComponentInstanceCategories.h` | Lean POD component instance descriptors: SpatialOrigin, Geometry, Physics Constraints, Particle, Fluid, Light, Camera, Collision, Vehicle |
| `ComponentInstanceStorage.cpp` | Implementation of contiguous ComponentClass instance arrays, unique naming, cycle-free tree links & category lookup |
| `ComponentInstanceStorage.h` | Cache-contiguous typed component instance storage with EnclosingIndex/FirstNestedIndex/NextPeerIndex hierarchy links |
| `ComponentWorldPlacement.cpp` | Implementation of ComponentWorldPlacement spawning, sparse per-instance overrides & effective transform resolution |
| `ComponentWorldPlacement.h` | World placement record binding a compiled ComponentClass into World Outliner rows with non-destructive per-instance overrides |
| `ControlVehicleClassBuilder.cpp` | Implementation of ControlVehicle FCT1 ingestion, RearWing synthesis, 4-corner Spring/Hinge wheels, Dock, Tether & VehicleDynamics |
| `ControlVehicleClassBuilder.h` | Deterministic ControlVehicle ComponentClass & FCT1 asset repository builder matching Vehicle Lab & ComponentViewer specs |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::ComponentClass`
- **Primary Types**: `ComponentClassArchetype`, `ComponentClassDefinition`, `ComponentClassToken`, `ComponentInstanceToken`, `WorldPlacementToken`, `DiagnosticSeverity`, `ClassDiagnosticIssue`, `ComponentClassValidator`, `ComponentCategory`, `PhysicsConstraintCategory`, `ConstraintAxis`, `ConstraintDriveMode`, `LightSourceCategory`, `CollisionVolumeCategory`, `GeometryCollisionProfile`, `FluidSourceCategory`, `CurveInterpolationForm`, `ComponentTransform` (+21 more)

## Usage

```cpp
#include "Engine/ComponentClass/ComponentClassDefinition.h"
#include "Engine/ComponentClass/ComponentClassToken.h"
#include "Engine/ComponentClass/ComponentClassValidator.h"

using namespace Frontier::ComponentClass;
ComponentClassArchetype Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
