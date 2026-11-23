# 🧩 `Engine/GeometricRaster/Rasterization/`

> **TL;DR**: Native Vulkan 1.2+ swapchain & offscreen presenter executing ImGuiOverlayRasterization.slang over GLFW OS windows or headless targets (2 source/header/shader files under `Engine/GeometricRaster/Rasterization/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `ImGuiOverlayRasterization.cpp` | Implementation of ImGuiOverlayRasterization driving ImGuiOverlayRasterization.slang over OS VkSwapchainKHR or headless targets |
| `ImGuiOverlayRasterization.h` | Native Vulkan 1.2+ swapchain & offscreen presenter executing ImGuiOverlayRasterization.slang over GLFW OS windows or headless targets |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `OverlayRasterizationStats`

## Usage

```cpp
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"

using namespace Frontier::GeometricRaster;
OverlayRasterizationStats Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
