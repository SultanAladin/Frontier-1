# 🧩 `Engine/Windowing/`

> **TL;DR**: Cross-platform window and surface interface with directive masks, application presets, and dynamic fullscreen mode transitions (3 source/header/shader files under `Engine/Windowing/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `PlatformWindow.h` | Cross-platform window and surface interface with directive masks, application presets, and dynamic fullscreen mode transitions |
| `PlatformWindowDesktop.cpp` | GLFW 3.4 cross-platform desktop window implementation (Win32, X11, Wayland, and offscreen GLFW_PLATFORM_NULL interface) |
| `PlatformWindowStub.cpp` | Headless deterministic window implementation supporting splash, windowed, maximized, and fullscreen mode transitions |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Windowing`
- **Primary Types**: `WindowPlatformCategory`, `WindowPresentationMode`, `WindowAttributeMask`, `WindowPlacementBounds`, `WindowConfigurationDescriptor`, `WindowExtentReading`, `HeadlessPlatformWindow`, `DesktopPlatformWindow`

## Usage

```cpp
#include "Engine/Windowing/PlatformWindow.h"

using namespace Frontier::Windowing;
WindowPlatformCategory Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
