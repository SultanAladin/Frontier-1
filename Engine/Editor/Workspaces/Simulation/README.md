# 🧩 `Engine/Editor/Workspaces/Simulation/`

> **TL;DR**: Full 1:1 ForceFields Inspector renderer: 4 tabs (Field / Formula / Layers / Cache), 19 ForceModel law schemas, (6 source/header/shader files under `Engine/Editor/Workspaces/Simulation/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ForceInspectorView.cpp` | Complete 1:1 ForceFields Inspector implementation matching VectorFieldVisualizer across all 4 tabs and 19 ForceModel laws |
| `ForceInspectorView.h` | Full 1:1 ForceFields Inspector renderer: 4 tabs (Field / Formula / Layers / Cache), 19 ForceModel law schemas, |
| `ForcePresetBrowserPanel.cpp` | Project-Zero pull-up Content Browser drawer & trapezoidal bottom handle hosting .ForceModel presets, 19 ForceModel laws & formulas |
| `ForcePresetBrowserPanel.h` | Project-Zero pull-up Content Browser drawer & trapezoidal bottom handle hosting .ForceModel presets, 19 ForceModel laws & formulas |
| `VectorFieldViewportSession.cpp` | Editor session binding ForceFieldAccumulator, FieldLatticeVulkanExecutor, VectorFieldOverlaySequence & 3-dock UI panes |
| `VectorFieldViewportSession.h` | Editor session binding ForceFieldAccumulator, FieldLatticeVulkanExecutor, VectorFieldOverlaySequence & 3-dock UI panes |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Editor::Simulation`, `InspectorSurfaceColors`, `Frontier`, `DrawerColors`
- **Primary Types**: `BeaufortEntry`, `ForceScalarSlot`, `ForceParameterDescriptor`, `ForceModelDescriptor`, `ForceInspectorTab`, `ForceInspectorViewTargets`, `ForceInspectorView`, `ContentAssetOrigin`, `ContentAssetCategory`, `ContentBrowserViewLayout`, `ContentBrowserAssetEntry`, `ForcePresetBrowserPanel`, `VectorFieldViewportSession`

## Usage

```cpp
#include "Engine/Editor/Workspaces/Simulation/ForceInspectorView.h"
#include "Engine/Editor/Workspaces/Simulation/ForcePresetBrowserPanel.h"
#include "Engine/Editor/Workspaces/Simulation/VectorFieldViewportSession.h"

using namespace Frontier::Editor::Simulation;
BeaufortEntry Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
