# 🧩 `Engine/Workspaces/ParametricSketcher/Document/`

> **TL;DR**: Vehicle plan V1: VehicleCurves.toml (edge loops exported from Blender as ordered limit points) — decode, encode (23 source/header/shader files under `Engine/Workspaces/ParametricSketcher/Document/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `SketcherCurveImport.cpp` | Vehicle plan V1: VehicleCurves.toml decode / encode with toml++ (all-or-nothing, units scaled to metres) |
| `SketcherCurveImport.h` | Vehicle plan V1: VehicleCurves.toml (edge loops exported from Blender as ordered limit points) — decode, encode |
| `SketcherDocument.cpp` | Sketcher document: construct tiles as sketch + sweep pairs, atomic commands, undo / redo, selection upkeep |
| `SketcherDocument.h` | Sketcher document: feature tree, atomic commands with undo / redo, selection by persistent name, regeneration |
| `SketcherDocumentCodec.cpp` | Sketcher document files (.sketcher): parameters, steps, sketches, references, captions (toml++) |
| `SketcherDocumentCodec.h` | Sketcher document files (.sketcher): parameters, steps, sketches, references, captions |
| `SketcherDrawingSheet.cpp` | Drawing sheet (Alt+D): shown solid bodies → kernel ComposeDrawingSheet (metres → millimetres) → SVG / DXF R12 |
| `SketcherDrawingSheet.h` | Drawing sheet (Alt+D): the shown solid bodies of the document as an ISO drawing — the kernel's Drawing/DrawingSheet |
| `SketcherExchange.cpp` | Document ↔ STEP / STL: kernel WriteStep / WriteStl (metres → millimetres) out, ReadStep + canonical WriteStep in |
| `SketcherExchange.h` | STEP / STL exchange for the document (priority 4 X4): bodies out as STEP AP214 or binary STL in millimetres, STEP |
| `SketcherImagePlane.cpp` | Image planes: picture decoding (stb_image), path resolution, world placement checks |
| `SketcherImagePlane.h` | Image planes: a blueprint / photo crop placed in world space as a reference behind the model (Plasticity / Blender |
| `SketcherImagePlaneToml.h` | [[ImagePlanes]] rows shared by VehicleCurves.toml and the .sketcher document (one encoding, one set of refusals) |
| `SketcherKnife.cpp` | Knife insert library: procedural built-ins and library STEP files normalized to the insert frame |
| `SketcherKnife.h` | Knife inserts (KitOps-style boolean library): built-in cutters authored procedurally and STEP files from a library |
| `SketcherMatcap.cpp` | Matcaps: analytic studio-rig composition of the built-in library, user-picture fitting, layer lookup |
| `SketcherMatcap.h` | Matcaps: the built-in engineering library (metals, plastics, rubber, clay, ceramic, paint) composed analytically, user |
| `SketcherMeasurement.cpp` | Inspect: selection → kernel measurement targets, Inspector rows, multi-body plane sections |
| `SketcherMeasurement.h` | Inspect: exact measurement of the selection (mass properties, areas, lengths, minimum distance, angles) and the exact |
| `SketcherScene.cpp` | Display arrays of the current body (per-face corners, edge segments, name tables) and the CPU ray pick |
| `SketcherScene.h` | Display arrays of the current body (per-face corners, edge segments, name tables) and the CPU ray pick |
| `SketcherSpecimen.cpp` | The bracket specimen: one modelling session (tiles, boolean, picks, fillet, chamfer) shared by validation and captures |
| `SketcherSpecimen.h` | The bracket specimen: one modelling session (tiles, boolean, picks, fillet, chamfer) shared by validation and captures |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Workspaces::Sketcher`
- **Primary Types**: `VehicleCurve`, `VehicleCurveFile`, `TileAuthoring`, `SketcherConnectivityWalk`, `EdgePathRoute`, `EdgePathCarrier`, `ResolvedDimensionOperand`, `SketcherConstructCategory`, `SketcherDrawingCategory`, `SketcherSelectionCategory`, `SketcherConstructRequest`, `SketcherDrawingRequest`, `SketcherSelection`, `SketcherCutRequest`, `SketcherCornerRequest`, `SketcherTrimRequest`, `SketcherRailDeformRequest`, `SketcherWrapRequest` (+69 more)

## Usage

```cpp
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherCurveImport.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"

using namespace Frontier::Workspaces::Sketcher;
VehicleCurve Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
