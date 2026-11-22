# 🧩 `Engine/Components/`

> **TL;DR**: Logical-to-physical 2D canvas surface for reusable engine UI controls, panels, and overlays (2 source/header/shader files under `Engine/Components/`).

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Appearance/`](Appearance/README.md) | Built-in interface themes, accent swatches, semantic alert swatches, and typography scale slots |
| [`Camera/`](Camera/README.md) | Unified 6-mode physical thin-lens CameraComponent with exponential smoothing, constraints, and non-Shipping diagnostics |
| [`ContentBrowser/`](ContentBrowser/README.md) | Named colour alias library supporting HEX/RGB/CMYK/HSL metrics, contrast ink selection, spectrum filtering, and TOML persistence |
| [`ControlCentre/`](ControlCentre/README.md) | Top-notch pull-down Control Centre, Appearance/Input/Notifications/Workspace settings, toast overlay, and modal dialogues |
| [`Controls/`](Controls/README.md) | Reusable capsule buttons, weight/scope chips, bordered circular icon buttons, segmented bars, and quick discs |
| [`Diagnostics/`](Diagnostics/README.md) | Reusable tactile schematic infographic canvas (Optical Bokeh Aperture selector, Wind Rose compass, Force Field reach & Release Cone diagrams) |
| [`Docking/`](Docking/README.md) | Shared workspace dock container enforcing intra-application multi-window docking and OS window tear-off. |
| [`Motion/`](Motion/README.md) | Unified 3-detent (Closed / Half / Full) touch-like drag settle physics & elastic clamping shared by pull-down/pull-up drawers |
| [`Notch/`](Notch/README.md) | Top-edge trapezoidal pull-down notch ("Frontier") with elastic carry, axis lock, and flick physics |
| [`Panes/`](Panes/README.md) | Interactive pan/zoom Assembly Sequence canvas visualizing ComponentClass hierarchy & physics construction flow with Bezier wires |
| [`VectorIcons/`](VectorIcons/README.md) | SVG 1.1 path decoder, round-cap stroker, and reusable vector icon repository for engine UI controls and panels |

## Files

| File | Purpose |
| :--- | :--- |
| `PixelSpace.cpp` | Logical-to-physical 2D canvas implementation over patched Dear ImGui construct lists. |
| `PixelSpace.h` | Logical-to-physical 2D canvas surface for reusable engine UI controls, panels, and overlays |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`
- **Primary Types**: `InterfaceKey`, `ColorRgba`, `Point2D`, `Rectangle2D`, `PixelSpace`

## Usage

```cpp
#include "Engine/Components/PixelSpace.h"

using namespace Frontier;
InterfaceKey Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
