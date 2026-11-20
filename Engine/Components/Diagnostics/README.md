# 🧩 `Engine/Components/Diagnostics/`

> **TL;DR**: Reusable tactile schematic infographic canvas (Optical Bokeh Aperture selector, Wind Rose compass, Force Field reach & Release Cone diagrams) (7 source/header/shader files under `Engine/Components/Diagnostics/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `DiagnosticInfographicCanvas.cpp` | Implementation of reusable DiagnosticInfographicCanvas (Optical Bokeh Aperture 4-card selector, Wind Rose, Force Field Reach & Release Cone) |
| `DiagnosticInfographicCanvas.h` | Reusable tactile schematic infographic canvas (Optical Bokeh Aperture selector, Wind Rose compass, Force Field reach & Release Cone diagrams) |
| `DiagnosticsOverlay.cpp` | Implementation of reusable floating DiagnosticsOverlay (metric tabs, view modes, sparkline plot, stats rows & move/resize) |
| `DiagnosticsOverlay.h` | Reusable floating Diagnostics overlay (metric tabs, view modes, sparkline plot, oversized readout, stats rows & move/resize grip) |
| `LogStreamDrawer.cpp` | Implementation of reusable LogStreamDrawer (channel filter pills & timestamped coloured diagnostic log stream) |
| `LogStreamDrawer.h` | Reusable collapsible diagnostic log stream drawer with channel filter chips, timestamped semantic badges & scroll offset |
| `ViewportTimingTelemetry.h` | Per-stage CPU/GPU timing breakdown and debug timing log utilities for Frontier diagnostics |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`
- **Primary Types**: `InfographicArchetype`, `DiagnosticInfographicSpec`, `DiagnosticInfographicResult`, `DiagnosticInfographicCanvas`, `DiagnosticsViewMode`, `DiagnosticsStatRow`, `DiagnosticsMetricChannel`, `DiagnosticsOverlaySession`, `DiagnosticsOverlaySpec`, `DiagnosticsOverlayResult`, `DiagnosticsOverlay`, `LogChannelCategory`, `LogStreamEntry`, `LogStreamDrawerSession`, `LogStreamDrawer`, `ViewportTimingBreakdown`

## Usage

```cpp
#include "Engine/Components/Diagnostics/DiagnosticInfographicCanvas.h"
#include "Engine/Components/Diagnostics/DiagnosticsOverlay.h"
#include "Engine/Components/Diagnostics/LogStreamDrawer.h"

using namespace Frontier;
InfographicArchetype Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
