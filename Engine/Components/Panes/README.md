# 🧩 `Engine/Components/Panes/`

> **TL;DR**: Interactive pan/zoom Assembly Sequence canvas visualizing ComponentClass hierarchy & physics construction flow with Bezier wires (12 source/header/shader files under `Engine/Components/Panes/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ExecutionSequencePane.cpp` | Implementation of ExecutionSequencePane (16/128px grid, Event + Call + Target pill blocks, cubic Bezier exec & instance wires) |
| `ExecutionSequencePane.h` | Interactive pan/zoom Assembly Sequence canvas visualizing ComponentClass hierarchy & physics construction flow with Bezier wires |
| `InspectorPanel.cpp` | Workspace Inspector pane featuring Showcase Folder Inspector, Add Notes editor, header typography, and capability tiles |
| `InspectorPanel.h` | Workspace Inspector pane featuring Showcase Folder Inspector, Add Notes editor, header typography, and capability tiles |
| `OutlinerPanel.cpp` | Generic Workspace Outliner and LayerStack tree pane with Showcase folders, search, filters, drag-reorder, and slot directives |
| `OutlinerPanel.h` | Generic Workspace Outliner and LayerStack tree pane with Showcase folders, search, filters, drag-reorder, and slot directives |
| `ViewportPanel.cpp` | Workspace Viewport pane with Edit/Simulate/Play-Pause strip, Construct sheet, Split Viewport, Diagnostics, and Settings |
| `ViewportPanel.h` | Workspace Viewport pane with Edit/Simulate/Play-Pause strip, Construct sheet, Split Viewport, Diagnostics, and Settings |
| `ViewportProjectionPane.cpp` | Implementation of reusable ViewportProjectionPane presenting WorkspaceDocument 2D/3D GPU views via PixelSpace::FillTexturedRectangle |
| `ViewportProjectionPane.h` | Reusable PixelSpace single/split 2D+3D GPU viewport pane bound to WorkspaceDocument & blitted via PixelSpace::FillTexturedRectangle |
| `WorkspaceAppearance.cpp` | Shared surface color tokens, VisualToken, VectorGraphic, Workspace icons, indicators, signs, and symbols |
| `WorkspaceAppearance.h` | Shared surface color tokens, VisualToken, VectorGraphic, Workspace icons, indicators, signs, and symbols |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`, `Frontier::WorkspaceAppearance`, `WorkspaceAppearance`
- **Primary Types**: `SequenceBlockCategory`, `SequenceBlock`, `SequenceWireLink`, `SequenceBlockPosition`, `ExecutionSequenceViewport`, `ExecutionSequencePane`, `FolderBrowseScope`, `FolderBrowseSort`, `InspectorPanel`, `OutlinerTitleVariant`, `OutlinerVisibilityFilter`, `OutlinerCategoryFilter`, `DropRelativePlacement`, `OutlinerSettingsDirective`, `OutlinerSlotToken`, `OutlinerConstructTemplate`, `OutlinerRecordEntry`, `OutlinerVisibleRow` (+26 more)

## Usage

```cpp
#include "Engine/Components/Panes/ExecutionSequencePane.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"

using namespace Frontier;
SequenceBlockCategory Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
