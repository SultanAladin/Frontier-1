# 🧩 `Engine/GeometrySpace/Parametric/Feature/`

> **TL;DR**: Cut (Plasticity C): a solid or sheet split by a sketch profile or an open curve swept through it, into two bodies (15 source/header/shader files under `Engine/GeometrySpace/Parametric/Feature/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `BodyCut.cpp` | Cut (Plasticity C): a solid or sheet split by a sketch profile or an open curve swept through it, into two bodies |
| `BodyCut.h` | Cut (Plasticity C): a solid or sheet split by a sketch profile or an open curve swept through it, into two bodies |
| `FeatureRepository.cpp` | Feature-tree specimens with closed-form volumes, parameter sweeps and expected refusals |
| `FeatureRepository.h` | Feature-tree specimens with closed-form volumes, parameter sweeps and expected refusals |
| `FeatureTree.cpp` | Parametric feature tree: parameters, sketches, sweeps, Booleans, blends, face operations; regeneration by replay |
| `FeatureTree.h` | Parametric feature tree: parameters, sketches, sweeps, Booleans, blends, face operations; regeneration by replay |
| `PathSweep.cpp` | Path verbs — each recognised case is rewritten as the exact primitive or sweep that produces the same solid |
| `PathSweep.h` | Path verbs reduced to exact constructions — pipe along a line / arc, sweep along a line, loft of coaxial circles (G2) |
| `ProfileSweep.cpp` | Solids from sketch profiles: linear extrude (planes, cylinders, holes) and revolve (plane … torus, poles, NURBS revolutions) |
| `ProfileSweep.h` | Solids from sketch profiles: linear extrude (planes, cylinders, holes) and revolve, full or partial (plane … torus, poles) |
| `ReplayDigest.cpp` | Byte-exact step digests: fields in declaration order, then the bound parameter values |
| `ReplayDigest.h` | Canonical step digests for incremental replay: equal digests, equal replay inputs |
| `SweepBlueprint.h` | Shared blueprint writer for profile sweeps (extrude, revolve, evolution): faces take their sense from an outward probe |
| `TopologicalNaming.cpp` | Persistent names from operation histories (research § L 🥇) and reference resolution: exact → source → geometric |
| `TopologicalNaming.h` | Persistent names from operation histories (research § L 🥇) and reference resolution: exact → source → geometric |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`, `Frontier::GeometrySpace::Parametric::Sweeping`
- **Primary Types**: `BodyCutOutcome`, `FeatureSpecimen`, `ReplayCheckpoint`, `FeatureStepCategory`, `FeatureCombination`, `FeatureStepStatus`, `FeatureScalar`, `FeatureParameter`, `FeaturePoleEdit`, `FeatureJoint`, `FeatureStep`, `EdgeWeightEntry`, `FeatureEdgeGroup`, `ModifierCategory`, `BevelLimitMode`, `FeatureModifier`, `FeatureModifierReport`, `FeatureTree` (+19 more)

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Feature/BodyCut.h"
#include "Engine/GeometrySpace/Parametric/Feature/FeatureRepository.h"
#include "Engine/GeometrySpace/Parametric/Feature/FeatureTree.h"

using namespace Frontier::GeometrySpace::Parametric;
BodyCutOutcome Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
