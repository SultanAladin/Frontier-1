# 🧩 `Engine/GeometrySpace/Parametric/Drawing/`

> **TL;DR**: Drawing sheet export: SVG (millimetre viewBox, one group per layer) and DXF R12 (AC1009 ASCII: layers, line types, entities) (6 source/header/shader files under `Engine/GeometrySpace/Parametric/Drawing/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `DrawingExport.cpp` | Drawing sheet export: SVG (millimetre viewBox, one group per layer) and DXF R12 (AC1009 ASCII: layers, line types, entities) |
| `DrawingExport.h` | Drawing sheet export: SVG (millimetre viewBox, one group per layer) and DXF R12 (AC1009 ASCII: layers, line types, entities) |
| `DrawingProjection.cpp` | Orthographic drawing views: projected edges, exact silhouettes, ray-cast hidden lines, section views with hatching |
| `DrawingProjection.h` | Orthographic drawing views: projected edges, exact silhouettes, ray-cast hidden lines, section views with hatching |
| `DrawingSheet.cpp` | Drawing sheets: paper, first / third angle view layout, standard scales, automatic dimensions, section line, title block |
| `DrawingSheet.h` | Drawing sheets: paper, first / third angle view layout, standard scales, automatic dimensions, section line, title block |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometrySpace::Parametric`
- **Primary Types**: `DrawingStandardView`, `DrawingStrokeCategory`, `DrawingVisibility`, `DrawingStroke`, `DrawingViewRequest`, `DrawingView`, `DrawingProjectionAngle`, `DrawingPaperSize`, `SheetLayer`, `SheetTextAnchor`, `SheetPolyline`, `SheetText`, `DrawingTitleBlock`, `DrawingDimensionCategory`, `DrawingDimension`, `PlacedDrawingView`, `DrawingSheetRequest`, `DrawingSheet`

## Usage

```cpp
#include "Engine/GeometrySpace/Parametric/Drawing/DrawingExport.h"
#include "Engine/GeometrySpace/Parametric/Drawing/DrawingProjection.h"
#include "Engine/GeometrySpace/Parametric/Drawing/DrawingSheet.h"

using namespace Frontier::GeometrySpace::Parametric;
DrawingStandardView Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
