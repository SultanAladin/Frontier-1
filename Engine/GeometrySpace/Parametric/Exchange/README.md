# 🧩 `Engine/GeometrySpace/Parametric/Exchange/`

> **TL;DR**: UTF-8 text → std::filesystem::path, the one conversion every file read or written by GeometryProcessing and Sketcher goes through (6 source/header/shader files under `Engine/GeometrySpace/Parametric/Exchange/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ExchangePath.h` | UTF-8 text → std::filesystem::path, the one conversion every file read or written by GeometryProcessing and Sketcher goes through |
| `StepExchange.cpp` | STEP AP214 writer: product structure, unit context, then one boundary representation per solid region — every instance |
| `StepExchange.h` | STEP exchange (ISO 10303-21 file, AP214 automotive_design schema): solid bodies → MANIFOLD_SOLID_BREP / BREP_WITH_VOIDS |
| `StepReading.cpp` | STEP reader: ISO 10303-21 scan → instances → kernel carriers in model units → one blueprint per solid → AssembleBody and |
| `StlExchange.cpp` | STL exchange: binary / ASCII writer over BodyTessellation, bit-exact welding reader, soundness measures |
| `StlExchange.h` | STL exchange: bodies → watertight PolygonNetworkStructure → binary / ASCII STL in millimetres, and the reader that welds a file |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `KnotForm`, `StepCarrier`, `StepWriter`, `StepRequest`, `StepExport`, `StepImportRequest`, `StepImport`, `StepCategory`, `StepParameter`, `StepPartial`, `StepInstance`, `StepFile`, `PartScanner`, `KnotRule`, `TransferUnits`, `BodyConversion`, `StepInterpreter`, `FloatCorner` (+5 more)

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Exchange/ExchangePath.h"
#include "Engine/GeometrySpace/Parametric/Exchange/StepExchange.h"
#include "Engine/GeometrySpace/Parametric/Exchange/StlExchange.h"

using namespace Frontier::GeometrySpace::Parametric;
KnotForm Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
