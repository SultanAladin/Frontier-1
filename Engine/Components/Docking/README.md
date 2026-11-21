# 🧩 `Engine/Components/Docking/`

> **TL;DR**: Shared workspace dock container enforcing intra-application multi-window docking and OS window tear-off. (4 source/header/shader files under `Engine/Components/Docking/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `WorkspaceDockContainer.cpp` | Application-scoped domain token issuance and patched Dear ImGui unrounded trapezoidal dockspace builder. |
| `WorkspaceDockContainer.h` | Shared workspace dock container enforcing intra-application multi-window docking and OS window tear-off. |
| `WorkspaceTabStrip.cpp` | Unrounded (0.0f) trapezoidal docking tab geometry and patched Dear ImGui theme synchronization. |
| `WorkspaceTabStrip.h` | Unrounded trapezoidal workspace docking tab geometry and patched Dear ImGui theme bridge. |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Components`
- **Primary Types**: `ApplicationDockDomainToken`, `HostWindowToken`, `WorkspaceDockPaneToken`, `DockPlacementDirection`, `PaneDirectiveMask`, `DockTransferVerdict`, `WorkspaceDockPaneDescriptor`, `WorkspaceDockPaneReading`, `HostWindowReading`, `WorkspaceDockContainer`, `ScreenPoint2D`, `RgbaColor8`, `WorkspaceTabMetrics`, `WorkspaceThemeSet`, `WorkspaceTabContour`

## Usage

```cpp
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Docking/WorkspaceTabStrip.h"

using namespace Frontier::Components;
ApplicationDockDomainToken Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
