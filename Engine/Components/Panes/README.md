# 🧩 `Engine/Components/Panes/`

> **TL;DR**: Reusable `PixelSpace` studio workspace panes — `DeterministicPathTracerPane`, `ExecutionSequencePane`, `InspectorPanel`, `OutlinerPanel`, `ViewportPanel`, `ViewportProjectionPane`, and `WorkspaceAppearance`.

---

## Files

| File | Purpose |
| :--- | :--- |
| `DeterministicPathTracerPane.cpp` | Native dark studio viewport + 330 px inspector pane driving `DeterministicPathTraceSequence` (`SceneFinalRender` & `GameRealtime`, orbit/pan/dolly, convergence bar, FNV-1a digest verification) |
| `DeterministicPathTracerPane.h` | Studio path-tracer workspace pane blitting Vulkan RGBA8 viewport output via `PixelSpace::FillTexturedRectangle` |
| `ExecutionSequencePane.cpp` | Interactive pan/zoom canvas visualizing `ComponentClass` structure & physics construction links with Bezier wires |
| `ExecutionSequencePane.h` | Interactive pan/zoom canvas visualizing `ComponentClass` structure & physics construction links with Bezier wires |
| `InspectorPanel.cpp` | Workspace Inspector pane featuring Showcase Folder Inspector, Add Notes editor, header typography, and capability tiles |
| `InspectorPanel.h` | Workspace Inspector pane featuring Showcase Folder Inspector, Add Notes editor, header typography, and capability tiles |
| `OutlinerPanel.cpp` | Generic Workspace Outliner and LayerStack tree pane with Showcase folders, search, filters, drag-reorder, and slot directives |
| `OutlinerPanel.h` | Generic Workspace Outliner and LayerStack tree pane with Showcase folders, search, filters, drag-reorder, and slot directives |
| `ViewportPanel.cpp` | Workspace Viewport pane with Edit/Simulate/Play-Pause strip, Construct sheet, Split Viewport, Diagnostics, and Engine selector (Orders 1–3) |
| `ViewportPanel.h` | Workspace Viewport pane with Edit/Simulate/Play-Pause strip, Construct sheet, Split Viewport, Diagnostics, and Settings |
| `ViewportProjectionPane.cpp` | Reusable `ViewportProjectionPane` presenting `WorkspaceDocument` 2D/3D GPU views via `PixelSpace::FillTexturedRectangle` |
| `ViewportProjectionPane.h` | Reusable `PixelSpace` single/split 2D+3D GPU viewport pane bound to `WorkspaceDocument` |
| `WorkspaceAppearance.cpp` | Shared surface color tokens, `VisualToken`, `VectorGraphic`, Workspace icons, indicators, signs, and symbols |
| `WorkspaceAppearance.h` | Shared surface color tokens, `VisualToken`, `VectorGraphic`, Workspace icons, indicators, signs, and symbols |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`, `Frontier::WorkspaceAppearance`
- **Primary Types**: `DeterministicPathTracerPane`, `ExecutionSequencePane`, `InspectorPanel`, `OutlinerPanel`, `ViewportPanel`, `ViewportProjectionPane`

## Build & Verification

- Built as part of `FrontierEngine` / `FrontierEditor` in root `CMakeLists.txt` (ISO C++20).
- Validated by `DeterministicPathTracerValidation` and UI validation suites under `Exhibits/`.
