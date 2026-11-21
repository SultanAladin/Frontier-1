# 🧩 `Engine/GeometrySpace/Parametric/Local/`

> **TL;DR**: Face verbs that change topology: extrude (side ring), inset (border ring + inner face), delete (heal or sheet), heal (merge), (10 source/header/shader files under `Engine/GeometrySpace/Parametric/Local/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ConnectivityRebuild.cpp` | Topology-changing face verbs by direct blueprint rebuild |
| `ConnectivityRebuild.h` | Face verbs that change topology: extrude (side ring), inset (border ring + inner face), delete (heal or sheet), heal (merge), |
| `FaceChange.cpp` | Generic face change: carriers replaced, edges re-intersected, vertices re-solved, body re-assembled and audited |
| `FaceChange.h` | Generic face change (research § K 🥇): new carriers for chosen faces, every edge and vertex re-solved against them |
| `FaceOperationRepository.cpp` | Face-operation specimens with hand-derived closed forms |
| `FaceOperationRepository.h` | Face-operation specimens: corpus section E (Block / HoleSlab / Can × 12 verbs), chains, extras and refusals |
| `FaceOperations.cpp` | Face verbs as carrier maps over ChangeFaces; shell through General Fuse; extrude / delete / heal / thicken by rebuild |
| `FaceOperations.h` | Face verbs (research § J + K): each one only chooses new carriers; ChangeFaces re-solves the body |
| `RedundantConnectivity.cpp` | Maximal faces and edges — KEF across one carrier, KEV / KEMR for the slits left behind, KVE across degree-two vertices |
| `RedundantConnectivity.h` | Maximal faces and edges — merge faces across edges that separate one carrier, join edges across degree-two vertices |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `FaceReplacement`, `FaceChangeOrigin`, `FaceChangeHistory`, `EdgeChangeCategory`, `FaceChangeStatistics`, `FaceChangeOutcome`, `FaceOperationSpecimen`, `FaceOperationCategory`, `FaceOperationSettings`, `FaceOperationOutcome`, `RedundancyOutcome`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Local/ConnectivityRebuild.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceChange.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceOperationRepository.h"

using namespace Frontier::GeometrySpace::Parametric;
FaceReplacement Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
