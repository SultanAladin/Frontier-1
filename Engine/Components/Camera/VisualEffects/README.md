# 🧩 `Engine/Components/Camera/VisualEffects/`

> **TL;DR**: Vulkan .slang GPU compute encoder executing CameraVisualEffects.comp.spv (DoF, ChromaticAberration, LensFlare, Vignette & Guides) (8 source/header/shader files under `Engine/Components/Camera/VisualEffects/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CameraOpticsEncoder.cpp` | Implementation of GPU .slang CameraOpticsEncoder executing CameraVisualEffects.comp.spv |
| `CameraOpticsEncoder.h` | Vulkan .slang GPU compute encoder executing CameraVisualEffects.comp.spv (DoF, ChromaticAberration, LensFlare, Vignette & Guides) |
| `ChromaticAberration.h` | Radial lateral chromatic dispersion uniform evaluation for CameraVisualEffects.slang |
| `DepthOfField.h` | Thin-lens Circle-of-Confusion diameter evaluation (mm & screen pixels) and GPU bitmask packing |
| `LensFlareEffect.h` | Multi-layer physical lens flare & coated polygonal aperture ghost uniform packing for CameraVisualEffects.slang |
| `MotionBlurEffect.h` | 3-Mode Camera Motion Blur (CameraVelocity reprojection, DirectionalShutter 180-deg blur, RadialZoom speed blur) |
| `VignetteEffect.h` | Natural cos^4 optical falloff parameter validation and GPU bitmask packing |
| `WorldDistortionEffect.h` | World-placed & particle-linked camera distortion anchors (Heat Shimmer, Black Hole Lensing, Glitch) + 3D->Screen projection |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::Components::VisualEffects`
- **Primary Types**: `CameraOpticsEncoder`, `ProjectedScreenDistortionAnchor`

## Usage

```cpp
#include "Engine/Components/Camera/VisualEffects/CameraOpticsEncoder.h"
#include "Engine/Components/Camera/VisualEffects/ChromaticAberration.h"
#include "Engine/Components/Camera/VisualEffects/DepthOfField.h"

using namespace Frontier::Components::VisualEffects;
CameraOpticsEncoder Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
